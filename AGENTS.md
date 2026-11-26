# Notes for coding agents

ESPHome firmware for a standalone sports scoreboard on an ESP32-S3
driving HUB75 LED panels: the NFL, college football, MLB, the NBA, the
WNBA, men's and women's college basketball, women's college volleyball,
the NHL, MLS and the Premier League. It follows games from ESPN's public API by itself and serves its own
settings page. CONTRIBUTING.md has the same ground for people; this file
is the short version.

## Layout

- `firmware/gameday-waveshare.yaml` (the Waveshare ESP32-S3-RGB-Matrix,
  the board ready-made scoreboards ship on), `firmware/gameday.yaml`
  (MoonHub75 pinout, for DIY boards) and `firmware/gameday-scoreboard75.yaml`
  (the early scoreboard75 test boards) are the three release builds. Each
  only names itself and picks a pin map from `firmware/controllers/`;
  everything else is in `firmware/gameday-common.yaml`.
  `firmware/gameday-2panel.yaml` is gameday.yaml booting two panels wide.
  The installer offers the Waveshare and both MoonHub75 builds; the
  Waveshare build hasn't run on hardware yet.
  `firmware/gameday-demo.yaml` and `firmware/gameday-scoreboard75-demo.yaml`
  add the scripted demo (`firmware/pages/demo.yaml`) for test builds; they
  are never released.
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

- ESPHome 2026.11.0, the version CI pins (`ESPHOME_VERSION` in
  `.github/workflows/build.yml`). Use `esphome compile
  firmware/gameday.yaml` (and the scoreboard75 file) to check a change;
  `esphome config` misses C++ errors.
- Host tests: `make -C tests` (needs g++). CI also builds
  `make -C tests replay` and runs the `--selftest` of
  `scripts/factory_flash.py`, `burnin_monitor.py`, `improv_scan.py`,
  `crash_report.py` and `tests/web/mock_device.py`. The last checks the device page's mock
  against `tests/fixtures/set_cases.json`, the same table the host tests
  run through `components/gameday/set_request.h`.
- A struct saved with `make_preference<T>` keeps its size: on ESP32 a blob
  of another length fails to load, so every setting in it resets at the
  update. Use its spare bytes, or save the new fields under a new
  preference key. Each has a `static_assert(sizeof(T) == <number>)` beside
  it; `python3 scripts/saved_size_lint.py .` (CI runs it) fails for one
  without.
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
  image), static internal RAM and ELF sections, and the memory each ESPN
  read takes on the host (`make -C tests heap`, `tests/heap_budget.cpp`),
  against `firmware/budget.json` (its `about` text has the rules). Flash
  fails over 90% of the build's OTA slot, and needs the repo owner's
  approval past the per-release allowance or when one PR adds more than
  32 KB. Static internal RAM and heap have tight caps: going over fails,
  and raising a cap in budget.json (with the reason in `about`) needs the
  owner's approval. JSON documents fail only when one grows more than
  `max_growth.json` against dev's last build. The job summary has the
  tables, room left in the OTA slot and the section diff; the
  `budget-report` artifact has the same figures as JSON. To see what
  moved the bytes, read "Where the bytes went" at the end of that summary:
  on a pull request into dev it lists, for moonhub75, each component whose
  flash or RAM changed against the same dev run, and the 20 symbols that
  changed most. It is ESPHome's `analyze-memory` analysis, run by
  `scripts/memory_report.py` on the finished build (the `memory-moonhub75`
  artifact has every symbol), and it never fails the job. Locally:
  after a compile, run `python scripts/memory_report.py extract
  firmware/.esphome/build/gameday-moonhub75 a.json` with the ESPHome
  venv's python, the same on the other build, then
  `python3 scripts/memory_report.py diff a.json b.json`.
  `python3 scripts/size_budget.py seed <sizes folder>` prints caps with
  10% headroom (at least 1 KB); `release-baseline <sizes folder> <tag>`
  resets the release block when a release is cut; CI checks that the new
  tag is a later release tag and that the baseline matches what its Build
  run measured.
