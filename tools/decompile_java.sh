#!/bin/sh
# Decompile the Android Java side for reading (output stays in build/, never committed).
set -e
cd "$(dirname "$0")/.."
jadx -q -d build/jadx --no-res input/extracted/base.apk
echo "decompiled to build/jadx/sources"
