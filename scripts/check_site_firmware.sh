#!/usr/bin/env bash
# Refuse to publish a site that would break updates: every variant's manifest
# must exist, match the release tag, and point only at files that are there.
# Usage: scripts/check_site_firmware.sh <site dir> <tag, e.g. v1.5.4>
#        scripts/check_site_firmware.sh --beta <site dir>
# --beta checks the bar panels' beta channel (site/firmware/beta/<variant>/)
# instead: its versions end in -beta.<build>, the same for both bar variants
# (there is no two-panel bar build).
set -euo pipefail

if [ "$1" = --beta ]; then
  site=$2
  dir="$site/firmware/beta"
  variants="moonhub75 scoreboard75"
  want=""
else
  site=$1
  dir="$site/firmware"
  variants="moonhub75 moonhub75-2panel scoreboard75"
  want=${2#v}
fi
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
  if [ -z "$want" ]; then
    # Beta: the first variant sets the version the second must match.
    if ! [[ "$got" =~ -beta\.[0-9]+$ ]]; then
      echo "::error::$m is version $got, not a beta (<release>-beta.<build>)"
      bad=1
    fi
    beta=${beta:-$got}
    if [ "$got" != "$beta" ]; then
      echo "::error::$m is version $got, the other variant is $beta"
      bad=1
    fi
  elif [ "$got" != "$want" ]; then
    echo "::error::$m is version $got, the release is $want"
    bad=1
  fi
  for f in $(jq -r '.builds[] | (.ota.path // empty), (.parts[]?.path // empty)' "$m"); do
    if [ ! -f "$dir/$v/$f" ]; then
      echo "::error::$m points at $f, which is not in the site"
      bad=1
    fi
  done
  if [ $bad = 0 ]; then
    echo "$v: manifest $got, all files present"
  fi
  fail=$((fail | bad))
done

exit $fail
