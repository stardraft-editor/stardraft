# The release of a tag (.github/workflows/release.yml), from the files of the tested build: dl/stardraft.exe,
# dl/stardraft.pdb, dl/dmg/*.dmg, dl/appimage/StarDraft-x86_64.AppImage. Run in the tag's checkout (tools/release.py
# reads the changelog there).
#   v<version>:                 the release "StarDraft 2 - v<version> (build <commit>)", marked Latest, and its Discord post.
#   v<version>-preview.<n>:     a pre-release for early testers (Windows, macOS and Linux), never Latest, no Discord post.
#   ci-test-<n>[-preview]:      a test of this script: the same release as a DRAFT (only the repository's members see it;
#                               the version is the changelog's newest), the macOS and Linux files always, no Discord
#                               post. Delete it after the check: gh release delete <tag> --yes --cleanup-tag (and
#                               symbols-<tag>).
# The environment: GH_TOKEN, GITHUB_REF_NAME, GITHUB_SHA, RELEASE_MAC, RELEASE_LINUX, DISCORD_RELEASE_WEBHOOK,
# DISCORD_GENERAL_WEBHOOK.
$ErrorActionPreference = 'Stop'
$tag = $env:GITHUB_REF_NAME
$test = $tag -like 'ci-test-*'
$preview = $tag -like '*-preview*'
if ($test) {
  $newest = Select-String -Path docs/user/CHANGELOG.md -Pattern '^## (\d+(\.\d+)+)' | Select-Object -First 1
  $version = $newest.Matches[0].Groups[1].Value
  if ($preview) { $version = "$version-preview.0" }
  Write-Output "A test release (a draft) of $tag, as the version $version"
} else {
  $version = $tag.Substring(1)
}
$zip = "StarDraft-$version-win64.zip"
Compress-Archive -Force -Path dl/stardraft.exe -DestinationPath $zip  # the program only (the notes are on the release page)
# The text: what is new in this version (its changelog section), then the base changelog (what StarDraft is and does,
# how to start) for new visitors; picture paths become addresses at this tag. tools/release.py makes it, the same text
# that `release.py notes <version>` shows before a release.
# A preview: its text is tools/preview_notes.md.
if ($preview) {
  Copy-Item tools/preview_notes.md notes.md
  $title = "StarDraft 2 $version"
} else {
  python3 -I tools/release.py notes $version --tag $tag --out notes.md
  if ($LASTEXITCODE) { throw "No release text for $version" }
  $title = python3 -I tools/release.py title $version
}
# A run again (Re-run of a failed run): a release that has its zip stays as it is (people can have that zip, and its
# symbols match it); a release without its zip gets this build's zip and symbols.
$assets = gh release view $tag --json assets --jq '.assets[].name' 2>$null
$exists = $LASTEXITCODE -eq 0
$global:LASTEXITCODE = 0
if ($exists -and @($assets) -contains $zip) {
  Write-Output "The release $tag has its zip: nothing changes. A post that did not go: Actions > Post screenshots to Discord."
  exit 0
}
# The symbols first (stardraft.pdb), so that the kept symbols match the zip that goes out: a draft release
# symbols-<tag>, which only the repository's members see, kept for good (an artifact goes after 90 days), to read crash
# reports' crash.dmp files. Not on the release page.
$sym = "StarDraft-$version-symbols.zip"
Compress-Archive -Force -Path dl/stardraft.pdb -DestinationPath $sym
$kept = @(gh release list --limit 200 --json tagName --jq '.[].tagName')
if ($kept -contains "symbols-$tag") {
  gh release upload "symbols-$tag" $sym --clobber  # (its zip did not go out: these symbols replace them)
} else {
  gh release create "symbols-$tag" $sym --draft --target $env:GITHUB_SHA --title "Symbols of $title" `
    --notes "stardraft.pdb of $title, for its crash reports (crash.dmp). A draft: only the repository's members see it."
}
if ($LASTEXITCODE) { throw "The symbols were not kept ($LASTEXITCODE)" }
if ($exists) {
  gh release upload $tag $zip
} elseif ($test) {
  $kind = @()  # (an array: a string would be splatted one character at a time)
  if ($preview) { $kind += '--prerelease' }
  gh release create $tag $zip --draft @kind --latest=false --target $env:GITHUB_SHA --title $title --notes-file notes.md
} elseif ($preview) {
  gh release create $tag $zip --title $title --prerelease --latest=false --verify-tag --notes-file notes.md
} else {
  gh release create $tag $zip --title $title --latest --verify-tag --notes-file notes.md
}
if ($LASTEXITCODE) { throw "The release $tag was not made ($LASTEXITCODE)" }

# A release tag with the repository variable RELEASE_MAC "on", or a preview: the macOS disk image goes on the release,
# as StarDraft-<version>-macos-arm64.dmg. RELEASE_MAC not "on" (the default): the disk image is only an artifact of the
# build. A run again leaves a release's disk image as it is.
function Attach($file, $name) {
  $have = @(gh release view $tag --json assets --jq '.assets[].name')
  if ($have -contains $name) { Write-Output "The release $tag has ${name}: nothing changes."; return }
  if ((Split-Path $file -Leaf) -ne $name) { Move-Item $file (Join-Path (Split-Path $file) $name); $file = Join-Path (Split-Path $file) $name }
  gh release upload $tag $file
  if ($LASTEXITCODE) { throw "$name did not go on the release $tag ($LASTEXITCODE)" }
}
if ($test -or $preview -or $env:RELEASE_MAC -eq 'on') {
  Attach (Get-ChildItem dl/dmg/*.dmg | Select-Object -First 1).FullName "StarDraft-$version-macos-arm64.dmg"
}
# A release tag with the repository variable RELEASE_LINUX "on", or a preview: the AppImage goes on the release, as
# StarDraft-<version>-x86_64.AppImage (the name Check for updates looks for).
if ($test -or $preview -or $env:RELEASE_LINUX -eq 'on') {
  Attach (Resolve-Path dl/appimage/StarDraft-x86_64.AppImage).Path "StarDraft-$version-x86_64.AppImage"
}

if ($test) { Write-Output "Test release $tag (a draft) is made: not posted in Discord."; exit 0 }
if ($preview) { Write-Output "Preview $tag is out (a pre-release): not posted in Discord."; exit 0 }
# Discord: one post in each channel that has a webhook (a webhook posts in its own channel only), with the screenshots
# of this version's changelog section as files (at most 10). A post that does not go makes the run fail after the
# release is out: post again in that channel with Actions > Post screenshots to Discord.
$hooks = @($env:DISCORD_RELEASE_WEBHOOK, $env:DISCORD_GENERAL_WEBHOOK) | Where-Object { $_ }
if (-not $hooks) { throw 'No Discord webhook (DISCORD_RELEASE_WEBHOOK, DISCORD_GENERAL_WEBHOOK): the release is out, not posted' }
$url = gh release view $tag --json url --jq .url
@{ content = "**$title** is out: $url" } | ConvertTo-Json | Set-Content payload.json -Encoding utf8NoBOM
$images = @(python3 -I tools/release.py notes $version --images)
$failed = 0
foreach ($hook in $hooks) {
  $form = @('-sS', '--fail-with-body', '-F', 'payload_json=<payload.json')
  for ($i = 0; $i -lt $images.Count; $i++) { $form += @('-F', "files[$i]=@$($images[$i])") }
  curl @form $hook
  if ($LASTEXITCODE -ne 0) {
    $failed++
    Write-Warning "The Discord post did not go to $(if ($hook -eq $env:DISCORD_RELEASE_WEBHOOK) { 'updates' } else { 'general' }) ($LASTEXITCODE)"
  }
}
if ($failed) { throw "$failed Discord posts did not go: post again with Actions > Post screenshots to Discord" }
