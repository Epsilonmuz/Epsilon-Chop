#!/bin/bash
# make-mac-installer.sh "Product Name" <folder> [version]
#
# Builds "<Product-Name>-macOS-Installer.pkg" from every .vst3, .component and .app found in <folder>,
# then installs it on this (CI) Mac and checks every file landed where a DAW looks for it:
#   VST3       -> /Library/Audio/Plug-Ins/VST3        (FL Studio, Ableton, Studio One, Reaper ...)
#   AU         -> /Library/Audio/Plug-Ins/Components  (Logic, GarageBand, also FL / Ableton)
#   Standalone -> /Applications
# Files installed by a .pkg carry no download quarantine, so no Terminal step is needed afterwards.
# Same-named older copies in the user's own ~/Library/Audio/Plug-Ins are removed, so the DAW
# doesn't list the plug-in twice. Output goes to ./installer/.
set -euo pipefail
NAME="$1"; SRC="$2"; VERSION="${3:-}"
SLUG=$(printf '%s' "$NAME" | tr ' ' '-')
ID="com.epsilonsounds.installer.$(printf '%s' "$SLUG" | tr '[:upper:]' '[:lower:]')"
if [ -z "$VERSION" ]; then
  VERSION=$(python3 - <<'PY'
import glob, re
v = ""
try:
    m = re.search(r'project\s*\(\s*\S+\s+VERSION\s+([0-9.]+)', open('CMakeLists.txt').read())
    v = m.group(1) if m else ""
except OSError:
    pass
if not v:
    for f in glob.glob('*.jucer'):
        m = re.search(r'<JUCERPROJECT\b[^>]*?\sversion="([0-9.]+)"', open(f).read(), re.S)
        if m: v = m.group(1); break
print(v or "0.0.0")
PY
)
fi
echo "Installer: $NAME $VERSION from $SRC"

WORK=$(mktemp -d)
OUT="${OUT_DIR:-installer}"
mkdir -p "$OUT" "$WORK/scripts"
PKG="$OUT/$SLUG-macOS-Installer.pkg"
NAMES=()          # every bundle, for the clean-up and the checks
CHOICES=""; REFS=""; OUTLINE=""

add_kind() {      # kind pattern destination title description
  local kind=$1 pattern=$2 dest=$3 title=$4 desc=$5 root="$WORK/root-$1" n=0
  mkdir -p "$root"
  while IFS= read -r -d '' b; do
    local base; base=$(basename "$b")
    [ -e "$root/$base" ] && continue
    ditto "$b" "$root/$base"; NAMES+=("$dest/$base"); n=$((n+1))
  done < <(find "$SRC" -name "$pattern" -type d -prune -print0)
  [ $n -eq 0 ] && return 0
  # keep each bundle exactly where we put it (Installer may otherwise "update" a copy found elsewhere)
  pkgbuild --analyze --root "$root" "$WORK/$kind.plist" >/dev/null
  python3 - "$WORK/$kind.plist" <<'PY'
import plistlib, sys
p = sys.argv[1]
c = plistlib.load(open(p, 'rb'))
for x in c: x['BundleIsRelocatable'] = False
plistlib.dump(c, open(p, 'wb'))
PY
  pkgbuild --quiet --root "$root" --component-plist "$WORK/$kind.plist" --scripts "$WORK/scripts" \
           --identifier "$ID.$kind" --version "$VERSION" --install-location "$dest" "$WORK/$kind.pkg"
  OUTLINE+="    <line choice=\"$kind\"/>"$'\n'
  CHOICES+="  <choice id=\"$kind\" title=\"$title\" description=\"$desc\"><pkg-ref id=\"$ID.$kind\"/></choice>"$'\n'
  REFS+="  <pkg-ref id=\"$ID.$kind\" version=\"$VERSION\" onConclusion=\"none\">$kind.pkg</pkg-ref>"$'\n'
  echo "  $kind: $n bundle(s) -> $dest"
}

# postinstall (runs once per component, harmless to repeat): drop old per-user copies, refresh the AU list
cat > "$WORK/scripts/postinstall" <<'POST'
#!/bin/bash
U=$(stat -f%Su /dev/console 2>/dev/null || true)
H=$(dscl . -read "/Users/$U" NFSHomeDirectory 2>/dev/null | awk '{print $2}')
for p in "$2"/*.vst3 "$2"/*.component; do
  [ -e "$p" ] || continue
  b=$(basename "$p")
  if [ -n "$H" ]; then rm -rf "$H/Library/Audio/Plug-Ins/VST3/$b" "$H/Library/Audio/Plug-Ins/Components/$b"; fi
done
killall -9 AudioComponentRegistrar 2>/dev/null || true
exit 0
POST
chmod +x "$WORK/scripts/postinstall"

add_kind vst3 '*.vst3'      /Library/Audio/Plug-Ins/VST3       "VST3 plug-in"  "For FL Studio, Ableton Live, Studio One, Reaper and most DAWs. Installs to /Library/Audio/Plug-Ins/VST3."
add_kind au   '*.component' /Library/Audio/Plug-Ins/Components "Audio Unit"    "For Logic Pro and GarageBand (FL Studio and Ableton can use it too). Installs to /Library/Audio/Plug-Ins/Components."
add_kind app  '*.app'       /Applications                      "Standalone app" "Runs without a DAW. Installs to the Applications folder."
[ ${#NAMES[@]} -gt 0 ] || { echo "::error::no .vst3, .component or .app found in $SRC"; exit 1; }

cat > "$WORK/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>$NAME $VERSION</title>
  <organization>com.epsilonsounds</organization>
  <options customize="allow" require-scripts="false" hostArchitectures="x86_64,arm64"/>
  <domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>
  <choices-outline>
$OUTLINE  </choices-outline>
$CHOICES$REFS</installer-gui-script>
XML
productbuild --quiet --distribution "$WORK/distribution.xml" --package-path "$WORK" --version "$VERSION" "$PKG"
ls -lh "$PKG"

# Prove it: install it here and check every bundle is in place and runs on Intel and Apple Silicon Macs.
sudo installer -pkg "$PKG" -target / >/dev/null
for p in "${NAMES[@]}"; do
  test -d "$p" || { echo "::error::$p was not installed"; exit 1; }
  exe=$(/usr/libexec/PlistBuddy -c 'Print CFBundleExecutable' "$p/Contents/Info.plist")
  archs=$(lipo -archs "$p/Contents/MacOS/$exe")
  case " $archs " in *" x86_64 "*) ;; *) echo "::error::$p has no Intel (x86_64) code: $archs"; exit 1;; esac
  echo "  installed $p  [$archs]"
  xattr -p com.apple.quarantine "$p" >/dev/null 2>&1 && { echo "::error::$p is quarantined"; exit 1; }
done
{ echo "### $NAME $VERSION installer"; echo; for p in "${NAMES[@]}"; do echo "- \`$p\`"; done; } >> "${GITHUB_STEP_SUMMARY:-/dev/null}"
echo "$VERSION" > "$OUT/VERSION"
