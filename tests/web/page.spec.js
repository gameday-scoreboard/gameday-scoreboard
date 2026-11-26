// Device page (firmware/web/bundle.js) against mock_device.py. Each test
// resets the mock, optionally with state overrides, and checks what the page
// shows and what it posts back to /gameday/set.
const fs = require("fs");
const base = require("@playwright/test");
const { expect } = base;

// Answer every off-device request locally so the suite needs no network.
// ESPN's scoreboard feeds the picker's "On now" tab and has no games unless a test routes one.
// Uncaught page errors fail the test.
const test = base.test.extend({
  page: async ({ page, baseURL }, use) => {
    const errors = [];
    page.on("pageerror", (e) => errors.push(e.message));
    await page.route(
      (url) => url.origin !== new URL(baseURL).origin,
      (route) => route.fulfill({ status: 404, body: "" }),
    );
    await page.route("https://site.api.espn.com/**", (route) => route.fulfill({ json: { events: [] } }));
    await use(page);
    expect(errors, "uncaught errors on the page").toEqual([]);
  },
});

// wifi: the saved network list the mock answers with; undefined keeps the
// mock's default, null is firmware without the list.
const reset = (request, state, fail, wifi) =>
  request.post("/_reset", { data: wifi === undefined ? { state, fail } : { state, fail, wifi } });
// The panel's next board update: Game Status on every open event stream.
const push = (request, name = "Game Status") => request.post("/_push?name=" + encodeURIComponent(name));
// Playlists post to the settings import (app.js setRequest); a post of only
// them reads back as the lists= text it stands for, so tests check it like a set.
const playlistsPost = (p) => {
  if (p.path !== "/gameday/settings") return null;
  let d;
  try { d = JSON.parse(p.body); } catch (_) { return null; }
  if (!d || !Array.isArray(d.lists) || Object.keys(d).some((k) => !["schema", "lists", "list"].includes(k))) return null;
  // A playlist's options come back as its @ tokens, in the firmware's order.
  const opts = (l) => (l.days ? ",@d:" + l.days + (l.start !== undefined ? ",@t:" + l.start + "-" + l.end : "") : "")
    + (l.only ? ",@only" : "") + ("today" in l ? (l.today ? ",@today:1" : ",@today:0") : "")
    + (l.color ? ",@c:" + l.color : "") + (l.icon ? ",@i:" + l.icon : "")
    + (l.lg || []).map((lg) => ",@lg:" + lg).join("");
  const q = { lists: d.lists.map((l) => l.n + "=" + l.t.join(",") + opts(l) + (l.lg === undefined ? ",@lg" : "")).join(";") };
  if (d.list !== undefined) q.list = String(d.list);
  return q;
};
const sets = async (request) =>
  (await (await request.get("/_log")).json())
    .map((p) => (p.path === "/gameday/set" ? p.q : playlistsPost(p)))
    .filter(Boolean);
const expectSet = (request, q) => expect.poll(() => sets(request)).toContainEqual(q);
const deviceState = async (request) => (await request.get("/gameday/state")).json();
// The entity control posts (/select/Effect/set?option=..., /number/Effect%20Speed/set?value=...)
const entityPosts = async (request) =>
  (await (await request.get("/_log")).json()).filter((p) => !p.path.startsWith("/gameday/")).map((p) => ({ path: p.path, q: p.q }));
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// A busy panel answers late. Each /gameday/state read is taken when it
// arrives and handed to the page `ms` later, so a read that left before a tap
// comes back with the settings from before it.
const slowReads = (page, ms) =>
  page.route("**/gameday/state*", async (route) => {
    const res = await route.fetch();
    await sleep(ms);
    await route.fulfill({ response: res });
  });
// Holds the first /gameday/set for `ms` before it reaches the mock, so a later
// post sent alongside it would land first.
const slowFirstPost = (page, ms) => {
  let first = true;
  return page.route("**/gameday/set*", async (route) => {
    if (first) { first = false; await sleep(ms); }
    await route.continue();
  });
};
// Checks the texts every 100 ms for `ms`, so a late read that redraws the old
// value fails the test instead of slipping between two checks.
const staysAs = async (locator, texts, ms) => {
  for (const end = Date.now() + ms; Date.now() < end; await sleep(100))
    expect(await locator.allTextContents()).toEqual(texts);
};
// The page is five tabs (Home, Teams, Display, Customize, Device) and only the
// open one shows, so a test opens a control's tab before it taps or sees it.
// A reload stays on the tab, which the address keeps.
const tab = (page, name) => page.locator(`.tabbar [data-tab="${name}"]`).click();
// The Device tab's rarely used cards are folded shut; this opens one (by id)
// unless it is open already.
const unfold = async (page, id) => {
  const card = page.locator(id);
  if (!(await card.evaluate((d) => d.open))) await card.locator("summary").click();
};

const LIVE = {
  s: "IN", ts: 14, os: 10, p: 1, tt: 2, ot: 3, rz: true,
  c: "8:12 - 2nd", d: "3rd & 4 at PHI 18", lp: "Prescott pass to Lamb for 12 yards",
};

test("shows a live game and the settings from /gameday/state", async ({ page, request }) => {
  await reset(request);
  const g = (await (await request.get("/gameday/state")).json()).game;
  await reset(request, { game: { ...g, ...LIVE }, odds: false, status: "DAL 14 PHI 10 | 8:12 2nd" });
  await page.goto("/");
  await expect(page.locator("#gstate")).toHaveText("LIVE");
  await expect(page.locator("#tA .abbr")).toHaveText("DAL");
  await expect(page.locator("#tA .abbr")).toHaveClass(/poss/);
  await expect(page.locator("#tB .abbr")).toHaveText("PHI");
  await expect(page.locator("#sA")).toHaveText("14");
  await expect(page.locator("#sB")).toHaveText("10");
  await expect(page.locator("#clock")).toHaveText("8:12 - 2nd");
  await expect(page.locator("#down")).toHaveText("3rd & 4 at PHI 18");
  await expect(page.locator("#down")).toHaveClass(/rz/);
  await expect(page.locator("#tA .pips i.on")).toHaveCount(2);
  await expect(page.locator("#play")).toHaveText("Last play: Prescott pass to Lamb for 12 yards");
  await expect(page.locator("#ticker")).toHaveText("DAL 14 PHI 10 | 8:12 2nd");

  await expect(page.locator("#modes button.on")).toHaveText(/^Favorite teams/);
  await expect(page.locator("#favbar")).toBeVisible();
  await tab(page, "display");
  await expect(page.locator('.sw[data-set="down"]')).toHaveClass(/\bon\b/);
  await expect(page.locator('.sw[data-set="odds"]')).not.toHaveClass(/\bon\b/);
  // One situation switch per sport, shown for the sports the panel follows:
  // this one follows only football.
  await expect(page.getByRole("switch", { name: "Down and distance", exact: true })).toHaveAttribute("data-set", "down");
  await expect(page.getByRole("switch", { name: "Count and at-bat" })).toBeHidden();
  await expect(page.getByRole("switch", { name: "Match stats" })).toBeHidden();
  await expect(page.locator("#favCtls .ctl.fav label")).toHaveText([
    /^1\. Dallas Cowboys/, /^2\. Houston Texans/, /^3\. Ole Miss Rebels/,
  ]);
  await expect(page.locator('.sw[data-set="today"]')).not.toHaveClass(/\bon\b/);
  await expect(page.locator("#panels .panelpick.on")).toHaveAttribute("data-cols", "2");
  await expect(page.locator("#tzline")).toContainText("US Central");
  await expect(page.locator("#hostname")).toHaveText("gameday-2f6a70.local");
  await expect(page.locator("#ver")).toHaveText("v1.5.4");
  await expect(page.locator("#credit")).toHaveText("© 2026 Brandon Harvey");
  await expect(page.locator(".foot a", { hasText: "GPLv3" })).toHaveAttribute("href", /NOTICE\.md$/);
  // A row per favorite, the one on the board marked.
  await expect(page.locator("#upnextList .un.fav .n")).toHaveText(["DAL vs PHI", "HOU vs WSH", "MISS vs LSU"]);
  await expect(page.locator("#upnextList .un.shown .n")).toHaveText("DAL vs PHI");
});

test("an upcoming game shows kickoff and network", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await expect(page.locator("#gstate")).toHaveText("UPCOMING");
  await expect(page.locator("#clock")).toHaveText("Sun 3:25 PM");
  await expect(page.locator("#down")).toHaveText("on FOX");
  await expect(page.locator("#msg")).toBeHidden();
});

// ESPN lists a game with no kickoff time yet at midnight Eastern (Friday 11 PM
// in the page's US Central) and the panel marks it tbd: its day and "TBD".
test("a game with no kickoff time yet shows its day and TBD", async ({ page, request }) => {
  const sat = 1792814400; // 2026-10-24T04:00Z
  const g = (await deviceState(request)).game;
  const t = (abbr, name, id) => ({ l: "ncaa", id, abbr, name });
  await reset(request, {
    game: { ...g, id: "1", oa: "MINN", kick: sat, tbd: true, k: "Oct 24 TBD", detail: "TBD" },
    next: [{ slot: 1, t: t("IOWA", "Iowa Hawkeyes", 2294), id: "1", s: "PRE", kick: sat, tbd: true, oi: 135, oa: "MINN", ts: 0, os: 0, tv: "", detail: "TBD" }],
  });
  await page.goto("/");
  await expect(page.locator("#clock")).toHaveText("Oct 24 TBD");
  await expect(page.locator("#upnextList .un.fav .w")).toHaveText("Sat, Oct 24 TBD");
});

test("a favorite says no game scheduled only once ESPN said so", async ({ page, request }) => {
  const t = (abbr, name, id) => ({ l: "nfl", id, abbr, name });
  await reset(request, {
    mode: 4,
    game: { s: "NOT_FOUND", l: "nfl", why: "Can't reach ESPN" },
    next: [
      { slot: 1, t: t("DAL", "Dallas Cowboys", 6) },
      { slot: 2, t: t("HOU", "Houston Texans", 34), none: true },
    ],
  });
  await page.goto("/");
  await expect(page.locator("#msg")).toHaveText("Can't reach ESPN");
  await expect(page.locator("#upnextList .un.fav .w")).toHaveText(["Not checked yet", "No game scheduled"]);
});

test("a bowl or playoff game shows its name, other games none", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await expect(page.locator("#gstate")).toHaveText("UPCOMING");
  await expect(page.locator("#gname")).toBeHidden();

  const g = (await (await request.get("/gameday/state")).json()).game;
  await reset(request, { game: { ...g, gn: "Super Bowl LX" } });
  await page.reload();
  await expect(page.locator("#gname")).toBeVisible();
  await expect(page.locator("#gname")).toHaveText("Super Bowl LX");

  await reset(request, { game: { ...g, ...LIVE, gn: "Rose Bowl" } });
  await page.reload();
  await expect(page.locator("#gstate")).toHaveText("LIVE");
  await expect(page.locator("#gname")).toHaveText("Rose Bowl");

  await reset(request, { game: { s: "NOT_FOUND", l: "nfl", gn: "Rose Bowl" }, next: [] });
  await page.reload();
  await expect(page.locator("#gstate")).toHaveText("NO GAME");
  await expect(page.locator("#gname")).toBeHidden();
});

test("no game renders the empty board for favorites and for live modes", async ({ page, request }) => {
  await reset(request, { game: { s: "NOT_FOUND", l: "nfl" }, next: [] });
  await page.goto("/");
  await expect(page.locator("#board")).toHaveClass(/empty/);
  await expect(page.locator("#gstate")).toHaveText("NO GAME");
  await expect(page.locator("#msg")).toHaveText("None of your favorites has a game coming up");
  await expect(page.locator("#upnext")).toBeHidden();

  await reset(request, { mode: 2, game: { s: "NOT_FOUND", l: "ncaa" }, next: [] });
  await page.reload();
  await expect(page.locator("#msg")).toHaveText("No live games right now");
  await expect(page.locator("#livebar")).toBeVisible();

  await reset(request, { mode: 5, game: { s: "NOT_FOUND", l: "nfl" }, next: [] });
  await page.reload();
  await expect(page.locator("#msg")).toHaveText("No live games right now");
  await expect(page.locator("#livebar")).toBeVisible();

  // No favorites set: Favorites runs the live board, so it says the same
  await reset(request, { mode: 4, favs: [{}, {}, {}, {}], game: { s: "NOT_FOUND", l: "nfl" }, next: [] });
  await page.reload();
  await expect(page.locator("#msg")).toHaveText("No live games right now");
  await expect(page.locator("#favbar")).toBeVisible();
  await expect(page.locator("#favName")).toHaveText("Add a team to get started");
  // Its button opens the team picker on the Teams tab.
  await page.locator("#favStart").click();
  await expect(page).toHaveURL(/#teams$/);
  await expect(page.getByRole("searchbox", { name: "Search teams" })).toBeFocused();
});

test("an empty board shows the panel's reason, and the default only when the panel has none", async ({ page, request }) => {
  await reset(request, { game: { s: "NOT_FOUND", l: "nfl", why: "Can't reach ESPN" }, next: [] });
  await page.goto("/");
  await expect(page.locator("#board")).toHaveClass(/empty/);
  await expect(page.locator("#gstate")).toHaveText("NO GAME");
  await expect(page.locator("#msg")).toHaveText("Can't reach ESPN");

  await reset(request, { game: { s: "NOT_FOUND", l: "nfl", why: "Getting ready" }, next: [] });
  await page.reload();
  await expect(page.locator("#msg")).toHaveText("Getting ready");

  // Favorites with teams has its own default, but the panel's reason wins
  await reset(request, { mode: 4, game: { s: "NOT_FOUND", l: "nfl", why: "Can't reach ESPN" }, next: [] });
  await page.reload();
  await expect(page.locator("#msg")).toHaveText("Can't reach ESPN");

  await reset(request, { game: { s: "NOT_FOUND", l: "nfl" }, next: [] });
  await page.reload();
  await expect(page.locator("#msg")).toHaveText("None of your favorites has a game coming up");
});

test("a device that never answers /gameday/state leaves the page waiting, not broken", async ({ page, request }) => {
  await reset(request, {}, ["state"]);
  await page.goto("/");
  // Entity controls still come from the event stream.
  await expect(page.locator("#quickCtls .ctl")).toHaveCount(2);
  await expect(page.locator("#showCtls .ctl")).toHaveCount(1);
  await expect(page.locator("#panelCtls .ctl")).toHaveCount(1);
  await expect(page.locator("#gstate")).toHaveText("WAITING");
  await expect(page.locator("#board")).toHaveClass(/empty/);
  await expect(page.locator("#modes button")).toHaveText([/^Favorite teams/, /^Live games/]);
});

test("Diagnostics shows the Wi-Fi history, its drops and the weak-signal hint", async ({ page, request }) => {
  await reset(request, {});
  await page.goto("/");
  await tab(page, "device");
  await unfold(page, "#diagCard");
  await expect(page.locator("#rssi")).toHaveText("-61 dBm");
  await expect(page.locator("#wifiWeak")).toBeHidden();
  // Up an hour: every drop since start is in the day.
  await expect(page.locator("#wifiDrops")).toHaveText("1 since start");
  await expect(page.locator("#wifiLast")).toHaveText("Last: signal lost, 40 min ago, back in 42 s");
  await expect(page.locator("#wifiSpark .ln")).toHaveCount(1);
  await expect(page.locator("#wifiSpark .gap")).toHaveCount(1);
  await expect(page.locator("#wifiSpark")).toHaveAttribute("aria-label", "Signal over the last 1 h: -64 to -58 dBm");
  await expect(page.locator("#wifiFrom")).toHaveText("1 h ago");

  // A full day, weak for the last stretch, three drops in it of two kinds.
  const hist = Array(288).fill(-66);
  for (let i = 270; i < 288; i++) hist[i] = -80;
  await reset(request, { wifi: {
    rssi: -81, step: 300, hist, weak: true, drops: 5, day: 3, causes: { signal: 2, router: 1, roam: 4 },
    last: "router", last_ago: 7300,
  } });
  await page.reload();
  await unfold(page, "#diagCard");
  await expect(page.locator("#wifiWeak")).toBeVisible();
  await expect(page.locator("#wifiWeak")).toHaveText(/Move the panel closer to the router/);
  await expect(page.locator("#wifiDrops")).toHaveText("3 in 24 h");
  await expect(page.locator("#wifiLast")).toHaveText(
    "Last: dropped by the router, 2 h 2 min ago. 2 signal lost, 1 dropped by the router");
  await expect(page.locator("#wifiSpark")).toHaveAttribute("aria-label", /weak for 1 h 30 min$/);
  await expect(page.locator("#wifiFrom")).toHaveText("24 h ago");

  // No drops, and only a few minutes of history: no line yet.
  await reset(request, { wifi: { rssi: -55, step: 300, hist: [-55], weak: false, drops: 0, day: 0, causes: {} } });
  await page.reload();
  await unfold(page, "#diagCard");
  await expect(page.locator("#wifiDrops")).toHaveText("None");
  await expect(page.locator("#wifiLast")).toHaveText("");
  await expect(page.locator("#wifiSparkBox")).toBeHidden();

  // Firmware before the history: the rows stay hidden, Signal still comes from the RSSI sensor.
  await reset(request, { wifi: null });
  await page.reload();
  await unfold(page, "#diagCard");
  await expect(page.locator("#hostname")).toHaveText("gameday-2f6a70.local");
  await expect(page.locator("#wifiDiag")).toBeHidden();
});

test("a failed settings post is retried once, then reported", async ({ page, request }) => {
  await reset(request, {}, ["set"]);
  await page.goto("/");
  await tab(page, "display");
  await expect(page.locator('.sw[data-set="opp"]')).toHaveClass(/\bon\b/);
  await page.locator('.sw[data-set="opp"]').click();
  await expect(page.locator("#toast")).toHaveText("Device did not respond (HTTP 500). Try again.");
  expect(await sets(request)).toEqual([{ opp: "0" }, { opp: "0" }]);
  // The switch shows what the panel still has.
  await expect(page.locator('.sw[data-set="opp"]')).toHaveClass(/\bon\b/);
});

test("mode pills post the mode and swap the settings bar", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await expect(page.locator("#modes button.on")).toHaveText(/^Favorite teams/);
  await page.locator('#modes button[data-mode="5"]').click();
  await expectSet(request, { mode: "5" });
  await expect(page.locator("#modes button.on")).toHaveText(/^Live games/);
  await expect(page.locator("#livebar")).toBeVisible();
  await expect(page.locator("#favbar")).toBeHidden();

  // Switch every is on the Teams tab.
  await tab(page, "teams");
  await page.locator("#rotate").fill("45");
  await page.locator("#rotate").press("Enter");
  await expectSet(request, { rotate: "30" }); // the page clamps to 1..30
  await expect(page.locator("#rotate")).toHaveValue("30");
  // A decimal goes as a whole number: the panel refuses anything else.
  await page.locator("#rotate").fill("7.6");
  await page.locator("#rotate").press("Enter");
  await expectSet(request, { rotate: "8" });
  await expect(page.locator("#rotate")).toHaveValue("8");

  await tab(page, "board");
  await page.locator('#modes button[data-mode="4"]').click();
  await expectSet(request, { mode: "4" });
  await expect(page.locator("#favbar")).toBeVisible();
  await expect(page.locator("#livebar")).toBeHidden();
});

test("favorite teams mode shows each favorite and posts its settings", async ({ page, request }) => {
  const t = (abbr, name, id) => ({ l: "nfl", id, abbr, name });
  await reset(request, {
    mode: 4,
    shown: 2,
    locked: true,
    next: [
      { slot: 1, t: t("DAL", "Dallas Cowboys", 6), id: "1", s: "PRE", kick: 1792531500, oi: 21, oa: "PHI", ts: 0, os: 0, tv: "FOX", detail: "" },
      { slot: 2, t: t("HOU", "Houston Texans", 34), id: "5", s: "IN", kick: 1792520000, oi: 30, oa: "JAX", ts: 14, os: 10, tv: "CBS", detail: "8:12 - 2nd" },
    ],
  });
  await page.goto("/");
  await expect(page.locator("#favbar")).toBeVisible();
  await expect(page.locator("#favName")).toHaveText("Houston Texans are on");
  await expect(page.locator("#favStart")).toBeHidden();
  await expect(page.locator("#upnextList .un.fav .n")).toHaveText(["DAL vs PHI", "HOU vs JAX"]);
  await expect(page.locator("#upnextList .un.shown .w")).toHaveText("14 - 10 · 8:12 - 2nd");

  // Locked also covers the lead-in before kickoff and the stretch after the final.
  const g = (await deviceState(request)).next;
  await reset(request, { mode: 4, shown: 1, locked: true, next: g });
  await page.reload();
  await expect(page.locator("#favName")).toHaveText("Dallas Cowboys play soon");
  await reset(request, { mode: 4, shown: 2, locked: true, next: [g[0], { ...g[1], s: "POST", done: true }] });
  await page.reload();
  await expect(page.locator("#favName")).toHaveText("Houston Texans just finished");

  await tab(page, "teams");
  await page.locator("#lockon").fill("200");
  await page.locator("#lockon").press("Enter");
  await expectSet(request, { lockon: "120" });
  await page.locator("#release").selectOption("300");
  await expectSet(request, { release: "300" });
  await expect(page.locator("#favrotate")).toBeHidden();
  await page.locator("#collide").selectOption("1");
  await expectSet(request, { collide: "1" });
  await expect(page.locator("#favrotate")).toBeVisible();
  await page.locator("#favrotate").fill("5");
  await page.locator("#favrotate").press("Enter");
  await expectSet(request, { rotate: "5" });
});

test("the team picker searches the team list and adds a favorite", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await tab(page, "teams");
  await page.locator("#favAdd").click();
  await expect(page.locator("#modal")).toHaveClass(/open/);
  // On the first favorite's league
  await expect(page.locator(".tabs button.on")).toHaveText("NFL");
  await page.locator("#q").fill("eagles");
  await expect(page.locator("#tiles .tile .n")).toContainText(["Philadelphia Eagles", "Boston College Eagles"]);
  await page.locator("#tiles .tile", { hasText: "Philadelphia Eagles" }).click();
  await expectSet(request, { favs: "nfl:6,nfl:34,ncaa:145,nfl:21" });
  await expect(page.locator("#modal")).toHaveClass(/open/);
});

test("the picker opens on today's live games and adds a side", async ({ page, request }) => {
  const side = (homeAway, id, abbr, name, score) => ({ homeAway, score, team: { id, abbreviation: abbr, shortDisplayName: name } });
  await page.route("https://site.api.espn.com/apis/site/v2/sports/football/nfl/scoreboard*", (route) =>
    route.fulfill({
      json: {
        events: [{
          id: "401772001",
          date: "2026-10-11T20:25Z",
          status: { type: { state: "in", shortDetail: "8:12 - 2nd" } },
          competitions: [{ competitors: [side("home", "6", "DAL", "Cowboys", "14"), side("away", "21", "PHI", "Eagles", "10")] }],
        }],
      },
    }),
  );
  await reset(request);
  await page.goto("/");
  await tab(page, "teams");
  await page.locator("#favAdd").click();
  await expect(page.locator(".tabs button.on")).toHaveText("On now");
  await expect(page.locator("#tiles .gh")).toHaveText(["Live now"]);
  await expect(page.locator("#tiles .side.on .n")).toHaveText("Cowboys");
  await page.locator("#tiles .side", { hasText: "Eagles" }).click();
  await expectSet(request, { favs: "nfl:6,nfl:34,ncaa:145,nfl:21" });
});

test("an On now search that matches no game finds the team instead", async ({ page, request }) => {
  const side = (homeAway, id, abbr, name, score) => ({ homeAway, score, team: { id, abbreviation: abbr, shortDisplayName: name } });
  await page.route("https://site.api.espn.com/apis/site/v2/sports/football/nfl/scoreboard*", (route) =>
    route.fulfill({
      json: {
        events: [{
          id: "401772001",
          date: "2026-10-11T20:25Z",
          status: { type: { state: "in", shortDetail: "8:12 - 2nd" } },
          competitions: [{ competitors: [side("home", "6", "DAL", "Cowboys", "14"), side("away", "21", "PHI", "Eagles", "10")] }],
        }],
      },
    }),
  );
  await reset(request);
  await page.goto("/");
  await tab(page, "teams");
  await page.locator("#favAdd").click();
  await expect(page.locator(".tabs button.on")).toHaveText("On now");
  // The Stars aren't playing today: the teams that match, with their league, under a Teams heading.
  await page.locator("#q").fill("dallas stars");
  await expect(page.locator("#tiles .gh")).toHaveText(["Teams"]);
  await expect(page.locator("#tiles .tile .n")).toHaveText(["Dallas Stars"]);
  await expect(page.locator("#tiles .tile .a")).toHaveText("NHL · DAL");
  await expect(page.locator("#tiles .none")).toHaveCount(0);
  await page.locator("#tiles .tile", { hasText: "Dallas Stars" }).click();
  await expectSet(request, { favs: "nfl:6,nfl:34,ncaa:145,nhl:9" });
  await expect(page.locator("#tiles .tile.on .n")).toHaveText("Dallas Stars");
  await expect(page.locator(".tabs button.on")).toHaveText("On now");
  // A team that is playing shows its game, not a tile.
  await page.locator("#q").fill("eagles");
  await expect(page.locator("#tiles .gh")).toHaveText(["Live now"]);
  await expect(page.locator("#tiles .side .n")).toHaveText(["Eagles", "Cowboys"]);
  await expect(page.locator("#tiles .tile")).toHaveCount(0);
  // Nothing matches at all: the old line.
  await page.locator("#q").fill("zzzz");
  await expect(page.locator("#tiles .none")).toHaveText("No game matches");
});

