#!/usr/bin/env bash
#
# deploy-config.sh - copie la configuration du depot vers le service installe.
#
# C'est la version DIRECTE : elle ecrase la configuration deployee par celle du
# depot, point. Pas de fusion, pas de python, pas de question.
#
#   depot                                  service installe
#   config/morfmonitor.json         --->   /opt/morfmonitor/morfmonitor.json
#   (a defaut : morfmonitor.example.json)
#
# A utiliser quand la configuration de reference est celle du depot. La
# configuration deployee est sauvegardee avant d'etre remplacee, donc rien n'est
# perdu : si elle contenait des reglages propres a la machine, ils sont dans le
# fichier .bak-<date> a cote.
#
# Pour ajouter seulement les nouvelles cles sans toucher a l'existant, utiliser
# plutot update-service.sh (qui appelle merge-config.py).
#
# Usage :
#   ./scripts/linux/deploy-config.sh                  # copie puis redemarre
#   ./scripts/linux/deploy-config.sh --no-restart     # copie seulement
#   MT_APP_DIR=/opt/autre ./scripts/linux/deploy-config.sh
#
# NE PAS prefixer par sudo : le script n'eleve QUE les ecritures systeme, comme
# le fait « config.sh shared ». Exiger sudo sur tout le script ferait tourner en
# root la lecture, la comparaison et l'affichage, sans aucun besoin -- et les
# deux sous-commandes de « config.sh » demanderaient l'inverse l'une de l'autre.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

APP_DIR="${MT_APP_DIR:-/opt/morfmonitor}"
SERVICE_NAME="${MT_SERVICE_NAME:-morfmonitor}"
DEST="$APP_DIR/morfmonitor.json"

RESTART=1
[[ "${1:-}" == "--no-restart" ]] && RESTART=0

# Source : la configuration reelle du depot si elle existe, sinon l'exemple.
SRC="$REPO_ROOT/config/morfmonitor.json"
[[ -f "$SRC" ]] || SRC="$REPO_ROOT/config/morfmonitor.example.json"
[[ -f "$SRC" ]] || { echo "Aucune configuration source dans $REPO_ROOT/config/" >&2; exit 1; }

[[ -d "$APP_DIR" ]] || { echo "Service non installe : $APP_DIR absent.
Lancer d'abord : sudo ./scripts/linux/install-service.sh" >&2; exit 1; }

echo "Source      : $SRC"
echo "Destination : $DEST"

# Sauvegarde avant ecrasement : l'ancienne configuration peut porter des
# reglages que personne ne saurait retrouver.
if [[ -f "$DEST" ]]; then
    BACKUP="$DEST.bak-$(date +%Y%m%d-%H%M%S)"
    sudo cp "$DEST" "$BACKUP"
    echo "Sauvegarde  : $BACKUP"
    # Apercu des differences, plafonne : le but est de voir d'un coup d'oeil ce
    # qui change, pas de relire les deux fichiers.
    if command -v diff >/dev/null 2>&1 && ! diff -q "$BACKUP" "$SRC" >/dev/null; then
        DIFF_OUT="$(diff -u "$BACKUP" "$SRC" | tail -n +3 || true)"
        LINES="$(printf '%s\n' "$DIFF_OUT" | wc -l)"
        echo
        echo "Differences appliquees :"
        printf '%s\n' "$DIFF_OUT" | head -n 30 | sed 's/^/    /'
        (( LINES > 30 )) && echo "    ... ($((LINES - 30)) lignes de plus ; comparer avec : diff -u \"$BACKUP\" \"$SRC\")"
        echo
    fi
fi

sudo install -m 0644 "$SRC" "$DEST"
echo "Configuration copiee."

if [[ $RESTART -eq 1 ]] && command -v systemctl >/dev/null 2>&1; then
    sudo systemctl restart "$SERVICE_NAME"
    sleep 1
    echo
    sudo systemctl --no-pager --lines=0 status "$SERVICE_NAME" || true
    echo
    echo "Verifier que le service collecte :"
    echo "    curl -s http://127.0.0.1:8790/api/all | head -c 200"
    echo "    journalctl -u $SERVICE_NAME -n 20"
else
    echo "Redemarrer pour appliquer : sudo systemctl restart $SERVICE_NAME"
fi
