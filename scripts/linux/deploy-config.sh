#!/usr/bin/env bash
#
# deploy-config.sh — copie les configurations du depot vers leurs emplacements
#                    d'installation. C'est LE script de deploiement.
#
# Il y a DEUX fichiers de configuration, et ils ne vont pas au meme endroit :
#
#   config/morfmonitor.json  ->  /etc/morfmonitor/morfmonitor.json
#       Reglages du service lui-meme : port, adresse d'ecoute, modules.
#       Lu par morfMonitor seul.
#
#   config/morfsystem.json   ->  /etc/morfsystem/morfsystem.json
#       Description de CE QUI EST SUPERVISE : services systemd, sondes reseau,
#       applications attendues. Lu par morfMonitor ET morfDashboard, d'ou
#       son emplacement partage dans /etc.
#
# Par defaut le script deploie LES DEUX, parce que c'est le geste courant et
# qu'obliger a s'en souvenir n'aide personne.
#
# Chaque fichier est sauvegarde avant d'etre remplace (.bak-<date> a cote), et
# les differences appliquees sont affichees. Rien n'est perdu.
#
# Ne PAS prefixer par sudo : le script n'eleve que les ecritures systeme. La
# lecture, la comparaison et l'affichage n'ont aucun besoin des droits root.
#
# Usage :
#   ./scripts/linux/deploy-config.sh                # les deux, puis redemarre
#   ./scripts/linux/deploy-config.sh --service      # seulement /opt
#   ./scripts/linux/deploy-config.sh --shared       # seulement /etc
#   ./scripts/linux/deploy-config.sh --no-restart   # sans redemarrer
#   ./scripts/linux/deploy-config.sh --if-absent    # ne placer que ce qui manque
#
# Pour AJOUTER les nouvelles cles sans ecraser vos reglages, voir
# update-service.sh ; pour diagnostiquer une configuration deployee, voir
# config-tool.sh.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# La configuration du service vit dans /etc, plus a cote du binaire : c'est la
# convention Linux, et cela survit a un effacement de /opt pour reinstaller.
# APP_DIR reste le dossier du BINAIRE, utilise ici seulement pour verifier que
# le service est bien installe avant de deployer sa configuration.
APP_DIR="${MORF_APP_DIR:-${MT_APP_DIR:-/opt/morfmonitor}}"
CONFIG_DIR="${MORF_CONFIG_DIR:-/etc/morfmonitor}"
SHARED_DIR="${MT_SHARED_DIR:-/etc/morfsystem}"
SERVICE_NAME="${MT_SERVICE_NAME:-morfmonitor}"

# Commande d'elevation. Vide quand on est deja root, et surchargeable par
# MT_SUDO pour deployer vers un emplacement accessible sans privileges — ce qui
# rend le script VERIFIABLE. Sans cela il n'etait testable que sur une machine
# reelle, et un « sudo » d'une autre plateforme (Windows en fournit un qui
# renvoie toujours 0) faisait passer les verifications pour bonnes a tort.
SUDO="${MT_SUDO-sudo}"
[[ "${EUID:-1}" -eq 0 ]] && SUDO=""

RESTART=1
DO_SERVICE=1
DO_SHARED=1
# --if-absent : ne rien ecraser, ne placer que les fichiers manquants. C'est ce
# dont l'installation a besoin -- elle doit produire un systeme qui fonctionne
# sans jamais effacer les reglages d'une installation precedente.
IF_ABSENT=0

for arg in "$@"; do
    case "$arg" in
        --no-restart) RESTART=0 ;;
        --if-absent)  IF_ABSENT=1 ;;
        --service)    DO_SHARED=0 ;;
        --shared)     DO_SERVICE=0 ;;
        -h|--help)    sed -n '3,34p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Option inconnue : $arg  (--service | --shared | --no-restart | --if-absent)" >&2; exit 2 ;;
    esac
done

