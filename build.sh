#!/bin/bash
# Builds NESticle for the web with Emscripten:  ./build.sh [web|node]
#   web  -> dist/{index.html,nesticle.js,nesticle.wasm,nesticle.data}
#   node -> dist/nesticle-node.js (headless; used by tests/render_png.js)
set -euo pipefail
cd "$(dirname "$0")"
[ -n "${EMSDK:-}" ] || source "${EMSDK_DIR:-$HOME/emsdk}/emsdk_env.sh" >/dev/null 2>&1
target=${1:-web}
mkdir -p build/obj dist

CXXFLAGS=(-std=c++17 -O2 -g1 -DWIN95 -DWASM -Wno-everything -ferror-limit=0
          -fno-delete-null-pointer-checks -fno-strict-aliasing
          -funsigned-char   # Watcom (the original compiler) has unsigned plain char; 1-bit char bitfields rely on it
          -Isrc -Ishim -Iport -include compat.h)

objs=()
for f in src/*.cpp port/*.cpp vendor/*.cpp; do
  [ -f "$f" ] || continue
  o=build/obj/$(echo "$f" | tr '/' '_').o
  if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ -n "$(find shim port src -name '*.h' -newer "$o" 2>/dev/null | head -1)" ]; then
    em++ "${CXXFLAGS[@]}" -c "$f" -o "$o" &
  fi
  objs+=("$o")
done
wait
for o in "${objs[@]}"; do [ -f "$o" ] || { echo "missing $o (compile failed)"; exit 1; }; done

case $target in
  web)  out=dist/nesticle.js ;;
  node) out=dist/nesticle-node.js ;;
esac
LDFLAGS=(--no-entry -sALLOW_MEMORY_GROWTH=1 -sFORCE_FILESYSTEM=1 -sNO_EXIT_RUNTIME=1
         -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,HEAPU8,HEAPU32,HEAP16,FS -sEXPORTED_FUNCTIONS=_malloc,_free
         -sSTACK_SIZE=1MB -sINITIAL_MEMORY=64MB -sMODULARIZE=1 -sEXPORT_NAME=createNesticle)
[ -d assets ] && LDFLAGS+=(--preload-file assets@/)
[ "$target" = node ] && LDFLAGS+=(-sENVIRONMENT=node -sNODERAWFS=0)
em++ "${objs[@]}" -o "$out" "${LDFLAGS[@]}" ${EXTRA_LDFLAGS:-}

[ "$target" = web ] && cp web/index.html dist/index.html
true
