#!/bin/bash

MODULES_DIR="Modules"
BUILD_DIR="Build"

# Configuration
VERSION="2.0"
BUILD_TYPE="LAB"
FLASH="y"

mkdir -p "$BUILD_DIR"


welcome() {
    clear
    echo "---------------------------------"
    echo "      Yamada Module Builder      "
    echo "---------------------------------"
    echo ""
}

success() {
    echo "---------------------------------"
    echo "    Build Process Completed      "
    printf "     Ambatukam : %s seconds\n" "$SECONDS"
    echo "---------------------------------"
}

# Function to flash module directly via ADB
flash_via_adb() {
    local zip_path="$1"
    local zip_name=$(basename "$zip_path")
    local remote_path="/data/local/tmp/$zip_name"
    local local_script="$BUILD_DIR/tmp_install.sh"
    local remote_script="/data/local/tmp/tmp_install.sh"

    echo ""
    echo "---------------------------------"
    echo "      Direct ADB Flashing        "
    echo "---------------------------------"

    # Check if adb is available
    if ! command -v adb >/dev/null 2>&1; then
        echo "❌ Error: 'adb' is not installed or not in PATH."
        return 1
    fi

    # Check if a device is connected
    local device_state=$(adb get-state 2>/dev/null)
    if [ "$device_state" != "device" ]; then
        echo "❌ Error: No device connected or device unauthorized."
        return 1
    fi

    # Check for Shell Root Access explicitly
    echo "🔎 Checking root access..."
    local root_check=$(adb shell su -c 'id -u' 2>/dev/null | tr -d '\r' | tr -d ' ')
    if [ "$root_check" != "0" ]; then
        echo "❌ Error: Please Grant \"Shell\" Root Access in Your Root Manager."
        return 1
    fi

    echo "📲 Pushing $zip_name to /data/local/tmp/..."
    if ! adb push "$zip_path" "$remote_path"; then
        echo "❌ Error: Failed to push file to device."
        return 1
    fi

    # 1. Create the installation script locally to avoid ADB multiline escaping issues
    cat << 'EOF' > "$local_script"
TARGET_ZIP="$1"

if command -v ksud >/dev/null 2>&1; then
    echo "✅ Detected: KernelSU Based"
    echo "📦 Installing module..."
    ksud module install "$TARGET_ZIP"
elif command -v magisk >/dev/null 2>&1; then
    echo "✅ Detected: Magisk Based"
    echo "📦 Installing module..."
    magisk module install "$TARGET_ZIP"
elif command -v apd >/dev/null 2>&1; then
    echo "✅ Detected: APatch"
    echo "📦 Installing module..."
    apd module install "$TARGET_ZIP"
else
    echo "❌ Error: No supported root manager found."
    rm -f "$TARGET_ZIP"
    exit 1
fi

echo "🧹 Cleaning up temporary files..."
rm -f "$TARGET_ZIP"
echo "✅ Flashing process completed on device!"
EOF

    # 2. Push the script to the device
    adb push "$local_script" "$remote_script" >/dev/null 2>&1

    echo "🔄 Flashing module via root manager..."
    # 3. Execute the script cleanly (no newlines to confuse su)
    adb shell su -c "sh '$remote_script' '$remote_path'"

    # 4. Clean up the script file on both ends
    adb shell rm -f "$remote_script"
    rm -f "$local_script"

    # Optional prompt to reboot the device
    echo ""
    echo "Rebooting device automatically... 👋"
    adb reboot
    echo "---------------------------------"
}

build_modules() {
    rm -rf "$BUILD_DIR"/*


    cd "$MODULES_DIR" || exit 1
    MODULE_ID=$(grep "^id=" "module.prop" | cut -d'=' -f2 | tr -d '[:space:]')

    if [ -f "module.prop" ]; then
        cp "module.prop" "module.prop.tmp"
        sed "s/^version=.*$/version=$VERSION/" "module.prop.tmp" > "module.prop"
        rm "module.prop.tmp"
    fi

    if [ -f "customize.sh" ]; then
        cp "customize.sh" "customize.sh.tmp"
        sed "s/^ui_print \"Version : .*$/ui_print \"Version : $VERSION\"/" "customize.sh.tmp" > "customize.sh"
        rm "customize.sh.tmp"
    fi

    ZIP_NAME="${MODULE_ID}-${VERSION}-${BUILD_TYPE}.zip"
    ZIP_PATH="../$BUILD_DIR/$ZIP_NAME"
    zip -q -r "$ZIP_PATH" ./*
    echo "Created: $ZIP_NAME"

    cd ..

    # --- ADB Flash ---
    if [[ "${FLASH,,}" == "y" || "${FLASH,,}" == "yes" ]]; then
        flash_via_adb "$BUILD_DIR/$ZIP_NAME"
    fi
}

welcome
SECONDS=0  # Start timing
build_modules
success