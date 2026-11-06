#!/usr/bin/env bash
#
# Automated deployment script for on-partition installation.
# Deploys configurations to target device with strict SELinux labels.
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DISPLAY_XML="${1:-}"

if [ -z "$DISPLAY_XML" ]; then
    # Look for existing display_id_*.xml under displayconfig/
    FOUND_XML="$(find "$SCRIPT_DIR/displayconfig" -name "display_id_*.xml" ! -name "*template*" | head -n 1 || true)"
    if [ -n "$FOUND_XML" ]; then
        DISPLAY_XML="$FOUND_XML"
    else
        echo "Usage: $0 /path/to/display_id_<ID>.xml"
        exit 1
    fi
fi

XML_BASENAME="$(basename "$DISPLAY_XML")"
echo "============================================================"
echo " Deploying ColorOS Auto-Brightness & Spline Fixes"
echo " Target Display Config: $XML_BASENAME"
echo "============================================================"

# Check ADB root / su
if ! adb shell "su -c 'id'" >/dev/null 2>&1; then
    echo "ERROR: Root access (su) via ADB is required for on-partition deployment."
    exit 2
fi

# Push files to /data/local/tmp staging area
echo "[1/3] Pushing staging files to device..."
adb push "$DISPLAY_XML" "/data/local/tmp/$XML_BASENAME"
adb push "$SCRIPT_DIR/displayconfig/display_brightness_config_default.xml" "/data/local/tmp/display_brightness_config_default.xml"
adb push "$SCRIPT_DIR/displayconfig/display_brightness_config_common.xml" "/data/local/tmp/display_brightness_config_common.xml"

# Remount partitions and apply files
echo "[2/3] Applying files to partitions and restoring SELinux contexts..."
adb shell "su -c '
set -e
mount -o remount,rw /vendor
mount -o remount,rw /system_ext

# Install displayconfig XML
mkdir -p /vendor/etc/displayconfig
cp /data/local/tmp/$XML_BASENAME /vendor/etc/displayconfig/$XML_BASENAME
chmod 0644 /vendor/etc/displayconfig/$XML_BASENAME
chown root:root /vendor/etc/displayconfig/$XML_BASENAME
chcon u:object_r:vendor_configs_file:s0 /vendor/etc/displayconfig/$XML_BASENAME
rm -f /data/local/tmp/$XML_BASENAME

# Install default brightness configuration
cp /data/local/tmp/display_brightness_config_default.xml /system_ext/etc/display_brightness_config_default.xml
chmod 0644 /system_ext/etc/display_brightness_config_default.xml
chown root:root /system_ext/etc/display_brightness_config_default.xml
chcon u:object_r:system_file:s0 /system_ext/etc/display_brightness_config_default.xml
rm -f /data/local/tmp/display_brightness_config_default.xml

# Install common brightness configuration
cp /data/local/tmp/display_brightness_config_common.xml /system_ext/etc/display_brightness_config_common.xml
chmod 0644 /system_ext/etc/display_brightness_config_common.xml
chown root:root /system_ext/etc/display_brightness_config_common.xml
chcon u:object_r:system_file:s0 /system_ext/etc/display_brightness_config_common.xml
rm -f /data/local/tmp/display_brightness_config_common.xml

sync
echo DEPLOYMENT_APPLIED_SUCCESSFULLY
'"

echo "[3/3] Done. To reload system_server display spline tables, reboot the device:"
echo "  adb reboot"
echo "============================================================"
