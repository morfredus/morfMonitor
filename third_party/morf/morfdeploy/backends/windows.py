"""Windows backend — x64.

A note on what a Windows service actually requires, because it decides the
shape of this file.

The Service Control Manager does not merely launch a program: it expects the
process to connect back, declare a service entry point and report its state
within about thirty seconds. A binary that does not do this is registered
without complaint by `sc.exe create` and then fails at start with error 1053,
"the service did not respond in a timely fashion". Nothing in that message
mentions the missing SCM handshake.

The morfSystem services are ordinary Qt console programs. They are SCM-aware on
no platform, and making them so would mean adding Windows-specific startup code
to every one of them -- inside programs whose whole point is to be identical
everywhere.

So there are two strategies here, and the manifest chooses:

  "scm"       real Windows service. Requires either an SCM-aware binary or a
              wrapper (WinSW, NSSM) that speaks the protocol on its behalf.
  "task"      scheduled task at boot. What the PowerShell scripts did. Not a
              service: no dependency ordering, no automatic restart on crash,
              and it does not appear in services.msc.

The default is "task" because it is what works today with an unmodified binary.
Declaring "scm" without a wrapper is refused at install time rather than
producing a service registered to fail.
"""

from __future__ import annotations

import ctypes
import shutil
import subprocess
from pathlib import Path

from ..manifest import Manifest
from .base import ServiceBackend

#: Wrappers that implement the SCM handshake for an ordinary executable.
WRAPPERS = ("winsw", "nssm")


class WindowsBackend(ServiceBackend):
    name = "windows"
    supported = True

    # -- Strategy ---------------------------------------------------------

    def _strategy(self, manifest: Manifest) -> str:
        declared = (manifest.app_dirs.get("windows_strategy") or "").lower()
        return declared if declared in ("scm", "task") else "task"

    def _wrapper(self) -> str | None:
        for name in WRAPPERS:
            found = shutil.which(name)
            if found:
                return found
        return None

    # -- Interrogation ----------------------------------------------------

    def is_installed(self, manifest: Manifest) -> bool:
        if self._strategy(manifest) == "scm":
            result = subprocess.run(
                ["sc.exe", "query", manifest.service_name],
                capture_output=True, text=True, check=False,
            )
            return result.returncode == 0
        result = subprocess.run(
            ["schtasks", "/Query", "/TN", manifest.service_name],
            capture_output=True, text=True, check=False,
        )
        return result.returncode == 0

    def status(self, manifest: Manifest) -> str:
        if self._strategy(manifest) == "scm":
            args = ["sc.exe", "query", manifest.service_name]
        else:
            args = ["schtasks", "/Query", "/TN", manifest.service_name, "/V", "/FO", "LIST"]
        result = subprocess.run(args, capture_output=True, text=True, check=False)
        return (result.stdout or result.stderr).strip()

    # -- Lifecycle --------------------------------------------------------

    def install(self, manifest: Manifest, app_dir: Path, run_user: str) -> None:
        target = app_dir / manifest.binary_name()

        if self._strategy(manifest) == "scm":
            wrapper = self._wrapper()
            if wrapper is None:
                raise RuntimeError(
                    f"{manifest.display_name} declares the 'scm' strategy, but no service\n"
                    f"wrapper ({', '.join(WRAPPERS)}) is on PATH.\n\n"
                    "A Qt console program cannot be a Windows service on its own: the\n"
                    "Service Control Manager expects it to report its state within about\n"
                    "thirty seconds, and a binary that does not will be registered\n"
                    "successfully and then fail to start with error 1053.\n\n"
                    "Install WinSW or NSSM, or use the 'task' strategy."
                )
            self._run(["sc.exe", "stop", manifest.service_name], check=False)
            self._run(["sc.exe", "delete", manifest.service_name], check=False)
            self._run([
                "sc.exe", "create", manifest.service_name,
                f"binPath= {wrapper} {target}",
                "start= auto",
                f"DisplayName= {manifest.display_name}",
            ])
            self._run(["sc.exe", "start", manifest.service_name])
            print(f"  Windows service registered via {Path(wrapper).name}")
            return

        # Scheduled task: recreated wholesale, /F overwriting any previous one.
        self._run([
            "schtasks", "/Create", "/F",
            "/TN", manifest.service_name,
            "/TR", f'"{target}"',
            "/SC", "ONSTART",
            "/RL", "HIGHEST",
            "/RU", "SYSTEM",
        ])
        self._run(["schtasks", "/Run", "/TN", manifest.service_name], check=False)
        print("  scheduled task registered (not a service: no restart on crash)")

    def stop(self, manifest: Manifest) -> None:
        if self._strategy(manifest) == "scm":
            self._run(["sc.exe", "stop", manifest.service_name], check=False)
        else:
            self._run(["schtasks", "/End", "/TN", manifest.service_name], check=False)

    def start(self, manifest: Manifest) -> None:
        if self._strategy(manifest) == "scm":
            self._run(["sc.exe", "start", manifest.service_name])
        else:
            self._run(["schtasks", "/Run", "/TN", manifest.service_name])

    def uninstall(self, manifest: Manifest) -> None:
        self.stop(manifest)
        if self._strategy(manifest) == "scm":
            self._run(["sc.exe", "delete", manifest.service_name], check=False)
        else:
            self._run(["schtasks", "/Delete", "/F", "/TN", manifest.service_name], check=False)

    # -- Privileges -------------------------------------------------------

    def requires_privileges(self) -> bool:
        return True

    def has_privileges(self) -> bool:
        try:
            return bool(ctypes.windll.shell32.IsUserAnAdmin())
        except (AttributeError, OSError):
            return False

    def privilege_hint(self) -> str:
        return "Re-run from a terminal opened with 'Run as administrator'."

    # -- Build ------------------------------------------------------------

    def build_as_user(self, repo_root: Path, preset: str, run_user: str) -> None:
        """No privilege drop: Windows has no sudo, and the elevated shell is
        the same user, so the build tree keeps ordinary ownership."""
        subprocess.run(
            f"cmake --preset {preset} && cmake --build --preset {preset}",
            cwd=repo_root, shell=True, check=True,
        )

    # -- Internals --------------------------------------------------------

    def _run(self, args: list, check: bool = True) -> None:
        result = subprocess.run(args, check=False)
        if check and result.returncode != 0:
            raise RuntimeError(f"{args[0]} {args[1]} failed ({result.returncode})")
