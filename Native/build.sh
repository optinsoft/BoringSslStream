#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

# --- Detect the .NET RID for the output directory -------------------------
# Each platform builds into its own directory: build/linux-x64,
# build/linux-arm64, build/osx-arm64, build/osx-x64.
# Override explicitly if needed:  RID=linux-arm64 ./build.sh
RID="${RID:-}"
if [[ -z "$RID" ]]; then
    case "$(uname -s)-$(uname -m)" in
        Linux-x86_64)              RID=linux-x64 ;;
        Linux-aarch64|Linux-arm64) RID=linux-arm64 ;;
        Darwin-arm64)              RID=osx-arm64 ;;
        Darwin-x86_64)             RID=osx-x64 ;;
        *) echo "Unsupported platform: $(uname -s)-$(uname -m). Set RID=<rid> explicitly." >&2; exit 1 ;;
    esac
fi

BUILD_DIR="build/$RID"

rm -rf "$BUILD_DIR"
cmake -G Ninja -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -j

echo
echo "OK: native library built into $BUILD_DIR:"
ls -1 "$BUILD_DIR"/libproxymap_boringssl.* "$BUILD_DIR"/proxymap_boringssl.dll 2>/dev/null || true