test("a league tab starts with that league's games today, finals folded, then its teams", async ({ page, request }) => {
  const side = (homeAway, id, abbr, name, score) => ({ homeAway, score, team: { id, abbreviation: abbr, shortDisplayName: name } });
  const ev = (id, date, state, detail, home, away, completed = true) => ({
    id, date, status: { type: { state, shortDetail: detail, completed } },
    competitions: [{ competitors: [side("home", ...home), side("away", ...away)] }],
  });
  let nflAsks = 0;
  await page.route("https://site.api.espn.com/apis/site/v2/sports/football/nfl/scoreboard*", (route) => {
    nflAsks++;
    return route.fulfill({
      json: {
        events: [
          ev("1", "2026-10-11T17:00Z", "post", "Final", ["6", "DAL", "Cowboys", "24"], ["21", "PHI", "Eagles", "20"]),
          ev("2", "2026-10-11T20:25Z", "in", "8:12 - 2nd", ["9", "GB", "Packers", "7"], ["3", "CHI", "Bears", "3"]),
          ev("3", "2026-10-12T00:20Z", "pre", "8:20 PM", ["12", "KC", "Chiefs", "0"], ["7", "DEN", "Broncos", "0"]),
          ev("4", "2026-10-11T17:00Z", "post", "Postponed", ["2", "BUF", "Bills", "0"], ["15", "MIA", "Dolphins", "0"], false),
        ],
      },
    });
  });
  await reset(request);
  await page.goto("/");
  await tab(page, "teams");
  await page.locator("#favAdd").click();
  // A game is live, so the picker moves to On now; the NFL tab has the same games.
  await expect(page.locator(".tabs button.on")).toHaveText("On now");
  await page.locator(".tabs button", { hasText: "NFL" }).click();
  await expect(page.locator("#tiles .gh")).toHaveText(["Live now", "Later today", "Final (1)", "Postponed or canceled (1)", "Teams"]);
  // The list On now just read is reused: no second ask for the tab.
  expect(nflAsks).toBe(1);
  // Finals and postponed games start folded; the live and later ones show.
  await expect(page.locator("#tiles .side:visible .n")).toHaveText(["Bears", "Packers", "Broncos", "Chiefs"]);
  await page.locator("#tiles summary", { hasText: "Final" }).click();
  await expect(page.locator("#tiles .side:visible .n")).toHaveText(["Bears", "Packers", "Broncos", "Chiefs", "Eagles", "Cowboys"]);
  // Then every NFL team, A to Z, after the games.
  await expect(page.locator("#tiles .tile")).toHaveCount(32);
  await expect(page.locator("#tiles .tile .n").first()).toHaveText("Arizona Cardinals");
  // The Cowboys are a favorite: checked on the game and on the team tile.
  await expect(page.locator("#tiles .side.on .n")).toHaveText("Cowboys");
  await expect(page.locator("#tiles .tile.on .n")).toContainText(["Dallas Cowboys"]);
  // A tap on a side adds the team, and its tile checks too.
  await page.locator("#tiles .side", { hasText: "Chiefs" }).click();
  await expectSet(request, { favs: "nfl:6,nfl:34,ncaa:145,nfl:12" });
  await expect(page.locator("#tiles .tile", { hasText: "Kansas City Chiefs" })).toHaveAttribute("aria-pressed", "true");
  // A league with nothing today: just the teams, no heading.
  await page.locator(".tabs button", { hasText: "NHL" }).click();
  await expect(page.locator("#tiles .tile")).toHaveCount(32);
  await expect(page.locator("#tiles .gh")).toHaveCount(0);
  await expect(page.locator("#tiles .game")).toHaveCount(0);
  // A search on a league tab is teams only, from every league.
  await page.locator(".tabs button", { hasText: "NFL" }).click();
  await page.locator("#q").fill("packers");
  await expect(page.locator("#tiles .tile .n")).toHaveText(["Green Bay Packers"]);
  await expect(page.locator("#tiles .game")).toHaveCount(0);
});

test("a league tab shows its teams at once and adds today's games when they arrive", async ({ page, request }) => {
  const side = (homeAway, id, abbr, name) => ({ homeAway, score: "0", team: { id, abbreviation: abbr, shortDisplayName: name } });
  let release;
  const held = new Promise((r) => (release = r));
  await page.route("https://site.api.espn.com/apis/site/v2/sports/football/nfl/scoreboard*", async (route) => {
    await held;
    return route.fulfill({
      json: {
        events: [{
          id: "3", date: "2026-10-12T00:20Z",
          status: { type: { state: "pre", shortDetail: "8:20 PM" } },
          competitions: [{ competitors: [side("home", "12", "KC", "Chiefs"), side("away", "7", "DEN", "Broncos")] }],
        }],
      },
    });
  });
  await reset(request);
  await page.goto("/");
  await tab(page, "teams");
  await page.locator("#favAdd").click();
  // Nothing live, so it opens on the first favorite's league before the games come.
  await expect(page.locator(".tabs button.on")).toHaveText("NFL");
  await expect(page.locator("#tiles .tile")).toHaveCount(32);
  await expect(page.locator("#tiles .gh")).toHaveCount(0);
  release();
  await expect(page.locator("#tiles .gh")).toHaveText(["Later today", "Teams"]);
  await expect(page.locator("#tiles .side .n")).toHaveText(["Broncos", "Chiefs"]);
  await expect(page.locator(".tabs button.on")).toHaveText("NFL");
});

test("the On now tab asks ESPN for every college game, not the Top 25", async ({ page, request }) => {
  // Without groups= the college basketball scoreboard lists only ranked teams' games.
  const side = (homeAway, id, abbr, name) => ({ homeAway, score: "40", team: { id, abbreviation: abbr, shortDisplayName: name } });
  const asked = {};
  for (const [lg, path] of [["mcbb", "basketball/mens-college-basketball"], ["ncaa", "football/college-football"]]) {
    await page.route(`https://site.api.espn.com/apis/site/v2/sports/${path}/scoreboard*`, (route) => {
      asked[lg] = new URL(route.request().url()).searchParams;
      const events = lg !== "mcbb" ? [] : [{
        id: "401820001",
        date: "2026-02-14T19:00Z",
        status: { type: { state: "in", shortDetail: "12:30 - 2nd Half" } },
        competitions: [{ competitors: [side("home", "2006", "AKR", "Akron"), side("away", "2000", "ACU", "Abilene Chrstn")] }],
      }];
      return route.fulfill({ json: { events } });
    });
  }
  await reset(request);
  await page.goto("/");
  await tab(page, "teams");
  await page.locator("#favAdd").click();
  await expect(page.locator(".tabs button.on")).toHaveText("On now");
  await expect(page.locator("#tiles .side .n")).toHaveText(["Abilene Chrstn", "Akron"]);
  expect(asked.mcbb.get("groups")).toBe("50");
  expect(asked.mcbb.get("limit")).toBe("300");
  expect(asked.mcbb.get("dates")).toMatch(/^\d{8}$/);
  expect(asked.ncaa.get("groups")).toBe("80");
});

test("the On now tab keeps a late game on today's list after midnight Eastern, as the panel does", async ({ page, request }) => {
  // 1:30 AM Eastern on Friday 10/23: West Coast games from Thursday are
  // still on. No league's day turns over until 4 AM Eastern.
  await page.clock.setFixedTime(new Date("2026-10-23T05:30:00Z"));
  const asked = {};
  for (const [lg, path] of [["nba", "basketball/nba"], ["nfl", "football/nfl"]]) {
    await page.route(`https://site.api.espn.com/apis/site/v2/sports/${path}/scoreboard*`, (route) => {
      asked[lg] = new URL(route.request().url()).searchParams.get("dates");
      return route.fulfill({ json: { events: [] } });
    });
  }
  await reset(request);
  await page.goto("/");
  await tab(page, "teams");
  await page.locator("#favAdd").click();
  await expect.poll(() => asked.nba).toBe("20261022");
  await expect.poll(() => asked.nfl).toBe("20261022");
});

test("ticker, celebration and startup switches post 0 and 1", async ({ page, request }) => {
  await reset(request, { play: false });
  await page.goto("/");
  await tab(page, "display");
  await expect(page.locator('.sw[data-set="down"]')).toHaveClass(/\bon\b/);
  await page.locator('.sw[data-set="down"]').click();
  await expectSet(request, { down: "0" });
  await page.locator('.sw[data-set="play"]').click();
  await expectSet(request, { play: "1" });
  await tab(page, "device");
  await unfold(page, "#setupCard");
  await page.locator('.sw[data-set="bootaddr"]').click();
  await expectSet(request, { bootaddr: "0" });
  await tab(page, "teams");
  await page.locator('.sw[data-set="today"]').click();
  await expectSet(request, { today: "1" });
  await expect(page.locator('.sw[data-set="jump"]')).not.toHaveClass(/\bon\b/);
  await expect(page.locator('.sw[data-set="banner"]')).toHaveClass(/\bon\b/);
  await page.locator('.sw[data-set="jump"]').click();
  await expectSet(request, { jump: "1" });
  await page.locator('.sw[data-set="banner"]').click();
  await expectSet(request, { banner: "0" });
  await expect(page.locator('.sw[data-set="down"]')).not.toHaveClass(/\bon\b/);
  await expect(page.locator('.sw[data-set="play"]')).toHaveClass(/\bon\b/);
});

test("Showing Effects reveals the effect controls, and they post", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  // Showing and the effect controls are on the Home tab, under the panel's power and brightness.
  const showing = page.getByRole("combobox", { name: "Showing" });
  await expect(showing).toHaveValue("Scoreboard");
  await expect(showing.locator("option")).toHaveText(["Scoreboard", "Clock", "Effects", "Smart board", "Flights", "Weather"]);
  await expect(page.locator("#fxCtls")).toBeHidden();
  // The Panel card's own controls are untouched by the hidden effect rows.
  await expect(page.locator("#quickCtls .ctl")).toHaveCount(2);
  await expect(page.locator("#showCtls .ctl")).toHaveCount(1);

  // Picking the Effects page: the mock applies it and pushes the new value on
  // the event stream, which is what reveals the controls.
  await showing.selectOption("Effects");
  await expect.poll(() => entityPosts(request)).toContainEqual({ path: "/select/Select%20Page/set", q: { option: "Effects" } });
  await expect(page.locator("#fxCtls")).toBeVisible();
  await expect(page.locator("#fxCtls p.hint")).toContainText("stays on until you pick another page");
  const effect = page.getByRole("combobox", { name: "Effect", exact: true });
  const palette = page.getByRole("combobox", { name: "Palette" });
  const speed = page.getByRole("slider", { name: "Speed", exact: true });
  const intensity = page.getByRole("slider", { name: "Intensity", exact: true });
  await expect(effect).toHaveValue("Matrix");
  await expect(effect.locator("option")).toHaveText(["Matrix", "Metaballs", "DNA", "Fireworks", "Snow Fall", "Distortion Waves", "Drift", "Plasma Ball"]);
  await expect(palette).toHaveValue("Default");
  await expect(speed).toHaveValue("128");
  await expect(speed).toHaveAttribute("max", "255");
  await expect(intensity).toHaveValue("128");

  await effect.selectOption("DNA");
  await expect.poll(() => entityPosts(request)).toContainEqual({ path: "/select/Effect/set", q: { option: "DNA" } });
  await palette.selectOption("Ocean");
  await expect.poll(() => entityPosts(request)).toContainEqual({ path: "/select/Effect%20Palette/set", q: { option: "Ocean" } });
  await speed.fill("200");
  await speed.dispatchEvent("change");
  await expect.poll(() => entityPosts(request)).toContainEqual({ path: "/number/Effect%20Speed/set", q: { value: "200" } });
  await expect(page.locator("#fxCtls .val").first()).toHaveText("200");
  await intensity.fill("40");
  await intensity.dispatchEvent("change");
  await expect.poll(() => entityPosts(request)).toContainEqual({ path: "/number/Effect%20Intensity/set", q: { value: "40" } });

  // Back to the scoreboard hides them again.
  await showing.selectOption("Scoreboard");
  await expect(page.locator("#fxCtls")).toBeHidden();
  await expect(page.locator("#showCtls .ctl")).toHaveCount(1);
});

// Effect playlists post to the settings import as {"schema": 1, "fxlists": [...]}.
const fxPosts = async (request) =>
  (await (await request.get("/_log")).json())
    .filter((p) => p.path === "/gameday/settings")
    .map((p) => { try { return JSON.parse(p.body); } catch (_) { return null; } })
    .filter((d) => d && Array.isArray(d.fxlists))
    .map((d) => d.fxlists);
const lastFx = async (request) => (await fxPosts(request)).pop();

test("effect playlists: made, edited, played, stopped by a hand pick, deleted", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await tab(page, "customize");
  const card = page.locator("#fxCard");
  await expect(card).toBeVisible();
  await expect(card.getByRole("heading")).toHaveText("Effect playlists");
  await expect(card).toContainText("No effect playlists yet.");
  // The Home tab's picker waits for a playlist.
  await expect(page.locator("#fxPickRow")).toBeHidden();

  await card.getByRole("button", { name: "New effect playlist" }).click();
  await expect.poll(() => lastFx(request)).toEqual([{ n: "Effects 1", shuffle: false, fx: [] }]);
  await expect(card.getByRole("textbox", { name: "Effect playlist name" })).toHaveValue("Effects 1");
  await expect(card.getByRole("button", { name: "Play Effects 1" })).toBeDisabled();

  // Renamed; a new effect starts as what the panel shows now, for 15 seconds.
  await card.getByRole("textbox", { name: "Effect playlist name" }).fill("Party");
  await card.getByRole("textbox", { name: "Effect playlist name" }).press("Enter");
  await expect.poll(() => lastFx(request)).toEqual([{ n: "Party", shuffle: false, fx: [] }]);
  await card.getByRole("button", { name: "Add an effect to Party" }).click();
  await expect.poll(() => lastFx(request)).toEqual([{ n: "Party", shuffle: false, fx: [{ e: "Matrix", p: "Default", s: 128, i: 128, w: 15 }] }]);
  await card.getByRole("button", { name: "Add an effect to Party" }).click();
  await expect(card.getByRole("combobox", { name: "Effect 2 in Party" })).toBeVisible();

  // The second effect: another effect and palette, a speed, a minute, then Shuffle.
  await card.getByRole("combobox", { name: "Effect 2 in Party" }).selectOption("Drift");
  await expect.poll(async () => (await lastFx(request))[0].fx[1].e).toBe("Drift");
  await card.getByRole("combobox", { name: "Palette 2 in Party" }).selectOption("Sunset");
  await expect.poll(async () => (await lastFx(request))[0].fx[1].p).toBe("Sunset");
  // Speed and intensity are sliders that save when let go.
  const slideTo = async (name, v) => {
    const r = card.getByRole("slider", { name });
    await r.fill(String(v));
    await r.dispatchEvent("change");
  };
  await expect(card.getByRole("spinbutton")).toHaveCount(0);
  await slideTo("Speed 2 in Party", 200);
  await expect.poll(async () => (await lastFx(request))[0].fx[1].s).toBe(200);
  const time = card.getByRole("combobox", { name: "Time 2 in Party" });
  await expect(time.locator("option")).toHaveCount(16);
  await expect(time.locator("option").first()).toHaveText("1 s");
  await expect(time.locator("option").last()).toHaveText("1 hour");
  await time.selectOption({ label: "1 min" });
  await expect.poll(async () => (await lastFx(request))[0].fx[1].w).toBe(60);
  await card.getByRole("switch", { name: "Shuffle Party" }).click();
  await expect.poll(async () => (await lastFx(request))[0].shuffle).toBe(true);
  // Moved up: Drift first.
  await card.getByRole("button", { name: "Move up effect 2 in Party" }).click();
  await expect.poll(async () => (await lastFx(request))[0].fx.map((e) => e.e)).toEqual(["Drift", "Matrix"]);
  const st = await deviceState(request);
  expect(st.fxlists).toEqual([{ n: "Party", shuffle: true, fx: [
    { e: "Drift", p: "Sunset", s: 200, i: 128, w: 60 }, { e: "Matrix", p: "Default", s: 128, i: 128, w: 15 }] }]);

  // Play: the panel goes to Effects, and the page says which one plays.
  await card.getByRole("button", { name: "Play Party" }).click();
  await expectSet(request, { fxlist: "1" });
  await expect(card.getByRole("button", { name: "Stop Party" })).toBeVisible();
  await expect(card).toContainText("Playing now");
  await tab(page, "board");
  await expect(page.getByRole("combobox", { name: "Showing" })).toHaveValue("Effects");
  const pick = page.getByRole("combobox", { name: "Effect playlist" });
  await expect(pick).toHaveValue("1");
  await expect(pick.locator("option")).toHaveText(["None", "Party"]);
  await expect(page.getByRole("combobox", { name: "Effect", exact: true })).toHaveValue("Drift");

  // Editing it while it plays keeps its place: another effect's change leaves
  // Drift up, and a change to Drift itself goes up at once.
  await tab(page, "customize");
  await slideTo("Intensity 2 in Party", 40);
  await expect.poll(async () => (await lastFx(request))[0].fx[1].i).toBe(40);
  await slideTo("Speed 1 in Party", 90);
  await expect.poll(async () => (await lastFx(request))[0].fx[0].s).toBe(90);
  expect((await deviceState(request)).fxlist).toBe(1);
  await expect(card).toContainText("Playing now");
  await tab(page, "board");
  await expect(page.getByRole("combobox", { name: "Effect", exact: true })).toHaveValue("Drift");
  await expect(page.locator("#fxCtls .val").first()).toHaveText("90");

  // A hand pick of the effect stops the playlist and keeps the pick.
  await page.getByRole("combobox", { name: "Effect", exact: true }).selectOption("DNA");
  await expect.poll(async () => (await deviceState(request)).fxlist).toBe(0);
  await expect(pick).toHaveValue("0");
  await expect(page.getByRole("combobox", { name: "Effect", exact: true })).toHaveValue("DNA");

  // The picker starts it again, and None stops it.
  await pick.selectOption("1");
  await expectSet(request, { fxlist: "1" });
  await expect.poll(async () => (await deviceState(request)).fxlist).toBe(1);
  await pick.selectOption("0");
  await expectSet(request, { fxlist: "0" });
  await expect.poll(async () => (await deviceState(request)).fxlist).toBe(0);

  // Deleted, after a yes.
  await tab(page, "customize");
  page.once("dialog", (d) => d.accept());
  await card.getByRole("button", { name: "Delete Party" }).click();
  await expect.poll(() => lastFx(request)).toEqual([]);
  await expect(card).toContainText("No effect playlists yet.");
});

test("effect playlists: four at most, eight effects each, and none on older firmware", async ({ page, request }) => {
  const fx = (e) => ({ e, p: "Default", s: 128, i: 128, w: 15 });
  const eight = Array.from({ length: 8 }, () => fx("Matrix"));
  await reset(request, { fxlists: ["A", "B", "C", "D"].map((n) => ({ n, shuffle: false, fx: n === "A" ? eight : [fx("DNA")] })) });
  await page.goto("/#customize");
  const card = page.locator("#fxCard");
  await expect(card.getByRole("textbox", { name: "Effect playlist name" })).toHaveCount(4);
  await expect(card.getByRole("button", { name: "New effect playlist" })).toBeHidden();
  await expect(card.getByRole("button", { name: "Add an effect to A" })).toBeHidden();
  await expect(card.getByRole("button", { name: "Add an effect to B" })).toBeVisible();
  // A name another one has is refused here.
  await card.getByRole("textbox", { name: "Effect playlist name" }).nth(1).fill("A");
  await card.getByRole("textbox", { name: "Effect playlist name" }).nth(1).press("Enter");
  await expect(page.locator("#toast")).toContainText("Another effect playlist has that name");
  expect(await fxPosts(request)).toEqual([]);
  // So is "None", Home Assistant's choice to stop a playlist.
  await card.getByRole("textbox", { name: "Effect playlist name" }).nth(1).fill("None");
  await card.getByRole("textbox", { name: "Effect playlist name" }).nth(1).press("Enter");
  await expect(page.locator("#toast")).toContainText("That name is taken");
  await expect(card.getByRole("textbox", { name: "Effect playlist name" }).nth(1)).toHaveValue("B");
  expect(await fxPosts(request)).toEqual([]);

  // Firmware without effect playlists: no card, no picker.
  await reset(request, { fxlists: null, fxlist: null, fxlistmax: null, fxmax: null, fxwaits: null });
  await page.reload();
  await expect(page.locator("#fxCard")).toBeHidden();
  await expect(page.locator("#fxPickRow")).toBeHidden();
});

test("a screen reader gets each control's name, role and state", async ({ page, request }) => {
  await reset(request, { play: false });
  await page.goto("/");
  await expect(page.locator("#toast")).toHaveAttribute("role", "status");

  // The section tabs: Home open, the arrow keys move between them.
  const sections = page.getByRole("tablist", { name: "Sections" });
  await expect(sections.getByRole("tab", { selected: true })).toHaveText("Home");
  await sections.getByRole("tab", { name: "Home" }).press("ArrowRight");
  await expect(sections.getByRole("tab", { selected: true })).toHaveText("Teams");
  await expect(page.getByRole("tabpanel", { name: "Teams" })).toBeVisible();
  await expect(page).toHaveURL(/#teams$/);

  await tab(page, "display");
  await expect(page.getByRole("switch", { name: "Down and distance" })).toBeChecked();
  await expect(page.getByRole("switch", { name: "Last play" })).not.toBeChecked();
  await page.getByRole("switch", { name: "Last play" }).click();
  await expectSet(request, { play: "1" });
  await expect(page.getByRole("switch", { name: "Last play" })).toBeChecked();
  // The entity controls built from the event stream
  await expect(page.getByRole("slider", { name: "Ticker speed" })).toHaveValue("5");
  await tab(page, "board");
  await expect(page.getByRole("combobox", { name: "Showing" })).toHaveValue("Scoreboard");
  await expect(page.getByRole("switch", { name: "Panel on" })).toBeChecked();
  await expect(page.getByRole("slider", { name: "Brightness" })).toHaveValue("128");

  const modes = page.getByRole("group", { name: "Mode" });
  await expect(modes.getByRole("button", { pressed: true })).toHaveText(/^Favorite teams/);
  await expect(modes.getByRole("button", { pressed: false })).toHaveText([/^Live games/]);
  await tab(page, "device");
  await unfold(page, "#matrixCard");
  await expect(page.getByRole("button", { name: /^One 128x64 panel/ })).toHaveAttribute("aria-pressed", "true");
  await expect(page.getByRole("button", { name: "One 64x64 panel" })).toHaveAttribute("aria-pressed", "false");

  // The picker's league tabs: one selected, in the Tab order, the arrow keys move between them.
  await tab(page, "teams");
  await page.getByRole("button", { name: "Add a favorite" }).click();
  await expect(page.getByRole("searchbox", { name: "Search teams" })).toBeFocused();
  const tabs = page.getByRole("tablist", { name: "Leagues" });
  await expect(tabs.getByRole("tab", { selected: true })).toHaveText("NFL");
  await expect(tabs.locator('[tabindex="0"]')).toHaveText("NFL");
  await tabs.getByRole("tab", { name: "NFL" }).press("ArrowRight");
  await expect(tabs.getByRole("tab", { selected: true })).toHaveText("NCAAF");
  await expect(tabs.getByRole("tab", { name: "NCAAF" })).toBeFocused();
  await expect(page.getByRole("tabpanel").locator(".tile .a").first()).toContainText("NCAAF");
  await page.keyboard.press("End");
  await expect(tabs.getByRole("tab", { selected: true })).toHaveText("NCAAVB");
  await page.keyboard.press("ArrowRight");
  await expect(tabs.getByRole("tab", { selected: true })).toHaveText("On now");
  await expect(tabs.locator('[tabindex="0"]')).toHaveText("On now");
  await page.getByRole("button", { name: "Done" }).click();

  await reset(request, { mode: 5, live: ["mlb", "nhl"], live_auto: false });
  await page.reload();
  await tab(page, "board");
  await expect(modes.getByRole("button", { pressed: true })).toHaveText(/^Live games/);
  const chips = page.getByRole("group", { name: "Leagues" });
  await expect(chips.getByRole("button", { pressed: true })).toHaveText(["MLB", "NHL"]);
  await chips.getByRole("button", { name: "NBA", exact: true }).click();
  await expectSet(request, { live: "mlb,nhl,nba" });
  await expect(chips.getByRole("button", { pressed: true })).toHaveText(["MLB", "NBA", "NHL"]);
});

test("favorites post the whole list in order on add, move and remove", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await tab(page, "teams");
  const rows = page.locator("#favCtls .ctl.fav");
  await expect(rows).toHaveCount(3);
  await expect(rows.nth(0).locator("small")).toHaveText("NFL · remote button 1");

  // Without a paired remote the rows leave the buttons out.
  await request.post("/_reset", { data: { wizmote: "No remote paired" } });
  await page.reload();
  await expect(rows.nth(0).locator("small")).toHaveText("NFL");
  await reset(request);
  await page.reload();
  await expect(rows.nth(0).locator("small")).toHaveText("NFL · remote button 1");

  await page.locator("#favAdd").click();
  await expect(page.locator("#modal")).toHaveClass(/open/);
  await page.locator(".tabs button", { hasText: "MLB" }).click();
  await page.locator("#tiles .tile", { hasText: "New York Yankees" }).click();
  await expectSet(request, { favs: "nfl:6,nfl:34,ncaa:145,mlb:10" });
  await page.locator("#close").click();
  await expect(page.locator("#modal")).not.toHaveClass(/open/);
  await expect(rows).toHaveCount(4);
  await expect(rows.nth(3).locator("label")).toHaveText(/^4\. New York Yankees/);

  await rows.nth(0).getByRole("button", { name: "Move down" }).click();
  await expectSet(request, { favs: "nfl:34,nfl:6,ncaa:145,mlb:10" });
  await rows.nth(2).getByRole("button", { name: "Remove" }).click();
  await expectSet(request, { favs: "nfl:34,nfl:6,mlb:10" });
  await expect(page.locator("#favCtls .ctl.fav label")).toHaveText([
    /^1\. Houston Texans/, /^2\. Dallas Cowboys/, /^3\. New York Yankees/,
  ]);
});

test("playlists post every playlist on new, teams, rename, move and remove", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await tab(page, "teams");
  await expect(page.locator("#listCtls")).toHaveText("No playlists yet.");
  await page.locator("#listAdd").click();
  await expectSet(request, { lists: "Playlist 1=" });
  const box = page.locator("#listCtls .plist");
  await expect(box).toHaveCount(1);
  await expect(box.nth(0).locator(".pbtn")).toHaveCount(0);  // buttons are set in the Remote card now
  await expect(box.nth(0).locator(".pteams .chip")).toHaveText(["DAL", "HOU", "MISS"]);
  await box.nth(0).locator(".pteams .chip", { hasText: "HOU" }).click();
  await expectSet(request, { lists: "Playlist 1=nfl:34" });
  await expect(box.nth(0).locator(".pteams .chip.on")).toHaveText(["HOU"]);
  await box.nth(0).locator(".pname").fill("Texans; and=more");
  await box.nth(0).locator(".pname").press("Enter");
  await expectSet(request, { lists: "Texans andmore=nfl:34" });
  await page.locator("#listAdd").click();
  await expectSet(request, { lists: "Texans andmore=nfl:34;Playlist 2=" });
  await expect(box).toHaveCount(2);
  await box.nth(0).getByRole("button", { name: "Move down Texans andmore" }).click();
  await expectSet(request, { lists: "Playlist 2=;Texans andmore=nfl:34" });
  await box.nth(1).getByRole("button", { name: "Remove Texans andmore" }).click();
  await expectSet(request, { lists: "Playlist 2=" });
  await expect(box).toHaveCount(1);
});

test("the playlists card has no One per sport button", async ({ page, request }) => {
  await reset(request, {
    favs: [
      { l: "nfl", id: 6, abbr: "DAL", name: "Dallas Cowboys" },
      { l: "nba", id: 6, abbr: "DAL", name: "Dallas Mavericks" },
      {},
      {},
    ],
  });
  await page.goto("/");
  await tab(page, "teams");
  await expect(page.locator("#listAdd")).toBeVisible();
  await expect(page.locator("#listActions button")).toHaveCount(1);
  await expect(page.locator("#listCtls")).toHaveText("No playlists yet.");
});

