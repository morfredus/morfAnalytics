#!/usr/bin/env bash
#
# update-service.sh — Met a jour morfSensor installe en service.
#
# Recupere le code (git pull), recompile, recopie le binaire dans le dossier fixe,
# puis redemarre le service. La config locale (morfanalytics.json) n'est jamais
# ecrasee. Complement de install-service.sh.
#
# Usage :
#   sudo ./scripts/linux/update-service.sh           # git pull + build + restart
#   sudo ./scripts/linux/update-service.sh --no-pull # rebuild seulement

set -euo pipefail

SERVICE_NAME="morfanalytics"
UNIT_DEST="/etc/systemd/system/$SERVICE_NAME.service"
APP_DIR="${MT_APP_DIR:-/opt/morfanalytics}"

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

# --- Recuperer le code (en tant que l'utilisateur) -----------------------
if [[ "${1:-}" != "--no-pull" ]]; then
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
BIN="$BUILD_DIR/service/morfanalytics"
[[ -x "$BIN" ]] || { echo "Echec : $BIN introuvable apres compilation." >&2; exit 1; }

# --- Recopier le binaire (config preservee) ------------------------------
echo "Copie du binaire vers $APP_DIR..."
systemctl stop "$SERVICE_NAME" 2>/dev/null || true
install -m 0755 "$BIN" "$APP_DIR/morfanalytics"
chown "$RUN_USER:$RUN_USER" "$APP_DIR/morfanalytics"

# --- Redemarrer ----------------------------------------------------------
systemctl start "$SERVICE_NAME"
sleep 1
echo "Mise a jour appliquee."
systemctl --no-pager --lines=0 status "$SERVICE_NAME" || true
