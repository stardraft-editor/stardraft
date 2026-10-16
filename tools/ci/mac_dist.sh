#!/bin/bash
# The macOS download: StarDraft.app, signed, in a disk image (.dmg) with a background and a link to Applications,
# notarized by Apple when the secrets are there (docs/dev/DEVELOPING.md, "Signing the macOS app").
#
# usage: tools/ci/mac_dist.sh <build/stardraft.app> <out.dmg> [<volume name>]
#
# The environment gives the secrets (GitHub: the repository's secrets; all empty in a fork or a pull request):
#   APPLE_CERT_P12       the Developer ID Application certificate and its key, a .p12 file in base64
#   APPLE_CERT_PASSWORD  the .p12 file's password
#   APPLE_ID, APPLE_TEAM_ID, APPLE_APP_PASSWORD   the Apple ID, its team and an app-specific password, for notarytool
# With the certificate: each Quick Look extension (with its sandbox, apps/quicklook/quicklook.entitlements), then the
# app (apps/editor/stardraft.entitlements) are signed with the Developer ID, the hardened runtime and a timestamp, then
# the .dmg; with the Apple ID too, the .dmg is notarized and its ticket stapled. Without the certificate: signed ad hoc
# (with the hardened runtime and the same entitlements) and a line says that signing was skipped.
set -euo pipefail

src=$1
dmg=$2
volume=${3:-StarDraft 2}
root=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${RUNNER_TEMP:-${TMPDIR:-/tmp}}/stardraft-dist.XXXXXX")
app="$work/StarDraft.app"
appEntitlements="$root/apps/editor/stardraft.entitlements"
qlEntitlements="$root/apps/quicklook/quicklook.entitlements"
case "$dmg" in /*) ;; *) dmg="$PWD/$dmg" ;; esac

ditto "$src" "$app"

# --- The certificate: a keychain of this run's own ---------------------------------------------------------------
identity="-"
keychain=""
if [ -n "${APPLE_CERT_P12:-}" ] && [ -n "${APPLE_CERT_PASSWORD:-}" ]; then
    keychain="$work/signing.keychain-db"
    keychainPassword=$(uuidgen)
    trap 'security delete-keychain "$keychain" >/dev/null 2>&1 || true' EXIT
    security create-keychain -p "$keychainPassword" "$keychain"
    security set-keychain-settings -lut 3600 "$keychain"
    security unlock-keychain -p "$keychainPassword" "$keychain"
    printf '%s' "$APPLE_CERT_P12" | base64 --decode > "$work/cert.p12"
    security import "$work/cert.p12" -k "$keychain" -P "$APPLE_CERT_PASSWORD" -f pkcs12 -T /usr/bin/codesign
    rm -f "$work/cert.p12"
    security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$keychainPassword" "$keychain" >/dev/null
    # (codesign finds the identity in the search list)
    security list-keychains -d user -s "$keychain" $(security list-keychains -d user | tr -d '"')
    identity=$(security find-identity -v -p codesigning "$keychain" | sed -nE 's/.*"(Developer ID Application:[^"]*)".*/\1/p' | head -n 1)
    if [ -z "$identity" ]; then
        echo "::error::APPLE_CERT_P12 has no Developer ID Application certificate"
        exit 1
    fi
    echo "Signing with: $identity"
else
    echo "Signing skipped: the secrets APPLE_CERT_P12 and APPLE_CERT_PASSWORD are not set. The app is signed ad hoc (macOS asks before it opens it the first time)."
fi

sign() {  # <path> <entitlements>
    if [ "$identity" = "-" ]; then
        codesign --force --options runtime --timestamp=none --entitlements "$2" --sign - "$1"
    else
        codesign --force --options runtime --timestamp --entitlements "$2" --sign "$identity" --keychain "$keychain" "$1"
    fi
}

