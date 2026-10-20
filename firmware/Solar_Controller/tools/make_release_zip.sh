#!/usr/bin/env bash
# Create a clean release zip that contains the sketch folder only.
# Run from repo root or from within the sketch folder.
set -euo pipefail

SKETCH_DIR="${1:-}"
if [[ -z "$SKETCH_DIR" ]]; then
  # assume this script is in <sketch>/tools
  SKETCH_DIR="$(cd "$(dirname "$0")/.." && pwd)"
fi

NAME="$(basename "$SKETCH_DIR")"
OUT="${NAME}.zip"

# Safety: ensure expected files exist
[[ -f "$SKETCH_DIR/${NAME}.ino" ]] || {
  echo "ERROR: expected $SKETCH_DIR/${NAME}.ino" >&2
  exit 1
}
[[ -d "$SKETCH_DIR/src" ]] || {
  echo "ERROR: expected $SKETCH_DIR/src" >&2
  exit 1
}

TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

# Copy to temp to avoid zipping build artifacts
mkdir -p "$TMPDIR/$NAME"
rsync -a --delete \
  --exclude '.git/' \
  --exclude '.vscode/' \
  --exclude 'build/' \
  --exclude '*.bin' \
  --exclude '*.elf' \
  --exclude '*.map' \
  --exclude '*.log' \
  --exclude 'tools/*.zip' \
  "$SKETCH_DIR/" "$TMPDIR/$NAME/"

( cd "$TMPDIR" && zip -qr "$OUT" "$NAME" )

mv "$TMPDIR/$OUT" "$(pwd)/$OUT"

echo "OK: created $OUT"
