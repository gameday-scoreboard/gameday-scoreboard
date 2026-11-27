# Game Day Scoreboard firmware

@AGENTS.md

AGENTS.md, imported above, has the layout, build and test commands, the C++
memory and testing rules, and the PR, docs and CHANGELOG rules shared with
outside contributors. This file adds what only maintainers need.
Sibling repo: the iOS companion app at ~/development/gameday-scoreboard-ios (github.com/gameday-scoreboard/gameday-scoreboard-ios).

## Hard rules

- Commit as `bharvey88 <8107750+bharvey88@users.noreply.github.com>`. No
  Claude credit or Co-Authored-By lines on commits or PRs. Multiline commit
  messages via `git commit -F <file>`.
- This repo is PUBLIC and open source. Never commit handoffs, session
  notes, specs, plans or reviews here: only product code, CHANGELOG, README
  and the site. Maintainer notes are kept privately and loaded through a
  gitignored CLAUDE.local.md.
- Every parallel agent works in its own git worktree.
- Branch from `dev` and open PRs against `dev` (the default branch), never
  `beta` or `main`. Changes move `dev` to `beta` to `main` by promotion PRs
  only: `beta` only takes a PR from `dev`, and `main`, which is what
  customers run and what the live site deploys from, only takes a PR from
  `beta`. Release tags go on `main`. CI fails a PR into `beta` or `main`
  from any other branch and refuses to publish a tag that isn't on `main`.
- Releases: `dev` is always ready to release, so promoting is one
  command: `python3 scripts/promote.py` (`--dry-run` first). It checks
  dev, works out the version the way a beta does (the newest tag plus
  one, or `YYYY.M.0` in a new month), opens and merges a release prep PR
  into `dev` that renames `## Unreleased` to that version and sets
  `version` in firmware/gameday-common.yaml, then opens the `dev` to
  `beta` PR. Nothing is written at promotion time: every PR into `dev`
  leaves CHANGELOG.md and the docs in their released shape (see the
  CHANGELOG rule in AGENTS.md). So shipping is one `beta` to `main` PR: its
  merge runs tag.yml, which tags `main`'s version and starts the release.
  Promotions to `beta` happen when the repo owner says so; `beta` to
  `main` only on the repo owner's "ship it". PRs into `dev` may
  auto-merge on green once benched; promotions never do. dev, beta and
  main require the build, test and size checks.
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
- ESPHome venv at ~/development/tools/esphome-venv flashes over USB and
  streams logs. Rebuild the web bundle with the venv's python (bare python3
  on the Mac is 3.9.6, too old) after editing firmware/web/app.js.
- The docs site: the gameday-scoreboard/docs repo publishes `main` every 15
  minutes. The label `docs: not needed` still passes "Docs updated", as a
  fallback for older PRs.
