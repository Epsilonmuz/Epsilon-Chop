#!/bin/bash
# publish-latest.sh "Product Name" file...
# Puts the files on this repo's rolling "latest" release, so these links always give the newest build:
#   https://github.com/<owner>/<repo>/releases/latest/download/<file name>
# Several jobs may publish to it at once; existing files with the same name are replaced.
set -euo pipefail
NAME="$1"; shift
TAG=latest
VERSION=$(cat installer/VERSION 2>/dev/null || echo "")
REPO_URL="https://github.com/$GITHUB_REPOSITORY"
NOTES=$(mktemp)
cat > "$NOTES" <<MD
**$NAME${VERSION:+ $VERSION}**, built from \`${GITHUB_SHA:0:7}\` on $(date -u +%Y-%m-%d). Replaced automatically by every build of main.

**Mac:** download the \`-macOS-Installer.pkg\` and double-click it. It puts the VST3 in /Library/Audio/Plug-Ins/VST3, the Audio Unit in /Library/Audio/Plug-Ins/Components and the app in Applications, then rescan plug-ins in your DAW (FL Studio: Options > Manage plugins > Find installed plugins).
The first time, macOS may say it can't check the installer for malware (it isn't notarised by Apple): click Done, then System Settings > Privacy & Security > Open Anyway.

**iPhone** (where there's an \`.ipa\`): open this page on the iPhone, download the \`.ipa\` and open it with SideStore, the same way as Finish Line.
MD
if ! gh release view "$TAG" >/dev/null 2>&1; then
  gh release create "$TAG" --title "$NAME (latest build)" --notes-file "$NOTES" --target "$GITHUB_SHA" --latest "$@" \
    || { sleep 8; gh release upload "$TAG" "$@" --clobber; }
else
  gh release upload "$TAG" "$@" --clobber
  gh release edit "$TAG" --title "$NAME (latest build)" --notes-file "$NOTES" --latest
  # move the tag to this build, so the release points at the code it was built from
  gh api -X PATCH "repos/$GITHUB_REPOSITORY/git/refs/tags/$TAG" -f sha="$GITHUB_SHA" -F force=true >/dev/null || true
fi
for f in "$@"; do echo "Published: $REPO_URL/releases/latest/download/$(basename "$f")"; done
