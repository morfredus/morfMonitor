"""The contract a platform must satisfy to host a morfSystem service.

The orchestration around a service install is identical everywhere: find or
build the binary, stop whatever runs, copy the binary and the configurations
into a fixed directory, then hand the result to the system's service manager.
Only that last step is platform business, and it is the only thing this
interface describes.

Keeping the interface this narrow is what makes a new platform cheap. A
contributor adding macOS writes one module implementing these methods; they
touch no orchestration, no configuration handling, and no project. If a method
here ever needs to know *which* platform it runs on, the boundary has been
drawn in the wrong place.
"""

from __future__ import annotations

import abc
from pathlib import Path

from ..manifest import Manifest


class ServiceBackend(abc.ABC):
    """A system service manager: systemd, Windows SCM, launchd."""

    #: Human-readable name, used in messages and diagnostics.
    name: str = "unknown"

    #: False for platforms whose backend exists but has never been validated.
    #: The parc supports what it can test; see `supported_note`.
    supported: bool = True

    #: Why a backend is not supported, shown verbatim to whoever hits it.
    supported_note: str = ""

    # -- Interrogation ----------------------------------------------------

    @abc.abstractmethod
    def is_installed(self, manifest: Manifest) -> bool:
        """True when the service is registered with the system."""

    @abc.abstractmethod
    def status(self, manifest: Manifest) -> str:
        """A short human-readable status, for display only.

        Never parsed: callers that need a decision use `is_installed`. Status
        text is the most volatile thing a service manager produces, and code
        that reads it breaks on a system upgrade rather than on a change of
        ours.
        """

    # -- Lifecycle --------------------------------------------------------

    @abc.abstractmethod
    def install(self, manifest: Manifest, app_dir: Path, run_user: str) -> None:
        """Register the service so it starts on boot, and start it now.

        Must be idempotent: reinstalling over an existing service is the normal
        update path, not an error.
        """

    @abc.abstractmethod
    def stop(self, manifest: Manifest) -> None:
        """Stop the service if it runs; do nothing if it does not.

        Called before the binary is replaced, so a service that is absent, or
        already stopped, is a success and not a failure -- the goal is "not
        running", and it is already met.
        """

    @abc.abstractmethod
    def start(self, manifest: Manifest) -> None:
        """Start the service."""

    @abc.abstractmethod
    def uninstall(self, manifest: Manifest) -> None:
        """Deregister the service.

        Leaves the application directory alone. It holds configurations the
        person edited by hand, and an uninstall that silently discards them is
        the kind of surprise no message can undo afterwards.
        """

    # -- Privileges -------------------------------------------------------

    @abc.abstractmethod
    def requires_privileges(self) -> bool:
        """True when the operations above need administrator rights."""

    @abc.abstractmethod
    def has_privileges(self) -> bool:
        """True when the current process holds those rights."""

    def privilege_hint(self) -> str:
        """How to re-run with the required rights, in this platform's terms."""
        return "Re-run with administrator privileges."
