#!/usr/bin/env bash
#
# config-tool.sh - gestion de la configuration DEPLOYEE de morfMonitor.
#
# Pourquoi cet outil existe
# -------------------------
# L'installation et la mise a jour ne remplacent JAMAIS morfmonitor.json : il
# porte des reglages locaux que l'utilisateur ne pourrait pas retrouver. C'est la
# bonne regle, mais elle laisse un angle mort. `merge-config.py` ajoute les cles
# APPARUES depuis l'installation ; il ne corrige pas une valeur DEJA PRESENTE
# devenue invalide.
#
# C'est ce qui est arrive au module `example`, herite du gabarit : la cle
# `modules` existait, la fusion n'y touchait pas, et le service tournait en
# repondant 503 sur toutes les routes /api/ faute de module de supervision.
#
# La reconciliation d'une valeur existante ne peut pas etre automatique : seul
# l'utilisateur sait si une valeur est un reglage voulu ou un residu. Cet outil
# la rend donc EXPLICITE et sure : on constate d'abord, on agit sur demande, et
# toute ecriture est precedee d'une sauvegarde datee.
#
# Actions :
#   status   ou est la configuration, est-elle valide, est-elle exploitable
#   check    diagnostic detaille (types de modules, cles manquantes, exposition)
#   diff     differences entre la configuration deployee et l'exemple
#   merge    AJOUTE les cles manquantes, ne modifie aucune valeur existante
#   reset    REMPLACE la configuration par l'exemple (confirmation requise)
#
# Vocabulaire aligne sur morfTools/shared-config.sh, qui gere de la meme facon
# la configuration partagee /etc/morfsystem/morfsystem.json.
#
# Usage :
#   ./scripts/linux/config-tool.sh status
#   sudo ./scripts/linux/config-tool.sh merge
#   sudo ./scripts/linux/config-tool.sh reset
#   sudo MT_APP_DIR=/opt/autre ./scripts/linux/config-tool.sh status

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

APP_DIR="${MT_APP_DIR:-/opt/morfmonitor}"
CONFIG_FILE="$APP_DIR/morfmonitor.json"
EXAMPLE_FILE="$REPO_ROOT/config/morfmonitor.example.json"
BINARY="$APP_DIR/morfmonitor"
SERVICE_NAME="${MT_SERVICE_NAME:-morfmonitor}"

ACTION="${1:-status}"

die() { echo "$*" >&2; exit 1; }

need_config() {
    [[ -f "$CONFIG_FILE" ]] || die "Configuration absente : $CONFIG_FILE
Installer le service d'abord : sudo ./scripts/linux/install-service.sh"
}

need_example() {
    [[ -f "$EXAMPLE_FILE" ]] || die "Exemple introuvable : $EXAMPLE_FILE"
}

# Sauvegarde datee avant toute ecriture. Jamais d'exception : une configuration
# perdue ne se reconstitue pas.
backup() {
    local dest="$CONFIG_FILE.bak-$(date +%Y%m%d-%H%M%S)"
    cp "$CONFIG_FILE" "$dest"
    echo "$dest"
}

run_check() {
    local args=("$CONFIG_FILE")
    [[ -x "$BINARY" ]] && args+=(--binary "$BINARY")
    [[ -f "$EXAMPLE_FILE" ]] && args+=(--example "$EXAMPLE_FILE")
    python3 "$SCRIPT_DIR/check-config.py" "${args[@]}"
}

case "$ACTION" in

status)
    echo "Dossier du service : $APP_DIR"
    echo "Configuration      : $CONFIG_FILE"
    if [[ -f "$CONFIG_FILE" ]]; then
        echo "                     presente ($(stat -c %y "$CONFIG_FILE" 2>/dev/null | cut -d. -f1))"
    else
        echo "                     ABSENTE"
        exit 1
    fi
    echo "Exemple du depot   : $EXAMPLE_FILE"
    echo "Binaire            : $BINARY $([[ -x "$BINARY" ]] || echo '(absent)')"
    echo
    run_check
    ;;

check)
    need_config
    run_check
    ;;

diff)
    need_config; need_example
    # Les cles _comment documentent l'exemple : les afficher noierait les vraies
    # differences sous du commentaire.
    python3 - "$EXAMPLE_FILE" "$CONFIG_FILE" <<'PY'
import json, sys

def strip(o):
    if isinstance(o, dict):
        return {k: strip(v) for k, v in o.items() if not k.startswith("_comment")}
    if isinstance(o, list):
        return [strip(v) for v in o]
    return o

ex, live = (strip(json.load(open(p, encoding="utf-8-sig"))) for p in sys.argv[1:3])
a = json.dumps(ex, indent=2, ensure_ascii=False, sort_keys=True).splitlines()
b = json.dumps(live, indent=2, ensure_ascii=False, sort_keys=True).splitlines()

import difflib
out = list(difflib.unified_diff(a, b, fromfile="exemple du depot",
                                tofile="configuration deployee", lineterm=""))
print("\n".join(out) if out else "Aucune difference (hors commentaires).")
PY
    ;;

merge)
    need_config; need_example
    B="$(backup)"
    ADDED="$(python3 "$SCRIPT_DIR/merge-config.py" "$EXAMPLE_FILE" "$CONFIG_FILE" || true)"
    if [[ -n "$ADDED" ]]; then
        echo "Cles ajoutees (valeurs existantes inchangees) :"
        echo "$ADDED" | sed 's/^/    /'
        echo "Sauvegarde : $B"
        echo
        echo "Redemarrer pour appliquer : sudo systemctl restart $SERVICE_NAME"
    else
        rm -f "$B"
        echo "Rien a ajouter : la configuration a deja toutes les cles de l'exemple."
        echo
        echo "Note : 'merge' n'ajoute que ce qui MANQUE. Si le service ne collecte"
        echo "rien alors que la fusion ne trouve rien a ajouter, la cause est une"
        echo "valeur existante devenue invalide -- lancer 'check'."
    fi
    ;;

reset)
    need_config; need_example
    echo "Cette action REMPLACE la configuration deployee par l'exemple du depot."
    echo "  deployee : $CONFIG_FILE"
    echo "  exemple  : $EXAMPLE_FILE"
    echo
    echo "Tout reglage local (adresses, ports, seuils) sera perdu et devra etre"
    echo "ressaisi. Une sauvegarde datee est conservee a cote du fichier."
    echo
    echo "Preferer 'merge' si seules des cles manquent, et 'diff' pour voir ce"
    echo "qui change avant de decider."
    echo
    # --yes n'existe que pour l'automatisation (banc de test, reprovisionnement).
    # En usage normal, la confirmation est demandee : sans terminal et sans
    # --yes, on refuse plutot que de deviner.
    if [[ "${2:-}" == "--yes" ]]; then
        echo "Confirmation fournie par --yes."
    elif [[ ! -t 0 ]]; then
        die "Confirmation impossible (entree non interactive) : action annulee.
Pour un usage scripte, passer explicitement : config-tool.sh reset --yes"
    else
        read -r -p "Taper REMPLACER pour confirmer : " answer
        [[ "$answer" == "REMPLACER" ]] || { echo "Annule."; exit 1; }
    fi

    B="$(backup)"
    install -m 0644 "$EXAMPLE_FILE" "$CONFIG_FILE"
    echo "Configuration remplacee. Sauvegarde : $B"
    echo
    run_check || true
    echo
    echo "Redemarrer pour appliquer : sudo systemctl restart $SERVICE_NAME"
    ;;

*)
    die "Action inconnue : $ACTION
Actions : status | check | diff | merge | reset"
    ;;
esac
