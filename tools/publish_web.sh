#!/usr/bin/env sh
# Copies the web pages (web/) into your GitHub Pages site, as <site>/world-randomizer/, so they are online at
# https://<you>.github.io/world-randomizer/. Commit and push the site afterwards.
#
#   tools/publish_web.sh <path to your <you>.github.io clone> [folder name, default world-randomizer]
set -e
SITE="$1"
NAME="${2:-world-randomizer}"
if [ -z "$SITE" ] || [ ! -d "$SITE" ]; then
    echo "Usage: $0 <path to your <you>.github.io clone> [folder name]" >&2
    exit 1
fi
HERE="$(cd "$(dirname "$0")/.." && pwd)"
python3 "$HERE/tools/make_conf_templates.py" >/dev/null 2>&1 || echo "note: conf templates not rebuilt (no python3)"
rm -rf "${SITE:?}/$NAME"
mkdir -p "$SITE/$NAME"
cp -R "$HERE/web/." "$SITE/$NAME/"
echo "Copied web/ to $SITE/$NAME. Now commit and push the site."
