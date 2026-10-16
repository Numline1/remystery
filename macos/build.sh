#!/bin/bash
# Builds build/ReMystery.saver (universal arm64 + x86_64) and build/ReMystery-macOS.zip.
# Set SIGN_IDENTITY to a "Developer ID Application" identity to sign for distribution;
# by default the bundle is ad-hoc signed.
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION="${VERSION:-$(cat VERSION 2>/dev/null || echo 1.0.0)}"
OUT=build/ReMystery.saver
TMP=build/macos-obj
rm -rf "$OUT" "$TMP"
mkdir -p "$OUT/Contents/MacOS" "$OUT/Contents/Resources" "$TMP"

for arch in arm64 x86_64; do
  xcrun swiftc -O -whole-module-optimization \
    -target "$arch-apple-macos12.0" -module-name ReMystery \
    -emit-executable -Xlinker -bundle \
    -framework ScreenSaver -framework WebKit -framework AppKit \
    macos/ReMysteryView.swift -o "$TMP/ReMystery-$arch"
done
lipo -create "$TMP/ReMystery-arm64" "$TMP/ReMystery-x86_64" -output "$OUT/Contents/MacOS/ReMystery"
sed "s/@VERSION@/$VERSION/g" macos/Info.plist > "$OUT/Contents/Info.plist"
cp src/index.html "$OUT/Contents/Resources/index.html"
for f in thumbnail.png thumbnail@2x.png; do
  [ -f "macos/$f" ] && cp "macos/$f" "$OUT/Contents/Resources/$f"
done

codesign --force --deep --sign "${SIGN_IDENTITY:--}" --timestamp=none "$OUT"
rm -rf "$TMP"
(cd build && rm -f ReMystery-macOS.zip && ditto -c -k --sequesterRsrc --keepParent ReMystery.saver ReMystery-macOS.zip)
echo "Built $OUT ($(du -sh "$OUT" | cut -f1)) and build/ReMystery-macOS.zip"
