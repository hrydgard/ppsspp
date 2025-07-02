#!/bin/bash

set -euo pipefail

# === CONFIGURATION ===

APP_NAME="PPSSPP"  # Name of your app bundle (no .app extension)
DSYM_PATH="build-ios/Release-iphoneos/${APP_NAME}.app.dSYM"

# === Find latest .xcarchive ===

ARCHIVE_DIR_BASE=~/Library/Developer/Xcode/Archives
LATEST_ARCHIVE=$(find "$ARCHIVE_DIR_BASE" -type d -name "*.xcarchive" -print0 | xargs -0 ls -td | head -n 1)

if [ ! -d "$LATEST_ARCHIVE" ]; then
  echo "❌ Could not find .xcarchive in $ARCHIVE_DIR_BASE"
  exit 1
fi

echo "📦 Found archive: $LATEST_ARCHIVE"

# === Extract binary UUID from .app inside archive ===

ARCHIVE_APP="$LATEST_ARCHIVE/Products/Applications/${APP_NAME}.app/${APP_NAME}"
if [ ! -f "$ARCHIVE_APP" ]; then
  echo "❌ App binary not found in archive: $ARCHIVE_APP"
  exit 1
fi

ARCHIVE_UUIDS=$(dwarfdump --uuid "$ARCHIVE_APP" | awk '{ print $2 }' | sort)

# === Extract UUID from your .dSYM ===

DSYM_BINARY="$DSYM_PATH/Contents/Resources/DWARF/${APP_NAME}"
if [ ! -f "$DSYM_BINARY" ]; then
  echo "❌ .dSYM binary not found: $DSYM_BINARY"
  exit 1
fi

DSYM_UUIDS=$(dwarfdump --uuid "$DSYM_BINARY" | awk '{ print $2 }' | sort)

# === Compare UUIDs ===

if [ "$ARCHIVE_UUIDS" != "$DSYM_UUIDS" ]; then
  echo "❌ UUID mismatch:"
  echo "Archive binary UUIDs: $ARCHIVE_UUIDS"
  echo "Your dSYM UUIDs:     $DSYM_UUIDS"
  exit 1
fi

echo "✅ UUIDs match!"

# === Copy dSYM into archive ===

DEST="$LATEST_ARCHIVE/dSYMs/$(basename "$DSYM_PATH")"

echo "📁 Copying dSYM to archive: $DEST"
cp -R "$DSYM_PATH" "$DEST"

echo "✅ dSYM successfully copied to archive."
