"""The four steps every install-service script performed.

Find or build the binary, stop whatever runs, copy the binary and the
configurations into a fixed directory, hand the result to the service manager.
Six projects each carried their own copy of this, differing by a service name
and a directory.

There is no `platform.system()` in this file, and that is the design rule
rather than a coincidence: the moment orchestration starts asking which system
it runs on, the platform boundary has leaked and the single core becomes a
label on top of the old duplication.
"""

from __future__ import annotations

import getpass
import os
import platform
import shutil
from pathlib import Path

from .backends import ServiceBackend, select
from .manifest import Manifest


class DeployError(RuntimeError):
    """A deployment step failed for a reason worth reporting as-is."""


def invoking_user() -> str:
    """Who asked for this, not who is running it.

    Under sudo the process is root, but the build must not be: a build tree
    owned by root inside the clone breaks the next ordinary build, in a place
    that says nothing about the install that caused it.
    """
    return os.environ.get("SUDO_USER") or getpass.getuser()


def detect_preset() -> tuple:
    """CMake preset and build directory for this machine.

    Architecture, not operating system: an ARM64 Raspberry Pi and an x64 Linux
    box run the same backend and need different presets, so this belongs to the
    build step rather than to any platform module.
    """
    if platform.system() == "Windows":
        return "mingw", "build-mingw"
    machine = platform.machine().lower()
    if machine in ("aarch64", "arm64"):
        return "linux-arm64", "build-arm64"
    return "linux", "build"


def locate_binary(manifest: Manifest, build_dir: str) -> Path | None:
    """The freshly built binary, if it is there.

    Several layouts are tried because the parc has more than one: a service
    subdirectory for the C++ services, the build root for the simpler ones.
    """
    name = manifest.binary_name()
    for candidate in (
        manifest.repo_root / build_dir / "service" / name,
        manifest.repo_root / build_dir / name,
    ):
        if candidate.is_file():
            return candidate
    return None