- Device page tests: in `tests/web`, `npm ci` and
  `npx playwright install chromium` once, then `npm test`.
- After editing `firmware/web/app.js` or `app.css`, run
  `python scripts/build_web.py` and commit `bundle.js` and `bundle-demo.js`.
  CI fails if either is stale.
- `components/gameday/teams.h`, `team_colors.h` and `timezones_parsed.h`
  are generated by `build_teams.py`, `build_team_colors.py` and
  `build_timezones.py`. Rerun the script rather than editing them by hand.

## Memory in C++

A panel runs for months between restarts, and its internal RAM is shared
with Wi-Fi, TLS, LVGL and the ESPHome API. Each allocation and free of a
different size fragments that heap: the free total can look fine while
the largest free block shrinks until a TLS or Wi-Fi buffer no longer
fits, and the panel stops reading scores or restarts. The device page
shows both figures, "Free Heap (internal)" and "Largest Free Block
(internal)". These rules follow ESPHome's own (the `embedded-memory`
skill under `.agents/skills` in esphome/esphome), adapted to this
firmware. They apply to code a PR adds or changes. Moving existing code
over is its own PR, never a side effect of another change.

- **Where memory comes from.** Plain `new`, `malloc`, `std::string` and
  `std::vector` take internal RAM in this build. PSRAM is for anything
  big or long lived: `espn::psram_alloc`, `PsramAllocator`, `PsVector`
  (`components/gameday/psram.h`), `PsString` (gameday.h), ESPHome's
  `RAMAllocator` (PSRAM first by default) and `json::SpiRamAllocator`
  for JSON documents (`json_allocator()` in `espn_parse_impl.h`). A
  `std::string` member of a struct kept in PSRAM still puts any text
  longer than 15 characters in internal RAM.
