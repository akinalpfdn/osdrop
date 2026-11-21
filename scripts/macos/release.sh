#!/usr/bin/env bash
# Builds the Mac app for a release, signs it with Developer ID, notarizes and staples it, packages
# it as Osdrop.dmg (also signed, notarized and stapled), adds the DMG to the draft release that the
# Release workflow made for the tag, and publishes that release. See docs/RELEASING.md.
#
# Usage: scripts/macos/release.sh [--no-publish]
#   --no-publish  upload the DMG but leave the release a draft
#
# Run it on the commit the version tag points at, after the Release workflow attached the Windows
# installer. Needs: Xcode, XcodeGen, a "Developer ID Application" certificate, the notarytool
# keychain profile "mac-notary", an authenticated gh, and dmgbuild (pip3 install dmgbuild).
set -euo pipefail

readonly NOTARY_PROFILE="mac-notary"
readonly APP_NAME="Osdrop"
readonly DMG_NAME="Osdrop.dmg"
readonly WINDOWS_INSTALLER="Osdrop-Setup.exe"

publish=true
case "${1:-}" in
    "") ;;
    --no-publish) publish=false ;;
    *) echo "usage: $0 [--no-publish]" >&2; exit 2 ;;
esac

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$root"
work="$root/build/release/macos"
log="$work/xcodebuild.log"

fail() {
    echo "error: $*" >&2
    exit 1
}

step() {
    echo "==> $*"
}

# Submits a file and waits; anything but Accepted prints Apple's log and stops the release.
notarize() {
    local file="$1" result status id
    result="$(xcrun notarytool submit "$file" --keychain-profile "$NOTARY_PROFILE" --wait --output-format json)"
    status="$(python3 -c 'import json, sys; print(json.load(sys.stdin).get("status", ""))' <<<"$result")"
    id="$(python3 -c 'import json, sys; print(json.load(sys.stdin).get("id", ""))' <<<"$result")"
    if [[ "$status" != "Accepted" ]]; then
        xcrun notarytool log "$id" --keychain-profile "$NOTARY_PROFILE" >&2 || true
        fail "notarization of $(basename "$file") ended with status '$status' (submission $id)"
    fi
    echo "Notarized $(basename "$file") (submission $id)"
}

step "Checks"
version="$(python3 scripts/release_version.py)"
tag="v$version"
[[ -z "$(git status --porcelain)" ]] || fail "the working tree has changes; release from a clean checkout"
tagged="$(git rev-parse -q --verify "refs/tags/$tag^{commit}")" || fail "no tag $tag; push the tag first"
[[ "$tagged" == "$(git rev-parse HEAD)" ]] || fail "HEAD is not the commit $tag points at; run: git checkout $tag"
identity="$(security find-identity -v -p codesigning | grep -m 1 '"Developer ID Application:' || true)"
[[ -n "$identity" ]] || fail "no Developer ID Application certificate in the keychain"
# "  1) <SHA-1> "Developer ID Application: <name> (<team ID>)""
certificate="$(awk '{print $2}' <<<"$identity")"
team="$(sed -n 's/.*(\([A-Z0-9]\{10\}\))".*/\1/p' <<<"$identity")"
[[ -n "$certificate" && -n "$team" ]] || fail "could not read the certificate from: $identity"
xcrun notarytool history --keychain-profile "$NOTARY_PROFILE" >/dev/null 2>&1 ||
    fail "the notarytool keychain profile '$NOTARY_PROFILE' does not work"
gh auth status >/dev/null 2>&1 || fail "gh is not signed in; run: gh auth login"
python3 -m dmgbuild --help >/dev/null 2>&1 || fail "dmgbuild is missing; run: pip3 install dmgbuild"
command -v xcodegen >/dev/null || fail "XcodeGen is missing; run: brew install xcodegen"
assets="$(gh release view "$tag" --json assets --jq '.assets[].name')" ||
    fail "no release for $tag yet; wait for the Release workflow to finish"
grep -qx "$WINDOWS_INSTALLER" <<<"$assets" ||
    fail "the $tag release has no $WINDOWS_INSTALLER yet; wait for the Release workflow to finish"
echo "Osdrop $version, team $team"

rm -rf "$work"
mkdir -p "$work"

step "Archive (Release, Developer ID)"
(cd macos && xcodegen generate --quiet)
xcodebuild archive -quiet \
    -project macos/Osdrop.xcodeproj -scheme Osdrop -configuration Release \
    -destination "generic/platform=macOS" -archivePath "$work/$APP_NAME.xcarchive" \
    CODE_SIGN_STYLE=Manual CODE_SIGN_IDENTITY="Developer ID Application" DEVELOPMENT_TEAM="$team" \
    >"$log" 2>&1 || { tail -n 40 "$log" >&2; fail "archive failed; full log: $log"; }

step "Export"
cat >"$work/ExportOptions.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>method</key>
    <string>developer-id</string>
    <key>signingStyle</key>
    <string>manual</string>
    <key>signingCertificate</key>
    <string>Developer ID Application</string>
    <key>teamID</key>
    <string>$team</string>
</dict>
</plist>
EOF
xcodebuild -exportArchive -quiet -archivePath "$work/$APP_NAME.xcarchive" \
    -exportPath "$work/export" -exportOptionsPlist "$work/ExportOptions.plist" \
    >>"$log" 2>&1 || { tail -n 40 "$log" >&2; fail "export failed; full log: $log"; }
app="$work/export/$APP_NAME.app"
codesign --verify --deep --strict "$app"
# The capture guard and the network helper ride inside the bundle, signed with it.
for part in Contents/MacOS/OsdropCaptureGuard Contents/MacOS/OsdropNetworkHelper \
    Contents/Library/LaunchDaemons/com.akinalpfdn.osdrop.network-helper.plist; do
    [[ -e "$app/$part" ]] || fail "the app has no $part"
done
built="$(/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "$app/Contents/Info.plist")"
[[ "$built" == "$version" ]] || fail "the app reports version $built, expected $version"

step "Notarize the app"
ditto -c -k --keepParent "$app" "$work/$APP_NAME.zip"
notarize "$work/$APP_NAME.zip"
xcrun stapler staple -q "$app"
# Stapling leaves extended attributes that make dmgbuild's copy fail on recent macOS.
xattr -cr "$app"
spctl --assess --type execute -v "$app" || fail "Gatekeeper rejects the stapled app"

step "DMG"
dmg="$work/$DMG_NAME"
python3 -m dmgbuild -s scripts/macos/dmg_settings.py -D app="$app" "$APP_NAME" "$dmg" >/dev/null
codesign --sign "$certificate" --timestamp "$dmg"
notarize "$dmg"
xcrun stapler staple -q "$dmg"
spctl --assess --type open --context context:primary-signature -v "$dmg" ||
    fail "Gatekeeper rejects the DMG"
(cd "$work" && shasum -a 256 "$DMG_NAME" >"$DMG_NAME.sha256")

step "Upload to the $tag release"
gh release upload "$tag" "$dmg" "$dmg.sha256" --clobber
if [[ "$publish" == true ]]; then
    gh release edit "$tag" --draft=false --latest
    echo "Published: $(gh release view "$tag" --json url --jq .url)"
else
    echo "Uploaded; the release is still a draft: $(gh release view "$tag" --json url --jq .url)"
fi