class Deployer:
    """Runs the four steps against a backend."""

    def __init__(self, manifest: Manifest, backend: ServiceBackend | None = None):
        self.manifest = manifest
        self.backend = backend or select()

        if not self.backend.supported:
            raise DeployError(self.backend.supported_note)

    # -- Preconditions ----------------------------------------------------

    def check_privileges(self) -> None:
        if self.backend.requires_privileges() and not self.backend.has_privileges():
            raise DeployError(
                f"Installing a {self.backend.name} service requires administrator rights.\n"
                + self.backend.privilege_hint()
            )

    # -- Step 1 -----------------------------------------------------------

    def ensure_binary(self, rebuild: bool = False) -> Path:
        preset, build_dir = detect_preset()
        binary = locate_binary(self.manifest, build_dir)

        if binary is not None and not rebuild:
            print(f"  binary found: {binary}")
            return binary

        print(f"  building (preset {preset})...")
        self.backend.build_as_user(self.manifest.repo_root, preset, invoking_user())

        binary = locate_binary(self.manifest, build_dir)
        if binary is None:
            raise DeployError(
                f"No binary named '{self.manifest.binary_name()}' under "
                f"{self.manifest.repo_root / build_dir} after building."
            )
        print(f"  built: {binary}")
        return binary

    # -- Step 2 -----------------------------------------------------------

    def stop_existing(self) -> None:
        self.backend.stop(self.manifest)

    # -- Step 3 -----------------------------------------------------------

    def install_files(self, binary: Path, app_dir: Path, config_dir: Path) -> list:
        """Place the binary and the configurations; return what was written."""
        app_dir.mkdir(parents=True, exist_ok=True)
        target = app_dir / self.manifest.binary_name()
        shutil.copy2(binary, target)
        target.chmod(0o755)
        print(f"  binary installed: {target}")
        written = [target]

        for config in self.manifest.configs:
            dest = config.resolved_dest(config_dir)
            dest.parent.mkdir(parents=True, exist_ok=True)

            # The real file wins over the example -- resolved HERE rather than
            # baked into the manifest. A manifest naming config/<name>.json is
            # right on the machine that wrote it and wrong in every clone,
            # because that file is local and untracked. morfNotify shipped
            # exactly that way and could not find its own configuration.
            source = self.manifest.repo_root / config.source
            if config.source.endswith(".example.json"):
                real = Path(str(source).replace(".example.json", ".json"))
                if real.is_file():
                    source = real

            # Never overwrite by default: these hold settings edited by hand on
            # this machine. Delivering a default over them destroys local state
            # nobody asked to lose.
            if dest.exists() and not config.overwrite:
                print(f"  config kept:      {dest}")
                continue

            # Nothing at the new location: adopt what an earlier convention
            # left behind, rather than installing a pristine example over a
            # machine that was already configured. The settings survive the
            # directory layout that happened to hold them.
            # The migration is attempted BEFORE the source is required, because
            # it does not need one: it copies from the previous location. Making
            # a missing source skip the whole entry meant morfNotify never
            # migrated a configuration that was sitting right there in /opt --
            # and then the service was registered anyway and crash-looped 85
            # times against a file nobody had put in place.
            previous = config.find_predecessor()
            if previous is not None:
                shutil.copy2(previous, dest)
                dest.chmod(0o644)
                written.append(dest)
                print(f"  config migrated:  {previous}")
                print(f"                 -> {dest}")
                print(f"  the old file is left in place; remove it once satisfied")
                continue

            # Nothing at the destination, nothing to migrate, and no source to
            # copy: this service will not start. Refusing here is the whole
            # point -- registering it anyway produces a unit that restarts
            # forever against a file that does not exist, and the journal fills
            # with an error the install never mentioned.
            if not source.is_file():
                homes = ", ".join(config.migrate_from) or "none declared"
                raise DeployError(
                    "\n".join([
                        f"No configuration to install for {self.manifest.display_name}.",
                        f"  declared source : {config.source} (absent from this clone)",
                        f"  destination     : {dest} (absent)",
                        f"  earlier homes   : {homes} (none found)",
                        "",
                        "The service would start and immediately exit. Nothing has "
                        "been registered.",
                    ])
                )

            shutil.copy2(source, dest)
            dest.chmod(0o644)
            written.append(dest)
            print(f"  config installed: {dest}")

        return written

    def chown_to_user(self, installed: list) -> None:
        """Give back only the files this install actually wrote.

        The shell scripts ran `chown -R` on the application directory, which is
        harmless for a dedicated /opt/<service> and dangerous anywhere else:
        morfSync puts its binary in /usr/local/bin, and recursing there would
        hand the whole system directory to one user. Narrowing to the files we
        placed removes the hazard entirely rather than guarding against it, and
        it is what was meant in the first place.

        A no-op on Windows, whose ownership model is different and whose
        ProgramData ACLs already grant what is needed.
        """
        if platform.system() == "Windows":
            return
        user = invoking_user()
        if user == "root":
            return
        for path in installed:
            try:
                shutil.chown(path, user=user, group=user)
            except (OSError, LookupError) as exc:
                print(f"  could not chown {path}: {exc}")

    def report_legacy_binaries(self) -> None:
        """Name the copies an earlier layout left behind, without touching them."""
        stale = [Path(p) for p in self.manifest.legacy_binaries]
        stale = [p for p in stale if p.exists()]
        if not stale:
            return
        print()
        print("An earlier layout installed this binary elsewhere. Still present:")
        for path in stale:
            print(f"    {path}")
        print("Nothing runs them now -- the service unit points at the new location.")
        print("Left in place deliberately: removing an executable you did not ask")
        print("to remove is not tidying up. Delete them once you are satisfied.")

    # -- Step 4 -----------------------------------------------------------

    def register(self, app_dir: Path) -> None:
        self.backend.install(self.manifest, app_dir, invoking_user())

    # -- Whole operations -------------------------------------------------

    def install(self, rebuild: bool = False) -> None:
        app_dir = self.manifest.app_dir()
        print(f"Installing {self.manifest.display_name} ({self.backend.name})")
        print(f"  user:   {invoking_user()}")
        print(f"  source: {self.manifest.repo_root}")
        print(f"  binaire : {app_dir}")
        print(f"  config  : {self.manifest.config_dir()}")
        print()

        self.check_privileges()
        binary = self.ensure_binary(rebuild=rebuild)
        self.stop_existing()
        written = self.install_files(binary, app_dir, self.manifest.config_dir())
        self.chown_to_user(written)
        self.register(app_dir)

        self.report_legacy_binaries()

        print()
        print(f"{self.manifest.display_name} installed and started.")
        if self.manifest.status_url:
            print(f"Check with:  curl {self.manifest.status_url}")

    def update(self) -> None:
        """Rebuild and replace, keeping the service registered.

        Distinct from install because it always rebuilds -- an update whose
        whole purpose is to ship new code must not silently reuse the binary
        that happens to be lying in the build directory.
        """
        if not self.backend.is_installed(self.manifest):
            raise DeployError(
                f"{self.manifest.display_name} is not installed on this machine.\n"
                "Run the install action first."
            )
        print(f"Updating {self.manifest.display_name} ({self.backend.name})")
        print()
        self.check_privileges()
        binary = self.ensure_binary(rebuild=True)
        self.stop_existing()
        app_dir = self.manifest.app_dir()
        written = self.install_files(binary, app_dir, self.manifest.config_dir())
        self.chown_to_user(written)
        self.register(app_dir)
        print()
        print(f"{self.manifest.display_name} updated.")

    def uninstall(self) -> None:
        print(f"Uninstalling {self.manifest.display_name} ({self.backend.name})")
        self.check_privileges()
        self.backend.uninstall(self.manifest)
        app_dir = self.manifest.app_dir()
        print()
        print("Service removed.")
        print(f"{app_dir} was kept -- it holds your configurations.")
        print(f"Remove it by hand if you mean to: {app_dir}")

    def status(self) -> None:
        if not self.backend.is_installed(self.manifest):
            print(f"{self.manifest.display_name}: not installed ({self.backend.name})")
            return
        print(self.backend.status(self.manifest))
