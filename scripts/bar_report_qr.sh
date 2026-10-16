#!/usr/bin/env bash
# The "Something wrong?" QR for a bar panel's setup card: scanning it opens
# an email (or, with --sms, a text) to us with the panel's name filled in.
# It is the same message as the button on the bar build's device page
# (firmware/web/app.js), for staff who never open that page.
#
# Usage: scripts/bar_report_qr.sh <panel name> [--sms <number>] [out dir]
#   scripts/bar_report_qr.sh gameday-aba970            -> gameday-aba970-report.svg/.png
#   scripts/bar_report_qr.sh gameday-aba970 --sms +15555550100
# The panel name is on the device page (Device, Name) and in the router's
# client list. Needs qrencode (apt install qrencode).
set -euo pipefail

name=${1:?panel name, like gameday-aba970}
shift
sms=""
if [ "${1:-}" = --sms ]; then
  sms=${2:?phone number after --sms}
  shift 2
fi
out=${1:-.}
email=support@gameday-scoreboard.com

enc() { jq -rn --arg s "$1" '$s|@uri'; }
if [ -n "$sms" ]; then
  url="sms:$sms?body=$(enc "Bar panel $name: ")"
else
  url="mailto:$email?subject=$(enc "Bar panel $name")&body=$(enc "Panel: $name
What's wrong:
")"
fi
mkdir -p "$out"
qrencode -t SVG -m 2 -o "$out/$name-report.svg" "$url"
qrencode -t PNG -m 2 -s 12 -o "$out/$name-report.png" "$url"
echo "$url"
echo "wrote $out/$name-report.svg and .png"
