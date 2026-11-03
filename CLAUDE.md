# Game Day Scoreboard firmware

ESPHome firmware for a standalone sports scoreboard (NFL, college football,
MLB, NBA, WNBA, men's college basketball, NHL, MLS and the Premier League) on
an ESP32-S3 HUB75 controller. One top-level YAML per pinout: gameday.yaml
(MoonHub75, also the pinout of our own controller board) and
gameday-scoreboard75.yaml (four early test boards, not a product); each picks
its pin map from firmware/controllers/ and shares everything else via
gameday-common.yaml.
Sibling repo: the iOS companion app at ~/development/gameday-scoreboard-ios (github.com/gameday-scoreboard/gameday-scoreboard-ios).
AGENTS.md has the layout, build and test commands shared with outside
contributors; read it too.

## Hard rules

- Commit as `bharvey88 <8107750+bharvey88@users.noreply.github.com>`. No
  Claude credit or Co-Authored-By lines on commits or PRs. Multiline commit
  messages via `git commit -F <file>`.
- This repo is PUBLIC and open source. Never commit handoffs, session
  notes, specs, plans or reviews here: only product code, CHANGELOG, README
  and the site. Maintainer notes are kept privately and loaded through a
  gitignored CLAUDE.local.md.
- The docs site (docs/wiki, Astro + Starlight; the gameday-scoreboard/docs
  repo publishes `main` to docs.gameday-scoreboard.com on GitHub Pages
  every 15 minutes) is part of every change. A PR
  that changes what an owner or DIY builder sees or does updates or adds
  the page for it under docs/wiki/src/content/docs/ in the same PR; CI
  ("Docs updated") fails a PR into `dev` that touches firmware/ or
  components/ without a docs change unless the PR description has a line
  `No owner-visible change, no docs: <why>`, which the reviewer checks.
  The label `docs: not needed` still passes, as a fallback for older PRs.
  Behavior that `main` doesn't have yet goes in a
  `:::note[Coming in the next release]` aside, removed at release time.
  Docs describe the firmware on the branch they sit on, written for the
  owner, no internal names, cards or notes. `cd docs/wiki && npm run
  build` must pass.
- PR descriptions say what changed and how it was tested, nothing else: no
  links to private repos or trackers, no card numbers or `Closes` lines
  for them, no private notes. No AI attribution or Claude-headed
  comments in PR text, comments or reviews. Reviews and progress notes go
  on the private card.
- Every parallel agent works in its own git worktree.
- Branch from `dev` and open PRs against `dev` (the default branch), never
  `beta` or `main`. Changes move `dev` to `beta` to `main` by promotion PRs
  only: `beta` only takes a PR from `dev`, and `main`, which is what
  customers run and what the live site deploys from, only takes a PR from
  `beta`. Release tags go on `main`. CI fails a PR into `beta` or `main`
  from any other branch and refuses to publish a tag that isn't on `main`.
- ESPHome versions by channel: `dev` and `beta` may pin an ESPHome beta;
  `main` and release tags build only on a final release. The pin is
  `ESPHOME_VERSION` at the top of build.yml, and CI (Branch rules) fails a
  PR into `main` or a `v*` tag when it isn't a final like `2026.10.0`. A fix
  that is only in a beta: copy the component into `components/` and stay
  on the final, wait for the final or ask for a patch-release backport, or
  the repo owner's `esphome-beta-ok` label on the promotion PR (then re-run
  the job). Diagram in CONTRIBUTING.md.
- Product is not part of Apollo Automation. Brand is "Game Day Scoreboard".
- Decisions not to reopen without the repo owner: OTA with no password, `api:` with
  no key, the device web page open ("panel trusts the home network"). Do not
  raise LAN hardening again. Prioritize customer-visible failures.
- A PR an owner would notice adds its bullet under `## Unreleased` in
  CHANGELOG.md, under `### ⚠️ Heads up`, `### ✨ New` or `### 🐛 Fixes`:
  one short line, starting with an emoji that fits it, the detail on the
  docs page (CHANGELOG.md's own top note has the format). Released
  sections stay as they shipped. A release renames
  Unreleased to the version (after checking `git log` since the last tag
  for merged changes with no bullet) and bumps `version` in
  firmware/gameday-common.yaml; a tag without a matching section fails CI.
- Name branches after the change (`espn-gzip`); CI's Branch rules job
  refuses a PR from a `card-<number>` branch.
- ESPHome venv at ~/development/tools/esphome-venv flashes over USB and
  streams logs; the repo owner's panel is gameday-2f6a70.local. Use `esphome
  compile`, not `esphome config`, to catch C++ errors. Host tests: `make -C
  tests`. Rebuild the web bundle with the venv's python (bare python3 on
  this Mac is 3.9.6, too old) after editing firmware/web/app.js.