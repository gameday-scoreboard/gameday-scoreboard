#!/usr/bin/env bash
# Give every manifest.json under a folder the product name. The web installer
# shows it ("Installing <name>") and Home Assistant titles the panel's update
# with it. build-action fills it from the ESPHome project name,
# bharvey88.gameday-scoreboard, which has to stay as it is: panels advertise
# it over mDNS, the iOS app finds panels by it and factory_flash.py checks it
# in the boot log. Only the name changes; the checksums cover the bins, not
# the manifest, so check_site_firmware.sh still passes.
# Usage: scripts/set_manifest_name.sh <dir>
set -euo pipefail

name="Game Day Scoreboard"
found=0
while IFS= read -r -d '' m; do
  jq --arg n "$name" '.name = $n' "$m" > "$m.tmp"
  mv "$m.tmp" "$m"
  echo "$m: name $name"
  found=1
done < <(find "$1" -name manifest.json -print0)
if [ $found = 0 ]; then
  echo "::error::no manifest.json under $1"
  exit 1
fi
