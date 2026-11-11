#!/bin/bash
# Runs a command up to four times, waiting 15, 30 and then 60 s between tries.
# For CI downloads that a short network blip on the runner can fail (a DNS
# lookup of a package mirror, say), so one blip doesn't fail the whole run.
#   scripts/retry.sh npx playwright install --only-shell chromium
set -uo pipefail

for wait in 15 30 60; do
  "$@" && exit 0
  echo "::warning::'$*' failed; trying again in ${wait} s"
  sleep "$wait"
done
"$@"