# --- The app: its extensions first, then the app (a signature seals what is in it) --------------------------------
for ext in "$app"/Contents/PlugIns/*.appex; do
    if [ -d "$ext" ]; then sign "$ext" "$qlEntitlements"; fi
done
sign "$app" "$appEntitlements"
codesign --verify --strict --deep --verbose=2 "$app"
# What the app says of itself: its document types, its extensions and their signatures (sdl_dist.inc: --check bundle).
"$app/Contents/MacOS/stardraft" --snapshot "$work/check.png" --check bundle | tee "$work/bundle.txt"
grep -q "^bundle: app signature: .*hardened runtime yes" "$work/bundle.txt"
grep -q "^bundle: app entitlements: .*com.apple.security.network.client" "$work/bundle.txt"
for ext in StarDraftPreview StarDraftThumbnail; do
    grep -q "^bundle: $ext.appex entitlements: .*com.apple.security.app-sandbox" "$work/bundle.txt"
done

# --- The disk image -------------------------------------------------------------------------------------------------
python3 -I "$root/tools/make_dmg_background.py" "$work/background.png" "$work/background@2x.png"
tiffutil -cathidpicheck "$work/background.png" "$work/background@2x.png" -out "$work/background.tiff"
rm -f "$dmg"
made=""
if python3 -m venv "$work/venv" && "$work/venv/bin/pip" install --quiet "dmgbuild==1.6.*"; then
    # (dmgbuild: hdiutil makes the image; dmgbuild writes the window's .DS_Store: the background, the icons' places)
    if "$work/venv/bin/dmgbuild" -s "$root/tools/ci/dmg_settings.py" -D app="$app" -D background="$work/background.tiff" \
        -D icon="$app/Contents/Resources/stardraft.icns" "$volume" "$dmg"; then
        made=dmgbuild
    fi
fi
if [ -z "$made" ]; then
    echo "::warning::dmgbuild did not make the disk image: a plain one (the app and the Applications link, no background)"
    mkdir "$work/plain"
    ditto "$app" "$work/plain/StarDraft.app"
    ln -s /Applications "$work/plain/Applications"
    hdiutil create -volname "$volume" -srcfolder "$work/plain" -fs HFS+ -format UDZO -ov "$dmg"
    made=hdiutil
fi
hdiutil verify "$dmg"

# What is in it: the app (its signature good), the Applications link, the background.
mount="$work/mount"
mkdir "$mount"
hdiutil attach -nobrowse -readonly -mountpoint "$mount" "$dmg" >/dev/null
ls -la "$mount"
ok=1
[ -d "$mount/StarDraft.app" ] || { echo "::error::no StarDraft.app in the image"; ok=0; }
[ -L "$mount/Applications" ] || { echo "::error::no Applications link in the image"; ok=0; }
codesign --verify --strict --deep "$mount/StarDraft.app" || ok=0
if [ "$made" = dmgbuild ]; then
    # (dmgbuild names it .background.tiff, beside .DS_Store, which places it in the window)
    [ -f "$mount/.background.tiff" ] || [ -d "$mount/.background" ] || { echo "::error::no background in the image"; ok=0; }
    [ -f "$mount/.DS_Store" ] || { echo "::error::no window settings (.DS_Store) in the image"; ok=0; }
fi
hdiutil detach "$mount" >/dev/null
[ "$ok" = 1 ]

# --- Signing and notarizing the image -------------------------------------------------------------------------------
if [ "$identity" != "-" ]; then
    codesign --force --timestamp --sign "$identity" --keychain "$keychain" "$dmg"
    if [ -n "${APPLE_ID:-}" ] && [ -n "${APPLE_TEAM_ID:-}" ] && [ -n "${APPLE_APP_PASSWORD:-}" ]; then
        xcrun notarytool submit "$dmg" --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" --password "$APPLE_APP_PASSWORD" \
            --wait --timeout 30m --output-format json | tee "$work/notary.json"
        status=$(python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1])).get("status", ""))' "$work/notary.json")
        if [ "$status" != "Accepted" ]; then
            id=$(python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1])).get("id", ""))' "$work/notary.json")
            [ -n "$id" ] && xcrun notarytool log "$id" --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" --password "$APPLE_APP_PASSWORD" || true
            echo "::error::Apple did not notarize the disk image ($status)"
            exit 1
        fi
        xcrun stapler staple "$dmg"
        xcrun stapler validate "$dmg"
        spctl --assess --type open --context context:primary-signature --verbose=2 "$dmg"
        echo "Notarized: $dmg"
    else
        echo "Notarization skipped: the secrets APPLE_ID, APPLE_TEAM_ID and APPLE_APP_PASSWORD are not set."
    fi
else
    echo "Notarization skipped: the image is signed ad hoc."
fi
echo "Made $dmg ($made)"
