#!/usr/bin/env bash
# Publish an assembled site's update feed to the mirror, a Cloudflare Pages
# project served at updates.gameday-scoreboard.com, then check the mirror
# serves what the site has. A panel reads the mirror when its manifest read
# from install.gameday-scoreboard.com fails (components/gameday/update_mirror.h), at
# the same path, so every channel goes: firmware/<variant>/, the beta
# channels, the two-panel build.
#
# Only each manifest and the files it names go up (the OTA and factory
# images). The .elf next to them is for crash decoding, and at about 45 MB it
# is over Pages' 25 MiB file limit.
#
# Needs CLOUDFLARE_API_TOKEN (Account, Cloudflare Pages, Edit) and
# CLOUDFLARE_ACCOUNT_ID. Without them it warns and stops, so a release still
# goes out on GitHub Pages alone.
# Usage: scripts/publish_mirror.sh <site dir>
set -euo pipefail

site=$1
project=${MIRROR_PROJECT:-gameday-firmware}
host=${MIRROR_HOST:-https://updates.gameday-scoreboard.com}

if [ -z "${CLOUDFLARE_API_TOKEN:-}" ] || [ -z "${CLOUDFLARE_ACCOUNT_ID:-}" ]; then
  echo "::warning::No Cloudflare token for the update mirror, so this deploy reached install.gameday-scoreboard.com only."
  exit 0
fi

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
manifests=$(cd "$site" && find firmware -name manifest.json | sort)
if [ -z "$manifests" ]; then
  echo "::error::No manifests under $site/firmware"
  exit 1
fi
for m in $manifests; do
  dir=$(dirname "$m")
  mkdir -p "$stage/$dir"
  cp "$site/$m" "$stage/$m"
  for f in $(jq -r '.builds[] | (.ota.path // empty), (.parts[]?.path // empty)' "$site/$m" | sort -u); do
    cp "$site/$dir/$f" "$stage/$dir/$f"
  done
done
echo "Mirror files:"
(cd "$stage" && find . -type f | sort)

wrangler() { npx --yes wrangler@4 "$@"; }
# The first deploy makes the project; its custom domain is added once by hand.
# Listed first, not piped: grep -q quitting early under pipefail would read as
# "no project" and try to create it again.
projects=$(wrangler pages project list)
if ! grep -qw -- "$project" <<<"$projects"; then
  wrangler pages project create "$project" --production-branch main
fi
wrangler pages deploy "$stage" --project-name "$project" --branch main --commit-dirty=true

# Every manifest live on the mirror with the same version and OTA image. The
# query string gets past any cache in front of it.
ok=0
for i in $(seq 1 20); do
  ok=1
  for m in $manifests; do
    want=$(jq -c '[.version, [.builds[].ota.md5]]' "$site/$m")
    got=$(curl -fsS "$host/$m?run=${GITHUB_RUN_ID:-local}-$i" | jq -c '[.version, [.builds[].ota.md5]]' 2>/dev/null || true)
    ota=$(jq -r '.builds[0].ota.path' "$site/$m")
    if [ "$got" != "$want" ] || ! curl -fsSI "$host/$(dirname "$m")/$ota" >/dev/null; then
      ok=0
      echo "try $i: $host/$m has '$got', want '$want'"
      break
    fi
  done
  if [ $ok = 1 ]; then
    echo "Mirror live at $host: $(echo "$manifests" | wc -l) manifests"
    exit 0
  fi
  sleep 15
done
echo "::error::$host doesn't serve this deploy's update feed 5 minutes after publishing it"
exit 1
