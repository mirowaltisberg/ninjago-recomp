#!/bin/sh
# Verifies that every Java_* export of the guest library is documented in docs/java-contract.md.
cd "$(dirname "$0")/.."
NM=$(xcrun -f llvm-nm)
missing=0
for e in $($NM -D --defined-only input/extracted/lib/armeabi-v7a/libLEGO_M1.so | grep -o 'Java_[A-Za-z0-9_]*' | sort -u); do
  grep -q "\`$e\`" docs/java-contract.md || { echo "undocumented export: $e"; missing=1; }
done
[ $missing = 0 ] && echo "all JNI exports documented"
exit $missing
