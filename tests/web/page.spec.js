// Device page (firmware/web/bundle.js) against mock_device.py. Each test
// resets the mock, optionally with state overrides, and checks what the page
// shows and what it posts back to /gameday/set.
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
const sets = async (request) =>
  (await (await request.get("/_log")).json()).filter((p) => p.path === "/gameday/set").map((p) => p.q);
const expectSet = (request, q) => expect.poll(() => sets(request)).toContainEqual(q);
const deviceState = async (request) => (await request.get("/gameday/state")).json();
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

const LIVE = {
  s: "IN", ts: 14, os: 10, p: 1, tt: 2, ot: 3, rz: true,
  c: "8:12 - 2nd", d: "3rd & 4 at PHI 18", lp: "Prescott pass to Lamb for 12 yards",
};

test("shows a live game and the settings from /gameday/state", async ({ page, request }) => {
  await reset(request);
  const g = (await (await request.get("/gameday/state")).json()).game;
  await reset(request, { game: { ...g, ...LIVE }, odds: false, status: "DAL 14 PHI 10 | 8:12 2nd" });
  await page.goto("/");
  await expect(page.locator("#conn")).toHaveClass(/\bon\b/);
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

  await expect(page.locator("#modes button.on")).toHaveText("My team");
  await expect(page.locator("#teambar")).toBeVisible();
  await expect(page.locator("#curName")).toHaveText("Dallas Cowboys");
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
  // The game on the board is left out of Up next.
  await expect(page.locator("#upnextList .un .n")).toHaveText(["@ Commanders", "vs Texans"]);
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
  await reset(request, {
    game: { ...g, id: "1", oa: "MINN", kick: sat, tbd: true, k: "Oct 24 TBD", detail: "TBD" },
    next: [
      { id: "1", kick: sat, tbd: true, oi: 135, oa: "MINN", on: "Minnesota", home: false, neutral: false, tv: "" },
      { id: "2", kick: sat + 7 * 86400, tbd: true, oi: 356, oa: "ILL", on: "Illinois", home: true, neutral: false, tv: "" },
      { id: "3", kick: 1795798800, oi: 2509, oa: "PUR", on: "Purdue", home: true, neutral: false, tv: "BTN" },
    ],
  });
  await page.goto("/");
  await expect(page.locator("#clock")).toHaveText("Oct 24 TBD");
  await expect(page.locator("#upnextList .un .w")).toHaveText(["Sat, Oct 31 TBD", /^Fri, Nov 27 11:00\sAM · BTN$/]);

  const t = (abbr, name, id) => ({ l: "ncaa", id, abbr, name });
  await reset(request, {
    mode: 4,
    next: [{ slot: 1, t: t("IOWA", "Iowa Hawkeyes", 2294), id: "1", s: "PRE", kick: sat, tbd: true, oi: 135, oa: "MINN", ts: 0, os: 0, tv: "", detail: "TBD" }],
  });
  await page.reload();
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

test("no game renders the empty board for my team and for live modes", async ({ page, request }) => {
  await reset(request, { game: { s: "NOT_FOUND", l: "nfl" }, next: [] });
  await page.goto("/");
  await expect(page.locator("#board")).toHaveClass(/empty/);
  await expect(page.locator("#gstate")).toHaveText("NO GAME");
  await expect(page.locator("#msg")).toHaveText("No upcoming game for this team");
  await expect(page.locator("#upnext")).toBeHidden();

  await reset(request, { mode: 2, game: { s: "NOT_FOUND", l: "ncaa" }, next: [] });
  await page.reload();
  await expect(page.locator("#msg")).toHaveText("No live games right now");
  await expect(page.locator("#livebar")).toBeVisible();

  await reset(request, { mode: 4, game: { s: "NOT_FOUND", l: "nfl" }, next: [] });
  await page.reload();
  await expect(page.locator("#msg")).toHaveText("None of your favorites has a game coming up");
  await expect(page.locator("#favbar")).toBeVisible();

  // No team picked: My team runs the live board, so it says the same
  await reset(request, { mode: 0, team: {}, game: { s: "NOT_FOUND", l: "nfl" }, next: [] });
  await page.reload();
  await expect(page.locator("#msg")).toHaveText("No live games right now");
  await expect(page.locator("#curName")).toHaveText("No team chosen");
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
  await expect(page.locator("#msg")).toHaveText("No upcoming game for this team");
});

test("a device that never answers /gameday/state leaves the page waiting, not broken", async ({ page, request }) => {
  await reset(request, {}, ["state"]);
  await page.goto("/");
  await expect(page.locator("#conn")).toHaveClass(/\bon\b/);
  await expect(page.locator("#gstate")).toHaveText("WAITING");
  await expect(page.locator("#board")).toHaveClass(/empty/);
  await expect(page.locator("#curName")).toHaveText("No team chosen");
  await expect(page.locator("#modes button")).toHaveText(["My team", "Live games", "Favorite teams"]);
  // Entity controls still come from the event stream.
  await expect(page.locator("#panelCtls .ctl")).toHaveCount(4);
});

test("Diagnostics shows the Wi-Fi history, its drops and the weak-signal hint", async ({ page, request }) => {
  await reset(request, {});
  await page.goto("/");
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
  await expect(page.locator("#wifiDrops")).toHaveText("None");
  await expect(page.locator("#wifiLast")).toHaveText("");
  await expect(page.locator("#wifiSparkBox")).toBeHidden();

  // Firmware before the history: the rows stay hidden, Signal still comes from the RSSI sensor.
  await reset(request, { wifi: null });
  await page.reload();
  await expect(page.locator("#hostname")).toHaveText("gameday-2f6a70.local");
  await expect(page.locator("#wifiDiag")).toBeHidden();
});

test("a failed settings post is retried once, then reported", async ({ page, request }) => {
  await reset(request, {}, ["set"]);
  await page.goto("/");
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
  await expect(page.locator("#modes button.on")).toHaveText("My team");
  await page.locator('#modes button[data-mode="5"]').click();
  await expectSet(request, { mode: "5" });
  await expect(page.locator("#modes button.on")).toHaveText("Live games");
  await expect(page.locator("#livebar")).toBeVisible();
  await expect(page.locator("#teambar")).toBeHidden();

  await page.locator("#rotate").fill("45");
  await page.locator("#rotate").press("Enter");
  await expectSet(request, { rotate: "30" }); // the page clamps to 1..30
  await expect(page.locator("#rotate")).toHaveValue("30");

  await page.locator('#modes button[data-mode="0"]').click();
  await expectSet(request, { mode: "0" });
  await expect(page.locator("#teambar")).toBeVisible();
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
  await expect(page.locator("#favName")).toHaveText("Locked on Houston Texans");
  await expect(page.locator("#upnextList .un.fav .n")).toHaveText(["DAL vs PHI", "HOU vs JAX"]);
  await expect(page.locator("#upnextList .un.shown .w")).toHaveText("14 - 10 · 8:12 - 2nd");

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

test("the team picker searches the team list and posts the team", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await expect(page.locator("#curName")).toHaveText("Dallas Cowboys");
  await page.locator("#pick").click();
  await expect(page.locator("#modal")).toHaveClass(/open/);
  await expect(page.locator(".tabs button.on")).toHaveText("NFL");
  await page.locator("#q").fill("eagles");
  await expect(page.locator("#tiles .tile .n")).toContainText(["Philadelphia Eagles", "Boston College Eagles"]);
  await page.locator("#tiles .tile", { hasText: "Philadelphia Eagles" }).click();
  await expectSet(request, { team: "nfl:21" });
  await expect(page.locator("#modal")).not.toHaveClass(/open/);
  await expect(page.locator("#curName")).toHaveText("Philadelphia Eagles");
});

test("the picker opens on today's live games and follows a side", async ({ page, request }) => {
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
  await expect(page.locator("#curName")).toHaveText("Dallas Cowboys");
  await page.locator("#pick").click();
  await expect(page.locator(".tabs button.on")).toHaveText("On now");
  await expect(page.locator("#tiles .gh")).toHaveText(["Live now"]);
  await expect(page.locator("#tiles .side.on .n")).toHaveText("Cowboys");
  await page.locator("#tiles .side", { hasText: "Eagles" }).click();
  await expectSet(request, { team: "nfl:21" });
  await expect(page.locator("#modal")).not.toHaveClass(/open/);
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
  await page.locator("#pick").click();
  await expect(page.locator(".tabs button.on")).toHaveText("On now");
  await expect(page.locator("#tiles .side .n")).toHaveText(["Abilene Chrstn", "Akron"]);
  expect(asked.mcbb.get("groups")).toBe("50");
  expect(asked.mcbb.get("limit")).toBe("300");
  expect(asked.mcbb.get("dates")).toMatch(/^\d{8}$/);
  expect(asked.ncaa.get("groups")).toBe("80");
});

test("ticker, celebration and startup switches post 0 and 1", async ({ page, request }) => {
  await reset(request, { play: false });
  await page.goto("/");
  await expect(page.locator('.sw[data-set="down"]')).toHaveClass(/\bon\b/);
  await page.locator('.sw[data-set="down"]').click();
  await expectSet(request, { down: "0" });
  await page.locator('.sw[data-set="play"]').click();
  await expectSet(request, { play: "1" });
  await page.locator('.sw[data-set="bootaddr"]').click();
  await expectSet(request, { bootaddr: "0" });
  await page.locator('.sw[data-set="today"]').click();
  await expectSet(request, { today: "1" });
  await expect(page.locator('.sw[data-set="down"]')).not.toHaveClass(/\bon\b/);
  await expect(page.locator('.sw[data-set="play"]')).toHaveClass(/\bon\b/);
});

test("a screen reader gets each control's name, role and state", async ({ page, request }) => {
  await reset(request, { play: false });
  await page.goto("/");
  await expect(page.locator("#conn")).toHaveClass(/on/);
  await expect(page.locator("#conn")).toHaveAttribute("aria-live", "polite");
  await expect(page.locator("#toast")).toHaveAttribute("role", "status");

  await expect(page.getByRole("switch", { name: "Down and distance" })).toBeChecked();
  await expect(page.getByRole("switch", { name: "Last play" })).not.toBeChecked();
  await page.getByRole("switch", { name: "Last play" }).click();
  await expectSet(request, { play: "1" });
  await expect(page.getByRole("switch", { name: "Last play" })).toBeChecked();
  // The entity controls built from the event stream
  await expect(page.getByRole("switch", { name: "Panel on" })).toBeChecked();
  await expect(page.getByRole("slider", { name: "Brightness" })).toHaveValue("128");
  await expect(page.getByRole("slider", { name: "Ticker speed" })).toHaveValue("5");
  await expect(page.getByRole("combobox", { name: "Showing" })).toHaveValue("Scoreboard");

  const modes = page.getByRole("group", { name: "Mode" });
  await expect(modes.getByRole("button", { pressed: true })).toHaveText("My team");
  await expect(modes.getByRole("button", { pressed: false })).toHaveText(["Live games", "Favorite teams"]);
  await expect(page.getByRole("button", { name: /^One 128x64 panel/ })).toHaveAttribute("aria-pressed", "true");
  await expect(page.getByRole("button", { name: "One 64x64 panel" })).toHaveAttribute("aria-pressed", "false");

  // The picker's league tabs: one selected, in the Tab order, the arrow keys move between them.
  await page.getByRole("button", { name: "Change team" }).click();
  await expect(page.getByRole("searchbox", { name: "Search teams" })).toBeFocused();
  const tabs = page.getByRole("tablist", { name: "Leagues" });
  await expect(tabs.getByRole("tab", { selected: true })).toHaveText("NFL");
  await expect(tabs.locator('[tabindex="0"]')).toHaveText("NFL");
  await tabs.getByRole("tab", { name: "NFL" }).press("ArrowRight");
  await expect(tabs.getByRole("tab", { selected: true })).toHaveText("NCAAF");
  await expect(tabs.getByRole("tab", { name: "NCAAF" })).toBeFocused();
  await expect(page.getByRole("tabpanel").locator(".tile .a").first()).toContainText("NCAAF");
  await page.keyboard.press("End");
  await expect(tabs.getByRole("tab", { selected: true })).toHaveText("NHL");
  await page.keyboard.press("ArrowRight");
  await expect(tabs.getByRole("tab", { selected: true })).toHaveText("On now");
  await expect(tabs.locator('[tabindex="0"]')).toHaveText("On now");
  await page.getByRole("button", { name: "Close" }).click();

  await reset(request, { mode: 5, live: ["mlb", "nhl"], live_auto: false });
  await page.reload();
  await expect(modes.getByRole("button", { pressed: true })).toHaveText("Live games");
  const chips = page.getByRole("group", { name: "Leagues" });
  await expect(chips.getByRole("button", { pressed: true })).toHaveText(["MLB", "NHL"]);
  await chips.getByRole("button", { name: "NBA", exact: true }).click();
  await expectSet(request, { live: "mlb,nhl,nba" });
  await expect(chips.getByRole("button", { pressed: true })).toHaveText(["MLB", "NBA", "NHL"]);
});

test("bar build: no switch, a bar beta note and an email with the panel's name", async ({ page, request }) => {
  await reset(request, { autoupd: null, bar: { report: "support@gameday-scoreboard.com" } });
  await page.goto("/");
  await expect(page.locator("#hostname")).not.toBeEmpty();
  await expect(page.locator("#autoupdRow")).toBeHidden();
  await expect(page.locator("#barRow")).toBeVisible();
  const href = await page.locator("#barReport").getAttribute("href");
  expect(href).toMatch(/^mailto:support@gameday-scoreboard\.com\?subject=Bar%20panel%20gameday-2f6a70&body=/);
  expect(decodeURIComponent(href.split("body=")[1])).toContain("Panel: gameday-2f6a70");

  await reset(request);
  await page.goto("/");
  await expect(page.locator("#hostname")).not.toBeEmpty();
  await expect(page.locator("#barRow")).toBeHidden();
});

test("favorites post the whole list in order on add, move and remove", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  const rows = page.locator("#favCtls .ctl.fav");
  await expect(rows).toHaveCount(3);
  await expect(rows.nth(0).locator("small")).toHaveText("NFL · remote button 1");

  await page.locator("#favAdd").click();
  await expect(page.locator("#modal")).toHaveClass(/open/);
  await page.locator(".tabs button", { hasText: "MLB" }).click();
  await page.locator("#tiles .tile", { hasText: "New York Yankees" }).click();
  await expectSet(request, { favs: "nfl:6,nfl:34,ncaa:145,mlb:10" });
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

test("the favorites list stops offering Add a favorite at the panel's limit", async ({ page, request }) => {
  const mlb = (n) => Array.from({ length: n }, (_, i) => ({ l: "mlb", id: i + 1 }));
  await reset(request, { favs: mlb(15) });
  await page.goto("/");
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
  await expect(page.locator("#modes button.on")).toHaveText("Live games");
  await expect(page.locator("#liveChips .chip")).toHaveText(["NFL", "NCAAF", "MLB", "MLS", "EPL", "NBA", "WNBA", "NCAAM", "NHL"]);
  await expect(page.locator("#liveChips .chip.on")).toHaveText(["MLB", "NHL"]);
  await page.locator("#liveChips .chip", { hasText: /^NBA$/ }).click();
  await expectSet(request, { live: "mlb,nhl,nba" });
  await expect(page.locator("#liveChips .chip.on")).toHaveText(["MLB", "NBA", "NHL"]);

  // The last league can't be switched off.
  await reset(request, { mode: 5, live: ["mlb"], live_auto: false });
  await page.reload();
  await page.locator("#liveChips .chip", { hasText: /^MLB$/ }).click();
  await expect(page.locator("#toast")).toHaveText("Pick at least one league");
  expect(await sets(request)).toEqual([]);

  // Mode 2 (live college football, from before) reads as Live games with NCAAF on.
  await reset(request, { mode: 2 });
  await page.reload();
  await expect(page.locator("#modes button.on")).toHaveText("Live games");
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
  // Nothing picked: the leagues of the team and favorites (NFL and NCAAF in the mock), drawn as a suggestion.
  await reset(request, { mode: 5 });
  await page.goto("/");
  await expect(lg).toHaveText("Moves on when it ends, or after Switch every. Leagues follow your team and favorites. Tap a league to choose by hand:");
  await expect(page.locator("#liveChips")).toHaveClass(/auto/);
  await expect(chipsOn(page)).toHaveText(["NFL", "NCAAF"]);
  await expect(back).toBeHidden();

  // The first tap posts the whole list with the tap applied, and the bar
  // turns manual before the device answers.
  await slowReads(page, 1000);
  await chip(page, "MLB").click();
  await expect(lg).toHaveText("Moves on when it ends, or after Switch every. Leagues:");
  await expect(page.locator("#liveChips")).not.toHaveClass(/auto/);
  await expect(back).toBeVisible();
  await expectSet(request, { live: "nfl,ncaa,mlb" });
  await staysAs(lg, ["Moves on when it ends, or after Switch every. Leagues:"], 1500);
  expect((await deviceState(request)).live_auto).toBe(false);

  // Back to automatic posts an empty list and the device works the leagues out again.
  await back.click();
  await expectSet(request, { live: "" });
  await expect(lg).toContainText("Leagues follow your team and favorites.");
  await expect(back).toBeHidden();
  await staysAs(lg, ["Moves on when it ends, or after Switch every. Leagues follow your team and favorites. Tap a league to choose by hand:"], 1500);
  await expect(chipsOn(page)).toHaveText(["NFL", "NCAAF"]);
  const st = await deviceState(request);
  expect([st.live_auto, st.live]).toEqual([true, ["nfl", "ncaa"]]);
  expect(await sets(request)).toEqual([{ live: "nfl,ncaa,mlb" }, { live: "" }]);

  // An old football live mode has fixed leagues: no automatic wording, no link.
  await reset(request, { mode: 2 });
  await page.reload();
  await expect(lg).toHaveText("Moves on when it ends, or after Switch every. Leagues:");
  await expect(back).toBeHidden();
});

test("Live games bar says when nothing is live and the panel shows your team or finals", async ({ page, request }) => {
  const name = page.locator("#liveName");
  await reset(request, { mode: 5, live: ["mlb"], live_auto: false });
  await page.goto("/");
  await expect(name).toHaveText("Following a random game in progress");

  await reset(request, { mode: 5, live: ["mlb"], live_auto: false, fallback: true });
  await page.reload();
  await expect(name).toHaveText("No live game right now, showing your team until one starts");
  await expect(page.locator("#liveLg")).toHaveText("Leagues:");

  await reset(request, { mode: 5, team: {}, recap: true, game: { s: "POST", l: "mlb" } });
  await page.reload();
  await expect(name).toHaveText("No live game right now, showing recent finals");
  await expect(page.locator("#liveLg")).toHaveText("Leagues follow your team and favorites. Tap a league to choose by hand:");
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
  await page.locator('#modes button[data-mode="0"]').click();
  expect(await page.locator("#teambar").isVisible()).toBe(true);
  await page.locator('#modes button[data-mode="5"]').click();
  expect(await page.locator("#livebar").isVisible()).toBe(true);
  await expect.poll(() => sets(request)).toEqual([{ mode: "4" }, { mode: "0" }, { mode: "5" }]);
  await staysAs(page.locator("#modes button.on"), ["Live games"], 2500);
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
  await expect(page.locator("#modes button.on")).toHaveText("Live games");
  expect((await deviceState(request)).mode).toBe(2);
});

test("install updates automatically: off by default, posts 0 and 1, hidden when the firmware has no such setting", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
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
  await expect(page.locator("#hostname")).not.toBeEmpty();
  await expect(page.locator("#autoupdRow")).toBeHidden();
});

test("follow beta: off by default, posts 0 and 1, hidden when the firmware has no beta channel", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
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
  await expect(page.locator("#hostname")).not.toBeEmpty();
  await expect(page.locator("#betaRow")).toBeHidden();
});

test("a beta is newer than its release number, and a release is behind the beta before it", async ({ page, request }) => {
  await reset(request, { beta: true });
  const fw = { id: "update/Firmware", domain: "update", name: "Firmware", state: "UPDATE AVAILABLE", summary: "", release_url: "" };
  let entity = { ...fw, value: "1.5.5-beta.812", current_version: "1.5.5" };
  await page.route("**/update/Firmware*", (route) => route.fulfill({ json: entity }));
  await page.goto("/");
  await expect(page.locator("#fwText")).toHaveText("Update available: v1.5.5-beta.812");
  await expect(page.locator("#fwInstall")).toBeVisible();

  // Beta turned off on a beta build: the release it was built from is not an update.
  entity = { ...fw, value: "1.5.5", current_version: "1.5.5-beta.812" };
  await page.reload();
  await expect(page.locator("#fwText")).toHaveText("You're ahead of the latest release, v1.5.5-beta.812");
  await expect(page.locator("#fwInstall")).toBeHidden();

  // The next beta is newer than the last.
  entity = { ...fw, value: "1.5.6-beta.3", current_version: "1.5.5-beta.812" };
  await page.reload();
  await expect(page.locator("#fwText")).toHaveText("Update available: v1.5.6-beta.3");
});

test("an update that rolled back says so and still offers Install", async ({ page, request }) => {
  await reset(request, { autoupd: true, update_failed: "2026.10.1" });
  const fw = { id: "update/Firmware", domain: "update", name: "Firmware", state: "UPDATE AVAILABLE", summary: "", release_url: "" };
  let entity = { ...fw, value: "2026.10.1", current_version: "2026.10.0" };
  await page.route("**/update/Firmware*", (route) => route.fulfill({ json: entity }));
  await page.goto("/");
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

test("a team picked in My team stays on the page through a slow re-read", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await expect(page.locator("#curName")).toHaveText("Dallas Cowboys");
  await slowReads(page, 1000);
  await page.locator("#pick").click();
  await page.locator(".tabs button", { hasText: "NHL" }).click();
  await page.locator("#tiles .tile", { hasText: "Anaheim Ducks" }).click();
  await expect(page.locator("#curName")).toHaveText("Anaheim Ducks");
  await staysAs(page.locator("#curName"), ["Anaheim Ducks"], 2500);
  expect((await deviceState(request)).team.id).toBe(25);
});

test("favorites and switches keep each tap through a slow re-read", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  const labels = page.locator("#favCtls .ctl.fav label");
  await expect(labels).toHaveCount(3);
  await slowReads(page, 1000);
  await page.locator("#favCtls .ctl.fav").nth(0).getByRole("button", { name: "Remove" }).click();
  await sleep(600);
  await page.locator("#favCtls .ctl.fav").nth(0).getByRole("button", { name: "Move down" }).click();
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

test("situation switches: one per sport the panel follows, each posting its own key", async ({ page, request }) => {
  const sw = (name) => page.getByRole("switch", { name, exact: true });
  // Nothing picked yet: every sport's switch.
  await reset(request, { team: {}, favs: [{}, {}, {}, {}] });
  await page.goto("/");
  await expect(sw("Down and distance")).toBeVisible();
  await expect(sw("Count and at-bat")).toBeVisible();
  await expect(sw("Match stats")).toBeVisible();

  // A Yankees panel with an Arsenal favorite: baseball and soccer, no football.
  await reset(request, {
    team: { l: "mlb", id: 10, abbr: "NYY", name: "New York Yankees" },
    favs: [{ l: "epl", id: 359, abbr: "ARS", name: "Arsenal" }, {}, {}, {}],
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

  // Hockey only: no situation line, so no switch.
  await reset(request, { team: { l: "nhl", id: 25, abbr: "ANA", name: "Anaheim Ducks" }, favs: [{}, {}, {}, {}] });
  await page.reload();
  await expect(page.locator("#tickerCtls .ctl[data-sport]:visible")).toHaveCount(0);
  await expect(sw("Last play")).toBeVisible();

  // Live games leagues picked by hand count too.
  await reset(request, { team: { l: "nhl", id: 25, abbr: "ANA", name: "Anaheim Ducks" }, favs: [{}, {}, {}, {}],
    live_auto: false, live: ["nhl", "mls"] });
  await page.reload();
  await expect(sw("Match stats")).toBeVisible();
  await expect(sw("Down and distance")).toBeHidden();
});

test("the picker has a tab per league and follows a team from another sport", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await page.locator("#pick").click();
  await expect(page.locator(".tabs button")).toHaveText(["On now", "NFL", "NCAAF", "MLB", "MLS", "EPL", "NBA", "WNBA", "NCAAM", "NHL"]);
  await page.locator(".tabs button", { hasText: "NHL" }).click();
  await expect(page.locator(".tabs button.on")).toHaveText("NHL");
  await expect(page.locator("#tiles .tile")).toHaveCount(32);
  await page.locator("#tiles .tile", { hasText: "Anaheim Ducks" }).click();
  await expectSet(request, { team: "nhl:25" });
  await expect(page.locator("#curName")).toHaveText("Anaheim Ducks");
  await expect(page.locator("#curLg")).toHaveText("NHL");
});

test("other sports show their situation line and no timeout pips", async ({ page, request }) => {
  const mlb = {
    s: "IN", l: "mlb", sport: 1, ta: "NYY", ti: 10, ts: 3, oa: "TOR", oi: 14, os: 2, p: 0, tt: 0, ot: 0,
    c: "Bot 7th", sit: "2-1", outs: 2, bases: 5, d: "", k: "", tc: "0C2340", oc: "134A8E", id: "401800001",
  };
  await reset(request, { team: { l: "mlb", id: 10, abbr: "NYY", name: "New York Yankees" }, game: mlb, next: [] });
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

test("timezone follows the browser, or a zone picked by hand", async ({ page, request }) => {
  await reset(request, { tz: 0, tz_name: "US Eastern", tz_auto: true });
  await page.goto("/");
  await expectSet(request, { tz: "1" }); // the browser is pinned to America/Chicago
  await expect(page.locator("#toast")).toHaveText("Timezone set to US Central from this browser");
  await expect(page.locator("#tzline")).toContainText("US Central · set from this browser");

  await page.locator("#tzline a", { hasText: "choose manually" }).click();
  await expect(page.locator("#tzpick")).toBeVisible();
  await page.locator("#tzsel").selectOption({ label: "US Pacific" });
  await expectSet(request, { tzauto: "0", tz: "4" });
  await expect(page.locator("#tzline")).toContainText("US Pacific · chosen by hand");
});

test("the panel picker posts the new layout", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
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
  await expect(page.locator("#health")).toBeVisible();
  await expect(page.locator("#hStart")).toHaveText("Restart, 2 h 1 min ago");
  await expect(page.locator("#hBoots")).toHaveText("Start 2 since the power came on");
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

test("a hot or under-powered panel says why it is dimmed, and the power cap clears", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await expect(page.locator("#hTemp")).toHaveText("47 C");
  await expect(page.locator("#hPower")).toHaveText("0");
  await expect(page.locator("#guard")).toBeHidden();

  const h = (await deviceState(request)).health;
  await reset(request, { health: { ...h, temp: 78.4, heat: 2 } });
  await page.reload();
  await expect(page.locator("#guard")).toBeVisible();
  await expect(page.locator("#guardName")).toHaveText("Too warm, dimming");
  await expect(page.locator("#guardText")).toContainText("at 45% of its brightness");
  await expect(page.locator("#guardClear")).toBeHidden();
  await expect(page.locator("#hTemp")).toHaveText("78 C, dimmed to 45%");

  // The power cap wins over the heat, and Clear posts power_ok.
  await reset(request, { health: { ...h, heat: 1, brownouts: 3, power: "capped" } });
  await page.reload();
  await expect(page.locator("#guardName")).toHaveText("Check the power supply");
  await expect(page.locator("#hPower")).toHaveText("3, brightness capped");
  await page.locator("#guardClear").click();
  await expect.poll(async () =>
    (await (await request.get("/_log")).json()).filter((p) => p.path === "/gameday/action").map((p) => p.q.do),
  ).toContain("power_ok");
  await expect(page.locator("#guardName")).toHaveText("Too warm, dimming");
  await expect(page.locator("#guardClear")).toBeHidden();

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

  // Forgetting the last one leaves the empty line.
  page.once("dialog", (d) => d.accept());
  await rows.nth(0).locator("button").click();
  await expect(rows).toHaveCount(0);
  await expect(page.locator("#netsEmpty")).toBeVisible();

  // Firmware without the list: the section stays hidden.
  await reset(request, {}, [], null);
  await page.goto("/");
  await expect(page.locator("#hostname")).not.toBeEmpty();
  await expect(page.locator("#netsCard")).toBeHidden();
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
  await expect(page.locator("#conn")).toHaveClass(/\bon\b/);
  await expect(page.locator("#gstate")).toHaveText("UPCOMING");
  await expect(page.locator("#gone")).toBeHidden();

  // The panel drops off the network: the stream ends and the state read gets no answer.
  await reset(request, {}, ["down"]);
  await expect(page.locator("#gone")).toHaveText(/^Panel not answering since \d{1,2}:\d{2}\s[AP]M$/);
  await expect(page.locator("#board")).toHaveClass(/dim/);
  await expect(page.locator("#conn")).not.toHaveClass(/\bon\b/);
  // The last score stays up, dimmed.
  await expect(page.locator("#tA .abbr")).toHaveText("DAL");

  // Back: the next poll answers and the stream reopens without waiting out its retry.
  await reset(request);
  await expect(page.locator("#gone")).toBeHidden({ timeout: 20000 });
  await expect(page.locator("#board")).not.toHaveClass(/dim/);
  await expect(page.locator("#conn")).toHaveClass(/\bon\b/);
});

test("a dropped stream alone doesn't call the panel gone", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await expect(page.locator("#conn")).toHaveClass(/\bon\b/);
  // A phone waking up, or the stream refused: the page asks the panel, and it answers.
  await reset(request, {}, ["events"]);
  await expect(page.locator("#conn")).not.toHaveClass(/\bon\b/);
  await staysAs(page.locator("#gone:visible"), [], 2000);
  await reset(request);
  await expect(page.locator("#conn")).toHaveClass(/\bon\b/, { timeout: 20000 });
  await expect(page.locator("#gone")).toBeHidden();
});

test("Reboot says it's restarting and reloads when the panel is back", async ({ page, request }) => {
  await reset(request);
  page.on("dialog", (d) => d.accept());
  await page.goto("/");
  await expect(page.locator("#conn")).toHaveClass(/\bon\b/);
  await page.locator("#actions button", { hasText: "Reboot" }).click();
  const restarting = "Restarting. This page reconnects when the panel is back.";
  await expect(page.locator("#gone")).toHaveText(restarting);
  await expect(page.locator("#board")).toHaveClass(/dim/);
  await expect(page.locator("#conn")).not.toHaveClass(/\bon\b/);

  // Down for a few seconds, then back: the page reloads on its own.
  await reset(request, {}, ["down"]);
  await sleep(4500);
  await expect(page.locator("#gone")).toHaveText(restarting);
  const reloaded = page.waitForEvent("load", { timeout: 10000 });
  await reset(request);
  await reloaded;
  await expect(page.locator("#conn")).toHaveClass(/\bon\b/);
  await expect(page.locator("#gone")).toBeHidden();
});

test("both resets say the panel restarts into setup and the page won't reconnect", async ({ page, request, baseURL }) => {
  await reset(request);
  page.on("dialog", (d) => d.accept());
  await page.goto("/");
  await expect(page.locator("#gstate")).toHaveText("UPCOMING");
  await page.locator("#actions button", { hasText: "Reset Wi-Fi" }).click();
  await expect(page.locator("#gone")).toHaveText(
    "The panel is restarting into Wi-Fi setup for 15 minutes, so this page won't reconnect. Its settings are kept,"
    + " and unplugging it and plugging it back in takes it back to the Wi-Fi it already knows."
    + " To set it up again, use the Game Day app or join its \"Game Day 2f6a70\" Wi-Fi.");
  await expect(page.locator("#board")).toHaveClass(/dim/);
  await expect(page.locator("#conn")).not.toHaveClass(/\bon\b/);

  await page.reload();
  await expect(page.locator("#gstate")).toHaveText("UPCOMING");
  await page.locator("#actions button", { hasText: "Factory reset" }).click();
  await expect(page.locator("#gone")).toHaveText(
    "The panel is restarting into setup with everything erased, so this page won't reconnect."
    + " To set it up again, use the Game Day app or join its \"Game Day 2f6a70\" Wi-Fi.");
  // The mock still answers, but the page has stopped asking it.
  const asked = [];
  page.on("request", (r) => { if (r.url().startsWith(baseURL)) asked.push(r.url()); });
  await sleep(1500);
  expect(asked).toEqual([]);
  await expect(page.locator("#conn")).not.toHaveClass(/\bon\b/);
});

test("a reset the panel never got doesn't claim it's resetting", async ({ page, request }) => {
  await reset(request);
  page.on("dialog", (d) => d.accept());
  await page.goto("/");
  await expect(page.locator("#conn")).toHaveClass(/\bon\b/);
  await page.route("**/button/**", (route) => route.fulfill({ status: 500, body: "" }));
  await page.locator("#actions button", { hasText: "Factory reset" }).click();
  await expect(page.locator("#toast")).toHaveText("Device did not respond (HTTP 500). Try again.");
  await expect(page.locator("#gone")).toBeHidden();
  await expect(page.locator("#conn")).toHaveClass(/\bon\b/);
});

const actions = async (request) =>
  (await (await request.get("/_log")).json()).filter((p) => p.path === "/gameday/action").map((p) => p.q);

test("the banner posts its text and seconds, and Take down posts no text", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
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

test("the message card posts its text, address and timing, and Clear turns it off", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
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

test("the message card says how the list's last read went, and hides on firmware without it", async ({ page, request }) => {
  const url = "https://example.com/plates.txt";
  await reset(request, { msgurl: url, msgread: { status: 200, entries: 412 } });
  await page.goto("/");
  await expect(page.locator("#msgUrl")).toHaveValue(url);
  await expect(page.locator("#msgRead")).toHaveText("The list has 412 entries.");
  await reset(request, { msgurl: url, msgread: { status: 404 } });
  await page.goto("/");
  await expect(page.locator("#msgRead")).toHaveText("That address answered with an error (HTTP 404). It tries again in a few minutes.");
  await reset(request, { msgurl: url, msgread: { status: 0 } });
  await page.goto("/");
  await expect(page.locator("#msgRead")).toHaveText("The panel couldn't reach that address. It tries again in a few minutes.");

  await reset(request, { msg: null, msgurl: null, msgevery: null, msgread: null });
  await page.goto("/");
  await expect(page.locator("#hostname")).not.toBeEmpty();
  await expect(page.locator("#msgCard")).toBeHidden();
});
