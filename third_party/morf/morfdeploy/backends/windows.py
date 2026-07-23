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

    # -- Runtime dependencies ---------------------------------------------

    def install_runtime(self, installed_binary: Path) -> None:
        """Bring the Qt and MinGW DLLs the service needs next to its binary.

        The reason the base method is a no-op and this one is not: Linux
        resolves Qt from where the package manager put it, so a copied binary
        just runs. Windows has no system-wide Qt. An executable installed on its
        own starts to a 'Qt6Core.dll not found' dialog -- which, for a service,
        the SCM reports only as a start failure, mentioning none of the missing
        files. windeployqt, shipped with Qt, reads the executable's imports and
        places exactly the Qt libraries and plugins it needs beside it.

        windeployqt covers Qt and, with --compiler-runtime, the MinGW runtime
        (libgcc, libstdc++, libwinpthread). A second, best-effort pass with ldd
        widens the net to any remaining third-party DLL the binaries still
        resolve from the toolchain -- the belt-and-suspenders ComponentHub
        settled on -- and is simply skipped where no MSYS2 shell is present.
        """
        tool = (shutil.which("windeployqt")
                or shutil.which("windeployqt6")
                or shutil.which("windeployqt-qt6"))
        if tool is None:
            raise RuntimeError(
                "windeployqt is not on PATH, so the Qt runtime cannot be placed\n"
                f"beside {installed_binary.name}. Installed alone, the service starts\n"
                "to a missing-DLL error the Service Control Manager reports only as a\n"
                "failed start, naming none of the absent files.\n\n"
                "Run this install from the same MSYS2/MinGW shell that built the\n"
                "binary (windeployqt ships with Qt), or add Qt's bin directory to PATH."
            )

        self._run([tool, "--no-translations", "--compiler-runtime",
                   str(installed_binary)])
        print(f"  Qt runtime deployed beside {installed_binary.name} (windeployqt)")

        self._copy_toolchain_dlls(installed_binary.parent)

    def _copy_toolchain_dlls(self, app_dir: Path) -> None:
        """Copy any DLL the installed binaries still resolve from the MinGW tree.

        Best-effort: it needs ldd from an MSYS2 shell. Absent it, windeployqt's
        --compiler-runtime has already placed the usual ones, so a missing bash
        is a narrower net, not a broken install. Mirrors ComponentHub's
        deploy-mingw.sh, inlined here so morfdeploy carries no external script.
        """
        bash = shutil.which("bash")
        if bash is None:
            return
        script = (
            'cd "$1" || exit 0; '
            'for f in *.exe *.dll; do [ -f "$f" ] && ldd "$f" 2>/dev/null; done '
            "| grep -iE '/(mingw64|ucrt64|clang64|mingw32)/bin/' "
            "| awk '{print $3}' | sort -u "
            '| while read -r dll; do cp -u "$dll" . 2>/dev/null || true; done'
        )
        subprocess.run([bash, "-c", script, "bash", str(app_dir)], check=False)

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
