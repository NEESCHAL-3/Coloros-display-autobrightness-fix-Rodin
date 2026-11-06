#!/usr/bin/env bash
#
# ColorOS Display & Auto-Brightness Sensor Compatibility Shim
# Automated NDK Build Script
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET_LIB="${TARGET_LIB:-sensors.mt6899.so}"
ANDROID_API="${ANDROID_API:-35}"

echo "============================================================"
echo " Building Sensor Compatibility Shim: $TARGET_LIB"
echo " Target Android API: $ANDROID_API"
echo "============================================================"

# Detect Android NDK
NDK=""
if [ -n "${ANDROID_NDK_HOME:-}" ] && [ -d "$ANDROID_NDK_HOME" ]; then
    NDK="$ANDROID_NDK_HOME"
elif [ -n "${ANDROID_NDK_ROOT:-}" ] && [ -d "$ANDROID_NDK_ROOT" ]; then
    NDK="$ANDROID_NDK_ROOT"
elif [ -d "$HOME/Android/Sdk/ndk/30.0.16248370" ]; then
    NDK="$HOME/Android/Sdk/ndk/30.0.16248370"
elif [ -d "$HOME/Android/Sdk/ndk/28.2.13676358" ]; then
    NDK="$HOME/Android/Sdk/ndk/28.2.13676358"
else
    # Find newest available NDK under Android SDK
    FOUND_NDK="$(find "$HOME/Android/Sdk/ndk/" -maxdepth 1 -mindepth 1 -type d 2>/dev/null | sort -V | tail -n 1 || true)"
    if [ -n "$FOUND_NDK" ] && [ -d "$FOUND_NDK" ]; then
        NDK="$FOUND_NDK"
    fi
fi

if [ -z "$NDK" ]; then
    echo "ERROR: Android NDK not found."
    echo "Please set ANDROID_NDK_HOME or install NDK via Android SDK manager."
    exit 1
fi

echo "Using NDK: $NDK"

CLANG="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android${ANDROID_API}-clang"
if [ ! -x "$CLANG" ]; then
    echo "ERROR: Compiler binary not executable: $CLANG"
    exit 2
fi

BUILD_DIR="$SCRIPT_DIR/build"
mkdir -p "$BUILD_DIR"

OUTPUT_FILE="$BUILD_DIR/$TARGET_LIB"

echo "Compiling $OUTPUT_FILE ..."
"$CLANG" -shared -fPIC -O2 \
    -Wall -Wextra -Werror \
    -Wl,-soname,"$TARGET_LIB" \
    "$SCRIPT_DIR/src/sensors_compat.c" \
    -o "$OUTPUT_FILE" \
    -llog -ldl -pthread

echo
echo "Build complete."
file "$OUTPUT_FILE"
sha256sum "$OUTPUT_FILE"
echo "============================================================"
