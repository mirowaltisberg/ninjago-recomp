#!/bin/bash
# App icon: the original App Store artwork (iTunesArtwork, 512x512) from the user's own iOS IPA in input/, scaled to
# the single 1024x1024 icon iOS uses now. Written to build/assets (git-ignored), never committed. Without an IPA the
# icon set stays empty and the app builds without an icon. Run by `xcodegen generate` (preGenCommand in ios/project.yml).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CAT="$ROOT/build/assets/Assets.xcassets"
SET="$CAT/AppIcon.appiconset"
rm -rf "$CAT" && mkdir -p "$SET"
printf '{"info":{"author":"xcode","version":1}}\n' > "$CAT/Contents.json"
IPA="$(ls "$ROOT"/input/*.ipa 2>/dev/null | head -1 || true)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
if [ -n "$IPA" ] && unzip -q -j -o "$IPA" iTunesArtwork -d "$TMP" 2>/dev/null; then
    sips -s format png -z 1024 1024 "$TMP/iTunesArtwork" --out "$SET/AppIcon-1024.png" > /dev/null
    printf '{"images":[{"filename":"AppIcon-1024.png","idiom":"universal","platform":"ios","size":"1024x1024"}],"info":{"author":"xcode","version":1}}\n' > "$SET/Contents.json"
    echo "app icon: from $(basename "$IPA")"
else
    printf '{"images":[{"idiom":"universal","platform":"ios","size":"1024x1024"}],"info":{"author":"xcode","version":1}}\n' > "$SET/Contents.json"
    echo "app icon: none (put your iOS IPA in input/ for the original artwork)"
fi
