#!/usr/bin/env bash
#
# new-service.sh — Amorce un nouveau service morfSystem a partir de ce template.
#
# Copie tout le projet dans un nouveau dossier en remplacant les noms :
#     morfMonitor  -> <NomCamel>   (projet / bibliotheque / cible CMake)
#     morfmonitor         -> <nom>        (namespace, dossier include, binaire,
#                                           unite systemd, fichier de config)
#     MORFMONITOR         -> <NOM>        (prefixe de macro)
#
# Usage :
#     scripts/new-service.sh <nom> <NomCamel> [dossier_destination]
# Exemple :
#     scripts/new-service.sh morfwatch morfWatch
#       -> cree ../morfWatch, pret a compiler, ou coder le metier.

set -euo pipefail

LOWER="${1:-}"; CAMEL="${2:-}"
if [[ -z "$LOWER" || -z "$CAMEL" ]]; then
    echo "Usage : $0 <nom-minuscule> <NomCamel> [dossier_destination]" >&2
    echo "Ex.   : $0 morfwatch morfWatch" >&2
    exit 1
fi
UPPER="$(echo "$LOWER" | tr '[:lower:]' '[:upper:]')"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DEST="${3:-$(cd "$ROOT/.." && pwd)/${CAMEL}}"

if [[ -e "$DEST" ]]; then
    echo "Destination deja existante : $DEST" >&2
    exit 1
fi

echo "Template : $ROOT"
echo "Nouveau  : $DEST  ($CAMEL / $LOWER / $UPPER)"

# --- 1. Copier l'arbre (sans .git ni artefacts de build) -----------------
mkdir -p "$DEST"
if command -v rsync >/dev/null; then
    rsync -a --exclude='.git' --exclude='build' --exclude='build-*' "$ROOT"/ "$DEST"/
else
    cp -a "$ROOT"/. "$DEST"/
    rm -rf "$DEST/.git" "$DEST"/build "$DEST"/build-* 2>/dev/null || true
fi

# --- 2. Remplacer les jetons dans tous les fichiers texte ----------------
# (on ne touche pas a third_party/, code vendore de morfBeacon)
grep -rlZ --binary-files=without-match \
     -e 'morfMonitor' -e 'morfmonitor' -e 'MORFMONITOR' \
     "$DEST" 2>/dev/null | while IFS= read -r -d '' f; do
    case "$f" in */third_party/morf/beacon/*) continue;; esac  # ne pas toucher au code vendore
    sed -i -e "s/morfMonitor/$CAMEL/g" \
           -e "s/morfmonitor/$LOWER/g" \
           -e "s/MORFMONITOR/$UPPER/g" "$f"
done

# --- 3. Renommer les fichiers/dossiers portant l'ancien nom --------------
mv "$DEST/include/morfmonitor"                 "$DEST/include/$LOWER"
mv "$DEST/config/morfmonitor.example.json"     "$DEST/config/$LOWER.example.json"
mv "$DEST/scripts/linux/morfmonitor.service"   "$DEST/scripts/linux/$LOWER.service"

echo "Termine."
echo "  cd \"$DEST\""
echo "  cmake --preset mingw && cmake --build --preset mingw   # doit compiler tel quel"
echo "  puis : coder le metier dans src/ExampleModule.* et adapter ModuleFactory / CMakeLists."
