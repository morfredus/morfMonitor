#!/usr/bin/env bash
# Resynchronise la copie vendorée de morfBeacon dans third_party/morf/beacon
# depuis le dépôt source voisin.
#
# Source par défaut : le dossier parent du projet (ex. 01-Travail/).
# Surcharge possible : MORF_SRC_BASE=/chemin/vers/les/depots scripts/sync-morf.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"           # racine du projet
SRC_BASE="${MORF_SRC_BASE:-$(cd "$ROOT/.." && pwd)}"

sync_one() {
  local name="$1" srcdir="$2" dstdir="$3"
  if [ ! -d "$srcdir" ]; then
    echo "!! Source introuvable pour $name : $srcdir" >&2
    echo "   (définir MORF_SRC_BASE si les dépôts sont ailleurs)" >&2
    return 1
  fi
  rm -rf "$dstdir/include" "$dstdir/src"
  cp -r "$srcdir/include" "$dstdir/include"
  cp -r "$srcdir/src"     "$dstdir/src"
  cp    "$srcdir/VERSION" "$dstdir/VERSION"
  echo "OK  $name  (version $(cat "$dstdir/VERSION"))"
}

# morfMonitor n'embarque que morfBeacon. Le dépôt source peut s'appeler
# « morfBeacon » ou « morfBeacon » : on prend le premier trouvé.
if [ -d "$SRC_BASE/morfBeacon" ]; then
  BEACON_SRC="$SRC_BASE/morfBeacon"
else
  BEACON_SRC="$SRC_BASE/morfBeacon"
fi

sync_one morfBeacon "$BEACON_SRC" "$ROOT/third_party/morf/beacon"
echo "Synchronisation terminée. Le CMakeLists vendoré n'est pas modifié."
