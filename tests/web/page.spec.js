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

const reset = (request, state, fail) => request.post("/_reset", { data: { state, fail } });
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
  await expect(page.locator("#conn")).toHaveClass(/on/);
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
  await expect(page.locator('.sw[data-set="down"]')).toHaveClass(/on/);
  await expect(page.locator('.sw[data-set="odds"]')).not.toHaveClass(/on/);
  // The ticker switches are named for every sport, not football.
  await expect(page.getByRole("button", { name: "Game situation", exact: true })).toHaveAttribute("data-set", "down");
  await expect(page.locator("#tickerCtls")).not.toContainText("Down and distance");
  await expect(page.locator("#favCtls .ctl.fav label")).toHaveText([
    /^1\. Dallas Cowboys/, /^2\. Houston Texans/, /^3\. Ole Miss Rebels/,
  ]);
  await expect(page.locator('.sw[data-set="today"]')).not.toHaveClass(/on/);
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

test("a device that never answers /gameday/state leaves the page waiting, not broken", async ({ page, request }) => {
  await reset(request, {}, ["state"]);
  await page.goto("/");
  await expect(page.locator("#conn")).toHaveClass(/on/);
  await expect(page.locator("#gstate")).toHaveText("WAITING");
  await expect(page.locator("#board")).toHaveClass(/empty/);
  await expect(page.locator("#curName")).toHaveText("No team chosen");
  await expect(page.locator("#modes button")).toHaveText(["My team", "Live games", "Favorite teams"]);
  // Entity controls still come from the event stream.
  await expect(page.locator("#panelCtls .ctl")).toHaveCount(4);
});

test("a failed settings post is retried once, then reported", async ({ page, request }) => {
  await reset(request, {}, ["set"]);
  await page.goto("/");
  await expect(page.locator('.sw[data-set="opp"]')).toHaveClass(/on/);
  await page.locator('.sw[data-set="opp"]').click();
  await expect(page.locator("#toast")).toHaveText("Device did not respond (HTTP 500). Try again.");
  expect(await sets(request)).toEqual([{ opp: "0" }, { opp: "0" }]);
  // The switch shows what the panel still has.
  await expect(page.locator('.sw[data-set="opp"]')).toHaveClass(/on/);
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
  await expect(page.locator('.sw[data-set="down"]')).toHaveClass(/on/);
  await page.locator('.sw[data-set="down"]').click();
  await expectSet(request, { down: "0" });
  await page.locator('.sw[data-set="play"]').click();
  await expectSet(request, { play: "1" });
  await page.locator('.sw[data-set="bootaddr"]').click();
  await expectSet(request, { bootaddr: "0" });
  await page.locator('.sw[data-set="today"]').click();
  await expectSet(request, { today: "1" });
  await expect(page.locator('.sw[data-set="down"]')).not.toHaveClass(/on/);
  await expect(page.locator('.sw[data-set="play"]')).toHaveClass(/on/);
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
  // The steps from backlog#148: an old Live college panel, MLB on, NCAAF off,
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
  await expect(sw).not.toHaveClass(/on/);
  await sw.click();
  await expectSet(request, { autoupd: "1" });
  await expect(sw).toHaveClass(/on/);
  await sw.click();
  await expectSet(request, { autoupd: "0" });

  await reset(request, { autoupd: null });
  await page.goto("/");
  await expect(page.locator("#hostname")).not.toBeEmpty();
  await expect(page.locator("#autoupdRow")).toBeHidden();
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
  await expect(page.locator('.sw[data-set="down"]')).not.toHaveClass(/on/);
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
