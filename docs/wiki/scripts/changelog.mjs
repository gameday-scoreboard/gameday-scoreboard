// Builds src/content/docs/changelog.md from the repo's CHANGELOG.md.
// The page is a build artifact (gitignored); edit CHANGELOG.md instead.
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const source = resolve(here, '../../../CHANGELOG.md');
const target = resolve(here, '../src/content/docs/changelog.md');

let text;
try {
	text = readFileSync(source, 'utf8');
} catch (err) {
	console.error(`changelog: cannot read ${source}: ${err.message}`);
	process.exit(1);
}

const lines = text.replace(/\r\n/g, '\n').split('\n');
const first = lines.findIndex((line) => /^## v/.test(line));
if (first === -1) {
	console.error(`changelog: no "## v" release heading found in ${source}`);
	process.exit(1);
}

const releases = lines.slice(first).join('\n').trimEnd();
const page = `---
title: "What's new"
description: "Every Game Day Scoreboard firmware release, newest first."
tableOfContents:
  maxHeadingLevel: 2
---

Release notes for every firmware version. Your panel shows its version on its device page.

${releases}
`;

mkdirSync(dirname(target), { recursive: true });
writeFileSync(target, page);
console.log(`changelog: wrote ${target} (${lines.slice(first).filter((l) => /^## v/.test(l)).length} releases)`);
