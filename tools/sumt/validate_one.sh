#!/bin/sh
# One file: shadow summaries, then (if any generic functions) the validator.
#   validate_one.sh <moss> <out-dir> <file.moss>
MOSS=$1; OUT=$2; FILE=$3
j="$OUT/val/$(printf '%s' "$FILE" | tr '/' '_').jsonl"
MOSS_SUMT_SHADOW="$j" timeout 120 "$MOSS" check "$FILE" >/dev/null 2>&1
if [ -s "$j" ]; then
  MOSS_SUMT_SHADOW="$j" MOSS_SUMT_VALIDATE=1 timeout 1800 "$MOSS" check "$FILE" >/dev/null 2>&1
else
  rm -f "$j"
fi
exit 0
