#!/usr/bin/env bash
#
# update-service.sh — Met a jour morfAnalytics installe en service.
#
# Recupere le code (git pull), recompile, recopie le binaire dans le dossier fixe,
# rafraichit l'unite systemd, complete la configuration, puis redemarre le
# service. Complement de install-service.sh.
#
# Configuration : les valeurs deja en place ne sont JAMAIS modifiees, mais les
# parametres APPARUS depuis l'installation sont ajoutes (voir merge-config.py).
# Sans cela, une version introduisant un parametre le laissait absent
# indefiniment et la fonction correspondante ne s'activait jamais, en silence.
#
# Usage :
#   sudo ./scripts/linux/update-service.sh              # git pull + build + restart
#   sudo ./scripts/linux/update-service.sh --no-pull    # rebuild seulement
#   sudo ./scripts/linux/update-service.sh --no-config  # ne pas toucher au .json

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
BIN="$BUILD_DIR/service/morfanalytics"
[[ -x "$BIN" ]] || { echo "Echec : $BIN introuvable apres compilation." >&2; exit 1; }

# --- Recopier le binaire (config preservee) ------------------------------
echo "Copie du binaire vers $APP_DIR..."
systemctl stop "$SERVICE_NAME" 2>/dev/null || true
install -m 0755 "$BIN" "$APP_DIR/morfanalytics"
chown "$RUN_USER:$RUN_USER" "$APP_DIR/morfanalytics"

# --- Rafraichir l'unite systemd ------------------------------------------
# Elle non plus n'etait pas mise a jour : une modification du fichier .service
# dans le depot ne parvenait jamais a /etc/systemd/system.
NEW_UNIT="$(mktemp)"
sed -e "s/__RUN_USER__/$RUN_USER/g" -e "s#__APP_DIR__#$APP_DIR#g"     "$SCRIPT_DIR/morfanalytics.service" > "$NEW_UNIT"
if ! cmp -s "$NEW_UNIT" "$UNIT_DEST"; then
    echo "Unite systemd modifiee : mise a jour."
    install -m 0644 "$NEW_UNIT" "$UNIT_DEST"
    systemctl daemon-reload
fi
rm -f "$NEW_UNIT"

# --- Completer la configuration ------------------------------------------
CONFIG_FILE="$APP_DIR/$SERVICE_NAME.json"
EXAMPLE_FILE="$REPO_ROOT/config/$SERVICE_NAME.example.json"
if [[ $NO_CONFIG -eq 0 && -f "$CONFIG_FILE" && -f "$EXAMPLE_FILE" ]]; then
    if command -v python3 >/dev/null 2>&1; then
        # Sauvegarde avant toute modification : la config porte des reglages que
        # l'utilisateur ne pourrait pas retrouver.
        BACKUP="$CONFIG_FILE.bak-$(date +%Y%m%d-%H%M%S)"
        cp "$CONFIG_FILE" "$BACKUP"
        ADDED="$(python3 "$SCRIPT_DIR/merge-config.py" "$EXAMPLE_FILE" "$CONFIG_FILE" || true)"
        if [[ -n "$ADDED" ]]; then
            chown "$RUN_USER:$RUN_USER" "$CONFIG_FILE"
            echo
            echo "Nouveaux parametres ajoutes a $CONFIG_FILE :"
            echo "$ADDED" | sed 's/^/    /'
            echo "  (valeurs existantes inchangees ; sauvegarde : $BACKUP)"
            echo "  A RENSEIGNER si besoin avant que la fonction correspondante s'active."
            echo
        else
            rm -f "$BACKUP"
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
