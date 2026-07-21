"""One entry point, four actions, every platform.

Replaces install-service.sh, install-service.ps1, update-service.sh,
update-service.ps1, uninstall-service.sh and uninstall-service.ps1 in each
project. A project keeps a two-line wrapper so `./service.sh install` still
reads the way it always did.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from .backends import select
from .core import DeployError, Deployer
from .manifest import Manifest, ManifestError


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="morfdeploy",
        description="Install, update or remove a morfSystem service on this machine.",
    )
    parser.add_argument(
        "action",
        choices=("install", "update", "uninstall", "status"),
        help="What to do",
    )
    parser.add_argument(
        "--repo",
        type=Path,
        default=Path.cwd(),
        help="Project root holding service.json (default: current directory)",
    )
    parser.add_argument(
        "--rebuild",
        action="store_true",
        help="Rebuild even when a binary is already present (install only)",
    )
    return parser


def main(argv: list | None = None) -> int:
    args = build_parser().parse_args(argv)

    try:
        manifest = Manifest.load(args.repo.resolve())
    except ManifestError as exc:
        print(str(exc), file=sys.stderr)
        return 2

    # `status` must work on an unsupported platform: refusing to even report
    # what is installed would be unhelpful where being honest is the point.
    if args.action == "status":
        backend = select()
        if not backend.supported:
            print(backend.supported_note, file=sys.stderr)
            return 3

    try:
        deployer = Deployer(manifest)
        if args.action == "install":
            deployer.install(rebuild=args.rebuild)
        elif args.action == "update":
            deployer.update()
        elif args.action == "uninstall":
            deployer.uninstall()
        else:
            deployer.status()
    except DeployError as exc:
        print(f"\n{exc}", file=sys.stderr)
        return 1
    except NotImplementedError as exc:
        print(f"\n{exc}", file=sys.stderr)
        return 3
    except KeyboardInterrupt:
        print("\nInterrupted.", file=sys.stderr)
        return 130

    return 0


if __name__ == "__main__":
    sys.exit(main())
