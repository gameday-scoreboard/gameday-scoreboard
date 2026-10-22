#!/usr/bin/env bash
# Add the beta channels to an assembled site from the rolling beta-firmware
# prerelease, which publish-beta in build.yml refreshes from the beta branch:
# site/firmware/beta/<variant>/ for the bar builds (firmware/pages/bar.yaml),
# and site/firmware/<variant>-beta/ for panels with "Follow beta" on.
#
# Never fails the site: a stable release or a docs change must not wait on
# the beta. With no beta yet, or one that doesn't check out, the site goes
# out without that channel (its panels then find no update) and the run shows
# a warning. Each channel goes on or stays off by itself.
# Usage: scripts/add_beta_channel.sh <site dir>
set -uo pipefail

site=$1
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

if ! gh release view beta-firmware --repo "$GITHUB_REPOSITORY" >/dev/null 2>&1; then
  echo "No beta-firmware prerelease yet; the site has no beta channel."
  exit 0
fi
ok=1
# unzip -d makes only the last directory, not firmware/ above it.
mkdir -p "$tmp/firmware/beta"
for v in moonhub75 scoreboard75; do
  if ! gh release download beta-firmware --repo "$GITHUB_REPOSITORY" -p "firmware-beta-$v.zip" -D "$tmp" ||
     ! unzip -q -o "$tmp/firmware-beta-$v.zip" -d "$tmp/firmware/beta"; then
    ok=0
  fi
done
if [ $ok = 1 ] && bash "$(dirname "$0")/check_site_firmware.sh" --beta "$tmp"; then
  mkdir -p "$site/firmware"
  rm -rf "$site/firmware/beta"
  cp -r "$tmp/firmware/beta" "$site/firmware/beta"
  echo "Beta channel: $(jq -r .version "$site/firmware/beta/scoreboard75/manifest.json")"
else
  echo "::warning::The beta-firmware prerelease is missing a file or doesn't check out; this deploy has no beta channel."
fi

ok=1
for v in moonhub75 scoreboard75; do
  if ! gh release download beta-firmware --repo "$GITHUB_REPOSITORY" -p "firmware-$v-beta.zip" -D "$tmp" ||
     ! unzip -q -o "$tmp/firmware-$v-beta.zip" -d "$tmp/firmware"; then
    ok=0
  fi
done
if [ $ok = 1 ] && bash "$(dirname "$0")/check_site_firmware.sh" --follow-beta "$tmp"; then
  for v in moonhub75 scoreboard75; do
    rm -rf "$site/firmware/$v-beta"
    cp -r "$tmp/firmware/$v-beta" "$site/firmware/$v-beta"
  done
  echo "Follow beta channel: $(jq -r .version "$site/firmware/scoreboard75-beta/manifest.json")"
else
  echo "::warning::The beta-firmware prerelease has no Follow beta builds, or they don't check out; this deploy leaves them out."
fi
exit 0
