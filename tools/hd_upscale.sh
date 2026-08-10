#!/bin/bash
# Usage: hd_upscale.sh <src_dir> <out_dir> <model> [realesrgan_dir]
# Upscales every PNG in <src_dir> 4x into <out_dir>/<samebasename>.png
set -euo pipefail
SRC="$1"; OUT="$2"; MODEL="${3:-realesrgan-x4plus-anime}"
RE="${4:-$HOME/tools/realesrgan}"
BIN="$RE/realesrgan-ncnn-vulkan"
mkdir -p "$OUT"
[ -x "$BIN" ] || { echo "realesrgan not found at $BIN"; exit 2; }
n=0
for f in "$SRC"/*.png; do
  [ -e "$f" ] || continue
  base="$(basename "$f")"
  "$BIN" -i "$f" -o "$OUT/$base" -s 4 -n "$MODEL" -m "$RE/models" -f png >/dev/null 2>&1
  n=$((n+1))
done
echo "upscaled $n file(s) with model $MODEL -> $OUT"