test("a playlist's options post as tokens: days, hours, only this list, only today", async ({ page, request }) => {
  await reset(request, { lists: [{ n: "Sundays", f: [0] }] });
  await page.goto("/");
  await tab(page, "teams");
  const box = page.locator("#listCtls .plist").nth(0);
  const days = box.locator(".pdays .chip");
  await expect(days).toHaveText(["S", "M", "T", "W", "T", "F", "S"]);
  await expect(box.locator(".phours")).toHaveCount(0);
  await days.nth(0).click();
  await expectSet(request, { lists: "Sundays=nfl:6,@d:0" });
  await expect(box.locator(".phours small")).toHaveText("all day");
  await days.nth(6).click();
  await expectSet(request, { lists: "Sundays=nfl:6,@d:06" });
  await box.getByLabel("from Sundays").fill("11:00");
  await expectSet(request, { lists: "Sundays=nfl:6,@d:06,@t:11:00-00:00" });
  await box.getByLabel("to Sundays").fill("23:00");
  await expectSet(request, { lists: "Sundays=nfl:6,@d:06,@t:11:00-23:00" });
  await expect(box.locator(".phours small")).toHaveText("");
  await box.getByLabel("to Sundays").fill("02:00");
  await expectSet(request, { lists: "Sundays=nfl:6,@d:06,@t:11:00-02:00" });
  await expect(box.locator(".phours small")).toHaveText("into the next morning");
  await box.getByRole("switch", { name: "Only this list's games Sundays" }).click();
  await expectSet(request, { lists: "Sundays=nfl:6,@d:06,@t:11:00-02:00,@only" });
  await box.getByLabel("Only today's games Sundays").selectOption("1");
  await expectSet(request, { lists: "Sundays=nfl:6,@d:06,@t:11:00-02:00,@only,@today:1" });
  await box.getByLabel("Only today's games Sundays").selectOption("");
  await expectSet(request, { lists: "Sundays=nfl:6,@d:06,@t:11:00-02:00,@only" });
  // Clearing the last day drops the hours with it.
  await days.nth(0).click();
  await days.nth(6).click();
  await expectSet(request, { lists: "Sundays=nfl:6,@only" });
  await expect(box.locator(".phours")).toHaveCount(0);
  // The state reads back what the mock kept.
  const st = await deviceState(request);
  expect(st.lists[0]).toEqual({ n: "Sundays", f: [0], days: "", start: "00:00", end: "00:00", only: true, color: "", icon: "", lg: [] });
});

test("a playlist's color and icon post as tokens and show beside its name and in Playing", async ({ page, request }) => {
  await reset(request, { mode: 4, list: 0, lists: [{ n: "Sundays", f: [0], days: "0" }, { n: "Hoops", f: [1] }] });
  await page.goto("/");
  await tab(page, "teams");
  const box = page.locator("#listCtls .plist").nth(0);
  const colors = box.getByRole("group", { name: "Color of Sundays" }).getByRole("button");
  const icons = box.getByRole("group", { name: "Icon of Sundays" }).getByRole("button");
  await expect(colors).toHaveCount(9);
  await expect(icons).toHaveCount(7);
  await expect(colors.nth(0)).toHaveAttribute("aria-pressed", "true");
  // No look: no mark beside the name.
  await expect(box.locator(".ctl > .pmark")).toBeHidden();
  await box.getByRole("button", { name: "Blue" }).click();
  await expectSet(request, { lists: "Sundays=nfl:6,@d:0,@c:blue;Hoops=nfl:34" });
  await expect(box.getByRole("button", { name: "Blue" })).toHaveAttribute("aria-pressed", "true");
  await expect(box.locator(".ctl > .pmark.dot")).toHaveCSS("background-color", "rgb(48, 112, 255)");
  await box.getByRole("button", { name: "Star" }).click();
  await expectSet(request, { lists: "Sundays=nfl:6,@d:0,@c:blue,@i:star;Hoops=nfl:34" });
  // The icon is the panel's pixels in the list's color.
  await expect(box.locator(".ctl > .pmark svg path")).toHaveAttribute("fill", "#3070ff");
  // The state reads back what the mock kept, in the firmware's key order.
  const st = await deviceState(request);
  expect(JSON.stringify(st.lists[0])).toBe(JSON.stringify({ n: "Sundays", f: [0], days: "0", start: "00:00", end: "00:00", only: false, color: "blue", icon: "star", lg: [] }));
  // A rename and a move keep them.
  await box.getByRole("button", { name: "Move down Sundays" }).click();
  await expectSet(request, { lists: "Hoops=nfl:34;Sundays=nfl:6,@d:0,@c:blue,@i:star" });
  // Playing (Home tab) marks the list that plays, and its option comes in its color.
  await tab(page, "board");
  await page.locator("#favList").selectOption({ label: "Sundays" });
  await expectSet(request, { mode: "4", list: "2", sport: "none" });
  await expect(page.locator("#favListMark svg path")).toHaveAttribute("fill", "#3070ff");
  await expect(page.locator("#favList option", { hasText: "Sundays" })).toHaveCSS("color", "rgb(48, 112, 255)");
  await page.locator("#favList").selectOption({ label: "Hoops" });
  await expect(page.locator("#favListMark")).toBeHidden();
  // None takes each back off.
  await tab(page, "teams");
  const moved = page.locator("#listCtls .plist").nth(1);
  await moved.getByRole("group", { name: "Icon of Sundays" }).getByRole("button", { name: "None" }).click();
  await expectSet(request, { lists: "Hoops=nfl:34;Sundays=nfl:6,@d:0,@c:blue" });
  await moved.getByRole("group", { name: "Color of Sundays" }).getByRole("button", { name: "None" }).click();
  await expectSet(request, { lists: "Hoops=nfl:34;Sundays=nfl:6,@d:0" });
});

test("a playlist's options survive a rename and a reorder, and the bar says when a schedule has it on", async ({ page, request }) => {
  await reset(request, {
    mode: 4, list: 2, sched: 2,
    lists: [{ n: "A", f: [0] }, { n: "Sundays", f: [1], days: "0", start: "11:00", end: "23:00", only: true, today: false }],
  });
  await page.goto("/");
  await tab(page, "teams");
  await expect(page.locator("#favName")).toHaveText("Playing your Sundays playlist, on its schedule");
  const box = page.locator("#listCtls .plist");
  await expect(box.nth(1).locator(".pdays .chip.on")).toHaveText(["S"]);
  await expect(box.nth(1).getByLabel("Only today's games Sundays")).toHaveValue("0");
  await box.nth(1).getByRole("button", { name: "Move up Sundays" }).click();
  await expectSet(request, { lists: "Sundays=nfl:34,@d:0,@t:11:00-23:00,@only,@today:0;A=nfl:6" });
  // The mock keeps the playing playlist by name, so it is number 1 now.
  await expect(page.locator("#favList")).toHaveValue("1");
  await box.nth(0).locator(".pname").fill("Sunday ball");
  await box.nth(0).locator(".pname").press("Enter");
  await expectSet(request, { lists: "Sunday ball=nfl:34,@d:0,@t:11:00-23:00,@only,@today:0;A=nfl:6", list: "1" });
});

test("the Remote card sets each button: a favorite, a sport, a playlist, all, Live games or nothing, and marks the favorite it lands on", async ({ page, request }) => {
  const t = (l, id, abbr, name) => ({ l, id, abbr, name });
  await reset(request, {
    favs: [t("nfl", 6, "DAL", "Dallas Cowboys"), t("nba", 6, "DAL", "Dallas Mavericks"), t("ncaa", 251, "TEX", "Texas Longhorns"), {}],
    lists: [{ n: "Sundays", f: [0] }],
  });
  await page.goto("/");
  await tab(page, "device");
  await unfold(page, "#remoteCard");
  const btns = page.locator("#remoteBtns .rbtn");
  await expect(btns).toHaveCount(4);
  const b1 = btns.nth(0).locator("select");
  // Grouped by sport, every league: each league's live games, the sport
  // row with two favorites in it, the favorites, then another team of each
  // league; the pages under Showing last.
  await expect(b1.locator("optgroup")).toHaveCount(8);
  expect(await b1.locator("optgroup").evaluateAll((gs) => gs.map((g) => g.label))).toEqual(
    ["Your playlists", "Football", "Baseball", "Basketball", "Hockey", "Soccer", "Volleyball", "Showing"]);
  await expect(b1.locator("optgroup[label=Football] option")).toHaveText(["All NFL live games", "All college football live games",
    "My football teams", "Dallas Cowboys", "Texas Longhorns", "Another NFL team...", "Another college football team..."]);
  // One basketball favorite: no row of its own.
  await expect(b1.locator("optgroup[label=Basketball] option")).toHaveText(["All NBA live games", "All WNBA live games",
    "All men's college basketball live games", "All women's college basketball live games", "Dallas Mavericks",
    "Another NBA team...", "Another WNBA team...", "Another men's college basketball team...",
    "Another women's college basketball team..."]);
  await expect(b1.locator("optgroup[label=Hockey] option")).toHaveText(["All NHL live games", "Another NHL team..."]);
  await expect(b1.locator("optgroup[label=Volleyball] option")).toHaveText(["All women's college volleyball live games",
    "Another women's college volleyball team..."]);
  await expect(b1.locator("optgroup[label=Showing] option")).toHaveText(["Scoreboard", "Clock", "Smart board", "Effects"]);
  await expect(b1.locator(":scope > option")).toHaveText(["Its own favorite", "All favorites", "Live games", "Nothing"]);
  await expect(b1).toHaveValue("fav");
  await expect(btns.nth(0).locator("label small")).toHaveText("Favorite 1: Dallas Cowboys.");
  await expect(btns.nth(3).locator("label small")).toHaveText("Favorite 4 (empty).");
  // Button 1 to the Longhorns: the Favorites card's mark moves with it.
  await b1.selectOption("team:ncaa:251");
  await expectSet(request, { remote1: "team:ncaa:251" });
  const rows = page.locator("#favCtls .ctl.fav");
  // Button 3's own favorite is the Longhorns too, so both land there.
  await expect(rows.nth(2).locator("small")).toHaveText("College football · remote button 1, 3");
  await expect(rows.nth(0).locator("small")).toHaveText("NFL");
  await expect(btns.nth(0).locator("label small")).toHaveCount(0);
  await btns.nth(1).locator("select").selectOption("sport:football");
  await expectSet(request, { remote2: "sport:football" });
  await btns.nth(2).locator("select").selectOption("list:Sundays");
  await expectSet(request, { remote3: "list:Sundays" });
  await expect(page.locator("#listCtls .plist").nth(0).locator(".pbtn")).toHaveText("Button 3");
  await btns.nth(3).locator("select").selectOption("live");
  await expectSet(request, { remote4: "live" });
  await btns.nth(3).locator("select").selectOption("none");
  await expectSet(request, { remote4: "none" });
  // Making another playlist leaves the buttons as they are.
  await tab(page, "teams");
  await page.locator("#listAdd").click();
  await expectSet(request, { lists: "Sundays=nfl:6;Playlist 2=" });
  const st = await deviceState(request);
  expect(st.remote.map((r) => r.v)).toEqual(["team:ncaa:251", "sport:football", "list:Sundays", "none"]);
});

test("a remote button plays one league's live games, leaving the owner's own leagues alone", async ({ page, request }) => {
  const t = (l, id, abbr, name) => ({ l, id, abbr, name });
  await reset(request, { favs: [t("nfl", 6, "DAL", "Dallas Cowboys"), {}, {}, {}] });
  await page.goto("/");
  await tab(page, "device");
  await unfold(page, "#remoteCard");
  const btns = page.locator("#remoteBtns .rbtn");
  await btns.nth(1).locator("select").selectOption("league:nhl");
  await expectSet(request, { remote2: "league:nhl" });
  await expect(btns.nth(1).locator("label small")).toHaveText("Leaves your own Live games leagues as they are.");
  const st = await deviceState(request);
  expect(st.favs.filter((f) => f.id).length).toBe(1);
  expect(st.remote[1]).toEqual({ v: "league:nhl", gone: false });
});

test("a team that isn't a favorite goes on a button only: marked, counted, out of playlists, and kept as a favorite with a tap", async ({ page, request }) => {
  const t = (l, id, abbr, name) => ({ l, id, abbr, name });
  await reset(request, {
    favs: [t("nfl", 6, "DAL", "Dallas Cowboys"), t("ncaa", 251, "TEX", "Texas Longhorns"), {}, {}],
    lists: [{ n: "Sundays", f: [0] }],
  });
  await page.goto("/");
  await tab(page, "device");
  await unfold(page, "#remoteCard");
  const btns = page.locator("#remoteBtns .rbtn");
  const b1 = btns.nth(0).locator("select").first();
  // Another NFL team opens the NFL's list; nothing is posted until a team is picked.
  await b1.selectOption("pick:nfl");
  const team = btns.nth(0).locator("select.rteam");
  await expect(team).toBeVisible();
  await expect(team.locator("option").first()).toHaveText("Pick a team");
  await expect(team.locator("option", { hasText: "Philadelphia Eagles" })).toHaveCount(1);
  await team.selectOption("nfl:21");
  await expectSet(request, { remote1: "team:nfl:21" });
  let st = await deviceState(request);
  expect(st.favs[2]).toMatchObject({ l: "nfl", id: 21, btn: true });
  // The button shows it as Another NFL team, the Eagles picked.
  await expect(b1).toHaveValue("pick:nfl");
  await expect(btns.nth(0).locator("select.rteam")).toHaveValue("nfl:21");
  await expect(btns.nth(0).locator("label small")).toHaveText("Button only: not in All favorites, playlists or take over.");
  // Button 3's own favorite is that place: it counts as empty.
  await expect(btns.nth(2).locator("label small")).toHaveText("Favorite 3 (empty).");
  // The Favorites card marks it; playlists don't offer it; no Eagles in the football sport row's picks.
  await tab(page, "teams");
  const rows = page.locator("#favCtls .ctl.fav");
  await expect(rows).toHaveCount(3);
  await expect(rows.nth(2)).toHaveClass(/btnonly/);
  await expect(rows.nth(2).locator("small")).toHaveText("NFL · Button only (button 1)");
  await expect(page.locator("#listCtls .plist").nth(0).locator(".pteams .chip")).toHaveCount(2);
  // A tap on it in the team picker keeps it as a favorite.
  await page.locator("#favAdd").click();
  await page.locator("#q").fill("eagles");
  const tile = page.locator("#tiles [data-key='nfl:21']");
  await expect(tile).toHaveAttribute("aria-pressed", "false");
  await tile.click();
  await expectSet(request, { keepfav: "nfl:21" });
  st = await deviceState(request);
  expect(st.favs[2].btn).toBeUndefined();
  await expect(tile).toHaveAttribute("aria-pressed", "true");
});

test("a button-only team leaves the favorites when no button is set to it, and a full list adds none", async ({ page, request }) => {
  const t = (l, id, abbr, name) => ({ l, id, abbr, name });
  await reset(request, {
    favs: [t("nfl", 6, "DAL", "Dallas Cowboys"), {}, {}, {}],
    remote: [{ v: "team:nfl:21" }, { v: "fav" }, { v: "fav" }, { v: "fav" }],
  });
  await request.post("/gameday/set", { form: { remote1: "team:nfl:21" } });
  let st = await deviceState(request);
  expect(st.favs[1]).toMatchObject({ id: 21, btn: true });
  await page.goto("/");
  await tab(page, "device");
  await unfold(page, "#remoteCard");
  const btns = page.locator("#remoteBtns .rbtn");
  await btns.nth(0).locator("select").first().selectOption("live");
  await expectSet(request, { remote1: "live" });
  st = await deviceState(request);
  expect(st.favs.filter((f) => f.id).map((f) => f.id)).toEqual([6]);
  // Sixteen favorites: the picker says so and posts nothing.
  const nfl = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16].map((id) => t("nfl", id, "T" + id, "Team " + id));
  await reset(request, { favs: nfl });
  await page.reload();
  await tab(page, "device");
  await unfold(page, "#remoteCard");
  await btns.nth(0).locator("select").first().selectOption("pick:nfl");
  await btns.nth(0).locator("select.rteam").selectOption("nfl:21");
  await expect(page.locator("#toast")).toContainText("Your favorites are full (16)");
  st = await deviceState(request);
  expect(st.remote[0].v).toBe("fav");
});

test("a remote button picks a page under Showing; an Effects button saves the effect playing now, and Change edits its own without touching the panel", async ({ page, request }) => {
  await reset(request, {});
  // What Effects is set to on the panel now, before the page reads it
  await request.post("/select/Effect/set?option=DNA");
  await request.post("/select/Effect%20Palette/set?option=Ocean");
  await request.post("/number/Effect%20Speed/set?value=40");
  await page.goto("/");
  await tab(page, "device");
  await unfold(page, "#remoteCard");
  const btns = page.locator("#remoteBtns .rbtn");
  await expect(btns).toHaveCount(4);
  await btns.nth(0).locator("select").selectOption("clock");
  await expectSet(request, { remote1: "clock" });
  await btns.nth(1).locator("select").selectOption("smartboard");
  await expectSet(request, { remote2: "smartboard" });
  await btns.nth(2).locator("select").selectOption("scoreboard");
  await expectSet(request, { remote3: "scoreboard" });
  await btns.nth(3).locator("select").selectOption("fx");
  await expectSet(request, { remote4: "fx:DNA,Ocean,40,128" });
  const b4 = btns.nth(3).locator("select");
  await expect(b4).toHaveValue("fx:DNA,Ocean,40,128");
  await expect(b4.locator("option:checked")).toHaveText("Effects: DNA, Ocean");
  // Change opens the button's own controls; editing them changes the button only.
  const before = (await entityPosts(request)).length;
  const change = page.locator("#remoteBtns .rfx .linkbtn");
  await expect(change).toHaveText("Change");
  await expect(page.locator("#remoteBtns .rfxedit")).toHaveCount(0);
  await change.click();
  const edit = page.locator("#remoteBtns .rfxedit");
  await expect(edit).toBeVisible();
  await edit.locator("select").nth(0).selectOption("Snow Fall");
  await expectSet(request, { remote4: "fx:Snow Fall,Ocean,40,128" });
  await edit.locator("select").nth(1).selectOption("Red & Blue");
  await expectSet(request, { remote4: "fx:Snow Fall,Red & Blue,40,128" });
  await edit.locator("input[type=range]").nth(1).fill("200");
  await expectSet(request, { remote4: "fx:Snow Fall,Red & Blue,40,200" });
  expect((await entityPosts(request)).length).toBe(before);
  const st = await deviceState(request);
  expect(st.remote.map((r) => r.v)).toEqual(["clock", "smartboard", "scoreboard", "fx:Snow Fall,Red & Blue,40,200"]);
  await expect(b4.locator("option:checked")).toHaveText("Effects: Snow Fall, Red & Blue");
  await page.locator("#remoteBtns .rfx .linkbtn").click();
  await expect(page.locator("#remoteBtns .rfxedit")).toHaveCount(0);
});

test("a remote button whose target is gone falls back to its own favorite and the page says so", async ({ page, request }) => {
  const t = (l, id, abbr, name) => ({ l, id, abbr, name });
  await reset(request, {
    favs: [t("nfl", 6, "DAL", "Dallas Cowboys"), t("nfl", 34, "HOU", "Houston Texans"), {}, {}],
    lists: [{ n: "Sundays", f: [0] }],
    remote: [{ v: "team:nba:6" }, { v: "list:Weekdays" }, { v: "sport:hockey" }, { v: "fav" }],
  });
  await page.goto("/");
  await tab(page, "device");
  await unfold(page, "#remoteCard");
  const btns = page.locator("#remoteBtns .rbtn");
  await expect(btns.nth(0).locator("label small")).toHaveText("That team left your favorites, so it is favorite 1: dallas cowboys for now.");
  await expect(btns.nth(1).locator("label small")).toHaveText("That playlist is gone, so it is favorite 2: houston texans for now.");
  await expect(btns.nth(2).locator("label small")).toHaveText("No favorites in that sport now, so it is favorite 3 (empty) for now.");
  await expect(btns.nth(0).locator("select")).toHaveValue("team:nba:6");
  await expect(btns.nth(0).locator("select option").last()).toHaveText("nba:6 (gone)");
  // The favorites the buttons land on for now carry their marks.
  const rows = page.locator("#favCtls .ctl.fav");
  await expect(rows.nth(0).locator("small")).toHaveText("NFL · remote button 1");
  await expect(rows.nth(1).locator("small")).toHaveText("NFL · remote button 2");
  // Set to the Texans, button 1 is back in use.
  await btns.nth(0).locator("select").selectOption("team:nfl:34");
  await expectSet(request, { remote1: "team:nfl:34" });
  await expect(rows.nth(1).locator("small")).toHaveText("NFL · remote button 1, 2");
});

test("a remote button picks an effect playlist; an emptied or deleted one says so", async ({ page, request }) => {
  const fx = (e) => ({ e, p: "Default", s: 128, i: 128, w: 15 });
  await reset(request, {
    favs: [{ l: "nfl", id: 6, abbr: "DAL", name: "Dallas Cowboys" }, {}, {}, {}],
    lists: [{ n: "Party", f: [] }],
    fxlists: [{ n: "Party", shuffle: false, fx: [fx("Matrix"), fx("DNA")] }, { n: "Calm", shuffle: true, fx: [fx("Drift")] },
      { n: "Empty", shuffle: false, fx: [] }],
  });
  await page.goto("/");
  await tab(page, "device");
  await unfold(page, "#remoteCard");
  const btns = page.locator("#remoteBtns .rbtn");
  const b1 = btns.nth(0).locator("select");
  // Under Showing, in full, beside the team playlist of the same name; one with no effects isn't offered.
  const showing = b1.locator('optgroup[label="Showing"] option');
  await expect(showing).toHaveText(["Scoreboard", "Clock", "Smart board", "Effects", "Effect playlist: Party", "Effect playlist: Calm"]);
  await expect(b1.locator('optgroup[label="Your playlists"] option')).toHaveText(["Party"]);
  await b1.selectOption("fxlist:Party");
  await expectSet(request, { remote1: "fxlist:Party" });
  await btns.nth(1).locator("select").selectOption("fxlist:Calm");
  await expectSet(request, { remote2: "fxlist:Calm" });
  await expect.poll(async () => (await deviceState(request)).remote.slice(0, 2)).toEqual(
    [{ v: "fxlist:Party", gone: false }, { v: "fxlist:Calm", gone: false }]);
  await expect(b1).toHaveValue("fxlist:Party");
  await expect(b1.locator("option:checked")).toHaveText("Effect playlist: Party");
  // The effect playlist's row says which button plays it.
  await tab(page, "customize");
  await expect(page.locator("#fxListCtls .fxlist").nth(0).locator(".pbtn")).toHaveText("Button 1");
  await expect(page.locator("#fxListCtls .fxlist").nth(1).locator(".pbtn")).toHaveText("Button 2");
  // Calm emptied and Party deleted: both buttons fall back to their own favorites, and say so.
  await request.post("/gameday/set", { form: { fxlists: "Calm=;Empty=" } });
  await tab(page, "device");
  await expect(btns.nth(0).locator("label small")).toHaveText("That effect playlist is gone or has no effects, so it is favorite 1: dallas cowboys for now.");
  await expect(btns.nth(1).locator("label small")).toHaveText("That effect playlist is gone or has no effects, so it is favorite 2 (empty) for now.");
  await expect(b1).toHaveValue("fxlist:Party");
  await expect(b1.locator("option:checked")).toHaveText("Effect playlist: Party (gone)");
});

test("a remote button set to an effect playlist follows it through a rename", async ({ page, request }) => {
  await reset(request, {
    fxlists: [{ n: "Party", shuffle: false, fx: [{ e: "Matrix", p: "Default", s: 128, i: 128, w: 15 }] }],
    remote: [{ v: "fav" }, { v: "fav" }, { v: "fxlist:Party" }, { v: "fav" }],
  });
  await page.goto("/");
  await tab(page, "customize");
  const name = page.locator("#fxListCtls .fxlist").nth(0).locator(".pname");
  await name.fill("Disco");
  await name.press("Enter");
  await expect.poll(async () => (await deviceState(request)).remote[2]).toEqual({ v: "fxlist:Disco", gone: false });
  await tab(page, "device");
  await unfold(page, "#remoteCard");
  await expect(page.locator("#remoteBtns .rbtn").nth(2).locator("select")).toHaveValue("fxlist:Disco");
});

test("a remote button set to a playlist follows it through a rename", async ({ page, request }) => {
  const t = (l, id, abbr, name) => ({ l, id, abbr, name });
  await reset(request, {
    favs: [t("nfl", 6, "DAL", "Dallas Cowboys"), t("nfl", 34, "HOU", "Houston Texans"), {}, {}],
    lists: [{ n: "Sundays", f: [0] }, { n: "Texans", f: [1] }],
    remote: [{ v: "list:Sundays" }, { v: "fav" }, { v: "fav" }, { v: "fav" }],
  });
  await page.goto("/");
  await tab(page, "teams");
  const name = page.locator("#listCtls .plist").nth(0).locator(".pname");
  await name.fill("Game day");
  await name.press("Enter");
  await expectSet(request, { lists: "Game day=nfl:6;Texans=nfl:34" });
  await expect.poll(async () => (await deviceState(request)).remote[0]).toEqual({ v: "list:Game day", gone: false });
  await tab(page, "device");
  await unfold(page, "#remoteCard");
  await expect(page.locator("#remoteBtns .rbtn").nth(0).locator("select")).toHaveValue("list:Game day");
});

// A restored backup or fav1=none leaves an empty slot ahead of the teams;
// playlists count places in the slot list, gaps and all.
test("a gap in the favorites keeps each playlist's teams", async ({ page, request }) => {
  const t = (l, id, abbr, name) => ({ l, id, abbr, name });
  await reset(request, {
    favs: [{}, t("nfl", 6, "DAL", "Dallas Cowboys"), t("nfl", 34, "HOU", "Houston Texans"), {}],
    lists: [{ n: "Both", f: [1, 2] }],
  });
  await page.goto("/");
  await tab(page, "teams");
  const chips = page.locator("#listCtls .plist").nth(0).locator(".pteams .chip");
  await expect(chips).toHaveText(["DAL", "HOU"]);
  await expect(page.locator("#listCtls .plist .pteams .chip.on")).toHaveText(["DAL", "HOU"]);
  await chips.filter({ hasText: "HOU" }).click();
  await expectSet(request, { lists: "Both=nfl:6" });
  await expect(page.locator("#listCtls .plist .pteams .chip.on")).toHaveText(["DAL"]);
});

// All 16 playlists with every favorite are about 3 KB, far past the panel's
// 512-byte URL and its 1 KB form body; the mock refuses both as the panel does.
test("all 16 playlists with every favorite save", async ({ page, request }) => {
  const favs = Array.from({ length: 16 }, (_, i) => ({ l: "nfl", id: i + 1, abbr: "T" + (i + 1), name: "Team " + (i + 1) }));
  const all = Array.from({ length: 16 }, (_, i) => i);
  const keys = favs.map((f) => "nfl:" + f.id).join(",");
  await reset(request, { favs, lists: Array.from({ length: 15 }, (_, i) => ({ n: "Playlist " + (i + 1), f: all })) });
  await page.goto("/");
  await tab(page, "teams");
  await expect(page.locator("#listCtls .plist")).toHaveCount(15);
  await page.locator("#listAdd").click();
  const want = Array.from({ length: 15 }, (_, i) => "Playlist " + (i + 1) + "=" + keys).join(";") + ";Playlist 16=";
  await expectSet(request, { lists: want });
  await page.locator("#listCtls .plist").nth(15).locator(".pteams .chip").first().click();
  await expectSet(request, { lists: want + "nfl:1" });
  await expect(page.locator("#toast")).not.toContainText("did not respond");
});

