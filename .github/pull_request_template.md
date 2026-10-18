<!-- PRs go into dev. beta and main only take promotion PRs. -->

## What and why

<!-- What changes on the panel, its page or the build, and why. "Fixes #N" if it closes an issue. -->

## How it was tested

<!-- Keep the lines that apply, delete the rest. -->

- [ ] `make -C tests` passes
- [ ] Device page tests pass (`npm test` in tests/web)
- [ ] `esphome compile` for both firmware/gameday.yaml and firmware/gameday-scoreboard75.yaml
- [ ] Ran on a panel: which controller and layout, how long, what it showed

## Checklist

- [ ] Edited firmware/web/app.js or app.css? Ran `python scripts/build_web.py` and committed bundle.js
- [ ] Changed what an owner or DIY builder sees or does? Updated the page under docs/wiki/src/content/docs/ (or this PR needs the `docs: not needed` label)
- [ ] No secrets.yaml, Wi-Fi details, notes or plans in the diff
