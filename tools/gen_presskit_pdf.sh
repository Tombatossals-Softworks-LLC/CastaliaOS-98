#!/bin/sh
# gen_presskit_pdf.sh - Render presskit/index.html to a printable PDF using a
# headless Chromium/Chrome. The page carries @media print styles tuned for this.
# Usage: sh tools/gen_presskit_pdf.sh  (override the browser with CHROME=/path).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/presskit/CastaliaOS-98-PE-PressKit.pdf"
IN="file://$ROOT/presskit/index.html"

if [ -z "$CHROME" ]; then
  for c in chromium chromium-browser google-chrome google-chrome-stable; do
    if command -v "$c" >/dev/null 2>&1; then CHROME="$c"; break; fi
  done
fi
# Playwright's bundled Chromium (this repo's environment), if present.
if [ -z "$CHROME" ] && [ -n "$PLAYWRIGHT_BROWSERS_PATH" ]; then
  CHROME="$(find "$PLAYWRIGHT_BROWSERS_PATH" -name chrome -type f 2>/dev/null | head -1)"
fi
if [ -z "$CHROME" ]; then
  echo "No Chromium/Chrome found. Set CHROME=/path/to/chrome and retry." >&2
  exit 1
fi

"$CHROME" --headless=new --no-sandbox --no-pdf-header-footer \
  --print-to-pdf-no-header --print-to-pdf="$OUT" "$IN" 2>/dev/null
echo "  PDF   $OUT"
