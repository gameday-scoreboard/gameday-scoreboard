#!/usr/bin/env bash
# Refuse to publish a site that would break updates: every variant's manifest
# must exist, match the release tag, and point only at files that are there
# and whose md5 and sha256 match the manifest's.
# Usage: scripts/check_site_firmware.sh <site dir> <tag, e.g. v1.5.4>
#        scripts/check_site_firmware.sh --beta <site dir>
#        scripts/check_site_firmware.sh --follow-beta <site dir> [version]
# --beta checks the bar panels' beta channel (site/firmware/beta/<variant>/)
# instead: its versions end in -beta.<build>, the same for both bar variants
# (there is no two-panel bar build). --follow-beta checks the beta for panels
# with "Follow beta" on (site/firmware/<variant>-beta/) the same way, and
# against the given version if there is one.
set -euo pipefail

case "$1" in
  --beta)
    site=$2
    dir="$site/firmware"
    variants="beta/moonhub75 beta/scoreboard75"
    beta_mode=1
    want=""
    ;;
  --follow-beta)
    site=$2
    dir="$site/firmware"
    variants="moonhub75-beta scoreboard75-beta"
    beta_mode=1
    want=${3:-}
    ;;
  *)
    site=$1
    dir="$site/firmware"
    variants="moonhub75 moonhub75-2panel scoreboard75"
    beta_mode=0
    want=${2#v}
    ;;
esac
fail=0

for v in $variants; do
  m="$dir/$v/manifest.json"
  if [ ! -f "$m" ]; then
    echo "::error::$m is missing"
    fail=1
    continue
  fi
  bad=0
  got=$(jq -r .version "$m")
  if [ $beta_mode = 1 ]; then
    # Beta: the given version, or else the first variant's, is the one
    # every variant must have.
    if ! [[ "$got" =~ -beta\.[0-9]+$ ]]; then
      echo "::error::$m is version $got, not a beta (<release>-beta.<build>)"
      bad=1
    fi
    beta=${beta:-${want:-$got}}
    if [ "$got" != "$beta" ]; then
      echo "::error::$m is version $got, not $beta"
      bad=1
    fi
  elif [ "$got" != "$want" ]; then
    echo "::error::$m is version $got, the release is $want"
    bad=1
  fi
  # One line per file: path, md5, sha256 ("-" when the manifest has none).
  files=$(jq -r '.builds[] | (.ota // empty), (.parts[]? // empty) | select(.path)
    | "\(.path) \(.md5 // "-") \(.sha256 // "-")"' "$m")
  while read -r f md5 sha; do
    [ -n "$f" ] || continue
    if [ ! -f "$dir/$v/$f" ]; then
      echo "::error::$m points at $f, which is not in the site"
      bad=1
      continue
    fi
    if [ "$md5" = - ] || [ "$sha" = - ]; then
      echo "::error::$m has no md5 or sha256 for $f"
      bad=1
      continue
    fi
    has=$(md5sum "$dir/$v/$f" | cut -d' ' -f1)
    if [ "$has" != "${md5,,}" ]; then
      echo "::error::$f in $v has md5 $has, $m says $md5"
      bad=1
    fi
    has=$(sha256sum "$dir/$v/$f" | cut -d' ' -f1)
    if [ "$has" != "${sha,,}" ]; then
      echo "::error::$f in $v has sha256 $has, $m says $sha"
      bad=1
    fi
  done <<< "$files"
  if [ $bad = 0 ]; then
    echo "$v: manifest $got, all files present with matching checksums"
  fi
  fail=$((fail | bad))
done

exit $fail
