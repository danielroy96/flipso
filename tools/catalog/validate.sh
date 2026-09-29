#!/usr/bin/env bash
# Run the Flipper Apps Catalog's own validator, tools/bundle.py, over HEAD - the
# check its CI makes on a manifest PR. It clones this repository at HEAD, builds
# the .fap, checks the icon and screenshots, holds the description and changelog
# to its markdown subset, and writes the bundle to dist/catalog-bundle.zip.
#
# Lint is left to `ufbt lint`, which CI runs as a job of its own. Extra
# arguments go to bundle.py, e.g. --nobuild.
#
# Environment:
#   CATALOG_DIR  an existing catalog checkout, instead of cloning main
#
# bundle.py runs `ufbt update` itself, so it refreshes the SDK on the channel
# ufbt is already set to.
set -euo pipefail

ROOT=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
HERE="$ROOT/tools/catalog"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

if [ -n "$(git -C "$ROOT" status --porcelain)" ]; then
    echo "note: validating HEAD - uncommitted changes are not included" >&2
fi

CATALOG=${CATALOG_DIR:-}
if [ -z "$CATALOG" ]; then
    CATALOG="$WORK/catalog"
    git clone -q --depth 1 https://github.com/flipperdevices/flipper-application-catalog.git "$CATALOG"
fi

# The catalog pins Pillow 10.4, which has no wheels past Python 3.13; a newer
# Pillow converts the images identically, so fall back to one rather than fail.
VENV="$HERE/.venv"
[ -x "$VENV/bin/python3" ] || python3 -m venv "$VENV"
if ! "$VENV/bin/pip" install -q -r "$CATALOG/tools/requirements.txt" 2>/dev/null; then
    sed 's/^Pillow==.*/Pillow/' "$CATALOG/tools/requirements.txt" > "$WORK/requirements.txt"
    "$VENV/bin/pip" install -q -r "$WORK/requirements.txt"
fi
# bundle.py shells out to `ufbt`; use the one the requirements just installed.
export PATH="$VENV/bin:$PATH"

# The bundler checks that the manifest sits at applications/<category>/<appid>/.
fam() { sed -n "s/^ *$1=\"\\(.*\\)\",*$/\\1/p" "$ROOT/application.fam"; }
MANIFEST="$WORK/applications/$(fam fap_category)/$(fam appid)/manifest.yml"
mkdir -p "$(dirname "$MANIFEST")"
SHA=$(git -C "$ROOT" rev-parse HEAD)
sed -e "s|^\\( *origin:\\).*|\\1 $ROOT|" -e "s|^\\( *commit_sha:\\).*|\\1 $SHA|" \
    "$HERE/manifest.yml" > "$MANIFEST"

mkdir -p "$ROOT/dist"
python3 "$CATALOG/tools/bundle.py" --nolint "$@" "$MANIFEST" "$ROOT/dist/catalog-bundle.zip"

echo
echo "Valid. The manifest to submit for this commit:"
echo
sed "s|^\\( *commit_sha:\\).*|\\1 $SHA|" "$HERE/manifest.yml"
