#!/usr/bin/env bash
set -euo pipefail

# This script builds the Xon project to WebAssembly using Emscripten.
# It first generates the parser using Lemon, then compiles the source files
# to produce the WASM module and JavaScript glue code.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CACHE_DIR="$SCRIPT_DIR/.emscripten-cache"

echo "🌐 Building Xon for WebAssembly..."

# Check if Emscripten is installed
if ! command -v emcc &> /dev/null; then
    echo "❌ Emscripten not found. Install from: https://emscripten.org/docs/getting_started/downloads.html"
    exit 1
fi

# Keep Emscripten cache local to avoid system-cache permission issues.
mkdir -p "$CACHE_DIR"
export EM_CACHE="$CACHE_DIR"

# Generate parser first.
echo "📝 Generating parser..."
"$ROOT_DIR/tools/lemon" "$ROOT_DIR/src/xon.lemon" || {
    status=$?
    if [[ -f "$ROOT_DIR/src/xon.c" && -f "$ROOT_DIR/src/xon.h" ]]; then
        echo "⚠️  lemon exited with status ${status} (likely parser conflicts); continuing with generated parser artifacts"
    else
        exit "${status}"
    fi
}

# Compile to WebAssembly
echo "🔨 Compiling to WASM..."
cd "$SCRIPT_DIR"
emcc "$ROOT_DIR/src/xon_api.c" "$ROOT_DIR/src/lexer.c" "$ROOT_DIR/src/logger.c" \
    -o xon.js \
    -s WASM=1 \
    -s EXPORTED_FUNCTIONS='["_malloc","_free","_xonify_string","_xon_eval","_xon_to_json","_xon_to_xon","_xon_free","_xon_string_free","_xon_get_last_error","_xon_get_last_error_stack"]' \
    -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","FS","UTF8ToString","stringToUTF8"]' \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s MODULARIZE=1 \
    -s EXPORT_NAME="XonModule" \
    -s INVOKE_RUN=0 \
    -I"$ROOT_DIR/include" -I"$ROOT_DIR/src" \
    -O3 \
    --no-entry

echo ""
echo "✅ WASM build complete!"
echo "   Output: play.ground/xon.js and play.ground/xon.wasm"
echo ""
echo "📖 To test the playground:"
echo "   cd play.ground"
echo "   npm start"
echo "   open http://localhost:8000"
