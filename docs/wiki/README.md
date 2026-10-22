# Game Day Scoreboard docs

This is the documentation site for Game Day Scoreboard, built with [Astro](https://astro.build) and [Starlight](https://starlight.astro.build). It covers setting up a panel, using it, fixing problems, and building one on your own board. It is published at [docs.gameday-scoreboard.com](https://docs.gameday-scoreboard.com).

## Run it locally

You need Node 24 or newer.

```
cd docs/wiki
npm install
npm run dev
```

`npm run dev` serves the site at http://localhost:4321 and reloads as you edit. `npm run build` makes the static site in `dist/` along with a search index, and `npm run preview` serves that build.

## Where pages live

Pages are Markdown or MDX files under `src/content/docs/`, one folder per sidebar group:

- `get-started/` for unboxing, install and first setup
- `using/` for everyday use and settings
- `help/` for troubleshooting
- `diy/` for running the firmware on your own board

Each folder has an `index.md` for its landing page. The sidebar builds itself from the folders, so a new file shows up without touching the config.

Every page starts with frontmatter:

```
---
title: Pick your team
description: One sentence that says what the page covers.
sidebar:
  order: 3
---
```

`title` is required and `description` should always be there. `sidebar.order` sets the position within its group, lowest first. For callouts use Starlight's asides, `:::note`, `:::tip` and `:::caution`, each closed with `:::`.

Images go in `src/assets/` and are pulled in with a relative Markdown image link. Files in `public/` are served as they are.

## The changelog page

`src/content/docs/changelog.md` is generated from the `CHANGELOG.md` at the repo root by `scripts/changelog.mjs`, which runs before every build. Do not edit the generated file and do not commit it (it is gitignored). Change `CHANGELOG.md` instead.

## Publishing

The site is published to docs.gameday-scoreboard.com by GitHub Pages in the [gameday-scoreboard/docs](https://github.com/gameday-scoreboard/docs) repo. Its Publish workflow builds this repo's `main` every 15 minutes and deploys it when `main` has moved, so what is live is always what customers run. Nothing is edited over there. Here, a pull request that touches `docs/wiki/` or `CHANGELOG.md` builds the site through the Docs workflow (`.github/workflows/docs.yml`) to catch broken pages.

A pull request into `dev` that changes `firmware/` or `components/` must also change a page under `src/content/docs/`, or carry the label `docs: not needed`. The Docs updated workflow (`.github/workflows/docs-check.yml`) enforces that.