test("a playlist needs no favorites: sports alone", async ({ page, request }) => {
  await reset(request, { favs: [{}, {}, {}, {}] });
  await page.goto("/");
  await tab(page, "teams");
  await expect(page.locator("#listCtls")).toHaveText("No playlists yet.");
  await page.locator("#listAdd").click();
  await expectSet(request, { lists: "Playlist 1=" });
  const box = page.locator("#listCtls .plist").nth(0);
  await expect(box.locator(".pteams .chip")).toHaveCount(0);
  await box.getByRole("group", { name: "Sports in Playlist 1" }).getByRole("button", { name: "NHL" }).click();
  await expectSet(request, { lists: "Playlist 1=,@lg:nhl" });
  await expect(box.locator(".psports small")).toHaveText("Plays live NHL games. Add teams to have them take over while they play.");
});

test("a playlist's sports post as @lg tokens beside its teams, and the bar says it plays their live games", async ({ page, request }) => {
  await reset(request, {
    mode: 4,
    favs: [
      { l: "nfl", id: 6, abbr: "DAL", name: "Dallas Cowboys" },
      { l: "nba", id: 6, abbr: "DAL", name: "Dallas Mavericks" },
      {},
      {},
    ],
    lists: [{ n: "Sunday", f: [0, 1] }],
    list: 1,
  });
  await page.goto("/");
  await tab(page, "teams");
  const box = page.locator("#listCtls .plist").nth(0);
  const sports = box.getByRole("group", { name: "Sports in Sunday" }).getByRole("button");
  // Every league the panel knows, in kLeagues order.
  await expect(sports).toHaveText(["NFL", "College football", "MLB", "MLS", "Premier League", "NBA", "WNBA",
    "Men's college basketball", "Women's college basketball", "NHL", "Women's college volleyball"]);
  await expect(box.locator(".psports small")).toHaveCount(0);
  await expect(box.getByRole("switch", { name: "Only this list's games Sunday" })).toBeVisible();
  await box.getByRole("button", { name: "NFL", exact: true }).click();
  await expectSet(request, { lists: "Sunday=nfl:6,nba:6,@lg:nfl" });
  await expect(box.getByRole("button", { name: "NFL", exact: true })).toHaveAttribute("aria-pressed", "true");
  await expect(box.locator(".psports small")).toHaveText("Plays live NFL games. Its teams take over while their game is on.");
  // Favorite teams' own rules don't apply to live games.
  await expect(box.getByRole("switch", { name: "Only this list's games Sunday" })).toHaveCount(0);
  await expect(box.getByLabel("Only today's games Sunday")).toHaveCount(0);
  // A second sport goes in kLeagues order, after the options and look.
  await box.getByRole("button", { name: "Blue" }).click();
  await expectSet(request, { lists: "Sunday=nfl:6,nba:6,@c:blue,@lg:nfl" });
  await box.getByRole("button", { name: "NHL" }).click();
  await expectSet(request, { lists: "Sunday=nfl:6,nba:6,@c:blue,@lg:nfl,@lg:nhl" });
  const st = await deviceState(request);
  expect(st.lists[0].lg).toEqual(["nfl", "nhl"]);
  // An app that doesn't know sports yet sends none: the playlist keeps its own.
  await request.post("/gameday/settings", { data: { schema: 1, lists: [{ n: "Sunday", t: ["nfl:6"] }] } });
  expect((await deviceState(request)).lists[0].lg).toEqual(["nfl", "nhl"]);
  await request.post("/gameday/settings", { data: { schema: 1, lists: [{ n: "Sunday", t: ["nfl:6", "nba:6"], color: "blue", lg: ["nfl", "nhl"] }] } });
  await tab(page, "board");
  await expect(page.locator("#favName")).toHaveText("Playing your Sunday playlist: live NFL and NHL games, and its teams while they play");
  // Taking the sports back out makes it a playlist of teams again.
  await tab(page, "teams");
  await box.getByRole("button", { name: "NFL", exact: true }).click();
  await expectSet(request, { lists: "Sunday=nfl:6,nba:6,@c:blue,@lg:nhl" });
  await box.getByRole("button", { name: "NHL" }).click();
  await expectSet(request, { lists: "Sunday=nfl:6,nba:6,@c:blue" });
  await expect(box.getByRole("switch", { name: "Only this list's games Sunday" })).toBeVisible();
});

test("Favorite teams plays a playlist: the picker posts it, the bar names it, the others dim", async ({ page, request }) => {
  const t = (abbr, name, id) => ({ l: "nfl", id, abbr, name });
  await reset(request, {
    mode: 4,
    lists: [{ n: "Texans", f: [1] }],
    list: 0,
    next: [
      { slot: 1, t: t("DAL", "Dallas Cowboys", 6), id: "1", s: "PRE", kick: 1792531500, oi: 21, oa: "PHI", ts: 0, os: 0, tv: "FOX", detail: "" },
      { slot: 2, t: t("HOU", "Houston Texans", 34), id: "5", s: "PRE", kick: 1792620000, oi: 30, oa: "JAX", ts: 0, os: 0, tv: "CBS", detail: "" },
    ],
  });
  await page.goto("/");
  await expect(page.locator("#favListRow")).toBeVisible();
  await expect(page.locator("#favList option")).toHaveText(["All favorites", "Texans"]);
  await expect(page.locator("#favName")).toHaveText("None of your teams are playing. Showing their next games.");
  await expect(page.locator("#upnextList .un.out")).toHaveCount(0);
  await page.locator("#favList").selectOption("1");
  await expectSet(request, { mode: "4", list: "1", sport: "none" });
  await expect(page.locator("#favName")).toHaveText("Playing your Texans playlist");
  await expect(page.locator("#upnextList .un.out .n")).toHaveText(["DAL vs PHI"]);
});

test("Playing has a row per sport after All favorites, then the owner's playlists", async ({ page, request }) => {
  const t = (l, abbr, name, id) => ({ l, id, abbr, name });
  await reset(request, {
    mode: 4,
    favs: [t("nfl", "DAL", "Dallas Cowboys", 6), t("nba", "DAL", "Dallas Mavericks", 6), t("ncaa", "TEX", "Texas Longhorns", 251), {}],
    lists: [{ n: "Football", f: [0] }],
    list: 1,
    next: [
      { slot: 1, t: t("nfl", "DAL", "Dallas Cowboys", 6), id: "1", s: "PRE", kick: 1792531500, oi: 21, oa: "PHI", ts: 0, os: 0, tv: "FOX", detail: "" },
      { slot: 2, t: t("nba", "DAL", "Dallas Mavericks", 6), id: "2", s: "PRE", kick: 1792540000, oi: 13, oa: "LAL", ts: 0, os: 0, tv: "ESPN", detail: "" },
      { slot: 3, t: t("ncaa", "TEX", "Texas Longhorns", 251), id: "3", s: "PRE", kick: 1792620000, oi: 333, oa: "ALA", ts: 0, os: 0, tv: "ABC", detail: "" },
    ],
  });
  await page.goto("/");
  await expect(page.locator("#favList option")).toHaveText(["All favorites", "All football", "All basketball", "Football"]);
  await expect(page.locator("#favList")).toHaveValue("1");
  await page.locator("#favList").selectOption("s:basketball");
  await expectSet(request, { mode: "4", list: "0", sport: "basketball" });
  await expect(page.locator("#favList")).toHaveValue("s:basketball");
  await expect(page.locator("#favName")).toHaveText("None of your basketball teams are playing. Showing their next games.");
  await expect(page.locator("#upnextList .un.out .n")).toHaveText(["DAL vs PHI", "TEX vs ALA"]);
  expect((await deviceState(request)).sport).toBe("basketball");
  // The sport rows aren't playlists: nothing in the Playlists card for them.
  await expect(page.locator("#listCtls .plist")).toHaveCount(1);
  await page.locator("#favList").selectOption("0");
  await expectSet(request, { mode: "4", list: "0", sport: "none" });
  await expect(page.locator("#favName")).toHaveText("None of your teams are playing. Showing their next games.");
  expect((await deviceState(request)).sport).toBe("");
});

test("sport rows come and go with the favorites", async ({ page, request }) => {
  const t = (l, abbr, name, id) => ({ l, id, abbr, name });
  await reset(request, { mode: 4, favs: [t("nfl", "DAL", "Dallas Cowboys", 6), t("ncaa", "TEX", "Texas Longhorns", 251), {}, {}] });
  await page.goto("/");
  // One sport: All favorites already is it, so there are no rows and no picker.
  await expect(page.locator("#favbar")).toBeVisible();
  await expect(page.locator("#favListRow")).toBeHidden();
  await request.post("/gameday/set?favs=nfl:6,ncaa:251,nhl:25");
  await page.reload();
  await expect(page.locator("#favListRow")).toBeVisible();
  await expect(page.locator("#favList option")).toHaveText(["All favorites", "All football", "All hockey"]);
  await page.locator("#favList").selectOption("s:hockey");
  await expectSet(request, { mode: "4", list: "0", sport: "hockey" });
  // The hockey favorite goes: so does its row, and every favorite plays.
  await request.post("/gameday/set?favs=nfl:6,ncaa:251");
  await page.reload();
  await expect(page.locator("#favListRow")).toBeHidden();
  expect((await deviceState(request)).sport).toBe("");
});

test("the playing picker is hidden without playlists", async ({ page, request }) => {
  await reset(request, { mode: 4 });
  await page.goto("/");
  await expect(page.locator("#favbar")).toBeVisible();
  await expect(page.locator("#favListRow")).toBeHidden();
});

test("the favorites picker stays open: checks, a tap adds or takes out, a count, Done", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await tab(page, "teams");
  await expect(page.locator("#favCtls .ctl.fav")).toHaveCount(3);
  await page.locator("#favAdd").click();
  await expect(page.locator("#modal")).toHaveClass(/open/);
  await expect(page.locator("#favCount")).toHaveText("3 of 16 favorites");
  await expect(page.locator("#close")).toHaveText("Done");
  await page.locator(".tabs button", { hasText: "NFL" }).click();
  // Teams already in the list show checked.
  const dal = page.locator("#tiles .tile", { hasText: "Dallas Cowboys" });
  await expect(dal).toHaveClass(/\bon\b/);
  await expect(dal).toHaveAttribute("aria-pressed", "true");

  const gb = page.locator("#tiles .tile", { hasText: "Green Bay Packers" });
  await expect(gb).toHaveAttribute("aria-pressed", "false");
  await gb.click();
  await expectSet(request, { favs: "nfl:6,nfl:34,ncaa:145,nfl:9" });
  await expect(page.locator("#modal")).toHaveClass(/open/);
  await expect(gb).toHaveAttribute("aria-pressed", "true");
  await expect(page.locator("#favCount")).toHaveText("4 of 16 favorites");
  await expect(page.locator("#toast")).toHaveText("Added the Green Bay Packers");

  // A tap on a checked team takes it out.
  await dal.click();
  await expectSet(request, { favs: "nfl:34,ncaa:145,nfl:9" });
  await expect(dal).toHaveAttribute("aria-pressed", "false");
  await expect(page.locator("#favCount")).toHaveText("3 of 16 favorites");
  await expect(page.locator("#toast")).toHaveText("Removed the Dallas Cowboys");

  await page.locator("#close").click();
  await expect(page.locator("#modal")).not.toHaveClass(/open/);
  await expect(page.locator("#favCtls .ctl.fav label")).toHaveText([/^1\. Houston Texans/, /^2\. Ole Miss/, /^3\. Green Bay Packers/]);
});

test("the favorites picker says when the list is full", async ({ page, request }) => {
  const mlb = (n) => Array.from({ length: n }, (_, i) => ({ l: "mlb", id: i + 1 }));
  await reset(request, { favs: mlb(15) });
  await page.goto("/");
  await tab(page, "teams");
  await page.locator("#favAdd").click();
  await expect(page.locator("#favCount")).toHaveText("15 of 16 favorites");
  await page.locator(".tabs button", { hasText: "NFL" }).click();
  await page.locator("#tiles .tile", { hasText: "Dallas Cowboys" }).click();
  await expect(page.locator("#favCount")).toHaveText("16 of 16 favorites");
  const before = (await sets(request)).length;
  await page.locator("#tiles .tile", { hasText: "Green Bay Packers" }).click();
  await expect(page.locator("#toast")).toHaveText("That's the most favorites the panel holds");
  await sleep(500);
  expect((await sets(request)).length).toBe(before);
});

test("the favorites list stops offering Add a favorite at the panel's limit", async ({ page, request }) => {
  const mlb = (n) => Array.from({ length: n }, (_, i) => ({ l: "mlb", id: i + 1 }));
  await reset(request, { favs: mlb(15) });
  await page.goto("/");
  await tab(page, "teams");
  await expect(page.locator("#favCtls .ctl.fav")).toHaveCount(15);
  await expect(page.locator("#favAdd")).toBeVisible();

  await reset(request, { favs: mlb(16) });
  await page.reload();
  await expect(page.locator("#favCtls .ctl.fav")).toHaveCount(16);
  await expect(page.locator("#favAdd")).toBeHidden();
  // Past the four remote slots a row has no remote button.
  await expect(page.locator("#favCtls .ctl.fav").nth(4).locator("small")).toHaveText("MLB");
});

test("Live games chips post the leagues, and move an old live mode to mode 5", async ({ page, request }) => {
  await reset(request, { mode: 5, live: ["mlb", "nhl"], live_auto: false });
  await page.goto("/");
  await expect(page.locator("#livebar")).toBeVisible();
  await expect(page.locator("#modes button.on")).toHaveText(/^Live games/);
  await expect(page.locator("#liveChips .chip")).toHaveText(["NFL", "NCAAF", "MLB", "MLS", "EPL", "NBA", "WNBA", "NCAAM", "NCAAW", "NHL", "NCAAVB"]);
  await expect(page.locator("#liveChips .chip.on")).toHaveText(["MLB", "NHL"]);
  await page.locator("#liveChips .chip", { hasText: /^NBA$/ }).click();
  await expectSet(request, { live: "mlb,nhl,nba" });
  await expect(page.locator("#liveChips .chip.on")).toHaveText(["MLB", "NBA", "NHL"]);
  // The re-read after the tap updates the chips in place, so focus stays put.
  await expect(page.locator("#liveChips .chip", { hasText: /^NBA$/ })).toBeFocused();

  // The last league can't be switched off.
  await reset(request, { mode: 5, live: ["mlb"], live_auto: false });
  await page.reload();
  await page.locator("#liveChips .chip", { hasText: /^MLB$/ }).click();
  await expect(page.locator("#toast")).toHaveText("Pick at least one league");
  expect(await sets(request)).toEqual([]);

  // Mode 2 (live college football, from before) reads as Live games with NCAAF on.
  await reset(request, { mode: 2 });
  await page.reload();
  await expect(page.locator("#modes button.on")).toHaveText(/^Live games/);
  await expect(page.locator("#liveChips .chip.on")).toHaveText(["NCAAF"]);
  await page.locator("#liveChips .chip", { hasText: /^NFL$/ }).click();
  await expectSet(request, { live: "ncaa,nfl", mode: "5" });
});

const chip = (page, tab) => page.locator("#liveChips .chip", { hasText: new RegExp("^" + tab + "$") });
const chipsOn = (page) => page.locator("#liveChips .chip.on");

test("Live games chips match the panel through taps, a slow re-read, mode switches and a reload", async ({ page, request }) => {
  // The steps from the reported bug: an old Live college panel, MLB on, NCAAF off,
  // Favorite teams and back. The re-read after each tap is still out when
  // the next tap comes.
  await reset(request, { mode: 2 });
  await page.goto("/");
  await expect(chipsOn(page)).toHaveText(["NCAAF"]);
  await slowReads(page, 1000);
  await chip(page, "MLB").click();
  await expect(chipsOn(page)).toHaveText(["NCAAF", "MLB"]);
  await sleep(600);
  await chip(page, "NCAAF").click();
  await expect(chipsOn(page)).toHaveText(["MLB"]);
  await staysAs(chipsOn(page), ["MLB"], 2500);

  await page.locator('#modes button[data-mode="4"]').click();
  await expect(page.locator("#favbar")).toBeVisible();
  await sleep(600);
  await page.locator('#modes button[data-mode="5"]').click();
  await expect(page.locator("#livebar")).toBeVisible();
  await staysAs(chipsOn(page), ["MLB"], 2500);

  expect(await sets(request)).toEqual([{ live: "ncaa,mlb", mode: "5" }, { live: "mlb" }, { mode: "4" }, { mode: "5" }]);
  const st = await deviceState(request);
  expect([st.mode, st.live]).toEqual([5, ["mlb"]]);
  await page.reload();
  await expect(chipsOn(page)).toHaveText(["MLB"]);
});

test("chip taps faster than the page re-reads each save the list on screen", async ({ page, request }) => {
  await reset(request, { mode: 5, live: ["mlb"], live_auto: false });
  await page.goto("/");
  await expect(chipsOn(page)).toHaveText(["MLB"]);
  await slowReads(page, 1000);
  await chip(page, "NBA").click();
  await chip(page, "NHL").click();
  await chip(page, "MLB").click();
  await expect(chipsOn(page)).toHaveText(["NBA", "NHL"]);
  await expect.poll(() => sets(request)).toEqual([{ live: "mlb,nba" }, { live: "mlb,nba,nhl" }, { live: "nba,nhl" }]);
  await staysAs(chipsOn(page), ["NBA", "NHL"], 2500);
  expect((await deviceState(request)).live).toEqual(["nba", "nhl"]);
});

test("Live games bar says when the leagues are automatic, and goes back to automatic", async ({ page, request }) => {
  const lg = page.locator("#liveLg");
  const back = page.locator("#liveAuto");
  const AUTO = "Leagues follow your favorites. Tap one to choose by hand.";
  // Nothing picked: the leagues of the team and favorites (NFL and NCAAF in the mock), drawn as a suggestion.
  await reset(request, { mode: 5 });
  await page.goto("/");
  await expect(lg).toHaveText(AUTO);
  await expect(page.locator("#liveChips")).toHaveClass(/auto/);
  await expect(chipsOn(page)).toHaveText(["NFL", "NCAAF"]);
  await expect(back).toBeHidden();

  // The first tap posts the whole list with the tap applied, and the bar
  // turns manual before the device answers.
  await slowReads(page, 1000);
  await chip(page, "MLB").click();
  await expect(lg).toBeHidden();
  await expect(page.locator("#liveChips")).not.toHaveClass(/auto/);
  await expect(back).toBeVisible();
  await expectSet(request, { live: "nfl,ncaa,mlb" });
  await staysAs(lg, [""], 1500);
  expect((await deviceState(request)).live_auto).toBe(false);

  // Back to automatic posts an empty list and the device works the leagues out again.
  await back.click();
  await expectSet(request, { live: "" });
  await expect(lg).toHaveText(AUTO);
  await expect(back).toBeHidden();
  await staysAs(lg, [AUTO], 1500);
  await expect(chipsOn(page)).toHaveText(["NFL", "NCAAF"]);
  const st = await deviceState(request);
  expect([st.live_auto, st.live]).toEqual([true, ["nfl", "ncaa"]]);
  expect(await sets(request)).toEqual([{ live: "nfl,ncaa,mlb" }, { live: "" }]);

  // An old football live mode has fixed leagues: no automatic wording, no link.
  await reset(request, { mode: 2 });
  await page.reload();
  await expect(lg).toBeHidden();
  await expect(back).toBeHidden();
});

test("Pick the best game switch posts and changes what the Live games bar says", async ({ page, request }) => {
  const name = page.locator("#liveName");
  const sw = page.locator('.sw[data-set="best"]');
  await reset(request, { mode: 5, live: ["mlb"], live_auto: false });
  await page.goto("/");
  // The switch is in the Teams tab's Live games card; the bar stays on the Home tab.
  await tab(page, "teams");
  // Off to start, and the line under it says what each side does.
  await expect(sw).toHaveAttribute("aria-checked", "false");
  const hint = sw.locator("xpath=following-sibling::small");
  await expect(hint).toContainText("Off, a random live game");
  await expect(hint).toContainText("On, the closest game with the least time left");
  await expect(name).toHaveText("Showing a random live game. A new one every minute, or when it ends.");
  await sw.click();
  await expectSet(request, { best: "1" });
  await expect(sw).toHaveAttribute("aria-checked", "true");
  await expect(name).toHaveText("Showing the best game on. It moves only at a break in play.");
  expect((await deviceState(request)).best).toBe(true);
  await sw.click();
  await expectSet(request, { best: "0" });
  await expect(sw).toHaveAttribute("aria-checked", "false");
  await expect(name).toHaveText("Showing a random live game. A new one every minute, or when it ends.");
  await expect(page.locator("#liveLg")).toBeHidden();
  expect((await deviceState(request)).best).toBe(false);
  // Off, the line names Switch every's minutes.
  await reset(request, { mode: 5, live: ["mlb"], live_auto: false, rotate: 5 });
  await page.reload();
  await expect(name).toHaveText("Showing a random live game. A new one every 5 minutes, or when it ends.");
});

test("Favorites take over switch posts, shows the two-favorites rule, and the bar names the favorite", async ({ page, request }) => {
  const name = page.locator("#liveName");
  const sw = page.locator('.sw[data-set="favtake"]');
  const row = page.locator("#liveCollideRow");
  await reset(request, { mode: 5, live: ["nfl"], live_auto: false });
  await page.goto("/");
  await tab(page, "teams");
  await expect(sw).toHaveAttribute("aria-checked", "false");
  await expect(row).toBeHidden();
  await sw.click();
  await expectSet(request, { favtake: "1" });
  await expect(sw).toHaveAttribute("aria-checked", "true");
  await expect(row).toBeVisible();
  // The same rule as Favorite teams mode's Two games at once.
  await page.locator("#liveCollide").selectOption("1");
  await expectSet(request, { collide: "1" });
  expect((await deviceState(request)).collide).toBe(1);

  // A favorite's game has the board.
  await reset(request, { mode: 5, live: ["nfl"], live_auto: false, favtake: true,
    favlock: { l: "nfl", id: 6, abbr: "DAL", name: "Dallas Cowboys" } });
  await page.reload();
  await expect(name).toHaveText("Dallas Cowboys are on, so the panel stays on the game until it ends");
  await expect(page.locator("#liveLg")).toBeHidden();
});

test("Live games bar says when nothing is live and the panel shows your first favorite or finals", async ({ page, request }) => {
  const name = page.locator("#liveName");
  await reset(request, { mode: 5, live: ["mlb"], live_auto: false, best: true });
  await page.goto("/");
  await expect(name).toHaveText("Showing the best game on. It moves only at a break in play.");

  await reset(request, { mode: 5, live: ["mlb"], live_auto: false, fallback: true });
  await page.reload();
  await expect(name).toHaveText("Nothing live right now. Showing your team until a game starts.");
  await expect(page.locator("#liveLg")).toBeHidden();

  await reset(request, { mode: 5, favs: [{}, {}, {}, {}], recap: true, game: { s: "POST", l: "mlb" } });
  await page.reload();
  await expect(name).toHaveText("Nothing live right now. Showing recent finals.");
  await expect(page.locator("#liveLg")).toHaveText("Leagues follow your favorites. Tap one to choose by hand.");
});

test("a slow settings post isn't overtaken by the next tap's", async ({ page, request }) => {
  await reset(request, { mode: 5, live: ["mlb"], live_auto: false });
  await page.goto("/");
  await expect(chipsOn(page)).toHaveText(["MLB"]);
  await slowFirstPost(page, 800);
  await chip(page, "NBA").click();
  await chip(page, "NHL").click();
  await expect.poll(() => sets(request)).toEqual([{ live: "mlb,nba" }, { live: "mlb,nba,nhl" }]);
  expect((await deviceState(request)).live).toEqual(["mlb", "nba", "nhl"]);
  await expect(chipsOn(page)).toHaveText(["MLB", "NBA", "NHL"]);
});

test("switching modes away and back before the page re-reads saves the last mode", async ({ page, request }) => {
  await reset(request, { mode: 5, live: ["mlb"], live_auto: false });
  await page.goto("/");
  await expect(page.locator("#livebar")).toBeVisible();
  await slowReads(page, 1000);
  // Straight through: each tap's bar shows at once, before any re-read.
  await page.locator('#modes button[data-mode="4"]').click();
  expect(await page.locator("#favbar").isVisible()).toBe(true);
  await page.locator('#modes button[data-mode="5"]').click();
  expect(await page.locator("#livebar").isVisible()).toBe(true);
  await page.locator('#modes button[data-mode="4"]').click();
  expect(await page.locator("#favbar").isVisible()).toBe(true);
  await page.locator('#modes button[data-mode="5"]').click();
  expect(await page.locator("#livebar").isVisible()).toBe(true);
  await expect.poll(() => sets(request)).toEqual([{ mode: "4" }, { mode: "5" }, { mode: "4" }, { mode: "5" }]);
  await staysAs(page.locator("#modes button.on"), ["Live gamesGames on right now, in leagues you pick"], 2500);
  await expect(chipsOn(page)).toHaveText(["MLB"]);
  expect((await deviceState(request)).mode).toBe(5);
});

test("a chip tap the panel never got goes back to what the panel has", async ({ page, request }) => {
  await reset(request, { mode: 2 }, ["set"]);
  await page.goto("/");
  await expect(chipsOn(page)).toHaveText(["NCAAF"]);
  await chip(page, "MLB").click();
  await expect(page.locator("#toast")).toHaveText("Device did not respond (HTTP 500). Try again.");
  await expect(chipsOn(page)).toHaveText(["NCAAF"]);
  await expect(page.locator("#modes button.on")).toHaveText(/^Live games/);
  expect((await deviceState(request)).mode).toBe(2);
});

test("install updates automatically: off by default, posts 0 and 1, hidden when the firmware has no such setting", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await tab(page, "device");
  const sw = page.locator('.sw[data-set="autoupd"]');
  await expect(page.locator("#autoupdRow")).toBeVisible();
  await expect(sw).not.toHaveClass(/\bon\b/);
  await sw.click();
  await expectSet(request, { autoupd: "1" });
  await expect(sw).toHaveClass(/\bon\b/);
  await sw.click();
  await expectSet(request, { autoupd: "0" });

  await reset(request, { autoupd: null });
  await page.goto("/");
  await tab(page, "device");
  await expect(page.locator("#hostname")).not.toBeEmpty();
  await expect(page.locator("#autoupdRow")).toBeHidden();
});

