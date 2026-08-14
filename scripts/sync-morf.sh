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
# « morfBeacon » ou « morfBeacon_travail » : on prend le premier trouvé.
if [ -d "$SRC_BASE/morfBeacon" ]; then
  BEACON_SRC="$SRC_BASE/morfBeacon"
else
  BEACON_SRC="$SRC_BASE/morfBeacon_travail"
fi

sync_one morfBeacon "$BEACON_SRC" "$ROOT/third_party/morf/beacon"

# Le coeur de deploiement (morfdeploy) vient de morfTools et n'a ni include/ ni
# src/ : c'est un paquet Python, copie tel quel. Sans cette resynchronisation,
# la copie vendoree derive du jour ou morfTools evolue -- « morf doctor » le
# signale, mais autant ne pas creer l'ecart.
if [ -d "$SRC_BASE/morfTools" ]; then
  TOOLS_SRC="$SRC_BASE/morfTools"
else
  TOOLS_SRC="$SRC_BASE/morfTools_travail"
fi
# Coeur de deploiement (morfdeploy) : source de verite = depot dedie « morfDeploy »
# (promu sur le modele de morfBeacon). Repli transitoire sur morfTools/lib/morfdeploy
# tant que la migration du parc n'est pas terminee.
if [ -d "$SRC_BASE/morfDeploy" ]; then
  DEPLOY_SRC="$SRC_BASE/morfDeploy/morfdeploy"        ; DEPLOY_VER="$SRC_BASE/morfDeploy/VERSION"
elif [ -d "$SRC_BASE/morfDeploy_travail" ]; then
  DEPLOY_SRC="$SRC_BASE/morfDeploy_travail/morfdeploy"; DEPLOY_VER="$SRC_BASE/morfDeploy_travail/VERSION"
else
  DEPLOY_SRC="$TOOLS_SRC/lib/morfdeploy"              ; DEPLOY_VER=""
fi
DEPLOY_DST="$ROOT/third_party/morf/morfdeploy"
if [ -d "$DEPLOY_SRC" ]; then
  rm -rf "$DEPLOY_DST"
  mkdir -p "$DEPLOY_DST"
  cp -r "$DEPLOY_SRC/." "$DEPLOY_DST/"
  find "$DEPLOY_DST" -name __pycache__ -type d -prune -exec rm -rf {} +
  [ -n "$DEPLOY_VER" ] && [ -f "$DEPLOY_VER" ] && cp "$DEPLOY_VER" "$DEPLOY_DST/VERSION"
  echo "OK  morfdeploy$([ -f "$DEPLOY_DST/VERSION" ] && echo "  (version $(cat "$DEPLOY_DST/VERSION"))")"
else
  echo "!! Source introuvable pour morfdeploy : $DEPLOY_SRC" >&2
  echo "   (definir MORF_SRC_BASE si les depots sont ailleurs)" >&2
fi

echo "Synchronisation terminée. Le CMakeLists vendoré n'est pas modifié."
