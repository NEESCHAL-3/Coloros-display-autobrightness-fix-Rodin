#!/usr/bin/env bash
#
# Helper script to determine the 64-bit Physical Display ID required by Android 15 / ColorOS.
#

set -euo pipefail

echo "============================================================"
echo " Querying Physical Display ID from connected device..."
echo "============================================================"

if ! command -v adb >/dev/null 2>&1; then
    echo "ERROR: adb command not found in PATH."
    exit 1
fi

DEVICE_ID="$(adb shell dumpsys display 2>/dev/null | grep -E -o "local:[0-9]+" | head -n 1 | cut -d':' -f2 || true)"

if [ -z "$DEVICE_ID" ]; then
    DEVICE_ID="$(adb shell dumpsys display 2>/dev/null | grep -E -o "mPhysicalDisplayId=[0-9]+" | head -n 1 | cut -d'=' -f2 || true)"
fi

if [ -z "$DEVICE_ID" ]; then
    echo "ERROR: Could not retrieve Physical Display ID automatically."
    echo "Ensure device is connected, boot completed, and USB debugging is authorized."
    echo
    echo "Manual lookup command:"
    echo "  adb shell dumpsys display | grep -E 'DisplayDeviceInfo|mPhysicalDisplayId'"
    exit 2
fi

echo "Identified Physical Display ID: $DEVICE_ID"
echo
echo "Required configuration target path:"
echo "  /vendor/etc/displayconfig/display_id_${DEVICE_ID}.xml"
echo
echo "You can create this configuration from the template via:"
echo "  cp displayconfig/display_id_template.xml displayconfig/display_id_${DEVICE_ID}.xml"
echo "============================================================"