test("between games: a row per card that is on, in the panel's order, the rest behind Add a card, each posting cards=", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await tab(page, "display");
  await expect(page.locator("#cdCard")).toBeVisible();
  await expect(page.locator("#cardsOld")).toBeHidden();
  const want = (await deviceState(request)).cards;
  // Every card on to start but news and weather (on once a town is set) and the Game Day card.
  expect(want.filter((c) => !c.on).map((c) => c.id)).toEqual(["news", "weather", "gamedaycard"]);
  // The cards you make are switched on the Customize tab, so the list only names them.
  const own = ["custom", "message", "picture"];
  const shown = want.filter((c) => c.on && !own.includes(c.id));
  const rows = page.locator("#cardList .xrow");
  await expect(rows).toHaveCount(shown.length);
  await expect(rows.locator(".xname > span")).toHaveText(shown.map((c) => c.name));
  await expect(page.locator("#cardList .also")).toContainText("the date countdown, the message card and your pictures");
  const row = (id) => page.locator(`#cardList .xrow[data-card="${id}"]`);

  // A row opens to say what its card shows.
  await expect(row("clock").locator(".xbody")).toBeHidden();
  await row("clock").locator(".xbtn").click();
  await expect(row("clock").locator(".xbody")).toBeVisible();
  await expect(row("clock").locator(".xbtn")).toHaveAttribute("aria-expanded", "true");

  await row("clock").locator(".lrm").click();
  await expectSet(request, { cards: "clock:0" });
  await expect(row("clock")).toHaveCount(0);
  await expect.poll(async () => (await deviceState(request)).cards.find((c) => c.id === "clock").on).toBe(false);

  // The cards that are off wait in the sheet, a row each with its Add button.
  const sheet = page.locator(".lsheet");
  await expect(page.locator("#cardAdd")).toHaveText("Add a card (4 more)");
  await page.locator("#cardAdd").click();
  await expect(sheet).toBeVisible();
  await expect(sheet.locator(".laddrow .ln")).toHaveText(["Clock", "News headlines", "Weather", "Game Day card"]);
  await sheet.getByRole("button", { name: "Add News headlines" }).click();
  await expectSet(request, { cards: "news:1" });
  await expect.poll(async () => (await deviceState(request)).news).toBe(true);
  await expect(sheet.locator(".laddrow .ln")).toHaveText(["Clock", "Weather", "Game Day card"]);
  await sheet.getByRole("button", { name: "Done" }).click();
  await expect(sheet).toBeHidden();
  await expect(row("news")).toHaveCount(1);

  // The countdown's row is the countdown switch.
  await row("countdown").locator(".lrm").click();
  await expectSet(request, { cards: "countdown:0" });
  await expect.poll(async () => (await deviceState(request)).countdown).toBe(false);
  await expect(row("countdown")).toHaveCount(0);
  await page.locator("#cardAdd").click();
  await sheet.getByRole("button", { name: "Add Clock" }).click();
  await expectSet(request, { cards: "clock:1" });
  await expect(row("clock")).toHaveCount(1);
});

test("between games: a card with nothing to show says what it needs, and its switch still works", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await tab(page, "display");
  const row = (id) => page.locator(`#cardList .xrow[data-card="${id}"]`);
  const own = (id) => page.locator(`.cardsw[data-card="${id}"]`);
  await expect(row("next")).toBeVisible();
  await expect(row("next")).not.toHaveClass(/\bwaiting\b/);
  await expect(row("next").locator(".card-hint")).toBeHidden();

  // The cards you make are switched where they are made, on the Customize tab.
  await page.locator("#cardList .also").getByRole("button", { name: "Change them under Customize" }).click();
  await expect(page.locator("#pane-customize")).toBeVisible();
  // No day picked, no message and no picture yet.
  await expect(own("custom").locator(".card-hint")).toHaveText("Pick its day first");
  await expect(own("message").locator(".card-hint")).toHaveText("Add text or a list first");
  await expect(own("picture").locator(".card-hint")).toHaveText("Add a picture first");
  await expect(own("message").locator(".sw")).toHaveAttribute("aria-checked", "true");
  await own("message").locator(".sw").click();
  await expectSet(request, { cards: "message:0" });
  await expect(own("message").locator(".sw")).toHaveAttribute("aria-checked", "false");

  await page.locator("#cddate").fill("2026-12-25");
  await page.locator("#cddate").dispatchEvent("change");
  await expectSet(request, { cddate: "2026-12-25" });
  await expect(own("custom").locator(".card-hint")).toBeHidden();

  // Top 25 waits for a college team.
  await reset(request, { favs: [{ l: "nfl", id: 6, abbr: "DAL", name: "Dallas Cowboys" }, {}, {}, {}] });
  await page.goto("/");
  await tab(page, "display");
  await expect(row("top25")).toHaveClass(/\bwaiting\b/);
  await expect(row("top25").locator(".card-hint")).toContainText("college");
});

test("between games: the custom countdown posts its name and day", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await tab(page, "customize");
  await expect(page.locator("#cdClear")).toBeHidden();
  await page.locator("#cdname").fill("Christmas");
  await page.locator("#cdname").press("Tab");
  await expectSet(request, { cdname: "Christmas" });
  await page.locator("#cddate").fill("2026-12-25");
  await page.locator("#cddate").dispatchEvent("change");
  await expectSet(request, { cddate: "2026-12-25" });
  await expect.poll(async () => (await deviceState(request)).cdname).toBe("Christmas");
  await expect(page.locator("#cdClear")).toBeVisible();
  await page.locator("#cdClear").click();
  await expectSet(request, { cdname: "", cddate: "none" });
  await expect.poll(async () => (await deviceState(request)).cddate).toBe("");
  await expect(page.locator("#cdname")).toHaveValue("");
  await expect(page.locator("#cdClear")).toBeHidden();
});

test("between games: firmware from before the list has a switch per card, each posting its own key", async ({ page, request }) => {
  await reset(request, { cards: null });
  await page.goto("/");
  await tab(page, "display");
  await expect(page.locator("#cardsOld")).toBeVisible();
  await expect(page.locator("#cardList .xrow")).toHaveCount(0);
  await expect(page.locator("#cardAdd")).toHaveCount(0);
  await expect(page.locator("#wxOnRow")).toBeVisible();
  for (const [key, on] of [["countdown", true], ["gameleaders", true], ["top25", true], ["news", false], ["wxon", false]]) {
    const sw = page.locator(`.sw[data-set="${key}"]`);
    if (on) await expect(sw).toHaveClass(/\bon\b/);
    else await expect(sw).not.toHaveClass(/\bon\b/);
    await sw.click();
    await expectSet(request, { [key]: on ? "0" : "1" });
    await expect.poll(async () => (await deviceState(request))[key]).toBe(!on);
  }
  // Nor the Customize cards' own switches.
  await expect(page.locator(".cardsw:not([hidden])")).toHaveCount(0);

  // And firmware from before each card leaves its row out.
  await reset(request, { cards: null, gameleaders: null, top25: null, news: null });
  await page.goto("/");
  await tab(page, "display");
  await expect(page.locator("#hostname")).not.toBeEmpty();
  await expect(page.locator("#cdCard")).toBeVisible();
  await expect(page.locator("#glRow")).toBeHidden();
  await expect(page.locator("#top25Row")).toBeHidden();
  await expect(page.locator("#newsRow")).toBeHidden();
  // Before the countdown card there was no Between games card at all.
  await reset(request, { cards: null, countdown: null, cdname: null, cddate: null });
  await page.goto("/");
  await tab(page, "display");
  await expect(page.locator("#hostname")).not.toBeEmpty();
  await expect(page.locator("#cdCard")).toBeHidden();
});

test("between games: weather is added from the sheet, a town is looked up, and the page says what was read", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await tab(page, "display");
  const row = page.locator('#cardList .xrow[data-card="weather"]');
  // Off to start: the town box waits out of sight until the card is added.
  await expect(row).toHaveCount(0);
  await expect(page.locator("#wxBox")).toBeHidden();
  await page.locator("#cardAdd").click();
  await page.locator(".lsheet").getByRole("button", { name: "Add Weather" }).click();
  await expectSet(request, { cards: "weather:1" });
  await page.locator(".lsheet").getByRole("button", { name: "Done" }).click();
  // On with no town yet: greyed with what it needs, and the town box opens inside its row.
  await expect(row).toHaveClass(/\bwaiting\b/);
  await expect(row.locator(".card-hint")).toHaveText("Set your town first");
  await row.locator(".xbtn").click();
  await expect(row.locator("#wxBox")).toBeVisible();
  // The block's own switch is for firmware without the list.
  await expect(page.locator("#wxOnRow")).toBeHidden();
  await expect(page.locator("#wxPlace")).toBeHidden();
  await page.locator("#town").fill("75019");
  await page.locator("#townGo").click();
  await expectSet(request, { town: "75019" });
  await expect(page.locator("#wxPlace")).toHaveText("Found: Coppell, Texas");
  await expect(page.locator("#wxRead")).toHaveText("Read a minute ago: 71°F, cloudy, high 81, low 61.");
  await expect(row).not.toHaveClass(/\bwaiting\b/);
  await expect(row.locator("#wxBox")).toBeVisible();
  // Degrees: Auto says which it is showing; Celsius turns the forecast over.
  await expect(page.locator("#wxUnitRow")).toBeVisible();
  await expect(page.locator("#wxUnit")).toHaveValue("0");
  await expect(page.locator("#wxUnit option").first()).toHaveText("Auto (°F now)");
  await page.locator("#wxUnit").selectOption("2");
  await expectSet(request, { wxunit: "2" });
  await expect(page.locator("#wxRead")).toHaveText("Read a minute ago: 22°C, cloudy, high 27, low 16.");
  await page.locator("#wxUnit").selectOption("1");
  await expectSet(request, { wxunit: "1" });

  // A town Open-Meteo doesn't know; Enter posts it too.
  await page.locator("#town").fill("Nowhere");
  await page.locator("#town").press("Enter");
  await expectSet(request, { town: "Nowhere" });
  await expect(page.locator("#wxPlace")).toContainText('No place found for "Nowhere"');
  await expect(page.locator("#wxRead")).toBeHidden();
  await expect(row).toHaveClass(/\bwaiting\b/);

  // Removed: the row goes and the town box with it.
  await row.locator(".lrm").click();
  await expectSet(request, { cards: "weather:0" });
  await expect(row).toHaveCount(0);
  await expect(page.locator("#wxBox")).toBeHidden();
  await expect.poll(async () => (await deviceState(request)).wxon).toBe(false);

  // A read that failed, waiting to try again, and one held for a live game.
  await reset(request, { town: "75019", wxon: true, wx: { place: "Coppell, Texas", lat: 32.95, lon: -97.02, lookup: 200, read: 0, wait_s: 240 } });
  await page.goto("/");
  await tab(page, "display");
  await row.locator(".xbtn").click();
  await expect(page.locator("#wxRead")).toHaveText("Couldn't reach Open-Meteo, tries again in 4 minutes.");
  await reset(request, { town: "75019", wxon: true, wx: { place: "Coppell, Texas", lat: 32.95, lon: -97.02, lookup: 200, read: -1, held: true } });
  await page.goto("/");
  await expect(page.locator("#wxRead")).toHaveText("Not read yet: waiting for the game to end.");

  // Firmware from before the weather card leaves the block out.
  await reset(request, { town: null, wxon: null });
  await page.goto("/");
  await tab(page, "display");
  await expect(page.locator("#hostname")).not.toBeEmpty();
  await expect(page.locator("#cdCard")).toBeVisible();
  await expect(page.locator("#wxBox")).toBeHidden();
  // and firmware from before the degrees setting leaves its row out.
  await reset(request, { town: "75019", wxon: true, wxunit: null });
  await page.goto("/");
  await tab(page, "display");
  await page.locator('#cardList .xrow[data-card="weather"] .xbtn').click();
  await expect(page.locator("#wxBox")).toBeVisible();
  await expect(page.locator("#wxUnitRow")).toBeHidden();
});

test("pictures: adds a PNG and a GIF as they are, fits a big still, refuses a wide GIF, deletes", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await tab(page, "customize");
  await expect(page.locator("#picCard")).toBeVisible();
  const pic = (name, mimeType) => ({ name, mimeType,
    buffer: fs.readFileSync(require("path").join(__dirname, "../fixtures/pictures", name)) });
  const uploads = async () => (await (await request.get("/_log")).json())
    .filter((p) => p.path === "/gameday/picture" && !p.q.delete);

  // A PNG that fits the panel goes byte for byte.
  await page.setInputFiles("#picFile", pic("blue.png", "image/png"));
  await expect(page.locator("#picNote")).toHaveText("Added. It's up next between games.");
  await expect(page.locator("#picList .pic")).toHaveCount(1);
  await expect(page.locator("#picList .pic label").first()).toHaveText("PNG64 by 32, 1 KB");
  await expect(page.locator("#picList img").first()).toHaveAttribute("src", /^\/gameday\/picture\?slot=1&id=/);
  expect((await uploads())[0]).toMatchObject({ bytes: 121, type: "image/png" });

  // So does a GIF.
  await page.setInputFiles("#picFile", pic("box_d2.gif", "image/gif"));
  await expect(page.locator("#picList .pic")).toHaveCount(2);
  await expect(page.locator("#picList .pic label").nth(1)).toHaveText(/^GIF, 4 frames128 by 64/);

  // A still too tall for the panel is redrawn to fit, as a PNG.
  await page.setInputFiles("#picFile", pic("tall.png", "image/png"));
  await expect(page.locator("#picList .pic")).toHaveCount(3);
  const st = await deviceState(request);
  expect(st.pics[2]).toMatchObject({ s: 3, k: "png", w: 63, h: 64 });

  // A GIF too wide never leaves the browser.
  await page.setInputFiles("#picFile", pic("wide.gif", "image/gif"));
  await expect(page.locator("#picNote")).toHaveText("That GIF is 129 by 64 pixels. A GIF can be up to 128 by 64; resize it first.");
  expect(await uploads()).toHaveLength(3);

  // Delete the first.
  page.once("dialog", (d) => d.accept());
  await page.locator("#picList .pic").first().locator("button").click();
  await expect(page.locator("#picList .pic")).toHaveCount(2);
  expect((await deviceState(request)).pics.map((p) => p.s)).toEqual([2, 3]);

  // Firmware from before the picture card leaves the card out.
  await reset(request, { pics: null, picroom: null });
  await page.goto("/");
  await tab(page, "customize");
  await expect(page.locator("#hostname")).not.toBeEmpty();
  await expect(page.locator("#picCard")).toBeHidden();
});

// message.h msg_turn: every 0 never hides the card between games, so the
// picture card never gets a turn. Every other Show it does.
const PIC_NEXT = "Added. It's up next between games.";
const PIC_HELD = "Added, but the message card holds the board between games, so the picture won't come up. Under Message card, change Show it from Whenever no game is on to a time, like Every 2 minutes, to give pictures a turn.";

const LIST = "http://192.168.1.5/names.txt";
for (const [what, state, note] of [
  ["a message on Every minute", { msg: "Wings $5", msgevery: 1 }, PIC_NEXT],
  ["a message on Whenever no game is on", { msg: "Wings $5", msgevery: 0 }, PIC_HELD],
  ["no message on Whenever no game is on", { msg: "", msgevery: 0 }, PIC_NEXT],
  ["the message card off on Whenever no game is on", { msg: "Wings $5", msgevery: 0, off: "message" }, PIC_NEXT],
  ["a list that was read", { msgurl: LIST, msgevery: 0, msgread: { status: 200, entries: 412 } }, PIC_HELD],
  ["a list on its first page", { msgurl: LIST, msgevery: 0, msgread: { status: 200 } }, PIC_HELD],
  ["a list read before whose last read failed", { msgurl: LIST, msgevery: 0, msgread: { status: 0, entries: 412 } }, PIC_HELD],
  ["a list not read yet", { msgurl: LIST, msgevery: 0 }, PIC_HELD],
  ["a list that can't be reached, with old text behind it", { msg: "Wings $5", msgurl: LIST, msgevery: 0, msgread: { status: 0 } }, PIC_NEXT],
  ["a list that answers an error", { msgurl: LIST, msgevery: 0, msgread: { status: 404 } }, PIC_NEXT],
  ["an empty list", { msgurl: LIST, msgevery: 0, msgread: { status: 200, entries: 0 } }, PIC_NEXT],
  ["a list on Every 5 minutes", { msgurl: LIST, msgevery: 5, msgread: { status: 200, entries: 412 } }, PIC_NEXT],
]) {
  test("pictures: the note after adding one, with " + what, async ({ page, request }) => {
    const { off, ...set } = state;
    await reset(request, set);
    if (off) await request.post("/gameday/set?cards=" + off + ":0");
    await page.goto("/");
    await tab(page, "customize");
    await expect(page.locator("#picCard")).toBeVisible();
    await page.setInputFiles("#picFile", { name: "blue.png", mimeType: "image/png",
      buffer: fs.readFileSync(require("path").join(__dirname, "../fixtures/pictures/blue.png")) });
    await expect(page.locator("#picNote")).toHaveText(note);
    await expect(page.locator("#picList .pic")).toHaveCount(1);
    await expect(page.locator("#picNote")).toHaveText(note);
  });
}

test("pictures: the note follows Show it, and no other note does", async ({ page, request }) => {
  await reset(request, { msg: "Wings $5", msgevery: 0 });
  await page.goto("/");
  await tab(page, "customize");
  const pic = (name, mimeType) => ({ name, mimeType,
    buffer: fs.readFileSync(require("path").join(__dirname, "../fixtures/pictures", name)) });
  await page.setInputFiles("#picFile", pic("blue.png", "image/png"));
  await expect(page.locator("#picNote")).toHaveText(PIC_HELD);
  await page.locator("#msgEvery").selectOption("2");
  await page.locator("#msgSave").click();
  await expect(page.locator("#picNote")).toHaveText(PIC_NEXT);

  // A refused file's note stays as it is when the setting changes.
  const wide = "That GIF is 129 by 64 pixels. A GIF can be up to 128 by 64; resize it first.";
  await page.setInputFiles("#picFile", pic("wide.gif", "image/gif"));
  await expect(page.locator("#picNote")).toHaveText(wide);
  await page.locator("#msgEvery").selectOption("0");
  await page.locator("#msgSave").click();
  await expect.poll(async () => (await deviceState(request)).msgevery).toBe(0);
  await expect(page.locator("#picNote")).toHaveText(wide);
});

test("pictures: the add button says when the panel keeps 4", async ({ page, request }) => {
  const four = [1, 2, 3, 4].map((s) => ({ s, k: "png", w: 64, h: 64, f: 1, b: 900, id: s }));
  await reset(request, { pics: four, picroom: 0 });
  await page.goto("/");
  await tab(page, "customize");
  await expect(page.locator("#picList .pic")).toHaveCount(4);
  await expect(page.locator("#picAdd")).toBeDisabled();
  await expect(page.locator("#picAdd")).toHaveText("4 pictures kept");
});

test("follow beta: off by default, posts 0 and 1, hidden when the firmware has no beta channel", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await tab(page, "device");
  const sw = page.locator('.sw[data-set="beta"]');
  await expect(page.locator("#betaRow")).toBeVisible();
  await expect(sw).not.toHaveClass(/\bon\b/);
  await sw.click();
  await expectSet(request, { beta: "1" });
  await expect(sw).toHaveClass(/\bon\b/);
  await sw.click();
  await expectSet(request, { beta: "0" });

  await reset(request, { beta: null });
  await page.goto("/");
  await tab(page, "device");
  await expect(page.locator("#hostname")).not.toBeEmpty();
  await expect(page.locator("#betaRow")).toBeHidden();
});

test("a beta carries the next release's number: newer than the last release, older than its own", async ({ page, request }) => {
  await reset(request, { beta: true });
  const fw = { id: "update/Firmware", domain: "update", name: "Firmware", state: "UPDATE AVAILABLE", summary: "", release_url: "" };
  let entity = { ...fw, value: "1.5.6-beta.812", current_version: "1.5.5" };
  await page.route("**/update/Firmware*", (route) => route.fulfill({ json: entity }));
  await page.goto("/");
  await tab(page, "device");
  await expect(page.locator("#fwText")).toHaveText("Update available: v1.5.6-beta.812");
  await expect(page.locator("#fwInstall")).toBeVisible();

  // Beta turned off on a beta build: the release it was building toward is an update.
  entity = { ...fw, value: "1.5.6", current_version: "1.5.6-beta.812" };
  await page.reload();
  await expect(page.locator("#fwText")).toHaveText("Update available: v1.5.6");
  await expect(page.locator("#fwInstall")).toBeVisible();

  // A beta of a release the panel already runs is behind it.
  entity = { ...fw, value: "1.5.6-beta.812", current_version: "1.5.6" };
  await page.reload();
  await expect(page.locator("#fwText")).toHaveText("You're ahead of the latest release, v1.5.6");
  await expect(page.locator("#fwInstall")).toBeHidden();

  // The next beta is newer than the last.
  entity = { ...fw, value: "1.5.6-beta.813", current_version: "1.5.6-beta.812" };
  await page.reload();
  await expect(page.locator("#fwText")).toHaveText("Update available: v1.5.6-beta.813");
});

test("an update that rolled back says so and still offers Install", async ({ page, request }) => {
  await reset(request, { autoupd: true, update_failed: "2026.10.1" });
  const fw = { id: "update/Firmware", domain: "update", name: "Firmware", state: "UPDATE AVAILABLE", summary: "", release_url: "" };
  let entity = { ...fw, value: "2026.10.1", current_version: "2026.10.0" };
  await page.route("**/update/Firmware*", (route) => route.fulfill({ json: entity }));
  await page.goto("/");
  await tab(page, "device");
  await expect(page.locator("#fwText")).toHaveText("The update to v2026.10.1 didn't take and the panel went back to v2026.10.0");
  await expect(page.locator("#fwSub")).toContainText("Install tries it again. It won't install this one on its own.");
  await expect(page.locator("#fwInstall")).toBeVisible();
  let asked = "";
  page.once("dialog", (d) => { asked = d.message(); d.dismiss(); });
  await page.locator("#fwInstall").click();
  expect(asked).toBe("Try v2026.10.1 again? It didn't take last time. The panel will reboot.");

  // Auto update off: nothing to say about the nightly install.
  await reset(request, { update_failed: "2026.10.1" });
  await page.reload();
  await expect(page.locator("#fwText")).toHaveText("The update to v2026.10.1 didn't take and the panel went back to v2026.10.0");
  await expect(page.locator("#fwSub")).not.toContainText("on its own");

  // The manifest moved past it: a plain update again.
  entity = { ...fw, value: "2026.10.2", current_version: "2026.10.0" };
  await reset(request, { autoupd: true, update_failed: "2026.10.1" });
  await page.reload();
  await expect(page.locator("#fwText")).toHaveText("Update available: v2026.10.2");
  await expect(page.locator("#fwSub")).toContainText("It installs on its own between 3 and 5 AM.");
});

test("Install, once confirmed, installs, says so, and reloads when the panel runs the new version", async ({ page, request }) => {
  await reset(request);
  const fw = { id: "update/Firmware", domain: "update", name: "Firmware", state: "UPDATE AVAILABLE", summary: "", release_url: "" };
  let entity = { ...fw, value: "2026.10.1", current_version: "2026.10.0" };
  await page.route("**/update/Firmware*", (route) => route.fulfill({ json: entity }));
  await page.goto("/");
  await tab(page, "device");
  await expect(page.locator("#fwText")).toHaveText("Update available: v2026.10.1");
  let asked = "";
  page.once("dialog", (d) => { asked = d.message(); d.accept(); });
  await page.locator("#fwInstall").click();
  expect(asked).toBe("Install v2026.10.1 now? The panel will reboot.");
  await expect.poll(async () => (await entityPosts(request)).map((p) => p.path)).toContain("/update/Firmware/install");
  await expect(page.locator("#toast")).toHaveText("Installing v2026.10.1");
  await expect(page.locator("#gone")).toHaveText("Installing v2026.10.1. This page reloads when the panel is back.");
  await expect(page.locator("#fwText")).toHaveText("Installing v2026.10.1");
  await expect(page.locator("#fwInstall")).toBeHidden();
  // Back on the new version: the page reloads by itself.
  const reloaded = page.waitForEvent("load", { timeout: 15000 });
  entity = { ...fw, state: "NO UPDATE", value: "2026.10.1", current_version: "2026.10.1" };
  await reloaded;
  await expect(page.locator("#gone")).toBeHidden();
});

test("Install, dismissed, does nothing", async ({ page, request }) => {
  await reset(request);
  const fw = { id: "update/Firmware", domain: "update", name: "Firmware", state: "UPDATE AVAILABLE", summary: "", release_url: "" };
  await page.route("**/update/Firmware*", (route) => route.fulfill({ json: { ...fw, value: "2026.10.1", current_version: "2026.10.0" } }));
  await page.goto("/");
  await tab(page, "device");
  page.once("dialog", (d) => d.dismiss());
  await page.locator("#fwInstall").click();
  await sleep(500);
  expect((await entityPosts(request)).map((p) => p.path)).not.toContain("/update/Firmware/install");
  await expect(page.locator("#gone")).toBeHidden();
  await expect(page.locator("#fwInstall")).toBeVisible();
});

test("Check for updates presses the button, then shows what the panel found", async ({ page, request }) => {
  await reset(request);
  const fw = { id: "update/Firmware", domain: "update", name: "Firmware", state: "NO UPDATE", summary: "", release_url: "" };
  let entity = { ...fw, value: "2026.10.0", current_version: "2026.10.0" };
  await page.route("**/update/Firmware*", (route) => route.fulfill({ json: entity }));
  await page.goto("/");
  await tab(page, "device");
  await expect(page.locator("#fwText")).toHaveText("You're up to date, v2026.10.0");
  entity = { ...fw, state: "UPDATE AVAILABLE", value: "2026.10.1", current_version: "2026.10.0" };
  const check = page.locator("#actions button");
  await expect(check).toHaveText("Check for updates");
  await check.click();
  await expect(check).toBeDisabled();
  await expect(check).toHaveText("Checking");
  await expect(page.locator("#fwText")).toHaveText("Checking for updates");
  await expect.poll(async () => (await entityPosts(request)).map((p) => p.path)).toContain("/button/Check%20for%20Updates/press");
  await expect(page.locator("#toast")).toHaveText("Update available: v2026.10.1", { timeout: 8000 });
  await expect(page.locator("#fwText")).toHaveText("Update available: v2026.10.1");
  await expect(check).toBeEnabled();
  await expect(check).toHaveText("Check for updates");
  await expect(page.locator("#fwInstall")).toBeVisible();
});

test("the remote card's Discovery switch and Unpair remote reach the panel", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await tab(page, "device");
  await unfold(page, "#remoteCard");
  const paths = async () => (await entityPosts(request)).map((p) => p.path);
  const discovery = page.locator("#remoteCtls .sw");
  await expect(discovery).toHaveAttribute("aria-checked", "false");
  await discovery.click();
  await expect(discovery).toHaveAttribute("aria-checked", "true");
  await expect.poll(paths).toContain("/switch/WizMote%20Auto-Discovery/turn_on");
  // Unpair asks first; no is no.
  const unpair = page.locator("#remoteActions button", { hasText: "Unpair remote" });
  page.once("dialog", (d) => d.dismiss());
  await unpair.click();
  await sleep(300);
  expect(await paths()).not.toContain("/button/Clear%20WizMote%20Pairing/press");
  page.once("dialog", (d) => d.accept());
  await unpair.click();
  await expect.poll(paths).toContain("/button/Clear%20WizMote%20Pairing/press");
});

