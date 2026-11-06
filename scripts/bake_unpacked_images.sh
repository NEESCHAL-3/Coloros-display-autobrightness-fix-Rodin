#!/usr/bin/env bash
#
# ColorOS ROM Porting Tool: Bake Auto-Brightness & Spline Fixes into Unpacked Images
#
# Usage:
#   ./scripts/bake_unpacked_images.sh /path/to/unpacked_rom_root [PHYSICAL_DISPLAY_ID]
#
# Example:
#   ./scripts/bake_unpacked_images.sh ~/coloros_rodin_port 4627039422300187648
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROM_ROOT="${1:-}"
DISPLAY_ID="${2:-4627039422300187648}"

if [ -z "$ROM_ROOT" ] || [ ! -d "$ROM_ROOT" ]; then
    echo "Usage:"
    echo "  $0 <path_to_unpacked_rom_directory> [PHYSICAL_DISPLAY_ID]"
    echo
    echo "Example:"
    echo "  $0 ~/unpacked_coloros 4627039422300187648"
    exit 1
fi

ROM_ROOT="$(readlink -f "$ROM_ROOT")"

echo "============================================================"
echo " Baking ColorOS Display & Auto-Brightness into Unpacked ROM"
echo " ROM Directory: $ROM_ROOT"
echo " Physical ID:   $DISPLAY_ID"
echo "============================================================"

# Resolve partition locations (supports both flat and subfolder unpack layouts)
VENDOR_DIR=""
if [ -d "$ROM_ROOT/vendor" ]; then
    VENDOR_DIR="$ROM_ROOT/vendor"
elif [ -d "$ROM_ROOT/VENDOR" ]; then
    VENDOR_DIR="$ROM_ROOT/VENDOR"
else
    VENDOR_DIR="$ROM_ROOT"
fi

SYSTEM_EXT_DIR=""
if [ -d "$ROM_ROOT/system_ext" ]; then
    SYSTEM_EXT_DIR="$ROM_ROOT/system_ext"
elif [ -d "$ROM_ROOT/SYSTEM_EXT" ]; then
    SYSTEM_EXT_DIR="$ROM_ROOT/SYSTEM_EXT"
elif [ -d "$ROM_ROOT/system/system_ext" ]; then
    SYSTEM_EXT_DIR="$ROM_ROOT/system/system_ext"
else
    SYSTEM_EXT_DIR="$ROM_ROOT"
fi

# 1. Install DisplayDeviceConfig XML into vendor
echo "[1/3] Installing DisplayDeviceConfig into vendor/etc/displayconfig/..."
TARGET_DDC="$VENDOR_DIR/etc/displayconfig/display_id_${DISPLAY_ID}.xml"
mkdir -p "$(dirname "$TARGET_DDC")"

SOURCE_DDC="$SCRIPT_DIR/displayconfig/display_id_${DISPLAY_ID}.xml"
if [ ! -f "$SOURCE_DDC" ]; then
    SOURCE_DDC="$SCRIPT_DIR/displayconfig/display_id_template.xml"
fi

cp -f "$SOURCE_DDC" "$TARGET_DDC"
chmod 0644 "$TARGET_DDC"
echo "  -> Installed: $TARGET_DDC"

# 2. Install ColorOS brightness tables into system_ext
echo "[2/3] Installing Brightness Splines into system_ext/etc/..."
TARGET_DEF="$SYSTEM_EXT_DIR/etc/display_brightness_config_default.xml"
TARGET_COM="$SYSTEM_EXT_DIR/etc/display_brightness_config_common.xml"
mkdir -p "$(dirname "$TARGET_DEF")"

cp -f "$SCRIPT_DIR/displayconfig/display_brightness_config_default.xml" "$TARGET_DEF"
chmod 0644 "$TARGET_DEF"
echo "  -> Installed: $TARGET_DEF"

cp -f "$SCRIPT_DIR/displayconfig/display_brightness_config_common.xml" "$TARGET_COM"
chmod 0644 "$TARGET_COM"
echo "  -> Installed: $TARGET_COM"

# 3. Append required display and auto-brightness properties
echo "[3/3] Injecting display and auto-brightness properties..."
PROP_SRC="$SCRIPT_DIR/props/display_brightness.prop"
TARGET_PROP=""
if [ -d "$ROM_ROOT/odm" ] && [ -f "$ROM_ROOT/odm/etc/build.prop" ]; then
    TARGET_PROP="$ROM_ROOT/odm/etc/build.prop"
elif [ -f "$VENDOR_DIR/build.prop" ]; then
    TARGET_PROP="$VENDOR_DIR/build.prop"
elif [ -f "$VENDOR_DIR/etc/build.prop" ]; then
    TARGET_PROP="$VENDOR_DIR/etc/build.prop"
fi

if [ -n "$TARGET_PROP" ] && [ -f "$PROP_SRC" ]; then
    echo "  -> Appending properties to $TARGET_PROP"
    echo "" >> "$TARGET_PROP"
    echo "# ColorOS Auto-Brightness & Display Remap Compatibility Properties" >> "$TARGET_PROP"
    cat "$PROP_SRC" >> "$TARGET_PROP"
    echo "  -> Properties injected successfully."
fi

# Summary & Repacking Guidance
echo "============================================================"
echo " Files baked successfully into unpacked partition trees."
echo
echo "When repacking with erofs-utils (mkfs.erofs) or make_ext4fs,"
echo "ensure file_contexts include the following labels:"
echo "  /vendor/etc/displayconfig(/.*)?                   u:object_r:vendor_configs_file:s0"
echo "  /system_ext/etc/display_brightness_config_.*\\.xml u:object_r:system_file:s0"
echo "============================================================"
