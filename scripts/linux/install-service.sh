#!/usr/bin/env bash
#
# install-service.sh — Installe morfSensor en service systemd robuste.
#
# Compile le service (si besoin), copie le binaire + la configuration dans un
# dossier FIXE (par defaut /opt/morfmonitor), hors du clone git, puis installe et
# active le service « morfmonitor » pointant la. Deplacer le depot (ou une synchro
# Syncthing) ne casse plus rien.
#
# Usage :
#   sudo ./scripts/linux/install-service.sh
#   sudo MT_APP_DIR=/opt/msensor ./scripts/linux/install-service.sh   # autre dossier
#   sudo ./scripts/linux/install-service.sh --uninstall

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

# --- Desinstallation ------------------------------------------------------
if [[ "${1:-}" == "--uninstall" ]]; then
    echo "Desinstallation de $SERVICE_NAME..."
    systemctl disable --now "$SERVICE_NAME" 2>/dev/null || true
    rm -f "$UNIT_DEST"
    systemctl daemon-reload
    echo "Service supprime. (Dossier $APP_DIR conserve — le retirer : sudo rm -rf $APP_DIR)"
    exit 0
fi

echo "Utilisateur  : $RUN_USER"
echo "Source       : $REPO_ROOT"
echo "Installation : $APP_DIR"

# --- 1. Trouver ou compiler le binaire -----------------------------------
# Choix du preset selon l'architecture (aarch64 -> linux-arm64, sinon linux).
ARCH="$(uname -m)"
if [[ "$ARCH" == "aarch64" || "$ARCH" == "arm64" ]]; then
    PRESET="linux-arm64"; BUILD_DIR="$REPO_ROOT/build-arm64"
else
    PRESET="linux";       BUILD_DIR="$REPO_ROOT/build"
fi
BIN="$BUILD_DIR/service/morfmonitor"

if [[ ! -x "$BIN" ]]; then
    echo "Binaire absent, compilation (preset $PRESET)..."
    # Compile en tant qu'utilisateur normal (pas root) pour ne pas polluer le depot.
    sudo -u "$RUN_USER" bash -lc "cd '$REPO_ROOT' && cmake --preset $PRESET && cmake --build --preset $PRESET"
fi
[[ -x "$BIN" ]] || { echo "Echec : $BIN introuvable apres compilation." >&2; exit 1; }

# --- 2. Arreter l'ancien lancement ---------------------------------------
systemctl stop "$SERVICE_NAME" 2>/dev/null || true

# --- 3. Copier binaire + config dans le dossier fixe ---------------------
mkdir -p "$APP_DIR"
install -m 0755 "$BIN" "$APP_DIR/morfmonitor"

# La config n'est PAS ecrasee si elle existe deja (reglages locaux preserves).
if [[ ! -f "$APP_DIR/morfmonitor.json" ]]; then
    install -m 0644 "$REPO_ROOT/config/morfmonitor.example.json" "$APP_DIR/morfmonitor.json"
    echo "Config initiale copiee : $APP_DIR/morfmonitor.json (a adapter)."
else
    echo "Config existante conservee : $APP_DIR/morfmonitor.json"
fi
chown -R "$RUN_USER:$RUN_USER" "$APP_DIR"

# --- 4. Installer et demarrer le service ---------------------------------
sed -e "s/__RUN_USER__/$RUN_USER/g" -e "s#__APP_DIR__#$APP_DIR#g" \
    "$SCRIPT_DIR/morfmonitor.service" > "$UNIT_DEST"
chmod 0644 "$UNIT_DEST"
systemctl daemon-reload
systemctl enable --now "$SERVICE_NAME"
echo "Service '$SERVICE_NAME' installe (ExecStart -> $APP_DIR/morfmonitor) et demarre."

echo
sleep 1
systemctl --no-pager --lines=0 status "$SERVICE_NAME" || true
echo
echo "Journaux :  journalctl -u $SERVICE_NAME -f"
echo "Test API :  curl http://127.0.0.1:8799/status"