test("an important release says so in the Firmware box", async ({ page, request }) => {
  await reset(request, { update_important: true });
  const fw = { id: "update/Firmware", domain: "update", name: "Firmware", state: "UPDATE AVAILABLE", release_url: "",
               summary: "Important: scores show again after an ESPN change." };
  const entity = { ...fw, value: "2026.10.3", current_version: "2026.10.2" };
  await page.route("**/update/Firmware*", (route) => route.fulfill({ json: entity }));
  await page.goto("/");
  await tab(page, "device");
  await expect(page.locator("#fwText")).toHaveText("Important update: v2026.10.3");
  await expect(page.locator("#fwSub")).toContainText("It fixes something broken, so install it when you can. You have v2026.10.2.");
  await expect(page.locator("#fwSub")).not.toContainText("on its own");
  await expect(page.locator("#fwSub .notes li").first()).toHaveText("Important: scores show again after an ESPN change.");
  await expect(page.locator("#fw")).toHaveClass(/important/);
  await expect(page.locator("#fwInstall")).toBeVisible();

  // With the nightly install on, the box still says it installs on its own.
  await reset(request, { update_important: true, autoupd: true });
  await page.reload();
  await expect(page.locator("#fwText")).toHaveText("Important update: v2026.10.3");
  await expect(page.locator("#fwSub")).toContainText("It installs on its own between 3 and 5 AM.");

  // A rollback of that very release explains itself first.
  await reset(request, { update_important: true, update_failed: "2026.10.3" });
  await page.reload();
  await expect(page.locator("#fwText")).toHaveText("The update to v2026.10.3 didn't take and the panel went back to v2026.10.2");
  await expect(page.locator("#fw")).not.toHaveClass(/important/);

  // The flag arrives with a later state read: the box follows it.
  await reset(request, {});
  await page.reload();
  await expect(page.locator("#fwText")).toHaveText("Update available: v2026.10.3");
  await expect(page.locator("#fw")).not.toHaveClass(/important/);
  await reset(request, { update_important: true });
  await expect(page.locator("#fwText")).toHaveText("Important update: v2026.10.3", { timeout: 15000 });
});

test("Diagnostics says when the ticker last said an update is ready", async ({ page, request }) => {
  // Nothing waiting and never shown: no row.
  await reset(request);
  await page.goto("/");
  await tab(page, "device");
  await unfold(page, "#diagCard");
  await expect(page.locator("#health")).toBeVisible();
  await expect(page.locator("#hNudgeRow")).toBeHidden();

  // A release waits and the line hasn't shown since the restart.
  const fw = { id: "update/Firmware", domain: "update", name: "Firmware", state: "UPDATE AVAILABLE", summary: "", release_url: "" };
  const entity = { ...fw, value: "2026.10.3", current_version: "2026.10.2" };
  await page.route("**/update/Firmware*", (route) => route.fulfill({ json: entity }));
  await page.reload();
  await unfold(page, "#diagCard");
  await expect(page.locator("#hNudgeRow")).toBeVisible();
  await expect(page.locator("#hNudge")).toHaveText("Not yet since the panel started");

  // Install updates automatically on: the ticker never says it.
  await reset(request, { autoupd: true });
  await page.reload();
  await unfold(page, "#diagCard");
  await expect(page.locator("#hNudge")).toHaveText("Not shown: this panel installs updates on its own");

  // The time it last started, in the browser's own time (the fixture's build time, for the same words as the Firmware row).
  await reset(request, { update_shown: 1792285260 });
  await page.reload();
  await unfold(page, "#diagCard");
  await expect(page.locator("#hNudge")).toHaveText("Oct 17, 8:01 PM");
  await expect(page.locator("#hFw")).toContainText("built Oct 17, 8:01 PM");
});

test("release notes link to the release's section of the site changelog, a beta to its commit", async ({ page, request }) => {
  await reset(request);
  const fw = { id: "update/Firmware", domain: "update", name: "Firmware", state: "UPDATE AVAILABLE", summary: "" };
  const commit = "https://github.com/gameday-scoreboard/gameday-scoreboard/commit/abc1234";
  let entity = { ...fw, value: "2026.10.1", current_version: "2026.10.0", release_url: "https://github.com/gameday-scoreboard/gameday-scoreboard/releases/tag/v2026.10.1" };
  await page.route("**/update/Firmware*", (route) => route.fulfill({ json: entity }));
  const link = page.locator("#fwSub a", { hasText: "Release notes" });
  await page.goto("/");
  await expect(link).toHaveAttribute("href", "https://install.gameday-scoreboard.com/changelog/#v2026.10.1");

  // Up to date: the running release's section.
  entity = { ...fw, state: "NO UPDATE", value: "2026.10.0", current_version: "2026.10.0", release_url: "" };
  await page.reload();
  await expect(link).toHaveAttribute("href", "https://install.gameday-scoreboard.com/changelog/#v2026.10.0");

  entity = { ...fw, value: "2026.10.1-beta.9", current_version: "2026.10.0", release_url: commit };
  await page.reload();
  await expect(link).toHaveAttribute("href", commit);
});

test("favorites and switches keep each tap through a slow re-read", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await tab(page, "teams");
  const labels = page.locator("#favCtls .ctl.fav label");
  await expect(labels).toHaveCount(3);
  await slowReads(page, 1000);
  await page.locator("#favCtls .ctl.fav").nth(0).getByRole("button", { name: "Remove" }).click();
  await sleep(600);
  await page.locator("#favCtls .ctl.fav").nth(0).getByRole("button", { name: "Move down" }).click();
  await tab(page, "display");
  await page.locator('.sw[data-set="opp"]').click();
  await sleep(600);
  await page.locator('.sw[data-set="down"]').click();
  const want = ["1. Ole Miss RebelsCollege football · remote button 1", "2. Houston TexansNFL · remote button 2"];
  await expect(labels).toHaveText(want);
  await expect(page.locator('.sw[data-set="down"]')).not.toHaveClass(/\bon\b/);
  await Promise.all([
    staysAs(labels, want, 2500),
    staysAs(page.locator('.sw.on[data-set="opp"], .sw.on[data-set="down"]'), [], 2500),
  ]);
  expect(await sets(request)).toEqual([
    { favs: "nfl:34,ncaa:145" }, { favs: "ncaa:145,nfl:34" }, { opp: "0" }, { down: "0" },
  ]);
  const st = await deviceState(request);
  expect([st.opp, st.down]).toEqual([false, false]);
});

test("pre-game leaders switch: every sport but soccer, posts leaders", async ({ page, request }) => {
  const sw = page.getByRole("switch", { name: "Pre-game season leaders", exact: true });
  await reset(request, {});
  await page.goto("/");
  await tab(page, "display");
  await expect(sw).toBeChecked();
  await sw.click();
  await expectSet(request, { leaders: "0" });
  await expect(sw).not.toBeChecked();
  expect((await deviceState(request)).leaders).toBe(false);

  // Hockey has leaders too.
  await reset(request, { favs: [{ l: "nhl", id: 25, abbr: "ANA", name: "Anaheim Ducks" }, {}, {}, {}] });
  await page.reload();
  await expect(sw).toBeVisible();

  // Soccer has none to turn off.
  await reset(request, { favs: [{ l: "epl", id: 359, abbr: "ARS", name: "Arsenal" }, {}, {}, {}] });
  await page.reload();
  await expect(sw).toBeHidden();
});

test("pictures switches: field strip and win chance, by sport", async ({ page, request }) => {
  const sw = (name) => page.getByRole("switch", { name, exact: true });
  // Nothing picked yet: both, on; the score margin chart is gone.
  await reset(request, { favs: [{}, {}, {}, {}] });
  await page.goto("/");
  await tab(page, "display");
  await expect(sw("Field strip")).toBeVisible();
  await expect(sw("Win chance")).toBeVisible();
  await expect(sw("Score margin")).toHaveCount(0);
  await expect(sw("Field strip")).toBeChecked();
  await sw("Field strip").click();
  await expectSet(request, { field: "0" });
  await expect(sw("Field strip")).not.toBeChecked();
  await sw("Win chance").click();
  await expectSet(request, { winbar: "0" });
  const st = await deviceState(request);
  expect([st.field, st.winbar]).toEqual([false, false]);

  // A football favorite: the strip and the win chance.
  await reset(request, { favs: [{ l: "nfl", id: 6, abbr: "DAL", name: "Dallas Cowboys" }, {}, {}, {}] });
  await page.reload();
  await expect(sw("Field strip")).toBeVisible();
  await expect(sw("Win chance")).toBeVisible();

  // Hockey only: nothing to turn off, so no card (ESPN has no win chance for hockey).
  await reset(request, { favs: [{ l: "nhl", id: 25, abbr: "ANA", name: "Anaheim Ducks" }, {}, {}, {}] });
  await page.reload();
  await expect(page.getByRole("heading", { name: "Live pictures" })).toBeHidden();

  // Basketball: the win chance alone.
  await reset(request, { favs: [{ l: "nba", id: 25, abbr: "SA", name: "San Antonio Spurs" }, {}, {}, {}] });
  await page.reload();
  await expect(sw("Field strip")).toBeHidden();
  await expect(sw("Win chance")).toBeVisible();
});

test("situation switches: one per sport the panel follows, each posting its own key", async ({ page, request }) => {
  const sw = (name) => page.getByRole("switch", { name, exact: true });
  // Nothing picked yet: every sport's switch.
  await reset(request, { favs: [{}, {}, {}, {}] });
  await page.goto("/");
  await tab(page, "display");
  await expect(sw("Down and distance")).toBeVisible();
  await expect(sw("Count and at-bat")).toBeVisible();
  await expect(sw("Match stats")).toBeVisible();

  // Yankees and Arsenal favorites: baseball and soccer, no football.
  await reset(request, {
    favs: [{ l: "mlb", id: 10, abbr: "NYY", name: "New York Yankees" }, { l: "epl", id: 359, abbr: "ARS", name: "Arsenal" }, {}, {}],
  });
  await page.reload();
  await expect(sw("Down and distance")).toBeHidden();
  await expect(sw("Count and at-bat")).toBeVisible();
  await expect(sw("Match stats")).toBeVisible();
  await sw("Count and at-bat").click();
  await expectSet(request, { count: "0" });
  await expect(sw("Count and at-bat")).not.toBeChecked();
  await expect(sw("Match stats")).toBeChecked();
  await sw("Match stats").click();
  await expectSet(request, { match: "0" });
  const st = await deviceState(request);
  expect([st.down, st.count, st.match]).toEqual([true, false, false]);

  // Hockey only: no situation line, so no situation switch; the leaders switch stays.
  await reset(request, { favs: [{ l: "nhl", id: 25, abbr: "ANA", name: "Anaheim Ducks" }, {}, {}, {}] });
  await page.reload();
  await expect(page.locator("#tickerCtls .ctl[data-sport]:visible")).toHaveCount(1);
  await expect(sw("Pre-game season leaders")).toBeVisible();
  await expect(sw("Last play")).toBeVisible();

  // Live games leagues picked by hand count too.
  await reset(request, { favs: [{ l: "nhl", id: 25, abbr: "ANA", name: "Anaheim Ducks" }, {}, {}, {}],
    live_auto: false, live: ["nhl", "mls"] });
  await page.reload();
  await expect(sw("Match stats")).toBeVisible();
  await expect(sw("Down and distance")).toBeHidden();
});

test("the picker has a tab per league and adds a team from another sport", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await tab(page, "teams");
  await page.locator("#favAdd").click();
  await expect(page.locator(".tabs button")).toHaveText(["On now", "NFL", "NCAAF", "MLB", "MLS", "EPL", "NBA", "WNBA", "NCAAM", "NCAAW", "NHL", "NCAAVB"]);
  await page.locator(".tabs button", { hasText: "NHL" }).click();
  await expect(page.locator(".tabs button.on")).toHaveText("NHL");
  await expect(page.locator("#tiles .tile")).toHaveCount(32);
  await page.locator("#tiles .tile", { hasText: "Anaheim Ducks" }).click();
  await expectSet(request, { favs: "nfl:6,nfl:34,ncaa:145,nhl:25" });
  await expect(page.locator("#tiles .tile", { hasText: "Anaheim Ducks" })).toHaveAttribute("aria-pressed", "true");
});

test("other sports show their situation line and no timeout pips", async ({ page, request }) => {
  const mlb = {
    s: "IN", l: "mlb", sport: 1, ta: "NYY", ti: 10, ts: 3, oa: "TOR", oi: 14, os: 2, p: 0, tt: 0, ot: 0,
    c: "Bot 7th", sit: "2-1", outs: 2, bases: 5, d: "", k: "", tc: "0C2340", oc: "134A8E", id: "401800001",
  };
  await reset(request, { game: mlb, next: [] });
  await page.goto("/");
  await expect(page.locator("#gstate")).toHaveText("LIVE");
  await expect(page.locator("#clock")).toHaveText("Bot 7th");
  await expect(page.locator("#down")).toHaveText("2-1, 2 outs");
  await expect(page.locator("#tA .pips")).toBeHidden();
  await expect(page.locator("#tB .pips")).toBeHidden();

  const nhl = { ...mlb, l: "nhl", sport: 3, ta: "ANA", ti: 25, oa: "LA", oi: 8, c: "12:04 - 2nd", sit: "Series tied 2-2", outs: -1 };
  await reset(request, { game: nhl, next: [] });
  await page.reload();
  await expect(page.locator("#down")).toHaveText("Series tied 2-2");
});

test("a splash shows for a few seconds, then is gone for screen readers too", async ({ page, request }) => {
  await reset(request, { splash: "TOUCHDOWN DAL" });
  await page.goto("/");
  await expect(page.locator("#splash")).toBeVisible();
  await expect(page.locator("#splash")).toHaveText("TOUCHDOWN DAL");
  // Hidden with visibility, not only faded out, so it leaves the accessibility tree.
  await expect(page.locator("#splash")).toBeHidden({ timeout: 6000 });
  await expect(page.getByText("TOUCHDOWN DAL")).toBeHidden();
});

test("timezone follows the browser, or a zone picked by hand", async ({ page, request }) => {
  await reset(request, { tz: 0, tz_name: "US Eastern", tz_auto: true });
  await page.goto("/");
  await tab(page, "device");
  await unfold(page, "#setupCard");
  await expectSet(request, { tz: "1" }); // the browser is pinned to America/Chicago
  await expect(page.locator("#toast")).toHaveText("Timezone set to US Central from this browser");
  await expect(page.locator("#tzline")).toContainText("US Central · set from this browser");

  // A button, so the keyboard reaches it.
  await page.locator("#tzline").getByRole("button", { name: "choose manually" }).focus();
  await page.keyboard.press("Enter");
  await expect(page.locator("#tzpick")).toBeVisible();
  await page.locator("#tzsel").selectOption({ label: "US Pacific" });
  await expectSet(request, { tzauto: "0", tz: "4" });
  await expect(page.locator("#tzline")).toContainText("US Pacific · chosen by hand");

  // The re-read after the tap updates the line in place, so focus stays on the button.
  await page.locator("#tzline").getByRole("button", { name: "follow this browser (US Central)" }).focus();
  await page.keyboard.press("Enter");
  await expectSet(request, { tzauto: "1" });
  await expect(page.locator("#tzline")).toContainText("· set from this browser");
  await expect(page.locator("#tzline").getByRole("button", { name: "choose manually" })).toBeFocused();
});

test("the panel picker posts the new layout", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await tab(page, "device");
  await unfold(page, "#matrixCard");
  await expect(page.locator("#panels .panelpick.on")).toHaveAttribute("data-cols", "2");
  await expect(page.locator("#panels .panelpick.on")).toContainText("One 128x64 panel");
  await page.locator('#panels .panelpick[data-cols="1"]').click();
  await expectSet(request, { panels: "1" });
  await expect(page.locator("#toast")).toHaveText("Restarting as 64x64");
  await expect(page.locator('#panels .panelpick[data-cols="2"]')).toBeDisabled();
});

test("the device box shows what the panel downloaded from ESPN", async ({ page, request }) => {
  await reset(request, { espn_kb: 850, espn_reads: 1 });
  await page.goto("/");
  await expect(page.locator("#espnData")).toHaveText("850 KB in 1 read");

  await reset(request, { espn_kb: 14321, espn_reads: 1260 });
  await page.reload();
  await expect(page.locator("#espnData")).toHaveText("14.3 MB in 1260 reads");

  // Firmware from before the counter sends neither field.
  await reset(request, { espn_kb: null, espn_reads: null });
  await page.reload();
  await expect(page.locator("#gstate")).toHaveText("UPCOMING");
  await expect(page.locator("#espnData")).toHaveText("");
});

test("diagnostics say why the panel restarted and how it is doing", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await tab(page, "device");
  await unfold(page, "#diagCard");
  await expect(page.locator("#health")).toBeVisible();
  await expect(page.locator("#hStart")).toHaveText("Restart, 2 h 1 min ago");
  await expect(page.locator("#hBoots")).toHaveText("2nd start since the power came on");
  await expect(page.locator("#hFault")).toHaveText("Crash, 1 d 1 h ago");
  await expect(page.locator("#hHeap")).toHaveText("143 KiB, largest piece 40 KiB");
  await expect(page.locator("#hEspn")).toHaveText("OK, 9 s ago");
  await expect(page.locator("#hFw")).toHaveText("1.5.4 stable, built Oct 17, 8:01 PM");
  // The Wi-Fi object, when there is one, fills in the signal.
  await expect(page.locator("#rssi")).toHaveText("-61 dBm");
  await expect(page.locator("#psram")).toHaveText("4101 KiB");

  // Pulled power: this start is the power coming on, nothing went wrong
  // since, and ESPN didn't answer the last read. No Wi-Fi object, so the
  // signal comes from the health block.
  const h = (await deviceState(request)).health;
  await reset(request, {
    health: { ...h, start: "power", boots: 1, up: 45, fault: undefined, fault_ago: undefined, rssi: undefined,
              espn: 0, espn_ago: 130, channel: "beta" },
    wifi: null,
  });
  await page.reload();
  await expect(page.locator("#hStart")).toHaveText("Power on, 45 s ago");
  await expect(page.locator("#hBoots")).toHaveText("");
  await expect(page.locator("#hFault")).toHaveText("None");
  await expect(page.locator("#hEspn")).toHaveText("No answer, 2 min ago");
  await expect(page.locator("#hFw")).toHaveText("1.5.4 beta, built Oct 17, 8:01 PM");
  await expect(page.locator("#rssi")).toHaveText("Not connected");

  // A brownout whose time isn't known, before the first ESPN read.
  await reset(request, {
    health: { ...h, start: "brownout", fault: "brownout", fault_ago: undefined, espn: undefined, espn_ago: undefined },
  });
  await page.reload();
  await expect(page.locator("#hStart")).toHaveText("Power dip (brownout), 2 h 1 min ago");
  await expect(page.locator("#hFault")).toHaveText("Power dip (brownout)");
  await expect(page.locator("#hEspn")).toHaveText("None yet");

  // Firmware from before the health block.
  await reset(request, { health: null });
  await page.reload();
  await expect(page.locator("#gstate")).toHaveText("UPCOMING");
  await expect(page.locator("#health")).toBeHidden();
});

test("a warm panel or one whose power dipped says so, with nothing to press", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await expect(page.locator("#hTemp")).toHaveText("47 C");
  await expect(page.locator("#hPower")).toHaveText("0");
  await expect(page.locator("#guard")).toBeHidden();

  const h = (await deviceState(request)).health;
  await reset(request, { health: { ...h, temp: 78.4, heat: 1 } });
  await page.reload();
  await expect(page.locator("#guard")).toBeVisible();
  await expect(page.locator("#guardName")).toHaveText("Running warm");
  await expect(page.locator("#guardText")).toContainText("Keep it out of direct sun");
  await expect(page.locator("#guard button")).toHaveCount(0);
  await expect(page.locator("#hTemp")).toHaveText("78 C, running warm");

  // The plug wins over the thermometer while both are up.
  await reset(request, { health: { ...h, heat: 1, brownouts: 3, power: "dipped" } });
  await page.reload();
  await expect(page.locator("#guardName")).toHaveText("Power dipped");
  await expect(page.locator("#guardText")).toContainText("not a phone charger or a USB port");
  await expect(page.locator("#guard button")).toHaveCount(0);
  await expect(page.locator("#hPower")).toHaveText("3, one at this start");

  // Settled after the dip: the bar goes, the count stays.
  await reset(request, { health: { ...h, brownouts: 3, power: "ok" } });
  await page.reload();
  await expect(page.locator("#guard")).toBeHidden();
  await expect(page.locator("#hPower")).toHaveText("3");

  // Before the first temperature reading there is no temperature row.
  await reset(request, { health: { ...h, temp: undefined } });
  await page.reload();
  await expect(page.locator("#hPower")).toHaveText("0");
  await expect(page.locator("#hTempRow")).toBeHidden();

  // Firmware from before the guard sends none of its keys.
  await reset(request, { health: { ...h, temp: undefined, heat: undefined, brownouts: undefined, power: undefined } });
  await page.reload();
  await expect(page.locator("#hFault")).toHaveText("Crash, 1 d 1 h ago");
  await expect(page.locator("#hPowerRow")).toBeHidden();
  await expect(page.locator("#guard")).toBeHidden();
});

test("saved Wi-Fi networks are listed in order and one can be forgotten", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await tab(page, "device");
  await unfold(page, "#netsCard");
  const rows = page.locator("#nets .ctl");
  await expect(page.locator("#netsCard")).toBeVisible();
  await expect(rows).toHaveCount(2);
  await expect(rows.nth(0).locator("label")).toContainText("Home");
  await expect(rows.nth(0).locator("label small")).toHaveText("Connected");
  await expect(rows.nth(1).locator("label")).toContainText("iPhone");
  await expect(page.locator("#netsEmpty")).toBeHidden();

  // Forget asks first; declining posts nothing.
  page.once("dialog", (d) => d.dismiss());
  await rows.nth(1).locator("button").click();
  await sleep(300);
  const forgets = async () =>
    (await (await request.get("/_log")).json()).filter((p) => p.path === "/gameday/wifi").map((p) => p.q);
  expect(await forgets()).toEqual([]);

  page.once("dialog", (d) => d.accept());
  await rows.nth(1).locator("button").click();
  await expect.poll(forgets).toEqual([{ forget: "iPhone" }]);
  await expect(rows).toHaveCount(1);
  await expect(rows.nth(0).locator("label")).toContainText("Home");

  // Forgetting the last one, which it's on, says it stays on it, and leaves the empty line.
  let asked = "";
  page.once("dialog", (d) => {
    asked = d.message();
    d.accept();
  });
  await rows.nth(0).locator("button").click();
  await expect(rows).toHaveCount(0);
  expect(asked).toContain("only saved network, so the panel stays on it");
  await expect(page.locator("#toast")).toContainText("Forgot Home. It's the only saved network");
  await expect(page.locator("#netsEmpty")).toBeVisible();

  // Forgetting the one it's on with others left names the next one and moves there.
  await reset(request, {}, [], [
    { ssid: "Home", connected: true },
    { ssid: "iPhone", connected: false },
  ]);
  await page.goto("/");
  await tab(page, "device");
  await unfold(page, "#netsCard");
  await expect(rows).toHaveCount(2);
  page.once("dialog", (d) => {
    asked = d.message();
    d.accept();
  });
  await rows.nth(0).locator("button").click();
  await expect.poll(() => asked).toBe("Forget Home? The panel moves to iPhone now.");
  await expect(page.locator("#toast")).toContainText("Forgot Home. Moving to iPhone now");
  await expect(rows).toHaveCount(1);
  await expect(rows.nth(0).locator("label small")).toHaveText("Connected");

  // With more than one left, the next one is named and the rest are a fallback.
  await reset(request, {}, [], [
    { ssid: "Home", connected: true },
    { ssid: "iPhone", connected: false },
    { ssid: "Friend", connected: false },
  ]);
  await page.goto("/");
  await tab(page, "device");
  await unfold(page, "#netsCard");
  await expect(rows).toHaveCount(3);
  page.once("dialog", (d) => {
    asked = d.message();
    d.accept();
  });
  await rows.nth(0).locator("button").click();
  await expect.poll(() => asked).toBe("Forget Home? The panel moves to iPhone now, or another saved network if it can't see iPhone.");
  await expect(page.locator("#toast")).toContainText("Forgot Home. Moving to the next saved network now");

  // Firmware without the list: the section stays hidden.
  await reset(request, {}, [], null);
  await page.goto("/");
  await tab(page, "device");
  await expect(page.locator("#hostname")).not.toBeEmpty();
  await expect(page.locator("#netsCard")).toBeHidden();
});

test("a network is added by name, and the page offers to switch when the panel sees it", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await tab(page, "device");
  await unfold(page, "#netsCard");
  const rows = page.locator("#nets .ctl");
  const msg = page.locator("#netMsg");
  await expect(rows).toHaveCount(2);
  // The scans the open form asks for are checked in their own test.
  const posts = async () =>
    (await (await request.get("/_log")).json()).filter((p) => p.path === "/gameday/wifi" && !p.q.scan);
  const add = async (name, pass) => {
    if (await page.locator("#netAdd").isVisible()) await page.locator("#netAdd").click();
    await page.locator("#netName").fill(name);
    await page.locator("#netPass").fill(pass);
    await page.locator("#netSave").click();
  };

  // Checked on the page first: nothing is posted.
  await add("Lake", "short");
  await expect(msg).toHaveText("A Wi-Fi password is 8 to 63 characters.");
  await add("Home", "password1");
  await expect(msg).toHaveText("The panel is on Home now, so its password already works.");
  expect(await posts()).toEqual([]);

  // Out of range: saved second, behind the network it is on, no offer. The
  // password goes in the body, not the address.
  page.on("dialog", (d) => d.accept());
  await add("Lake", "lakepass1");
  await expect(msg).toHaveText("Saved Lake. The panel joins it wherever it can't find a network it already knows.");
  expect(await posts()).toEqual([{ path: "/gameday/wifi", q: {}, form: { ssid: "Lake", password: "lakepass1" } }]);
  await expect(page.locator("#netForm")).toBeHidden();
  await expect(rows).toHaveCount(3);
  await expect(rows.nth(1).locator("label")).toContainText("Lake");

  // In range, but the switch fails: the panel is back on Home.
  await add("Attic", "atticpass");
  await expect(msg).toHaveText("The panel couldn't join Attic, so it went back to Home. Check the name and password, then add it again.",
    { timeout: 15000 });
  expect((await posts()).map((p) => p.q).filter((q) => q.switch)).toEqual([{ switch: "Attic" }]);

  // In range and it joins.
  const offers = [];
  page.removeAllListeners("dialog");
  page.on("dialog", (d) => { offers.push(d.message()); d.accept(); });
  await add("Cabin", "cabinpass");
  await expect(msg).toHaveText("The panel is on Cabin now.", { timeout: 15000 });
  expect(offers.length).toBe(1);
  expect(offers[0]).toContain("The panel can see Cabin. Switch to it now?");
  expect(offers[0]).toContain("it goes back to Home within a minute");
  await expect(rows.nth(0).locator("label small")).toHaveText("Connected");
  await expect(rows.nth(0).locator("label")).toContainText("Cabin");

  // Declining the offer leaves it saved; a saved name asks before its password changes.
  page.removeAllListeners("dialog");
  const asked = [];
  page.on("dialog", (d) => { asked.push(d.message()); asked.length === 1 ? d.accept() : d.dismiss(); });
  await add("Attic", "atticpass2");
  await expect(msg).toHaveText("Saved Attic. The panel joins it wherever it can't find a network it already knows.");
  expect(asked[0]).toBe("Attic is already saved. Replace its password?");
  expect(asked[1]).toContain("The panel can see Attic.");

  // Full: the page says which one drops.
  page.removeAllListeners("dialog");
  await reset(request, {}, [], [1, 2, 3, 4, 5].map((i) => ({ ssid: "n" + i, connected: i === 1 })));
  await page.goto("/");
  await tab(page, "device");
  await unfold(page, "#netsCard");
  await expect(rows).toHaveCount(5);
  page.once("dialog", (d) => { asked.push(d.message()); d.dismiss(); });
  await add("Lake", "lakepass1");
  await expect.poll(() => asked[asked.length - 1]).toBe("The list is full, so adding Lake drops n5. Add it?");
  expect(await posts()).toEqual([]);
});