# Source : la configuration REELLE du depot si elle existe, l'exemple sinon.
# Garder un vrai fichier dans le clone en fait donc la reference deployee.
pick_source() {
    local base="$1"
    if [[ -f "$REPO_ROOT/config/$base.json" ]]; then
        printf '%s\n' "$REPO_ROOT/config/$base.json"
    elif [[ -f "$REPO_ROOT/config/$base.example.json" ]]; then
        printf '%s\n' "$REPO_ROOT/config/$base.example.json"
    fi
    return 0
}

# Copie un fichier vers sa destination, en sauvegardant et en montrant ce qui
# change. Un deploiement muet laisse l'utilisateur sans moyen de savoir si son
# geste a fait ce qu'il croyait.
deploy_one() {
    local src="$1" dest="$2" label="$3"

    if (( IF_ABSENT )) && $SUDO test -f "$dest"; then
        echo "── $label"
        echo "   deja present, conserve : $dest"
        echo
        return 0
    fi

    echo "── $label"
    echo "   source      : $src"
    echo "   destination : $dest"

    $SUDO install -d -m 0755 "$(dirname "$dest")"

    if $SUDO test -f "$dest"; then
        local backup="$dest.bak-$(date +%Y%m%d-%H%M%S)"
        $SUDO cp -p "$dest" "$backup"
        echo "   sauvegarde  : $backup"
        if ! $SUDO diff -q "$backup" "$src" >/dev/null 2>&1; then
            local out lines
            out="$($SUDO diff -u "$backup" "$src" | tail -n +3 || true)"
            lines="$(printf '%s\n' "$out" | wc -l)"
            echo
            echo "   differences appliquees :"
            printf '%s\n' "$out" | head -n 25 | sed 's/^/      /'
            (( lines > 25 )) && echo "      ... ($((lines - 25)) lignes de plus)"
            echo
        else
            echo "   (identique : rien ne change)"
        fi
    fi

    $SUDO install -m 0644 "$src" "$dest"
    echo "   copie       : OK"
    echo
}

deployed=0

if (( DO_SERVICE )); then
    SRC="$(pick_source morfmonitor)"
    if [[ -z "$SRC" ]]; then
        echo "Aucune configuration de service dans $REPO_ROOT/config/" >&2; exit 1
    elif [[ ! -d "$APP_DIR" ]] && (( ! IF_ABSENT )); then
        echo "Service non installe : $APP_DIR absent." >&2
        echo "Lancer d'abord : ./scripts/linux/install-service.sh" >&2
        exit 1
    fi
    deploy_one "$SRC" "$CONFIG_DIR/morfmonitor.json" "Configuration du service   ->  $CONFIG_DIR"
    deployed=1
fi

if (( DO_SHARED )); then
    SRC="$(pick_source morfsystem)"
    if [[ -z "$SRC" ]]; then
        echo "Aucune configuration partagee dans $REPO_ROOT/config/" >&2; exit 1
    fi
    deploy_one "$SRC" "$SHARED_DIR/morfsystem.json" "Configuration partagee     ->  $SHARED_DIR"
    deployed=1
fi

(( deployed )) || { echo "Rien a deployer." >&2; exit 1; }

if (( RESTART )) && command -v systemctl >/dev/null 2>&1; then
    # La configuration partagee est lue par les DEUX programmes : redemarrer
    # morfMonitor seul laisserait le Dashboard sur l'ancienne description du parc.
    for unit in "$SERVICE_NAME" morfdashboard; do
        if $SUDO systemctl cat "$unit" >/dev/null 2>&1; then
            $SUDO systemctl restart "$unit"
            echo "Redemarre : $unit"
        fi
    done
    echo
    echo "Verifier :"
    echo "    curl -s http://127.0.0.1:8790/api/config | head -c 200"
    echo "    journalctl -u $SERVICE_NAME -n 20"
else
    echo "Redemarrer pour appliquer :"
    echo "    $SUDO systemctl restart $SERVICE_NAME morfdashboard"
fi
