// The demo build's Demo card (firmware/web/demo.js, in bundle-demo.js)
// against mock_device.py, which serves that bundle at /?demo.
const base = require("@playwright/test");
const { expect } = base;

// Same isolation as page.spec.js: nothing off the mock, and page errors fail.
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

const reset = (request) => request.post("/_reset", { data: {} });
const demoPosts = async (request) =>
  (await (await request.get("/_log")).json()).filter((p) => p.path === "/gameday/demo").map((p) => p.q);

test("release page has no Demo card", async ({ page, request }) => {
  await reset(request);
  await page.goto("/");
  await expect(page.locator("#conn")).toHaveClass(/\bon\b/);
  await expect(page.locator("#demo")).toHaveCount(0);
});

test("starts, shows and stops the demo", async ({ page, request }) => {
  await reset(request);
  await page.goto("/?demo");
  const card = page.locator("#demo");
  await expect(card).toBeVisible();
  await expect(page.locator("#demoNow")).toHaveText("Stopped, showing live scores");

  await card.getByRole("button", { name: "Start pro" }).click();
  await expect.poll(() => demoPosts(request)).toContainEqual({ do: "start", kind: "pro" });
  await expect(page.locator("#demoNow")).toHaveText("Pro: Dallas Cowboys vs Philadelphia Eagles");

  await card.getByRole("button", { name: "Start college" }).click();
  await expect(page.locator("#demoNow")).toHaveText("College: Ohio State Buckeyes vs Michigan Wolverines");

  await card.getByRole("button", { name: "Stop" }).click();
  await expect.poll(() => demoPosts(request)).toContainEqual({ do: "stop" });
  await expect(page.locator("#demoNow")).toHaveText("Stopped, showing live scores");
});

test("picks teams per league and switches to made-up teams", async ({ page, request }) => {
  await reset(request);
  await page.goto("/?demo");
  const home = page.locator('select[data-team="pro_home"]');
  await expect(home).toHaveValue("nfl:6");
  // Each list only has its own league.
  await expect(home.locator('option[value="ncaa:333"]')).toHaveCount(0);
  await expect(page.locator('select[data-team="college_home"] option[value="ncaa:333"]')).toHaveCount(1);

  await home.selectOption("nfl:26");
  await expect.poll(() => demoPosts(request)).toContainEqual({ pro_home: "nfl:26" });

  await page.locator("#demoMadeup").click();
  await expect.poll(() => demoPosts(request)).toContainEqual({ madeup: "1" });
  await page.locator("#demo").getByRole("button", { name: "Start pro" }).click();
  await expect(page.locator("#demoNow")).toHaveText("Pro: Harbor City Hawks vs Iron Valley Miners");
  await expect(page.locator("#demoMadeup")).toHaveClass(/\bon\b/);
});