test("the Add network form lists the networks the panel can see, and a tap fills in the name", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await tab(page, "device");
  await unfold(page, "#netsCard");
  await page.locator("#netAdd").click();
  const picks = page.locator("#netNearby .netpick");
  // Strongest first, without the network the panel is on; a saved one says so.
  await expect(picks).toHaveCount(4);
  await expect(picks.nth(0)).toContainText("Cabin");
  await expect(picks.nth(0).locator(".val")).toHaveText("Strong");
  await expect(picks.nth(1).locator(".val")).toHaveText("Good");
  await expect(picks.nth(2)).toContainText("Coffee Shop");
  await expect(picks.nth(2).locator(".val")).toHaveText("Open, weak");
  await expect(picks.nth(3)).toContainText("iPhone");
  await expect(picks.nth(3).locator("small")).toHaveText("Saved");
  const scans = async () =>
    (await (await request.get("/_log")).json()).filter((p) => p.path === "/gameday/wifi" && p.q.scan);
  expect((await scans()).length).toBe(1);

  // A tap fills in the name and moves on to the password.
  await picks.nth(1).click();
  await expect(page.locator("#netName")).toHaveValue("Attic");
  await expect(picks.nth(1)).toHaveAttribute("aria-pressed", "true");
  await expect(page.locator("#netPass")).toBeFocused();
  // An open one says to leave the password blank.
  await page.locator("#netPass").fill("something");
  await picks.nth(2).click();
  await expect(page.locator("#netName")).toHaveValue("Coffee Shop");
  await expect(page.locator("#netPass")).toHaveValue("");
  await expect(page.locator("#netMsg")).toHaveText("Coffee Shop is open, so leave the password blank.");
  // Typing another name takes the mark off.
  await page.locator("#netName").fill("Hidden");
  await expect(page.locator("#netNearby .netpick.on")).toHaveCount(0);

  // Look again scans again.
  await page.locator("#netRescan").click();
  await expect(page.locator("#netRescan")).toBeEnabled();
  expect((await scans()).length).toBe(2);

  // Nothing else in range: the page says to type it.
  await page.locator("#netCancel").click();
  await reset(request, {}, [], [{ ssid: "Home", connected: true }]);
  await page.route("**/gameday/wifi?_=*", async (route) => {
    const r = await route.fetch();
    const j = await r.json();
    j.nearby = [{ ssid: "Home", rssi: -50 }];
    await route.fulfill({ response: r, json: j });
  });
  await page.locator("#netAdd").click();
  await expect(page.locator("#netScanMsg")).toHaveText(
    "The panel can't see another 2.4 GHz network. Type its name below, or move closer and look again.");
  await expect(picks).toHaveCount(0);
});

test("the stale tag follows the device's consecutive misses", async ({ page, request }) => {
  await reset(request, { misses: 2 });
  await page.goto("/");
  await expect(page.locator("#gstate")).toHaveText("UPCOMING");
  await expect(page.locator("#stale")).toBeHidden();

  // Three misses is where the firmware adds "no update" to the ticker.
  await reset(request, { misses: 3, stale_s: 600 });
  await page.reload();
  await expect(page.locator("#gstate")).toHaveText("UPCOMING");
  await expect(page.locator("#stale")).toBeVisible();

  // The ticker warns with no game too, so the empty board shows it.
  await reset(request, { misses: 5, game: { s: "NOT_FOUND", l: "nfl" }, next: [] });
  await page.reload();
  await expect(page.locator("#gstate")).toHaveText("NO GAME");
  await expect(page.locator("#stale")).toBeVisible();

  await reset(request, { misses: 0, game: { s: "NOT_FOUND", l: "nfl" }, next: [] });
  await page.reload();
  await expect(page.locator("#gstate")).toHaveText("NO GAME");
  await expect(page.locator("#stale")).toBeHidden();
});

test("a Wi-Fi that wants a sign-in says how to fix it, in place of the panel's reason and the stale tag", async ({ page, request }) => {
  await reset(request, { captive: true, misses: 1, game: { s: "NOT_FOUND", l: "nfl", why: "Wi-Fi needs a sign-in" }, next: [] });
  await page.goto("/");
  await expect(page.locator("#gstate")).toHaveText("NO GAME");
  await expect(page.locator("#msg")).toHaveText("This Wi-Fi needs a sign-in. Use a home network or a hotspot.");
  await expect(page.locator("#stale")).toBeVisible();
  await expect(page.locator("#stale")).toHaveText("Wi-Fi needs a sign-in");

  // Over a game still on the board from before, only the tag says it
  await reset(request, { captive: true, misses: 1 });
  await page.reload();
  await expect(page.locator("#gstate")).toHaveText("UPCOMING");
  await expect(page.locator("#stale")).toHaveText("Wi-Fi needs a sign-in");

  // Cleared: back to the usual stale tag
  await reset(request, { captive: false, misses: 3, stale_s: 600 });
  await page.reload();
  await expect(page.locator("#stale")).toHaveText("data stale");
  await expect(page.locator("#stale")).toBeVisible();
});

test("says when the panel stops answering, and clears it when the panel is back", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await expect(page.locator("#gstate")).toHaveText("UPCOMING");
  await expect(page.locator("#gone")).toBeHidden();
  await expect(page.locator("#gonePill")).toBeHidden();
  await expect(page.locator("#board")).not.toHaveClass(/dim/);

  // The panel drops off the network: the stream ends and the state read gets no answer.
  await reset(request, {}, ["down"]);
  await expect(page.locator("#gone")).toHaveText(/^Panel not answering since \d{1,2}:\d{2}\s[AP]M$/);
  // The header says it too, on every tab.
  await expect(page.locator("#gonePill")).toHaveText("Not answering");
  await tab(page, "device");
  await expect(page.locator("#gonePill")).toBeVisible();
  await tab(page, "board");
  await expect(page.locator("#board")).toHaveClass(/dim/);
  // The last score stays up, dimmed.
  await expect(page.locator("#tA .abbr")).toHaveText("DAL");

  // Back: the next poll answers and the stream reopens without waiting out its retry.
  await reset(request);
  await expect(page.locator("#gone")).toBeHidden({ timeout: 20000 });
  await expect(page.locator("#gonePill")).toBeHidden();
  await expect(page.locator("#board")).not.toHaveClass(/dim/);
});

test("a dropped stream alone doesn't call the panel gone", async ({ page, request }) => {
  await reset(request);
  const isStream = (r) => new URL(r.url()).pathname === "/events";
  await page.goto("/");
  await expect(page.locator("#gstate")).toHaveText("UPCOMING");
  // A phone waking up, or the stream refused: the page asks the panel, and it answers.
  const ended = new Promise((ok) => {
    for (const ev of ["requestfinished", "requestfailed"]) page.on(ev, (r) => isStream(r) && ok());
  });
  await reset(request, {}, ["events"]);
  await ended;
  await staysAs(page.locator("#gone:visible"), [], 2000);
  await expect(page.locator("#board")).not.toHaveClass(/dim/);
  // Back: the stream opens again.
  const back = page.waitForResponse((r) => isStream(r) && r.status() === 200, { timeout: 20000 });
  await reset(request);
  await back;
  await expect(page.locator("#gone")).toBeHidden();
});

test("Reboot says it's restarting and reloads when the panel is back", async ({ page, request }) => {
  await reset(request);
  page.on("dialog", (d) => d.accept());
  await page.goto("/");
  await tab(page, "device");
  await expect(page.locator("#gstate")).toHaveText("UPCOMING");
  await page.locator("#resetActions button", { hasText: "Reboot" }).click();
  const restarting = "Restarting. This page reconnects when the panel is back.";
  await expect(page.locator("#gone")).toHaveText(restarting);
  await expect(page.locator("#board")).toHaveClass(/dim/);
  await expect(page.locator("#pane-board")).toBeVisible();

  // Down for a few seconds, then back: the page reloads on its own.
  await reset(request, {}, ["down"]);
  await sleep(4500);
  await expect(page.locator("#gone")).toHaveText(restarting);
  const reloaded = page.waitForEvent("load", { timeout: 10000 });
  await reset(request);
  await reloaded;
  await expect(page.locator("#gstate")).toHaveText("UPCOMING");
  await expect(page.locator("#gone")).toBeHidden();
  await expect(page.locator("#board")).not.toHaveClass(/dim/);
});

test("both resets say the panel restarts into setup and the page won't reconnect", async ({ page, request, baseURL }) => {
  await reset(request);
  page.on("dialog", (d) => d.accept());
  await page.goto("/");
  await tab(page, "device");
  await expect(page.locator("#gstate")).toHaveText("UPCOMING");
  await page.locator("#resetActions button", { hasText: "Reset Wi-Fi" }).click();
  await expect(page.locator("#gone")).toHaveText(
    "The panel is restarting into Wi-Fi setup for 15 minutes, so this page won't reconnect. Its settings are kept,"
    + " and unplugging it and plugging it back in takes it back to the Wi-Fi it already knows."
    + " To set it up again, use the Game Day app or join its \"Game Day 2f6a70\" Wi-Fi.");
  await expect(page.locator("#board")).toHaveClass(/dim/);
  // The page goes to the Home tab, where that line is.
  await expect(page.locator("#pane-board")).toBeVisible();
  await expect(page.locator("#pane-device")).toBeHidden();

  await page.reload();
  await tab(page, "device");
  await expect(page.locator("#gstate")).toHaveText("UPCOMING");
  await page.locator("#resetActions button", { hasText: "Factory reset" }).click();
  await expect(page.locator("#gone")).toHaveText(
    "The panel is restarting into setup with everything erased, so this page won't reconnect."
    + " To set it up again, use the Game Day app or join its \"Game Day 2f6a70\" Wi-Fi.");
  // The mock still answers, but the page has stopped asking it.
  const asked = [];
  page.on("request", (r) => { if (r.url().startsWith(baseURL)) asked.push(r.url()); });
  await sleep(1500);
  expect(asked).toEqual([]);
  await expect(page.locator("#board")).toHaveClass(/dim/);
});

test("a reset the panel never got doesn't claim it's resetting", async ({ page, request }) => {
  await reset(request);
  page.on("dialog", (d) => d.accept());
  await page.goto("/");
  await tab(page, "device");
  await expect(page.locator("#gstate")).toHaveText("UPCOMING");
  await page.route("**/button/**", (route) => route.fulfill({ status: 500, body: "" }));
  await page.locator("#resetActions button", { hasText: "Factory reset" }).click();
  await expect(page.locator("#toast")).toHaveText("Device did not respond (HTTP 500). Try again.");
  await expect(page.locator("#gone")).toBeHidden();
  await expect(page.locator("#board")).not.toHaveClass(/dim/);
});

const actions = async (request) =>
  (await (await request.get("/_log")).json()).filter((p) => p.path === "/gameday/action").map((p) => p.q);

test("the banner posts its text and seconds, and Take down posts no text", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await tab(page, "customize");
  await expect(page.locator("#msgCard")).toBeVisible();
  await page.locator("#sayGo").click();
  await expect(page.locator("#toast")).toHaveText("Type the banner first");
  await page.locator("#sayText").fill("Pizza's here!");
  await page.locator("#saySecs").fill("900");
  await page.locator("#sayGo").click();
  await expect.poll(() => actions(request)).toContainEqual({ do: "say", text: "Pizza's here!", secs: "600" });
  await expect(page.locator("#saySecs")).toHaveValue("600");
  await page.locator("#sayStop").click();
  await expect.poll(() => actions(request)).toContainEqual({ do: "say", text: "" });
});

test("Show on panel puts the FCC information on the panel", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await tab(page, "device");
  // It sits folded shut at the bottom of the Device tab, under the untitled card of resets.
  const cards = page.locator("#pane-device .grid > .card");
  await expect(cards.last()).toHaveId("fccCard");
  await expect(cards.nth(-2)).toHaveClass(/dangercard/);
  await expect(cards.nth(-2).locator("h2")).toHaveCount(0);
  await expect(cards.nth(-2).locator("button")).toHaveText(["Reboot", "Reset Wi-Fi", "Factory reset"]);
  await expect(page.locator("#fccShow")).toBeHidden();
  await page.getByText("FCC information", { exact: true }).click();
  await page.locator("#fccShow").click();
  await expect.poll(() => actions(request)).toContainEqual({ do: "regulatory" });
  await expect(page.locator("#toast")).toHaveText("FCC information on the panel");
});

test("the Device tab's rarely used cards are folded shut, each with a line on what is inside", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await tab(page, "device");
  const folds = page.locator("#pane-device details.card");
  await expect(folds.locator("summary h2")).toHaveText(["Setup", "Game events", "Wi-Fi networks", "Remote", "Matrix", "Diagnostics", "FCC information"]);
  for (const d of await folds.all()) expect(await d.evaluate((n) => n.open)).toBe(false);
  await expect(page.locator("#setupCard .fsum")).toHaveText("Time zone, startup, backup");
  await expect(page.locator("#netsSum")).toHaveText("2 saved");
  await expect(page.locator("#diagSum")).toHaveText("Signal -61 dBm");
  await expect(page.locator("#remoteSum")).toHaveText("Paired");
  await expect(page.locator("#matrixSum")).toHaveText("One 128x64 panel");
  // The Panel card and the look stay open.
  await expect(page.locator("#hostname")).toBeVisible();
  await expect(page.locator("#lookCard")).toBeVisible();

  // A tap opens one and turns its chevron; another closes it.
  await expect(page.locator("#rssi")).toBeHidden();
  await page.locator("#diagCard summary").click();
  await expect(page.locator("#rssi")).toBeVisible();
  await expect(page.locator("#diagCard")).toHaveAttribute("open", "");
  await page.locator("#diagCard summary").click();
  await expect(page.locator("#rssi")).toBeHidden();

  await request.post("/_reset", { data: { wizmote: "No remote paired" } });
  await page.reload();
  await expect(page.locator("#remoteSum")).toHaveText("No remote paired");
});

test("the Look switch picks Phone, Light or Dark, and this browser keeps it", async ({ page, request }) => {
  await reset(request);
  await page.emulateMedia({ colorScheme: "dark" });
  await page.goto("/#device");
  const look = page.getByRole("radiogroup", { name: "Look" });
  const html = page.locator("html");
  // Phone, the default, follows the system.
  await expect(look.getByRole("radio", { checked: true })).toHaveText("Phone");
  await expect(html).not.toHaveClass(/light/);
  await page.emulateMedia({ colorScheme: "light" });
  await expect(html).toHaveClass(/light/);
  expect(await page.evaluate(() => getComputedStyle(document.body).backgroundColor)).toBe("rgb(242, 243, 247)");
  // The scoreboard stays black.
  expect(await page.evaluate(() => getComputedStyle(document.querySelector("#board")).backgroundColor)).toBe("rgb(0, 0, 0)");

  await look.getByRole("radio", { name: "Dark" }).click();
  await expect(look.getByRole("radio", { checked: true })).toHaveText("Dark");
  await expect(html).not.toHaveClass(/light/);
  expect(await page.evaluate(() => localStorage.getItem("look"))).toBe("dark");
  await page.reload();
  await expect(look.getByRole("radio", { checked: true })).toHaveText("Dark");
  await expect(html).not.toHaveClass(/light/);

  await look.getByRole("radio", { name: "Light" }).click();
  await page.emulateMedia({ colorScheme: "dark" });
  await page.reload();
  await expect(html).toHaveClass(/light/);
  await expect(look.getByRole("radio", { checked: true })).toHaveText("Light");
});

test("the board's round power button turns the panel's light on and off, and its strip shows brightness as a percent", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  const power = page.locator("#quickCtls .sw.round");
  await expect(power).toHaveAccessibleName("Panel on");
  await expect(power).toHaveAttribute("aria-checked", "true");
  await expect(page.locator(".boardctl .val")).toHaveText("50%");
  const paths = async () => (await entityPosts(request)).map((p) => p.path);
  await power.click();
  await expect(power).toHaveAttribute("aria-checked", "false");
  await expect.poll(paths).toContain("/light/Game%20Day%20Scoreboard%202f6a70/turn_off");
  await power.click();
  await expect(power).toHaveAttribute("aria-checked", "true");
  await expect.poll(paths).toContain("/light/Game%20Day%20Scoreboard%202f6a70/turn_on");
  await page.getByRole("button", { name: "Refresh scores" }).click();
  await expect.poll(paths).toContain("/button/Refresh%20Now/press");
});

test("the Brightness slider sets the panel's light's brightness, 1 to 255", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  const slider = page.getByRole("slider", { name: "Brightness" });
  await expect(slider).toHaveValue("128");
  await slider.fill("200");
  await slider.dispatchEvent("change");
  await expect.poll(async () => (await entityPosts(request)).some((p) => p.path === "/light/Game%20Day%20Scoreboard%202f6a70/turn_on" && JSON.stringify(p.q).includes("200"))).toBe(true);
  await expect(page.locator(".boardctl .val")).toHaveText("78%");
});

test("the board shows the Smart board's rows, in their colors, while the panel shows it", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  const post = (path) => request.post(path);
  await post("/text/Smart%20board%20row%201/set?value=" + encodeURIComponent("[red] Front Door [#00FF64]Locked"));
  await post("/text/Smart%20board%20row%203/set?value=" + encodeURIComponent("CO2 547ppm [blink]"));
  await post("/select/Select%20Page/set?option=Smart%20board");
  const rows = page.locator("#sbView .sbrow");
  await expect(page.locator("#sbView")).toBeVisible();
  await expect(page.locator("#tA")).toBeHidden();
  await expect(rows.nth(0)).toHaveText("Front Door Locked");
  await expect(rows.nth(0).locator("span").first()).toHaveCSS("color", "rgb(255, 32, 32)");
  await expect(rows.nth(0).locator("span").nth(1)).toHaveCSS("color", "rgb(0, 255, 100)");
  await expect(rows.nth(1)).toBeHidden();
  // A bracket that isn't a color stays text
  await expect(rows.nth(2)).toHaveText("CO2 547ppm [blink]");
  await expect(page.locator("#sbView .sbhint")).toBeHidden();
  await post("/select/Select%20Page/set?option=Scoreboard");
  await expect(page.locator("#sbView")).toBeHidden();
  await expect(page.locator("#tA")).toBeVisible();
});

test("Smart board rows show in their colors and edit with words and a color, no tags", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await request.post("/text/Smart%20board%20row%201/set?value=" + encodeURIComponent("[red]Front Door"));
  await request.post("/select/Select%20Page/set?option=Smart%20board");
  const lines = page.locator("#sbRows .sbline");
  await expect(lines.nth(0)).toHaveText("Front Door");
  await expect(lines.nth(0).locator("span")).toHaveCSS("color", "rgb(255, 32, 32)");
  await expect(lines.nth(1)).toHaveText("Empty");
  await expect(page.getByRole("button", { name: "Add row 2" })).toBeVisible();

  await page.getByRole("button", { name: "Edit row 1" }).click();
  const text = page.getByRole("textbox", { name: "Row 1 text" });
  await expect(text).toHaveValue("Front Door");
  await expect(page.getByRole("radio", { name: "red" })).toHaveAttribute("aria-checked", "true");
  await page.getByRole("radio", { name: "blue" }).click();
  await text.fill("Back Door");
  await page.getByRole("button", { name: "Save" }).click();
  await expect.poll(() => entityPosts(request)).toContainEqual({ path: "/text/Smart%20board%20row%201/set", q: { value: "[#3070FF]Back Door" } });
  await expect(lines.nth(0)).toHaveText("Back Door");
  await expect(lines.nth(0).locator("span")).toHaveCSS("color", "rgb(48, 112, 255)");

  // Clear row empties it on the panel
  await page.getByRole("button", { name: "Edit row 1" }).click();
  await page.getByRole("button", { name: "Clear row" }).click();
  await expect.poll(() => entityPosts(request)).toContainEqual({ path: "/text/Smart%20board%20row%201/set", q: { value: "" } });
  await expect(lines.nth(0)).toHaveText("Empty");
});

test("an empty Smart board says where its rows come from", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await request.post("/select/Select%20Page/set?option=Smart%20board");
  await expect(page.locator("#sbView .sbhint")).toHaveText("Send rows from Home Assistant");
  await expect(page.locator("#sbView .sbrow").first()).toBeHidden();
});

test("the header is the panel's name, and a slim bar keeps it once it scrolls away", async ({ page, request }) => {
  await reset(request, { nick: "Den" });
  await page.setViewportSize({ width: 390, height: 600 });
  await page.goto("/#device");
  await expect(page.locator("h1")).toHaveText("Den");
  await expect(page.locator("#minibar")).toHaveText("Den");
  await expect(page.locator("body")).not.toHaveClass(/scrolled/);
  await page.evaluate(() => window.scrollTo(0, 400));
  await expect(page.locator("body")).toHaveClass(/scrolled/);
  await page.evaluate(() => window.scrollTo(0, 0));
  await expect(page.locator("body")).not.toHaveClass(/scrolled/);
});

test("the nickname names the page and the header, and saving it posts nick", async ({ page, request }) => {
  await reset(request, { nick: "Garage" });
  await page.goto("/");
  await tab(page, "device");
  await expect(page.locator("#nick")).toHaveValue("Garage");
  await expect(page.locator("#devname")).toHaveText("Garage");
  await expect(page).toHaveTitle("Garage · Game Day Scoreboard");

  await page.locator("#nick").fill("  Living room ");
  await page.locator("#nick").press("Enter");
  await expectSet(request, { nick: "Living room" });
  await expect(page.locator("#toast")).toHaveText("Nickname saved");
  await expect.poll(async () => (await deviceState(request)).nick).toBe("Living room");
  await expect(page).toHaveTitle("Living room · Game Day Scoreboard");

  await page.locator("#nick").fill("");
  await page.locator("#nick").press("Enter");
  await expectSet(request, { nick: "" });
  await expect(page.locator("#toast")).toHaveText("Nickname cleared");
  await expect(page).not.toHaveTitle(/·/);
  await expect(page.locator("#devname")).not.toHaveText("Living room");
});

test("the nickname keeps the tab title when something else renames it, and the box while it is being typed in", async ({ page, request }) => {
  await reset(request, { nick: "Garage" });
  await page.goto("/");
  await tab(page, "device");
  await expect(page).toHaveTitle("Garage · Game Day Scoreboard");
  // ESPHome's own page script names the tab after the device on its pings.
  await page.evaluate(() => { document.title = "Game Day Scoreboard 2f6a70"; });
  await expect(page).toHaveTitle("Garage · Game Day Scoreboard");

  await page.locator("#nick").fill("Gar");
  await request.post("/gameday/set?nick=Den");
  await expect.poll(async () => (await deviceState(request)).nick).toBe("Den");
  // The page hears of it from the panel's next board update, not its 15 s poll.
  await push(request);
  await expect(page).toHaveTitle("Den · Game Day Scoreboard");
  await expect(page.locator("#nick")).toHaveValue("Gar");
});

test("the nickname box hides on firmware without it", async ({ page, request }) => {
  await reset(request, { nick: null });
  await page.goto("/");
  await tab(page, "device");
  await expect(page.locator("#hostname")).not.toBeEmpty();
  await expect(page.locator("#nickRow")).toBeHidden();
});

test("the message card posts its text, address and timing, and Clear turns it off", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await tab(page, "customize");
  await page.locator("#msgText").fill("Wings $5 till 7\nPints $3");
  await page.locator("#msgEvery").selectOption("0");
  await page.locator("#msgSave").click();
  await expectSet(request, { msg: "Wings $5 till 7\nPints $3", msgurl: "", msgevery: "0" });
  await expect.poll(async () => (await deviceState(request)).msg).toBe("Wings $5 till 7\nPints $3");

  await page.locator("#msgUrl").fill("names.txt");
  await page.locator("#msgSave").click();
  await expect(page.locator("#toast")).toHaveText("The list's address starts with http:// or https://");

  await page.locator("#msgUrl").fill("https://example.com/names.txt");
  await page.locator("#msgSave").click();
  await expectSet(request, { msg: "Wings $5 till 7\nPints $3", msgurl: "https://example.com/names.txt", msgevery: "0" });
  await expect(page.locator("#msgRead")).toHaveText("Not read yet. The panel reads it when the card's turn comes.");

  await page.locator("#msgClear").click();
  await expectSet(request, { msg: "", msgurl: "" });
  await expect(page.locator("#msgText")).toHaveValue("");
  await expect(page.locator("#msgUrl")).toHaveValue("");
});

test("the game events card saves the heads-up and the webhook, refuses https, and sends a test", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await tab(page, "device");
  await unfold(page, "#evCard");
  await expect(page.locator("#evCard")).toBeVisible();
  await expect(page.locator("#evPregame")).toHaveValue("15");
  await page.locator("#evPregame").selectOption("30");
  await expectSet(request, { pregame: "30" });

  await page.locator("#evHook").fill("https://example.com/hook");
  await page.locator("#evSave").click();
  await expect(page.locator("#toast")).toHaveText("Use plain http://, the panel posts on your home network");
  await page.locator("#evHook").fill("http://192.168.1.20:1880/gameday");
  await page.locator("#evSave").click();
  await expectSet(request, { hook: "http://192.168.1.20:1880/gameday" });
  await expect(page.locator("#evHookRead")).toHaveText("Nothing posted yet. The next event goes there.");

  await page.locator("#evTest").click();
  await expect.poll(() => actions(request)).toContainEqual({ do: "event_test" });
});

test("the game events card says how the last post went, and hides on firmware without it", async ({ page, request }) => {
  await reset(request, { hook: "http://192.168.1.20/a", hookpost: { status: 200, sent: 3, dropped: 0 } });
  await page.goto("/");
  await tab(page, "device");
  await expect(page.locator("#evHookRead")).toHaveText("Posting fine: 3 events delivered.");
  await reset(request, { hook: "http://192.168.1.20/a", hookpost: { status: 0, sent: 0, dropped: 0 } });
  await page.reload();
  await expect(page.locator("#evHookRead")).toHaveText("The last post got no answer. Check the address and that the listener is running.");
  await reset(request, { pregame: null, hook: null });
  await page.reload();
  await expect(page.locator("#hostname")).not.toBeEmpty();
  await expect(page.locator("#evCard")).toBeHidden();
});

