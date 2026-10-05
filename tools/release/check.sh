#!/usr/bin/env bash
# Check that a tag can be released, and print its release notes.
#
#   tools/release/check.sh v1.1
#
# The tag has to be "v" + fap_version in application.fam - the version the app
# reports in About and the catalog lists - and changelog.md has to have an entry
# for it, which becomes the GitHub release's notes. The Release workflow runs
# this first, over the tagged commit; run it before tagging to catch the same
# mistakes without a round trip through CI.
set -euo pipefail

ROOT=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)

if [ $# -ne 1 ]; then
    echo "usage: $0 vMAJOR.MINOR" >&2
    exit 2
fi
TAG=$1

if ! [[ $TAG =~ ^v[0-9]+\.[0-9]+$ ]]; then
    # fap_version is MAJOR.MINOR: the firmware packs it into two 16-bit halves.
    echo "error: '$TAG' is not a release tag - expected vMAJOR.MINOR, e.g. v1.1" >&2
    exit 1
fi

VERSION=$(sed -n 's/^ *fap_version="\(.*\)",*$/\1/p' "$ROOT/application.fam")
if [ "v$VERSION" != "$TAG" ]; then
    echo "error: application.fam has fap_version=\"$VERSION\", so this commit is v$VERSION, not $TAG" >&2
    echo "       set fap_version=\"${TAG#v}\" and tag the commit that does" >&2
    exit 1
fi

# An entry runs from its "vX.Y:" line to the next one, as the catalog reads it.
NOTES=$(awk -v tag="$TAG:" '
    /^v[0-9]+\.[0-9]+:/ {
        colon = index($0, ":")
        inside = (substr($0, 1, colon) == tag)
        rest = substr($0, colon + 1)
        sub(/^[ \t]+/, "", rest)
        if(inside && rest != "") print rest
        next
    }
    inside
' "$ROOT/changelog.md" | sed -e '/./,$!d')
if [ -z "${NOTES//[[:space:]]/}" ]; then
    echo "error: changelog.md has no entry for $TAG - add a \"$TAG:\" line and what changed under it" >&2
    exit 1
fi

printf '%s\n' "$NOTES"
