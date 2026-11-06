#!/usr/bin/env bash
# Give every manifest.json under a folder the product name. The web installer
# shows it ("Installing <name>") and Home Assistant titles the panel's update
# with it. build-action fills it from the ESPHome project name,
# bharvey88.gameday-scoreboard, which has to stay as it is: panels advertise
# it over mDNS, the iOS app finds panels by it and factory_flash.py checks it
# in the boot log. Only the name changes; the checksums cover the bins, not
# the manifest, so check_site_firmware.sh still passes.
# The bar builds (a <variant>-bar folder, and the bar betas under beta/) get
# "Game Day Scoreboard (bar)", so the panel's update and the device page say
# which build it takes.
# It also sets new_install_improv_wait_time. After flashing, the installer
# offers the WiFi form only if the controller answers Improv within that
# many seconds (10 by default), and a first boot (PSRAM, the matrix, a
# WiFi scan) can take longer. A controller that answers sooner doesn't wait.
# Usage: scripts/set_manifest_name.sh <dir>
set -euo pipefail

improv_wait=30
found=0
while IFS= read -r -d '' m; do
  case "$m" in
    *-bar/manifest.json | */beta/*/manifest.json) name="Game Day Scoreboard (bar)" ;;
    *) name="Game Day Scoreboard" ;;
  esac
  jq --arg n "$name" --argjson w "$improv_wait" '.name = $n | .new_install_improv_wait_time = $w' "$m" > "$m.tmp"
  mv "$m.tmp" "$m"
  echo "$m: name $name"
  found=1
done < <(find "$1" -name manifest.json -print0)
if [ $found = 0 ]; then
  echo "::error::no manifest.json under $1"
  exit 1
fi
