#!/usr/bin/env bash
# Refuse to publish a site that would break updates: every variant's manifest
# must exist, match the release tag, and point only at files that are there
# and whose md5 and sha256 match the manifest's.
# Usage: scripts/check_site_firmware.sh <site dir> <tag, e.g. v1.5.4>
set -euo pipefail

site=$1
want=${2#v}
fail=0

for v in moonhub75 moonhub75-2panel scoreboard75; do
  m="$site/firmware/$v/manifest.json"
  if [ ! -f "$m" ]; then
    echo "::error::$m is missing"
    fail=1
    continue
  fi
  bad=0
  got=$(jq -r .version "$m")
  if [ "$got" != "$want" ]; then
    echo "::error::$m is version $got, the release is $want"
    bad=1
  fi
  # One line per file: path, md5, sha256 ("-" when the manifest has none).
  files=$(jq -r '.builds[] | (.ota // empty), (.parts[]? // empty) | select(.path)
    | "\(.path) \(.md5 // "-") \(.sha256 // "-")"' "$m")
  while read -r f md5 sha; do
    [ -n "$f" ] || continue
    if [ ! -f "$site/firmware/$v/$f" ]; then
      echo "::error::$m points at $f, which is not in the site"
      bad=1
      continue
    fi
    if [ "$md5" = - ] || [ "$sha" = - ]; then
      echo "::error::$m has no md5 or sha256 for $f"
      bad=1
      continue
    fi
    has=$(md5sum "$site/firmware/$v/$f" | cut -d' ' -f1)
    if [ "$has" != "${md5,,}" ]; then
      echo "::error::$f in $v has md5 $has, $m says $md5"
      bad=1
    fi
    has=$(sha256sum "$site/firmware/$v/$f" | cut -d' ' -f1)
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
