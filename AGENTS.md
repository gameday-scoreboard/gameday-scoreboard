# Notes for coding agents

ESPHome firmware for a standalone sports scoreboard on an ESP32-S3
driving HUB75 LED panels: the NFL, college football, MLB, the NBA, the
WNBA, men's college basketball, the NHL, MLS and the Premier League. It
follows games from ESPN's public API by itself and serves its own
settings page. CONTRIBUTING.md has the same ground for people; this file
is the short version.

## Layout

- `firmware/gameday.yaml` (MoonHub75 pinout) and
  `firmware/gameday-scoreboard75.yaml` (our controller board) are the two
  release builds. Each only names itself and picks a pin map from
  `firmware/controllers/`; everything else is in
  `firmware/gameday-common.yaml`. `firmware/gameday-2panel.yaml` is
  gameday.yaml booting two panels wide, offered by the installer.
  `firmware/gameday-demo.yaml` and `firmware/gameday-scoreboard75-demo.yaml`
  add the scripted demo (`firmware/pages/demo.yaml`) for test builds; they
  are never released.
- `firmware/gameday-bar.yaml` and `firmware/gameday-scoreboard75-bar.yaml`
  are the bar builds, for the free panels in bars: the same firmware plus
  `firmware/pages/bar.yaml`, which reads the beta channel
  (`install.gameday-scoreboard.com/firmware/beta/<variant>/`) and installs from it
  on its own at night. CI publishes them only from `beta`. Release
  firmware never installs anything unless the owner turns on "Install
  updates automatically".
- `components/gameday`: ESPN fetching, parsing and game logic (C++).
- `components/panel_layout`: the one-panel or two-panel layout, applied at
  boot from a saved preference.
- `components/saved_networks`: the up-to-five Wi-Fi networks the panel
  remembers (ESPHome keeps one), handed to the Wi-Fi component at boot.
- `firmware/pages`: the LVGL pages drawn on the panel.
- `firmware/web`: the device page. `bundle.js` is generated from `app.js`
  and `app.css`.
- `docs/site`: the installer and support site. Every page needs its own
  title, description, canonical link and share tags;
  `python3 scripts/site_seo.py check docs/site` lists what's missing.
  `docs/site/diy` is the page for DIY owners (boards, panels, wiring);
  keep it in step with `firmware/controllers/` and the README's Hardware
  section.
- `docs/wiki`: the documentation site (Astro + Starlight). The
  gameday-scoreboard/docs repo publishes this repo's `main` to
  docs.gameday-scoreboard.com on GitHub Pages. Pages are markdown under
  `docs/wiki/src/content/docs/`; `docs/wiki/README.md` has the
  conventions.
- `scripts`: generators (`build_web.py`, `build_teams.py`,
  `build_team_colors.py`, `build_timezones.py`), release notes and
  factory tools.
- `tests`: host tests for the parser and game logic, and Playwright tests
  for the device page in `tests/web`.

## Build and test

- ESPHome 2026.10.0b3, the version CI pins (`ESPHOME_VERSION` in
  `.github/workflows/build.yml`). Use `esphome compile
  firmware/gameday.yaml` (and the scoreboard75 file) to check a change;
  `esphome config` misses C++ errors.
- Host tests: `make -C tests` (needs g++). CI also builds
  `make -C tests replay` and runs the `--selftest` of
  `scripts/factory_flash.py`, `burnin_monitor.py`, `improv_scan.py`,
  `crash_report.py` and `tests/web/mock_device.py`. The last checks the device page's mock
  against `tests/fixtures/set_cases.json`, the same table the host tests
  run through `components/gameday/set_request.h`.
- A panel that restarted from a crash, a watchdog or a brownout keeps the
  report in flash (`components/crash_report`, `GET /gameday/crash`).
  `python3 scripts/crash_report.py <panel>` prints it and decodes the
  backtrace with the release's ELF.
- Fuzzing: `make -C tests fuzz` runs mutated ESPN documents and set
  requests through the parsers under the address and undefined behavior
  sanitizers for about a minute, as CI does on every PR;
  `make -C tests fuzz-long` runs 30 minutes a target. clang with libFuzzer
  makes it coverage guided; other compilers get a random mutator. A crash
  leaves its input in `tests/build/crash-*`; pass that file to
  `tests/build/fuzz_espn` (or `fuzz_set`) to replay it, and add a host test
  with the fix.
- Size budget: CI's "Size budget" job checks each build's flash (the OTA
  image) and static internal RAM (from the ELF), and the memory each ESPN
  read takes on the host (`make -C tests heap`, `tests/heap_budget.cpp`),
  against `firmware/budget.json`. A PR fails when a figure goes over its
  budget, or (into `dev`) grows more than the file's `max_growth` against
  dev's last build; the job summary has the table. A change that needs
  more raises that one entry in the same PR and says why.
  `python3 scripts/size_budget.py seed <sizes folder>` prints figures with
  10% headroom (at least 1 KB).
- Device page tests: in `tests/web`, `npm ci` and
  `npx playwright install chromium` once, then `npm test`.
- After editing `firmware/web/app.js` or `app.css`, run
  `python scripts/build_web.py` and commit `bundle.js` and `bundle-demo.js`.
  CI fails if either is stale.
- `components/gameday/teams.h`, `team_colors.h` and `timezones_parsed.h`
  are generated by `build_teams.py`, `build_team_colors.py` and
  `build_timezones.py`. Rerun the script rather than editing them by hand.

## Rules

- Branch from `dev` and open PRs against `dev`. `beta` and `main` only take
  promotion PRs, and CI enforces that.
- A PR description says what changed and how it was tested. No links to
  private repos or trackers, no ticket numbers from them, and no AI
  attribution in PR text or comments. Maintainers keep reviews and
  progress notes privately.
- Commit messages are one plain sentence about the behavior that changed
  ("Name each panel's hotspot after its suffix"), not "fix bug" or a
  conventional-commit prefix.
- Never commit `secrets.yaml`, Wi-Fi details, session notes, plans or
  specs. This repo is public; CI rejects common notes files.
- The panel trusts the home network on purpose: OTA has no password, the
  ESPHome API has no key and the device page is open. Don't add
  passwords or keys to the default build. The README section "On your
  network" explains why.
- Team logos come from ESPN at runtime. Don't commit league or team logos.
- Version bumps and CHANGELOG sections are written by maintainers at
  release time. Leave both alone in a feature PR.
- A change an owner or DIY builder can see or do comes with its docs
  page, updated or added under `docs/wiki/src/content/docs/` in the same
  PR. CI's "Docs updated" job fails a PR into `dev` that touches
  `firmware/` or `components/` without one, unless the PR carries the
  label `docs: not needed`. Describe behavior `main` doesn't have yet in a
  `:::note[Coming in the next release]` aside. `npm run build` in
  `docs/wiki` must pass.
