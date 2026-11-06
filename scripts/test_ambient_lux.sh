#!/usr/bin/env bash
#
# Real-time monitoring script for ambient lux, backlight, and synthesized ALS events.
#

set -euo pipefail

echo "============================================================"
echo " Monitoring Live Ambient Light & Backlight Correlation"
echo " Press Ctrl+C to terminate."
echo "============================================================"

adb shell "
while true; do
    bl=\$(cat /sys/class/mi_display/disp-DSI-0/backlight 2>/dev/null || cat /sys/devices/virtual/mi_display/disp-DSI-0/backlight 2>/dev/null || echo 'N/A')
    lux=\$(dumpsys display 2>/dev/null | grep -E 'mAmbientLux|realTimeLux' | head -n 1 | tr -d ' ' || echo 'N/A')
    brt=\$(settings get system screen_brightness 2>/dev/null || echo 'N/A')
    mode=\$(settings get system screen_brightness_mode 2>/dev/null || echo 'N/A')
    echo \"[$(date +%T)] Mode=\$mode | Slider=\$brt | Backlight=\$bl | Lux=\$lux\"
    sleep 1
done
"