- **ESPN reads** allocate on every read by design: the JSON document and
  the lists of games are in PSRAM, and `make -C tests heap` caps the
  internal RAM one read takes (the Size budget's host heap figures). A
  new parser field that is a `std::string` counts against that cap.
- **No heap allocation after setup in code that runs over and over:**
  `loop()`, LVGL lambdas and draw code, intervals and timers, web
  request handlers. Format text into a `char` buffer with `snprintf`,
  not with `std::string` concatenation or `std::to_string`, and keep a
  buffer that is reused as a member sized once.
- **Sizes known at build time** use `std::array`, or ESPHome's
  `StaticVector<T, N>` for a `push_back` interface. A size known once at
  startup uses `FixedVector<T>` with one `init(n)`. A byte buffer is a
  single allocation, not a `std::vector<uint8_t>` unless it has to grow:
  a `psram_alloc` pointer, or a `std::unique_ptr<uint8_t[]>` for a small
  one (plain `new[]` is internal RAM, so the 1 KB rule below applies).
- **No growing `std::vector` in internal RAM.** A list that grows at run
  time (teams, games, logos, messages) is a `PsVector` with a cap, and
  calls `reserve` once when the cap is known.
- **No `std::map`, `std::set`, `std::unordered_map` or `std::deque`.** A
  small table is an array of structs searched in a loop. A deque takes
  512 bytes for its first element.
- **Text that never changes** (labels, keys, a URL from the YAML) stays a
  `const char *` or ESPHome's `StringRef`, not a `std::string` copy.
- **Buffers over about 1 KB go to PSRAM.** Internal RAM only where the
  hardware or a driver needs it (DMA, an interrupt handler), with a
  comment saying which.
- **Task stacks** come out of internal RAM. Size a new task's stack from
  a measured high-water mark (`uxTaskGetStackHighWaterMark`) and put the
  measurement in a comment, not a round guess.
- **Never `new (std::nothrow)`.** Exceptions are off, so a failed
  allocation aborts either way. Use `psram_alloc`, or `RAMAllocator` and
  check for `nullptr` where the code can carry on without the memory.
- **Host-tested files** build without ESPHome: everything
  `tests/Makefile` compiles (`espn_parse.cpp`, `sport_*.cpp`,
  `logo_cache.cpp`) and the headers they and the tests include.
  `logo_cache.cpp` builds against the small stand-ins in
  `tests/logo_stubs/`, which have none of ESPHome's containers. These
  files use `std::array` and `psram.h`, which is the plain heap on the
  host. `StaticVector`, `FixedVector`, `StringRef` and `RAMAllocator` are
  for files only the firmware build compiles, such as `gameday.cpp`.
- **To check a PR,** read "Where the bytes went" in its Size budget
  summary: a new `_M_realloc_insert`, `_M_default_append`, `_Rb_tree`,
  `_Hashtable` or `deque` symbol there points at one of the patterns
  above.

## Tests and evidence

- Expected values in a test come from somewhere other than the code under
  test: a recorded ESPN document, the calendar, a value worked out by
  hand. A test that calls the function to get its own answer proves
  nothing.
- Cover the failures too: no answer, a cut-off or late answer, a missing
  field, an empty list, the largest and smallest values allowed.
- A fix to what a panel does is shown both ways: the same steps on a `dev`
  build, where the bug happens, and on the fix, where it doesn't. If the
  bug is rare, force it (drop Wi-Fi, a fake server that answers slowly, a
  build that injects the fault). If it can't be forced, the PR says why
  and what was checked instead.
- Show that the test ran: the requests went through, the panel was on the
  page under test, the log covers the whole run. A pass with nothing
  driving it isn't a pass.
- An accepted request isn't proof the panel did it. Read the state back or
  look at the screen. If a write may not have landed, check before sending
  it again.
- A PR says what wasn't tested: a board, a layout, a league, hardware.

## Shared data and workarounds

- Data that more than one task can reach (the main loop, the ESPN fetch
  task, the logo, webhook and web server tasks) has a comment where it is
  declared: which task writes it, which may read it, and what guards it.
- A workaround names the version it is for (ESPHome, ESP-IDF, a board
  revision), the failure it avoids, and where that was seen (an issue, a
  log, a measurement).

## Rules

- Branch from `dev` and open PRs against `dev`. `beta` and `main` only take
  promotion PRs, and CI enforces that.
- Name the branch after the change (`espn-gzip`, `power-cut-boot`). The
  name stays in the merge commit, so CI's Branch rules job refuses a head
  branch with a tracker's ticket in it, like `card-123-...`.
- A PR description says what changed, how it was tested, and who the change
  builds on: credit the data sources and projects it uses, with links. No links to
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
- A PR that changes what an owner sees or does adds one bullet under
  `## Unreleased` at the top of CHANGELOG.md, in its group (`### ⚠️ Heads
  up`, `### ✨ New` or `### 🐛 Fixes`): one short line for the owner,
  starting with an emoji. The detail goes on the docs page. The section
  starts with a plain summary line under 240 characters (the first PR
  after a release writes it; a later one rewrites it when its change is
  the bigger news), and a change to a feature that already has a line
  updates that line instead of adding another; a fix to something that
  hasn't shipped changes no line, or that feature's line. CI's "Changelog ready to
  release" job checks this on PRs into `dev`: a PR touching `firmware/`
  or `components/` changes Unreleased unless its description has `No
  owner-visible change, no docs: <why>` or `No changelog line: <why>`.
  Never edit a released section or the version number; the maintainers
  rename `## Unreleased` and set the version when a release goes out.
- A change an owner or DIY builder can see or do comes with its docs
  page, updated or added under `docs/wiki/src/content/docs/` in the same
  PR. CI's "Docs updated" job fails a PR into `dev` that touches
  `firmware/` or `components/` without one, unless the PR description
  has a line `No owner-visible change, no docs: <why>` (the reviewer checks
  that claim). Describe the behavior as it is on `dev`, in the present
  tense, for the owner, with no internal names: the docs site publishes from `main`, so the page goes live with
  the release. No "Coming in the next release" notes (CI refuses them);
  a phone app feature that waits for an app update uses
  `:::note[Coming in the next app update]`. `npm run build` in
  `docs/wiki` must pass.
