#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

echo "🔨 Building Xon Library and CLI..."

# Generate parser when possible. Fallback to checked-in parser sources when Lemon
# is unavailable on the current architecture.
if [[ "${XON_SKIP_PARSER_GEN:-0}" == "1" ]]; then
    echo "⚠️  Skipping parser regeneration (XON_SKIP_PARSER_GEN=1)."
else
    LEMON_BIN="./tools/lemon"
    PARSER_REGENERATED=0

    echo "🧩 Attempting parser regeneration with ${LEMON_BIN}..."
    if "$LEMON_BIN" -Ttools/lempar.c src/xon.lemon >/dev/null 2>&1; then
        PARSER_REGENERATED=1
        echo "✅ Parser regenerated with bundled Lemon."
    else
        echo "⚠️  Bundled Lemon is unavailable on this host. Building a local Lemon binary..."
        mkdir -p build
        if command -v cc >/dev/null 2>&1; then
            cc -O2 -o build/lemon-host tools/lemon.c
            if ./build/lemon-host -Ttools/lempar.c src/xon.lemon >/dev/null 2>&1; then
                PARSER_REGENERATED=1
                echo "✅ Parser regenerated with build/lemon-host."
            fi
        fi
    fi

    if [[ "$PARSER_REGENERATED" != "1" ]]; then
        if [[ "${XON_REQUIRE_PARSER_GEN:-0}" == "1" ]]; then
            echo "❌ Parser regeneration failed and XON_REQUIRE_PARSER_GEN=1 is set."
            exit 1
        fi
        if [[ -f src/xon.c && -f src/xon.h ]]; then
            echo "⚠️  Using checked-in src/xon.c and src/xon.h."
        else
            echo "❌ Parser regeneration failed and generated parser sources are missing."
            exit 1
        fi
    fi
fi

# Detect OS for shared library extension
if [[ "$OSTYPE" == "darwin"* ]]; then
    LIB_EXT="dylib"
    LIB_FLAGS="-dynamiclib"
else
    LIB_EXT="so"
    LIB_FLAGS="-shared -fPIC"
fi

# Build shared library
echo "📚 Building libxon.${LIB_EXT}..."
gcc $LIB_FLAGS -Wall -Wextra -std=c99 -Iinclude \
    -o libxon.${LIB_EXT} \
    src/xon_api.c src/lexer.c src/logger.c

# Build CLI tool
echo "🔧 Building xon CLI..."
gcc -Wall -Wextra -std=c99 -Iinclude \
    -o xon \
    src/main.c src/xon_api.c src/lexer.c src/logger.c

# Build example program
echo "📝 Building example program..."
gcc -Wall -Wextra -std=c99 -Iinclude \
    -o example_lib \
    examples/use_library.c \
    -L. -lxon

echo ""
echo "✅ Build complete!"
echo "   - libxon.${LIB_EXT} (shared library)"
echo "   - xon (CLI tool)"
echo "   - example_lib (library usage example)"
echo ""
echo "📖 To run the example:"
if [[ "$OSTYPE" == "darwin"* ]]; then
    echo "   DYLD_LIBRARY_PATH=. ./example_lib"
else
    echo "   LD_LIBRARY_PATH=. ./example_lib"
fi
