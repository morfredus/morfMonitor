#!/usr/bin/env bash
#
# update-service.sh — Met a jour morfMonitor installe en service.
#
# Recupere le code (git pull), recompile, recopie le binaire dans le dossier fixe,
# rafraichit l'unite systemd, complete la configuration, puis redemarre le
# service. Les valeurs deja presentes dans morfmonitor.json ne sont JAMAIS
# modifiees ; seuls les parametres apparus depuis l'installation y sont
# ajoutes, puis listes. Complement de install-service.sh.
#
# Usage :
#   sudo ./scripts/linux/update-service.sh           # git pull + build + restart
#   sudo ./scripts/linux/update-service.sh --no-pull # rebuild seulement

set -euo pipefail

SERVICE_NAME="morfmonitor"
UNIT_DEST="/etc/systemd/system/$SERVICE_NAME.service"
APP_DIR="${MT_APP_DIR:-/opt/morfmonitor}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
RUN_USER="${SUDO_USER:-$(logname 2>/dev/null || echo root)}"

if [[ "${EUID}" -ne 0 ]]; then
    echo "Ce script doit etre lance avec sudo :  sudo $0 $*" >&2
    exit 1
fi
if [[ ! -f "$UNIT_DEST" ]]; then
    echo "Service '$SERVICE_NAME' non installe. Lance d'abord :  sudo ./scripts/linux/install-service.sh" >&2
    exit 1
fi

NO_PULL=0
NO_CONFIG=0
for arg in "$@"; do
    case "$arg" in
        --no-pull)   NO_PULL=1 ;;
        --no-config) NO_CONFIG=1 ;;
        *) echo "Option inconnue : $arg" >&2; exit 1 ;;
    esac
done

# --- Recuperer le code (en tant que l'utilisateur) -----------------------
if [[ $NO_PULL -eq 0 ]]; then
    echo "git pull (utilisateur $RUN_USER)..."
    sudo -u "$RUN_USER" bash -c "cd '$REPO_ROOT' && git pull --ff-only"
fi

# --- Recompiler (preset selon l'architecture) ----------------------------
ARCH="$(uname -m)"
if [[ "$ARCH" == "aarch64" || "$ARCH" == "arm64" ]]; then
    PRESET="linux-arm64"; BUILD_DIR="$REPO_ROOT/build-arm64"
else
    PRESET="linux";       BUILD_DIR="$REPO_ROOT/build"
fi
echo "Compilation (preset $PRESET)..."
sudo -u "$RUN_USER" bash -lc "cd '$REPO_ROOT' && cmake --preset $PRESET && cmake --build --preset $PRESET"
BIN="$BUILD_DIR/service/morfmonitor"
[[ -x "$BIN" ]] || { echo "Echec : $BIN introuvable apres compilation." >&2; exit 1; }

# --- Recopier le binaire (config preservee) ------------------------------
echo "Copie du binaire vers $APP_DIR..."
systemctl stop "$SERVICE_NAME" 2>/dev/null || true
install -m 0755 "$BIN" "$APP_DIR/morfmonitor"
chown "$RUN_USER:$RUN_USER" "$APP_DIR/morfmonitor"

# --- Rafraichir l'unite systemd ------------------------------------------
# Une modification du fichier .service dans le depot ne parvenait jamais a
# /etc/systemd/system : le service continuait de tourner avec l'ancienne
# definition, sans que rien ne le signale.
if [[ -f "$SCRIPT_DIR/morfmonitor.service" ]]; then
    NEW_UNIT="$(mktemp)"
    sed -e "s/__RUN_USER__/$RUN_USER/g" -e "s#__APP_DIR__#${APP_DIR:-}#g" \
        "$SCRIPT_DIR/morfmonitor.service" > "$NEW_UNIT"
    if ! cmp -s "$NEW_UNIT" "$UNIT_DEST"; then
        echo "Unite systemd modifiee : mise a jour."
        install -m 0644 "$NEW_UNIT" "$UNIT_DEST"
        systemctl daemon-reload
    fi
    rm -f "$NEW_UNIT"
fi

# --- Completer la configuration ------------------------------------------
# Les valeurs deja en place ne sont JAMAIS modifiees, mais les parametres
# APPARUS depuis l'installation sont ajoutes. Sans cela, une version
# introduisant un parametre le laissait absent indefiniment et la fonction
# correspondante ne s'activait jamais, en silence.
CONFIG_FILE="$APP_DIR/morfmonitor.json"
EXAMPLE_FILE="$REPO_ROOT/config/morfmonitor.example.json"
if [[ $NO_CONFIG -eq 0 && -f "$EXAMPLE_FILE" ]]; then
    if [[ ! -f "$CONFIG_FILE" ]]; then
        # Config absente (installation partielle, dossier efface) : on la cree
        # plutot que de laisser le service demarrer sans.
        mkdir -p "$(dirname "$CONFIG_FILE")"
        install -m 0644 "$EXAMPLE_FILE" "$CONFIG_FILE"
        echo "Config absente : copiee depuis l'exemple -> $CONFIG_FILE (a adapter)."
    elif command -v python3 >/dev/null 2>&1; then
        # Sauvegarde avant toute modification : la config porte des reglages que
        # l'utilisateur ne pourrait pas retrouver.
        BACKUP="$CONFIG_FILE.bak-$(date +%Y%m%d-%H%M%S)"
        cp "$CONFIG_FILE" "$BACKUP"
        ADDED="$(python3 "$SCRIPT_DIR/merge-config.py" "$EXAMPLE_FILE" "$CONFIG_FILE" || true)"
        if [[ -n "$ADDED" ]]; then
            echo
            echo "Nouveaux parametres ajoutes a $CONFIG_FILE :"
            echo "$ADDED" | sed 's/^/    /'
            echo "  (valeurs existantes inchangees ; sauvegarde : $BACKUP)"
            echo "  A RENSEIGNER si besoin avant que la fonction correspondante s'active."
            echo
        else
            rm -f "$BACKUP"
        fi

        # La fusion ajoute ce qui MANQUE ; elle ne corrige pas une valeur deja
        # presente devenue invalide. Un module dont le type a disparu de la
        # fabrique reste donc en place, et le service demarre sans rien
        # superviser. Cette verification ne modifie rien : elle constate, pour
        # que la mise a jour ne laisse pas derriere elle un service muet.
        if [[ -f "$SCRIPT_DIR/check-config.py" ]]; then
            echo
            if python3 "$SCRIPT_DIR/check-config.py" "$CONFIG_FILE" \
                    --binary "$APP_DIR/morfmonitor" --example "$EXAMPLE_FILE" --hint-style sh; then
                :
            else
                echo
                echo "La configuration deployee ne permettra pas au service de collecter."
                echo "Inspecter puis corriger :"
                echo "    sudo $SCRIPT_DIR/config-tool.sh diff"
                echo "    sudo $SCRIPT_DIR/config-tool.sh reset"
                echo "La mise a jour se poursuit : ce diagnostic ne modifie rien."
            fi
            echo
        fi
    else
        echo "python3 absent : configuration non completee (voir $EXAMPLE_FILE)." >&2
    fi
fi

# --- Redemarrer ----------------------------------------------------------
systemctl start "$SERVICE_NAME"
sleep 1
echo "Mise a jour appliquee."
systemctl --no-pager --lines=0 status "$SERVICE_NAME" || true