test("the message card says how the list's last read went, and hides on firmware without it", async ({ page, request }) => {
  const url = "https://example.com/messages.txt";
  await reset(request, { msgurl: url, msgread: { status: 200, entries: 412 } });
  await page.goto("/");
  await tab(page, "customize");
  await expect(page.locator("#msgUrl")).toHaveValue(url);
  await expect(page.locator("#msgRead")).toHaveText("The list has 412 entries.");
  await reset(request, { msgurl: url, msgread: { status: 404 } });
  await page.goto("/");
  await tab(page, "customize");
  await expect(page.locator("#msgRead")).toHaveText("That address answered with an error (HTTP 404). It tries again in a few minutes.");
  await reset(request, { msgurl: url, msgread: { status: 0 } });
  await page.goto("/");
  await tab(page, "customize");
  await expect(page.locator("#msgRead")).toHaveText("The panel couldn't reach that address. It tries again in a few minutes.");

  await reset(request, { msg: null, msgurl: null, msgevery: null, msgread: null });
  await page.goto("/");
  await tab(page, "customize");
  await expect(page.locator("#hostname")).not.toBeEmpty();
  await expect(page.locator("#msgCard")).toBeHidden();
});

test("night schedule: off by default, its times, screen and brightness post, hidden on firmware without it", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await tab(page, "display");
  const sw = page.locator('.sw[data-set="night"]');
  await expect(page.locator("#nightCtls")).toBeVisible();
  await expect(sw).not.toHaveClass(/\bon\b/);
  await expect(page.locator("#nightSet")).toBeHidden();
  await expect(page.locator("#nightbar")).toBeHidden();

  await sw.click();
  await expectSet(request, { night: "1" });
  await expect(page.locator("#nightSet")).toBeVisible();
  await expect(page.locator("#nightStart")).toHaveValue("23:00");
  await expect(page.locator("#nightEnd")).toHaveValue("07:00");
  await expect(page.locator("#nightLevelRow")).toBeVisible();
  await expect(page.locator("#nightLevelHint")).toHaveText("How bright the panel is between 11 PM and 7 AM.");

  await page.locator("#nightStart").fill("22:30");
  await page.locator("#nightStart").dispatchEvent("change");
  await expectSet(request, { nightstart: "22:30" });
  await expect(page.locator("#nightLevelHint")).toHaveText("How bright the panel is between 10:30 PM and 7 AM.");

  // The same start and end would be no night at all, so it isn't posted.
  await page.locator("#nightEnd").fill("22:30");
  await page.locator("#nightEnd").dispatchEvent("change");
  await expect(page.locator("#toast")).toContainText("different time");
  await expect(page.locator("#nightEnd")).toHaveValue("07:00");
  expect((await sets(request)).some((q) => q.nightend)).toBe(false);

  await page.locator("#nightLevel").fill("40");
  await page.locator("#nightLevel").dispatchEvent("change");
  await expectSet(request, { nightlevel: "40" });

  // Screen off hides the slider and says the screen goes dark.
  await page.locator('.sw[data-set="nightscreen"]').click();
  await expectSet(request, { nightscreen: "0" });
  await expect(page.locator("#nightLevelRow")).toBeHidden();
  await expect(page.locator("#nightScreenHint")).toHaveText("Off: the screen goes dark between 10:30 PM and 7 AM.");

  await reset(request, { night: null, night_now: null });
  await page.goto("/");
  await tab(page, "display");
  await expect(page.locator("#hostname")).not.toBeEmpty();
  await expect(page.locator("#nightCtls")).toBeHidden();
});

test("game-day wake: off by default, its lead and after the game post, hidden on firmware without it", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await tab(page, "display");
  const sw = page.locator('.sw[data-set="wake"]');
  await expect(page.locator("#wakeCtls")).toBeVisible();
  await expect(sw).not.toHaveClass(/\bon\b/);
  await expect(page.locator("#wakeLeadRow")).toBeHidden();
  await expect(page.locator("#afterGame")).toHaveValue("scoreboard");
  await expect(page.locator("#afterHint")).toHaveText("The final, then your next game, as before.");

  await sw.click();
  await expectSet(request, { wake: "1" });
  await expect(page.locator("#wakeLeadRow")).toBeVisible();
  await expect(page.locator("#wakeLead")).toHaveValue("15");
  await page.locator("#wakeLead").selectOption("30");
  await expectSet(request, { wakelead: "30" });

  await page.locator("#afterGame").selectOption("off");
  await expectSet(request, { after: "off" });
  await expect(page.locator("#afterHint")).toHaveText(
    "Once the final has been up a while, the panel turns off. Game-day wake turns it back on before the next game.");
  await page.locator("#afterGame").selectOption("clock");
  await expectSet(request, { after: "clock" });
  await expect(page.locator("#afterHint")).toContainText("shows the time and your next game");
  await page.locator("#afterGame").selectOption("effects");
  await expectSet(request, { after: "effects" });
  await expect(page.locator("#afterHint")).toContainText("whenever no game of yours is on, the panel plays the effect");
  await page.locator("#afterGame").selectOption("flights");
  await expectSet(request, { after: "flights" });
  await expect(page.locator("#afterHint")).toContainText("the panel shows the planes near your town until your next game is close");
  await page.locator("#afterGame").selectOption("weather");
  await expectSet(request, { after: "weather" });
  await expect(page.locator("#afterHint")).toContainText("the panel shows the weather for your town until your next game is close");

  // Off with the wake off says how to bring it back, and asleep says so.
  await reset(request, { after: "off", wake_now: "asleep" });
  await page.reload();
  await expect(page.locator("#afterHint")).toHaveText(
    "Off since the last game. Turn on Game-day wake to bring it back before the next game.");
  // With the wake off, the lead still shows while After the game uses it.
  await expect(page.locator("#wakeLeadRow")).toBeVisible();

  // A lead the list lacks, set from an app, still shows.
  await reset(request, { wake: true, wakelead: 45 });
  await page.reload();
  await expect(page.locator("#wakeLead")).toHaveValue("45");

  await reset(request, { wake: null, wakelead: null, after: null, wake_now: null });
  await page.goto("/");
  await tab(page, "display");
  await expect(page.locator("#hostname")).not.toBeEmpty();
  await expect(page.locator("#wakeCtls")).toBeHidden();
});

test("night mode says it is on, until when, and when a live game holds it off", async ({ page, request }) => {
  await reset(request, { night: true, nightscreen: false, night_now: "off" });
  await page.goto("/");
  await expect(page.locator("#nightbar")).toBeVisible();
  await expect(page.locator("#nightName")).toHaveText("Night mode until 7:00 AM");
  await expect(page.locator("#nightText")).toContainText("The screen is off for the night");

  await reset(request, { night: true, nightend: "06:30", night_now: "dim" });
  await page.reload();
  await expect(page.locator("#nightName")).toHaveText("Night mode until 6:30 AM");
  await expect(page.locator("#nightText")).toContainText("dimmed for the night");

  await reset(request, { night: true, night_now: "game" });
  await page.reload();
  await expect(page.locator("#nightName")).toHaveText("Night mode paused for a live game");
  await expect(page.locator("#nightText")).toContainText("until 7:00 AM");

  await reset(request, { night: true, night_now: "" });
  await page.reload();
  await expect(page.locator("#hostname")).not.toBeEmpty();
  await expect(page.locator("#nightbar")).toBeHidden();
});

test("night mode can be turned off for the rest of tonight and back on, from its bar or Panel on", async ({ page, request }) => {
  await reset(request, { night: true, nightscreen: false, night_now: "off" });
  await page.goto("/");
  await expect(page.locator("#nightBtn")).toHaveText("Turn on for tonight");
  await page.locator("#nightBtn").click();
  await expect.poll(() => actions(request)).toContainEqual({ do: "night_skip" });
  await expect(page.locator("#nightName")).toHaveText("Night mode off until 11:00 PM");
  await expect(page.locator("#nightText")).toHaveText(
    "The panel is at normal brightness for the rest of tonight. Night mode starts again at 11:00 PM.");
  await expect(page.locator("#nightBtn")).toHaveText("Back to night mode");
  await page.locator("#nightBtn").click();
  await expect.poll(() => actions(request)).toContainEqual({ do: "night_resume" });
  await expect(page.locator("#nightName")).toHaveText("Night mode until 7:00 AM");

  // A live game already has it lit: nothing to turn on.
  await reset(request, { night: true, night_now: "game" });
  await page.reload();
  await expect(page.locator("#nightName")).toHaveText("Night mode paused for a live game");
  await expect(page.locator("#nightBtn")).toBeHidden();

  // Panel on while the night has it dim skips the night too; off doesn't.
  await reset(request, { night: true, night_now: "dim" });
  await page.reload();
  const power = page.getByRole("switch", { name: "Panel on" });
  await expect(power).toBeChecked();
  await power.click();
  await expect(power).not.toBeChecked();
  expect((await actions(request)).some((q) => q.do === "night_skip")).toBe(false);
  await power.click();
  await expect.poll(() => actions(request)).toContainEqual({ do: "night_skip" });
  await expect(page.locator("#nightName")).toHaveText("Night mode off until 11:00 PM");
});

test("Export downloads the settings, and Import restores a backup checked whole", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await tab(page, "device");
  await unfold(page, "#setupCard");
  await expect(page.locator("#backup")).toBeVisible();
  const [download] = await Promise.all([page.waitForEvent("download"), page.click("#bkExport")]);
  expect(download.suggestedFilename()).toMatch(/^gameday-2f6a70-settings-\d{4}-\d\d-\d\d\.json$/);
  const saved = fs.readFileSync(await download.path(), "utf8");
  const doc = JSON.parse(saved);
  // Saved compact, the size the panel takes back.
  expect(saved).toBe(JSON.stringify(doc) + "\n");
  expect(doc).toMatchObject({ schema: 1, name: "gameday-2f6a70", mode: 4,
                              favs: ["nfl:6", "nfl:34", "ncaa:145"], live: [], down: true, panels: 2, brightness: 128, scroll: 5 });
  await expect(page.locator("#bkNote")).toHaveText(/^Saved gameday-2f6a70-settings-/);

  const file = (name, body) => ({ name, mimeType: "application/json", buffer: Buffer.from(body) });
  const posts = async () => (await (await request.get("/_log")).json()).filter((p) => p.path === "/gameday/settings");

  // Another panel's backup, with an empty remote slot, its own nickname and a
  // key from newer firmware. This panel keeps its own nickname.
  const backup = { ...doc, name: "gameday-aba970", nick: "Den", mode: 5, favs: ["mlb:15", "", "nfl:6"],
                   down: false, brightness: 60, sparkle: true };
  let asked = "";
  page.once("dialog", (d) => { asked = d.message(); d.accept(); });
  await page.setInputFiles("#bkFile", file("den.json", JSON.stringify(backup)));
  await expect(page.locator("#bkNote")).toHaveText(
    "Restored. This panel kept its own nickname. This panel's firmware doesn't have: sparkle.");
  expect(asked).toBe("Replace this panel's settings with den.json (saved from gameday-aba970)? Wi-Fi networks and the WizMote pairing stay as they are, and so do this panel's nickname and layout.");
  const st = await deviceState(request);
  expect(st.nick).toBe("");
  expect(st.mode).toBe(5);
  expect(st.down).toBe(false);
  expect(st.favs.map((t) => t.abbr || "")).toEqual(["ATL", "", "DAL", ""]);
  expect((await (await request.get("/gameday/settings")).json()).brightness).toBe(60);

  // One bad value refuses the whole file.
  page.once("dialog", (d) => d.accept());
  await page.setInputFiles("#bkFile", file("bad.json", JSON.stringify({ ...doc, mode: "4" })));
  await expect(page.locator("#bkNote")).toHaveText("Not restored: mode should be a whole number. Nothing was changed.");
  expect((await deviceState(request)).mode).toBe(5);

  // Not a backup, or the owner says no: nothing is sent.
  const sent = (await posts()).length;
  await page.setInputFiles("#bkFile", file("notes.json", "[1, 2]"));
  await expect(page.locator("#bkNote")).toHaveText("That file isn't a settings backup.");
  page.once("dialog", (d) => d.dismiss());
  await page.setInputFiles("#bkFile", file("again.json", JSON.stringify(doc)));
  await sleep(300);
  expect((await posts()).length).toBe(sent);
  expect((await deviceState(request)).favs[0].abbr).toBe("ATL");

  // An indented file over 4 KB (older pages saved them that way) goes in,
  // sent compact.
  const roomy = JSON.stringify(doc, null, 2).replace(/\n/g, "\n" + " ".repeat(40));
  expect(roomy.length).toBeGreaterThan(4096);
  page.once("dialog", (d) => d.accept());
  await page.setInputFiles("#bkFile", file("roomy.json", roomy));
  await expect(page.locator("#bkNote")).toHaveText(/^Restored\./);
  expect((await posts()).at(-1).body).toBe(JSON.stringify(doc));
  expect((await deviceState(request)).favs[0].abbr).toBe("DAL");
});

test("Import of this panel's own backup with another layout waits out the restart", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await tab(page, "device");
  await unfold(page, "#setupCard");
  await expect(page.locator("#backup")).toBeVisible();
  let asked = "";
  page.once("dialog", (d) => { asked = d.message(); d.accept(); });
  await page.setInputFiles("#bkFile", { name: "one.json", mimeType: "application/json",
                                        buffer: Buffer.from(JSON.stringify({ schema: 1, name: "gameday-2f6a70", panels: 1 })) });
  await expect(page.locator("#gone")).toHaveText("Restarting with the restored panel layout. This page reconnects when the panel is back.");
  expect(asked).toBe("Replace this panel's settings with one.json? Wi-Fi networks and the WizMote pairing stay as they are.");
  expect((await deviceState(request)).panels).toBe(1);
});

test("Import of another panel's backup keeps this panel's layout", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await tab(page, "device");
  await unfold(page, "#setupCard");
  await expect(page.locator("#backup")).toBeVisible();
  page.once("dialog", (d) => d.accept());
  await page.setInputFiles("#bkFile", { name: "small.json", mimeType: "application/json",
                                        buffer: Buffer.from(JSON.stringify({ schema: 1, name: "gameday-aba970", panels: 1, down: false })) });
  await expect(page.locator("#bkNote")).toHaveText("Restored. This panel kept its own panel layout.");
  await expect(page.locator("#gone")).toBeHidden();
  const st = await deviceState(request);
  expect(st.panels).toBe(2);
  expect(st.down).toBe(false);
});

test("the backup section hides on firmware without it", async ({ page, request }) => {
  await reset(request, {}, ["settings"]);
  await page.goto("/");
  await tab(page, "device");
  await unfold(page, "#setupCard");
  await expect(page.locator("#tzline")).toBeVisible();
  await sleep(300);
  await expect(page.locator("#backup")).toBeHidden();
});

// These five were missing from the page's list of settings it holds while a
// post is out, so a tap showed only when a read came back.
test("Live pictures and update switches hold their tap through a slow read", async ({ page, request }) => {
  await reset(request, { autoupd: false, beta: false });
  await page.goto("/");
  await slowReads(page, 1000);
  await tab(page, "display");
  const field = page.locator('.sw[data-set="field"]');
  await expect(field).toHaveAttribute("aria-checked", "true");
  await field.click();
  // At once, not when the slow read lands.
  await expect(field).toHaveAttribute("aria-checked", "false", { timeout: 300 });
  for (const end = Date.now() + 2500; Date.now() < end; await sleep(100))
    expect(await field.getAttribute("aria-checked")).toBe("false");
  await expectSet(request, { field: "0" });
  await tab(page, "device");
  const beta = page.locator('.sw[data-set="beta"]');
  await beta.click();
  await expect(beta).toHaveAttribute("aria-checked", "true", { timeout: 300 });
  for (const end = Date.now() + 2500; Date.now() < end; await sleep(100))
    expect(await beta.getAttribute("aria-checked")).toBe("true");
});

test("a Panel on tap the panel never gets goes back", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  const power = page.locator("#quickCtls .sw");
  await expect(power).toHaveAttribute("aria-checked", "true");
  await page.route("**/light/**", (route) => route.abort());
  await power.click();
  await expect(page.locator("#toast")).toContainText("Device did not respond");
  await expect(power).toHaveAttribute("aria-checked", "true");
});

test("a mode tap says the mode's name alone", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await page.locator('#modes button[data-mode="5"]').click();
  await expect(page.locator("#toast")).toHaveText("Live games");
});

test("a settings save the panel never answers gives up after 8 seconds and the switch goes back", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await tab(page, "display");
  const sw = page.locator('.sw[data-set="play"]');
  await expect(sw).toHaveAttribute("aria-checked", "true");
  await page.route("**/gameday/set?*", () => {});  // never answered
  await sw.click();
  await expect(sw).toHaveAttribute("aria-checked", "false");
  await expect(page.locator("#toast")).toHaveText("Device did not respond (no answer). Try again.", { timeout: 10000 });
  await expect(sw).toHaveAttribute("aria-checked", "true");
});

// ---- the Flights page (components/gameday/flights.h) ----------------------
// The weather town the Flights page centers on, as the weather tests set it.
const flTown = { town: "75019", wxon: true, wx: { place: "Coppell, Texas", lat: 32.95, lon: -97.02, lookup: 200, read: 0, wait_s: 0 } };
// fl as the firmware sends it, with what a test changes.
const flDoc = (o) => Object.assign({ on: false, src: "adsblol", n: 0, heard: 0, read_s: -1, wait_s: 0, err: "none", place: "", radius: "" }, o);

test("flights: no card or After the game choice on firmware without the Flights page", async ({ page, request }) => {
  await reset(request, { flrad: null, flface: null, flrx: null, flkey_set: null, fl: null });
  await page.goto("/");
  await tab(page, "customize");
  await expect(page.locator("#hostname")).not.toBeEmpty();
  await expect(page.locator("#flCard")).toBeHidden();
  await tab(page, "display");
  await expect(page.locator('#afterGame option[value="flights"]')).toBeDisabled();
  await expect(page.locator('#afterGame option[value="flights"]')).toHaveJSProperty("hidden", true);
});

test("weather page: no After the game choice on firmware without it", async ({ page, request }) => {
  await reset(request, { wxpage: null });
  await page.goto("/");
  await tab(page, "display");
  await expect(page.locator('#afterGame option[value="flights"]')).toBeEnabled();
  await expect(page.locator('#afterGame option[value="weather"]')).toBeDisabled();
  await expect(page.locator('#afterGame option[value="weather"]')).toHaveJSProperty("hidden", true);
});

test("weather page: under Showing it says whose forecast, or asks for a town", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  const showing = page.getByRole("combobox", { name: "Showing" });
  await expect(page.locator('#afterGame option[value="weather"]')).toHaveJSProperty("hidden", false);
  await showing.selectOption("Weather");
  await expect(page.locator("#wxpCtls")).toBeVisible();
  await expect(page.locator("#flCtls")).toBeHidden();
  await expect(page.locator("#wxpHint")).toHaveText("Weather shows the forecast for your town. Set your town first.");
  await expect(page.locator("#wxpTown")).toHaveText("Set your town");

  await reset(request, flTown);
  await page.goto("/");
  await showing.selectOption("Weather");
  await expect(page.locator("#wxpHint")).toHaveText(
    "Weather stays up, through restarts too, until you pick another page. The forecast for Coppell, Texas.");
  await expect(page.locator("#wxpTown")).toHaveText("Change your town");
  // The link opens the Weather card's town
  await page.locator("#wxpTown").click();
  await expect(page.locator("#town")).toBeVisible();
  await tab(page, "board");
  await showing.selectOption("Scoreboard");
  await expect(page.locator("#wxpCtls")).toBeHidden();
});

test("flights: Within and Facing post, in the weather's units, and the card says where home is", async ({ page, request }) => {
  await reset(request, flTown);
  await page.goto("/");
  await tab(page, "customize");
  const card = page.locator("#flCard");
  await expect(card).toBeVisible();
  await expect(page.locator("#flHome")).toHaveText("Planes near Coppell, Texas");
  await expect(page.locator("#flTown")).toBeHidden();
  const within = card.getByRole("combobox", { name: "Within" });
  await expect(within.locator("option")).toHaveText(["5 mi", "10 mi", "15 mi", "25 mi", "50 mi"]);
  await expect(within).toHaveValue("1");
  await within.selectOption({ label: "25 mi" });
  await expectSet(request, { flrad: "3" });
  await expect(within).toHaveValue("3");
  const facing = card.getByRole("combobox", { name: /^Facing/ });
  await expect(facing).toHaveValue("0");
  await expect(facing.locator("option").first()).toHaveText("Any direction");
  await facing.selectOption({ label: "Southwest" });
  await expectSet(request, { flface: "6" });
  await expect.poll(async () => (await deviceState(request)).flface).toBe(6);

  // Celsius: the same steps in kilometers.
  await reset(request, Object.assign({ wxunit: 2, flrad: 3 }, flTown));
  await page.reload();
  await tab(page, "customize");
  await expect(within.locator("option")).toHaveText(["10 km", "15 km", "25 km", "40 km", "80 km"]);
  await expect(within).toHaveValue("3");
});

test("flights: with no town the card points to Weather", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await tab(page, "customize");
  await expect(page.locator("#flHome")).toBeHidden();
  const go = page.getByRole("button", { name: "Set your town in Weather first" });
  await expect(go).toBeVisible();
  // The Weather card is off, so the sheet that adds it opens on Display.
  await go.click();
  await expect(page.locator("#pane-display")).toBeVisible();
  await expect(page.locator(".lsheet").getByRole("button", { name: "Add Weather" })).toBeVisible();
});

test("flights: the receiver address saves and clears, and one with a space stays here", async ({ page, request }) => {
  await reset(request, flTown);
  await page.goto("/");
  await tab(page, "customize");
  const rx = page.getByRole("textbox", { name: /^ADS-B receiver/ });
  await expect(page.locator("#flRxClear")).toBeHidden();
  await rx.fill("192.168.1.50 ");
  await page.locator("#flRxSave").click();
  await expectSet(request, { flrx: "192.168.1.50" });
  await expect(page.locator("#flRxClear")).toBeVisible();
  await rx.fill("my receiver");
  await page.locator("#flRxSave").click();
  await expect(page.locator("#toast")).toHaveText("The receiver address can't have spaces.");
  await page.locator("#flRxClear").click();
  await expectSet(request, { flrx: "" });
  await expect(rx).toHaveValue("");
  await expect.poll(async () => (await deviceState(request)).flrx).toBe("");
});

test("flights: the adsb.lol key saves and clears and is never shown", async ({ page, request }) => {
  await reset(request, flTown);
  await page.goto("/");
  await tab(page, "customize");
  const key = page.locator("#flKey");
  await expect(key).toHaveAttribute("type", "password");
  await expect(page.locator("#flKeySet")).toBeHidden();
  await key.fill("ab12-secret-key");
  await page.locator("#flKeySave").click();
  await expectSet(request, { flkey: "ab12-secret-key" });
  // The box empties at once and the card says only that a key is saved.
  await expect(key).toHaveValue("");
  await expect(page.locator("#flKeySet")).toBeVisible();
  await expect(page.locator("#flKeySet")).toContainText("Key saved");
  await expect(page.locator("#flKeyNew")).toBeHidden();
  const st = await deviceState(request);
  expect(st.flkey_set).toBe(true);
  expect(JSON.stringify(st)).not.toContain("ab12-secret-key");
  expect(await page.content()).not.toContain("ab12-secret-key");
  // A reload shows the same, with nothing in the box.
  await page.reload();
  await tab(page, "customize");
  await expect(page.locator("#flKeySet")).toBeVisible();
  await expect(page.locator("#flKeyNew")).toBeHidden();
  expect(await page.content()).not.toContain("ab12-secret-key");

  await page.locator("#flKeyClear").click();
  await expectSet(request, { flkey: "" });
  await expect(page.locator("#flKeyNew")).toBeVisible();
  await expect(page.locator("#flKeySet")).toBeHidden();
  expect((await deviceState(request)).flkey_set).toBe(false);
});

test("flights: the status line says what the page is doing", async ({ page, request }) => {
  await reset(request, flTown);
  await page.goto("/");
  const status = page.locator("#flStatus");
  await tab(page, "customize");
  await expect(status).toHaveText("Not reading: Flights isn't on screen");
  // Up under Showing: the mock's first read, said under Showing too.
  await tab(page, "board");
  await page.getByRole("combobox", { name: "Showing" }).selectOption("Flights");
  await expect(page.locator("#flCtls")).toBeVisible();
  await expect(page.locator("#flNow")).toHaveText("8 planes nearby, from adsb.lol, read 20 s ago");
  await page.locator("#flGo").click();
  await expect(page.locator("#flCard")).toBeVisible();
  await expect(status).toHaveText("8 planes nearby, from adsb.lol, read 20 s ago");

  const cases = [
    [{ flrx: "192.168.1.50", fl: flDoc({ n: 1, heard: 1, read_s: 5 }) }, "1 plane nearby, from your receiver, read 5 s ago"],
    [{ fl: flDoc({ n: 0, read_s: 90 }) }, "No planes nearby, from adsb.lol, read 2 min ago"],
    [{ fl: flDoc({ err: "limit", wait_s: 3 * 3600 }) }, "Paused: adsb.lol's daily limit, back in 3 h"],
    [{ fl: flDoc({ err: "net" }) }, "Can't reach flight data"],
    [{ fl: flDoc({ err: "http", wait_s: 240 }) }, "Can't reach flight data, tries again in 4 min"],
    [{ flrx: "192.168.1.50", fl: flDoc({ err: "net" }) }, "Can't reach your receiver"],
    // The panel's cached document keeps an old read_s; the live ages win.
    [{ fl: flDoc({ n: 8, read_s: 0 }), ages: { wx_read_s: -1, fl_read_s: 45, fl_wait_s: 0 } }, "8 planes nearby, from adsb.lol, read 45 s ago"],
  ];
  for (const [o, want] of cases) {
    await reset(request, Object.assign({}, flTown, o));
    await request.post("/select/Select%20Page/set?option=Flights");
    await page.reload();
    await tab(page, "customize");
    await expect(status).toHaveText(want);
  }
});

test("flights: the credit line and its links are always on the card", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await tab(page, "customize");
  const card = page.locator("#flCard");
  await expect(card).toContainText(
    "Plane data from adsb.lol (ODbL) and routes from adsbdb, both free community services. If you enjoy this, please support them:");
  for (const [name, href] of [["Sponsor adsb.lol", "https://www.adsb.lol/sponsor"],
    ["Share your receiver", "https://www.adsb.lol/docs/get-started/introduction"],
    ["Request a free key", "https://www.adsb.lol/request"]]) {
    const a = card.getByRole("link", { name });
    await expect(a).toHaveAttribute("href", href);
    await expect(a).toHaveAttribute("target", "_blank");
    await expect(a).toHaveAttribute("rel", "noopener");
  }
});
