#!/bin/bash
# Back up the full 4MB flash in 256KB chunks, retrying any chunk that fails.
# Usage: ./backup_flash.sh [port] [baud]
set -u
PORT="${1:-/dev/cu.usbserial-2130}"
BAUD="${2:-115200}"
CHUNK=$((0x40000))
TOTAL=$((0x400000))
TRIES=5
OUT=factory-firmware-4MB.bin

cd "$(dirname "$0")"
mkdir -p chunks

for ((addr = 0; addr < TOTAL; addr += CHUNK)); do
  name=$(printf "chunks/%06x.bin" "$addr")
  if [[ -f "$name" && $(stat -f %z "$name") -eq $CHUNK ]]; then
    echo "$name already done"
    continue
  fi
  for ((try = 1; try <= TRIES; try++)); do
    printf "Reading 0x%06x (try %d/%d)... " "$addr" "$try" "$TRIES"
    if uvx esptool --port "$PORT" --baud "$BAUD" read-flash "$addr" "$CHUNK" "$name" >/dev/null 2>&1 \
       && [[ $(stat -f %z "$name") -eq $CHUNK ]]; then
      echo ok
      break
    fi
    echo failed
    rm -f "$name"
  done
  if [[ ! -f "$name" ]]; then
    echo "Giving up on 0x$(printf %06x "$addr"). Re-run the script to resume."
    exit 1
  fi
done

cat chunks/*.bin > "$OUT"
shasum -a 256 "$OUT" > "$OUT.sha256"
ls -l "$OUT"
echo "Done. You can delete the chunks/ folder."
