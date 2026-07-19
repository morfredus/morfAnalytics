#!/usr/bin/env bash
#
# uninstall-service.sh — Supprime entierement le service morfAnalytics, pour
# permettre une reinstallation depuis zero.
#
# Le --uninstall de install-service.sh se contente de retirer l'unite systemd et
# CONSERVE le dossier d'installation : pratique pour une mise a jour, mais une
# reinstallation « propre » repartait alors de l'ancienne configuration et de
# l'ancien cache. Ce script-ci retire tout par defaut.
#
# Ce qui est supprime :
#   - l'unite systemd et son activation ;
#   - le dossier d'installation (binaire, configuration, cache de travail).
#
# Ce qui n'est JAMAIS touche : le clone git. Le depot reste intact.
#
# Le cache de travail est une COPIE des mesures de l'appareil, jamais la source
# de verite : le supprimer ne perd rien. Il sera reconstitue a la premiere
# collecte suivant la reinstallation (comptez quelques minutes si l'historique
# est long).
#
# Usage :
#   sudo ./scripts/linux/uninstall-service.sh                # tout supprimer
#   sudo ./scripts/linux/uninstall-service.sh --keep-config  # garder le .json
#   sudo ./scripts/linux/uninstall-service.sh --dry-run      # montrer sans agir
#   sudo MT_APP_DIR=/opt/autre ./scripts/linux/uninstall-service.sh

set -euo pipefail

SERVICE_NAME="morfanalytics"
UNIT_DEST="/etc/systemd/system/$SERVICE_NAME.service"
APP_DIR="${MT_APP_DIR:-/opt/morfanalytics}"
CONFIG_FILE="$APP_DIR/$SERVICE_NAME.json"

KEEP_CONFIG=0
DRY_RUN=0
for arg in "$@"; do
    case "$arg" in
        --keep-config) KEEP_CONFIG=1 ;;
        --dry-run)     DRY_RUN=1 ;;
        -h|--help)     sed -n '2,30p' "$0"; exit 0 ;;
        *) echo "Option inconnue : $arg (voir --help)" >&2; exit 1 ;;
    esac
done

if [[ "${EUID}" -ne 0 ]]; then
    echo "Ce script doit etre lance avec sudo :  sudo $0 $*" >&2
    exit 1
fi

run() {
    if [[ $DRY_RUN -eq 1 ]]; then
        echo "  [simulation] $*"
    else
        "$@"
    fi
}

echo "Service      : $SERVICE_NAME"
echo "Unite        : $UNIT_DEST"
echo "Installation : $APP_DIR"
[[ $KEEP_CONFIG -eq 1 ]] && echo "Configuration : conservee"
[[ $DRY_RUN -eq 1 ]] && echo "Mode         : simulation (rien ne sera supprime)"
echo

# --- 1. Arreter et desactiver --------------------------------------------
if systemctl list-unit-files 2>/dev/null | grep -q "^$SERVICE_NAME.service"; then
    echo "Arret et desactivation du service..."
    run systemctl disable --now "$SERVICE_NAME" 2>/dev/null || true
else
    echo "Aucune unite systemd '$SERVICE_NAME' declaree."
fi

# --- 2. Retirer l'unite ---------------------------------------------------
if [[ -f "$UNIT_DEST" ]]; then
    echo "Suppression de l'unite systemd."
    run rm -f "$UNIT_DEST"
    run systemctl daemon-reload
    # Sans reset-failed, une unite ayant echoue reste listee en etat "failed"
    # apres suppression, ce qui laisse croire que la desinstallation a rate.
    run systemctl reset-failed "$SERVICE_NAME" 2>/dev/null || true
fi

# --- 3. Retirer le dossier d'installation --------------------------------
if [[ -d "$APP_DIR" ]]; then
    if [[ $KEEP_CONFIG -eq 1 && -f "$CONFIG_FILE" ]]; then
        BACKUP="/tmp/$SERVICE_NAME.json.$(date +%Y%m%d-%H%M%S)"
        echo "Sauvegarde de la configuration : $BACKUP"
        run cp "$CONFIG_FILE" "$BACKUP"
    fi
    echo "Suppression du dossier d'installation."
    run rm -rf "$APP_DIR"
    if [[ $KEEP_CONFIG -eq 1 ]]; then
        echo
        echo "Configuration conservee dans ${BACKUP:-/tmp}."
        echo "Apres reinstallation, la remettre en place :"
        echo "  sudo cp ${BACKUP:-<sauvegarde>} $CONFIG_FILE"
    fi
else
    echo "Dossier $APP_DIR deja absent."
fi

echo
if [[ $DRY_RUN -eq 1 ]]; then
    echo "Simulation terminee : rien n'a ete supprime."
else
    echo "Desinstallation terminee. Le clone git n'a pas ete touche."
    echo "Reinstaller :  sudo ./scripts/linux/install-service.sh"
fi
