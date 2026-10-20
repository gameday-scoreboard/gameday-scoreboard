// Game Day Scoreboard device page.
// Scores and settings come from the gameday component's own routes:
//   GET  /gameday/state           one JSON document with the game and every setting
//   POST /gameday/set?key=value   change settings (team=nfl:6, mode=1, fav2=ncaa:333, tz=3 ...)
//   POST /gameday/action?do=name  refresh, power_ok (clears the brownout cap),
//                                 say&text=...&secs=N (the banner)
// The ESPHome event stream (/events) still carries the entities that stay
// for Home Assistant (Power, Brightness, Firmware, WizMote...) and its
// "Game Status" event, sent on every board update, is the cue to re-read the
// state document.
// Bundled with the team table by scripts/build_web.py, which defines
// TEAMS = [[league, espnId, abbr, name], ...] and TZS = [[name, iana], ...].

(() => {
  "use strict";

  const $ = (sel, root = document) => root.querySelector(sel);
  const el = (tag, cls, text) => {
    const n = document.createElement(tag);
    if (cls) n.className = cls;
    if (text !== undefined) n.textContent = text;
    return n;
  };
  // A button whose state is a CSS class also says it to screen readers:
  // switches with aria-checked, the other on/off buttons with aria-pressed.
  const setOn = (n, on, attr) => {
    n.classList.toggle("on", on);
    n.setAttribute(attr, String(on));
  };

  // The leagues the firmware knows (components/gameday/leagues.h), in tab
  // order. prefix: team list label; tab: picker tab; name: the long name;
  // path: ESPN's; byId: logo files named by team id; scan: extra query for
  // today's scoreboard. scripts/build_web.py checks prefix, path, logo and
  // byId against leagues.h and stops the build when they differ.
  const LEAGUES = {
    nfl: { prefix: "NFL", tab: "NFL", name: "NFL", path: "football/nfl", logo: "nfl", byId: false, scan: "" },
    ncaa: { prefix: "NCAAF", tab: "NCAAF", name: "College football", path: "football/college-football", logo: "ncaa", byId: true, scan: "groups=80&limit=300&" },
    mlb: { prefix: "MLB", tab: "MLB", name: "MLB", path: "baseball/mlb", logo: "mlb", byId: false, scan: "" },
    mls: { prefix: "MLS", tab: "MLS", name: "MLS", path: "soccer/usa.1", logo: "soccer", byId: true, scan: "" },
    epl: { prefix: "EPL", tab: "EPL", name: "Premier League", path: "soccer/eng.1", logo: "soccer", byId: true, scan: "" },
    nba: { prefix: "NBA", tab: "NBA", name: "NBA", path: "basketball/nba", logo: "nba", byId: false, scan: "" },
    wnba: { prefix: "WNBA", tab: "WNBA", name: "WNBA", path: "basketball/wnba", logo: "wnba", byId: false, scan: "" },
    mcbb: { prefix: "NCAAM", tab: "NCAAM", name: "Men's college basketball", path: "basketball/mens-college-basketball", logo: "ncaa", byId: true, scan: "groups=50&limit=300&" },
    nhl: { prefix: "NHL", tab: "NHL", name: "NHL", path: "hockey/nhl", logo: "nhl", byId: false, scan: "" },
  };
  const lgInfo = (lg) => LEAGUES[lg] || LEAGUES.nfl;
  const logoUrl = (league, id, abbr) => {
    const l = lgInfo(league);
    return `https://a.espncdn.com/i/teamlogos/${l.logo}/500-dark/${l.byId ? id : String(abbr).toLowerCase()}.png`;
  };
  const keyOf = (t) => t[0] + ":" + t[1];
  const teamByKey = (key) => TEAMS.find((t) => keyOf(t) === key);
  const refKey = (r) => (r && r.id ? r.l + ":" + r.id : "");

  // ---- state -----------------------------------------------------------
  const ents = {};       // id -> last event payload ({id, name, state, value, option...})
  const byName = {};     // name -> id
  let D = null;          // /gameday/state document as the device last sent it
  let S = null;          // D with the page's unconfirmed posts on top; every render reads this
  let game = null;       // S.game
  let connected = false;
  let downSince = 0;     // page clock when the panel stopped answering, 0 while it answers
  let restart = "";      // what the board says while a reboot or reset runs
  let stopped = false;   // reset into setup: the panel won't be back on this network

  // ---- DOM -------------------------------------------------------------
  document.body.innerHTML = `
  <div class="wrap">
    <header class="top">
      <div class="brand">
        <div class="mark">GD</div>
        <div><h1>Game Day Scoreboard<small id="devname">connecting</small></h1></div>
      </div>
      <div class="conn" id="conn" aria-live="polite"><i></i><span>offline</span></div>
    </header>

    <section class="board empty" id="board">
      <div class="state" id="gstate">WAITING</div>
      <div class="stale" id="stale">data stale</div>
      <div class="row">
        <div class="team" id="tA"><img alt=""><div class="abbr">---</div><div class="rec"></div><div class="pips"><i></i><i></i><i></i></div></div>
        <div class="mid">
          <div class="score"><span id="sA">0</span><span class="dash">-</span><span id="sB">0</span></div>
          <div class="clock" id="clock">&nbsp;</div>
          <div class="down" id="down"></div>
        </div>
        <div class="team" id="tB"><img alt=""><div class="abbr">---</div><div class="rec"></div><div class="pips"><i></i><i></i><i></i></div></div>
      </div>
      <div class="gname" id="gname" hidden></div>
      <div class="msg" id="msg" hidden></div>
      <div class="ticker" id="ticker">Waiting for the device</div>
      <div class="gone" id="gone" hidden></div>
      <div class="play" id="play"></div>
      <div class="splash" id="splash"></div>
    </section>

    <div class="teambar guardbar" id="guard" role="status" hidden>
      <div class="cur"><div><div class="name" id="guardName"></div><div class="lg" id="guardText"></div></div></div>
      <button class="btn" id="guardClear" hidden>Clear</button>
    </div>

    <section class="upnext" id="upnext" hidden>
      <div class="uh">Up next</div>
      <div class="ul" id="upnextList"></div>
    </section>

    <div class="modes" id="modes" role="group" aria-label="Mode">
      <button data-mode="0" aria-pressed="false">My team</button>
      <button data-mode="5" aria-pressed="false">Live games</button>
      <button data-mode="4" aria-pressed="false">Favorite teams</button>
    </div>
    <div class="teambar" id="teambar">
      <div class="cur"><img id="curLogo" alt=""><div><div class="name" id="curName">No team chosen</div><div class="lg" id="curLg">Pick the team the panel should follow</div></div></div>
      <button class="btn primary" id="pick">Change team</button>
    </div>
    <div class="teambar livebar" id="livebar" hidden>
      <div class="cur"><div><div class="name" id="liveName">Following a random game in progress</div><div class="lg" id="liveLg">Moves on when it ends, or after Switch every. Leagues:</div><div class="chips" id="liveChips" role="group" aria-label="Leagues"></div><button class="linkbtn" id="liveAuto" hidden>Back to automatic</button></div></div>
      <div class="stepper"><label for="rotate">Switch every</label><input type="number" id="rotate" min="1" max="30" step="1"><span>min</span></div>
    </div>
    <div class="teambar favbar" id="favbar" hidden>
      <div class="cur"><div><div class="name" id="favName">Cycling through your favorites</div><div class="lg">A favorite's game takes the panel near kickoff. When two overlap, the slot order or Alternate below decides.</div></div></div>
      <div class="favctls">
        <div class="stepper"><label for="lockon">Take over</label><input type="number" id="lockon" min="5" max="120" step="1"><span>min before kickoff</span></div>
        <div class="stepper"><label for="release">After the final</label><select id="release">
          <option value="30">30 s</option><option value="60">1 min</option><option value="120">2 min</option><option value="300">5 min</option>
          <option value="600">10 min</option><option value="900">15 min</option><option value="1800">30 min</option><option value="3600">60 min</option>
        </select></div>
        <div class="stepper"><label for="collide">Two games at once</label><select id="collide">
          <option value="0">Stick with the higher team</option><option value="1">Alternate</option>
        </select><input type="number" id="favrotate" min="1" max="30" step="1" aria-label="Alternate every, in minutes" hidden><span id="favrotateUnit" hidden>min</span></div>
      </div>
    </div>

    <div class="grid">
      <div class="card">
        <h2>Ticker</h2>
        <p class="hint">What scrolls along the bottom of the panel during a game.</p>
        <div id="tickerCtls">
          <div class="ctl" data-sport="football"><label>Down and distance<small>Football: the down, yards to go and where the ball is.</small></label><button class="sw" data-set="down" aria-label="Down and distance"></button></div>
          <div class="ctl" data-sport="baseball"><label>Count and at-bat<small>Baseball: the count, outs, pitcher and batter.</small></label><button class="sw" data-set="count" aria-label="Count and at-bat"></button></div>
          <div class="ctl" data-sport="soccer"><label>Match stats<small>Soccer: goals, cards, possession and shots.</small></label><button class="sw" data-set="match" aria-label="Match stats"></button></div>
          <div class="ctl"><label>Last play<small>ESPN's latest play in every sport. In soccer, the latest goal or card.</small></label><button class="sw" data-set="play" aria-label="Last play"></button></div>
          <div class="ctl"><label>Pre-game odds and TV<small>The line, over/under and TV channel before the start, in every league, when ESPN has them. In football, who is favored to win during the game.</small></label><button class="sw" data-set="odds" aria-label="Pre-game odds and TV"></button></div>
        </div>
      </div>
      <div class="card">
        <h2>Celebrations</h2>
        <p class="hint">Full-screen splashes for scores. Yours always show.</p>
        <div id="celebCtls">
          <div class="ctl"><label>Opponent scores too<small>The other side's scores in football, baseball, hockey and soccer. Basketball celebrates only the win.</small></label><button class="sw" data-set="opp" aria-label="Opponent scores too"></button></div>
        </div>
      </div>
      <div class="card" id="msgCard" hidden>
        <h2>Message</h2>
        <div class="sub">Banner</div>
        <p class="hint">Your words over whatever the panel shows, for a few seconds.</p>
        <textarea id="sayText" class="field" rows="2" maxlength="200" placeholder="Pizza's here!" aria-label="Banner text"></textarea>
        <div class="ctl"><label for="saySecs">Seconds on screen</label><input type="number" id="saySecs" class="num" min="1" max="600" step="1" value="10"></div>
        <div class="actions"><button class="btn primary" id="sayGo">Show</button><button class="btn" id="sayStop">Take down</button></div>
        <div class="sub">Message card</div>
        <p class="hint">Takes turns with the scoreboard between games, and steps aside when a game is live. Nothing shows until you add text or a list.</p>
        <textarea id="msgText" class="field" rows="3" maxlength="200" placeholder="One per line, like Wings $5 till 7" aria-label="Message card text"></textarea>
        <label class="fl" for="msgUrl">Or a list from a web address<small>A plain text file, one entry per line, like a list of names. The panel reads it a page at a time, so it can be long. When there is an address, the card shows the list.</small></label>
        <input type="url" id="msgUrl" class="field" maxlength="160" placeholder="https://" autocomplete="off">
        <p class="hint" id="msgRead" hidden></p>
        <div class="ctl"><label for="msgEvery">Show it</label><select id="msgEvery">
          <option value="0">Whenever no game is on</option><option value="1">Every minute</option><option value="2">Every 2 minutes</option>
          <option value="5">Every 5 minutes</option><option value="10">Every 10 minutes</option><option value="15">Every 15 minutes</option><option value="30">Every 30 minutes</option>
        </select></div>
        <div class="actions"><button class="btn primary" id="msgSave">Save</button><button class="btn" id="msgClear">Clear</button></div>
      </div>
      <div class="card">
        <h2>Panel</h2>
        <div id="panelCtls"></div>
      </div>
      <div class="card">
        <h2>Favorites</h2>
        <p class="hint">Up to 16 teams from any league. In Favorite teams mode the order is the priority. Buttons 1 to 4 on a WizMote remote jump to the first four.</p>
        <div class="ctl"><label>Only today's games<small>The playlist skips favorites that aren't playing today.</small></label><button class="sw" data-set="today" aria-label="Only today's games"></button></div>
        <div id="favCtls"></div>
        <button class="btn" id="favAdd">Add a favorite</button>
      </div>
      <div class="card">
        <h2>Remote</h2>
        <p class="hint">Pair a WizMote: turn on discovery, then press any button on the remote.</p>
        <div class="ctl"><label>Status</label><span class="val" id="wizStatus" style="min-width:0;text-align:left"></span></div>
        <div id="remoteCtls"></div>
        <div class="actions" id="remoteActions" style="margin-top:8px"></div>
      </div>
      <div class="card">
        <h2>Setup</h2>
        <p class="hint">Things you set once.</p>
        <div class="sub">Matrix</div>
        <p class="hint">Click the picture that matches yours. The panel restarts with the new layout.</p>
        <div class="panels" id="panels">
          <button class="panelpick" data-cols="1" aria-label="One 64x64 panel">
            <svg viewBox="0 0 96 48" aria-hidden="true"><rect x="30" y="6" width="36" height="36" rx="3"/></svg>
            <span>One 64x64 panel</span>
          </button>
          <button class="panelpick" data-cols="2" aria-label="One 128x64 panel, or two 64x64 side by side">
            <svg viewBox="0 0 96 48" aria-hidden="true"><rect x="10" y="6" width="36" height="36" rx="3"/><rect x="50" y="6" width="36" height="36" rx="3"/></svg>
            <span>One 128x64 panel</span>
            <small>or two 64x64 side by side</small>
          </button>
        </div>
        <div class="sub">Time</div>
        <div class="tzline" id="tzline">Timezone not known yet</div>
        <div class="ctl" id="tzpick" hidden><label for="tzsel">Timezone</label><select id="tzsel"></select></div>
        <div class="sub">Startup</div>
        <div class="ctl">
          <label>Show how to connect when the panel starts<small>After it joins Wi-Fi: a QR code for the Game Day app, then the page address, for about 20 seconds. Turn it off to go straight to the scoreboard. Hold the boot button any time to show the address again.</small></label>
          <button class="sw" data-set="bootaddr" aria-label="Show how to connect when the panel starts"></button>
        </div>
      </div>
      <div class="card">
        <h2>Device</h2>
        <div class="actions" id="actions"></div>
        <div class="fw" id="fw" hidden>
          <div class="fwline"><span id="fwText">Firmware</span><button class="btn primary" id="fwInstall" hidden>Install</button></div>
          <div class="fwsub" id="fwSub"></div>
        </div>
        <div class="ctl" id="autoupdRow" hidden>
          <label>Install updates automatically<small>Between 3 and 5 AM, never during a live game. Off: the panel only tells you an update is out.</small></label>
          <button class="sw" data-set="autoupd" aria-label="Install updates automatically"></button>
        </div>
        <div class="ctl" id="betaRow" hidden>
          <label>Follow beta<small>Get test versions of the next release before everyone else. They can have bugs. Turned off, the panel stays on its beta until the next release.</small></label>
          <button class="sw" data-set="beta" aria-label="Follow beta"></button>
        </div>
        <div class="ctl" id="barRow" hidden>
          <label>Bar beta<small>This panel installs test versions on its own, between 3 and 5 AM and never during a live game.</small></label>
          <a class="btn" id="barReport" href="#">Something wrong?</a>
        </div>
        <div id="netsCard" hidden>
          <div class="sub">Wi-Fi networks</div>
          <p class="hint">The panel joins the first of these it can see, the last one that worked first. Set up another with the Game Day app and it is added here.</p>
          <div id="nets"></div>
          <p class="hint" id="netsEmpty" hidden>No saved networks.</p>
        </div>
        <div class="ctl" style="margin-top:8px"><label>Address</label><span class="val" id="ip"></span></div>
        <div class="ctl"><label>Name</label><span class="val" id="hostname"></span></div>
        <div class="sub">Diagnostics</div>
        <div id="health" hidden>
          <div class="ctl"><label>Last restart<small id="hBoots"></small></label><span class="val" id="hStart"></span></div>
          <div class="ctl"><label>Trouble since power on<small>Crashes, freezes and power dips</small></label><span class="val" id="hFault"></span></div>
          <div class="ctl"><label>Free memory</label><span class="val" id="hHeap"></span></div>
          <div class="ctl" id="hTempRow" hidden><label>Chip temperature<small>Above 75 C the panel dims until it cools</small></label><span class="val" id="hTemp"></span></div>
          <div class="ctl" id="hPowerRow" hidden><label>Power dips<small>Brownout restarts since the panel was made</small></label><span class="val" id="hPower"></span></div>
          <div class="ctl"><label>Last ESPN read</label><span class="val" id="hEspn"></span></div>
          <div class="ctl"><label>Firmware</label><span class="val" id="hFw"></span></div>
        </div>
        <div class="ctl"><label>Signal</label><span class="val" id="rssi"></span></div>
        <div id="wifiDiag" hidden>
          <p class="weak" id="wifiWeak" role="status" hidden>Weak signal. Move the panel closer to the router, or the router closer to the panel.</p>
          <div class="spark" id="wifiSparkBox" hidden>
            <svg id="wifiSpark" viewBox="0 0 288 48" preserveAspectRatio="none" role="img"></svg>
            <div class="sparklbl"><span id="wifiFrom"></span><span>Dashed: weak below -75 dBm</span><span>Now</span></div>
          </div>
          <div class="ctl"><label>Wi-Fi drops<small id="wifiLast"></small></label><span class="val" id="wifiDrops"></span></div>
        </div>
        <div class="ctl"><label>Free PSRAM</label><span class="val" id="psram"></span></div>
        <div class="ctl"><label>ESPN data<small>Scores and logos since the panel started</small></label><span class="val" id="espnData"></span></div>
      </div>
    </div>

    <p class="foot">
      <span id="ver"></span><span>·</span>
      <a href="https://github.com/gameday-scoreboard/gameday-scoreboard" target="_blank" rel="noopener">gameday-scoreboard</a>
      <span>·</span><span>scores from ESPN</span>
    </p>
  </div>

  <div class="modal" id="modal">
    <div class="sheet">
      <div class="head">
        <input type="search" id="q" placeholder="Search teams" aria-label="Search teams" autocomplete="off">
        <div class="tabs" role="tablist" aria-label="Leagues"><button data-lg="now" class="on" role="tab" aria-selected="true" aria-controls="tiles">On now</button></div>
        <button class="btn" id="close">Close</button>
      </div>
      <div class="tiles" id="tiles" role="tabpanel"></div>
    </div>
  </div>
  <div class="toast" id="toast" role="status"></div>`;

  // ---- helpers ---------------------------------------------------------
  let toastTimer;
  const toast = (t) => {
    const n = $("#toast");
    n.textContent = t;
    n.classList.add("on");
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => n.classList.remove("on"), 1800);
  };

  // Entity ids come as "domain/Name" (ESPHome 2026.x) or "domain-object_id"
  // (older builds). Control URLs are /{domain}/{entity name}/{action}, matched
  // against the entity's name, so the name is what goes in the URL.
  const domainOf = (e) => e.domain || String(e.id || "").split(/[\/-]/)[0];
  const post = async (id, action, params, attempt = 0) => {
    const e = ents[id] || { id };
    const domain = domainOf(e);
    const target = e.name || String(id).split("/")[1] || String(id).split("-").slice(1).join("-");
    const qs = params
      ? "?" + Object.entries(params).map(([k, v]) => k + "=" + encodeURIComponent(v)).join("&")
      : "";
    try {
      const r = await fetch(`/${domain}/${encodeURIComponent(target)}/${action}${qs}`, { method: "POST" });
      if (!r.ok) throw new Error("HTTP " + r.status);
      return true;
    } catch (e) {
      if (attempt < 1) {
        // The device only has a handful of sockets; a busy moment is normal.
        await new Promise((res) => setTimeout(res, 500));
        return post(id, action, params, attempt + 1);
      }
      toast("Device did not respond (" + (e.message || "network") + "). Try again.");
      return false;
    }
  };

  // ---- gameday routes ----------------------------------------------------
  const qs = (params) => Object.entries(params).map(([k, v]) => k + "=" + encodeURIComponent(v)).join("&");
  // A tap redraws from what it posted, not from the device's last answer: a
  // state read that left before the device applied the post still carries
  // the old value. Each posted setting is held here until a read shows it,
  // or a read that left after the device answered the post (APPLY_MS for
  // its main loop to apply it) comes back. Then the device's value wins.
  const APPLY_MS = 300;
  const held = {};       // key -> {v: the value as the state document has it, ack: when the post was answered}
  const ref = (key) => ({ l: key.split(":")[0], id: Number(key.split(":")[1]) });
  const stateForm = {
    mode: Number, rotate: Number, lockon: Number, release: Number, collide: Number,
    down: (v) => v === "1", count: (v) => v === "1", match: (v) => v === "1", play: (v) => v === "1", odds: (v) => v === "1", opp: (v) => v === "1",
    bootaddr: (v) => v === "1", today: (v) => v === "1",
    live: (v) => v.split(",").filter(Boolean),
    team: (v) => ref(v),
    favs: (v) => v.split(",").filter(Boolean).map(ref),
    msg: String, msgurl: String, msgevery: Number,
  };
  const sameForm = {
    mode: (a, b) => (a || 0) === (b || 0),
    team: (a, b) => refKey(a) === refKey(b),
    live: (a, b) => (a || []).slice().sort().join(",") === (b || []).slice().sort().join(","),
    favs: (a, b) => (a || []).map(refKey).filter(Boolean).join(",") === (b || []).map(refKey).filter(Boolean).join(","),
  };
  const same = (k, a, b) => (sameForm[k] ? sameForm[k](a, b) : a === b);
  const overlay = () => {
    if (!D) return;
    S = Object.assign({}, D);
    for (const k of Object.keys(held)) S[k] = held[k].v;
    game = S.game || null;
  };

  let stateTimer = null;
  let stateInflight = false;
  let stateAgain = false;
  const fetchState = async () => {
    if (stopped) return;
    // A read asked for during another one runs after it, so it sees the latest post.
    if (stateInflight) { stateAgain = true; return; }
    stateInflight = true;
    const left = Date.now();
    // A panel that lost power never closes the connection, so give up on it.
    const ctl = new AbortController();
    const timer = setTimeout(() => ctl.abort(), 8000);
    let reached = false;
    try {
      const r = await fetch("/gameday/state?_=" + left, { cache: "no-store", signal: ctl.signal });
      reached = true;
      answered();
      // The event stream retries every 30 seconds, and never after an HTTP
      // error. The panel is back, so reopen it now.
      if (!connected && !restart && Date.now() - streamAt > 10000) connect();
      if (r.ok) {
        D = await r.json();
        for (const k of Object.keys(held)) {
          const h = held[k];
          if (h.ack && (same(k, D[k], h.v) || left > h.ack + APPLY_MS)) delete held[k];
        }
        overlay();
        renderAll();
        if (Object.values(held).some((h) => h.ack)) scheduleState(APPLY_MS + 100);
      }
    } catch (_) {
      // The poll comes back around.
      if (!reached) unanswered();
    }
    clearTimeout(timer);
    stateInflight = false;
    if (stateAgain) { stateAgain = false; fetchState(); }
  };
  const scheduleState = (ms) => { clearTimeout(stateTimer); stateTimer = setTimeout(fetchState, ms); };
  const postSet = async (params, attempt = 0) => {
    try {
      const r = await fetch("/gameday/set?" + qs(params), { method: "POST" });
      if (!r.ok) throw new Error("HTTP " + r.status);
      return true;
    } catch (e) {
      if (attempt < 1) {
        await new Promise((res) => setTimeout(res, 500));
        return postSet(params, attempt + 1);
      }
      toast("Device did not respond (" + (e.message || "network") + "). Try again.");
      return false;
    }
  };
  // Settings post. Posts go one at a time, in tap order, so a slow one (or its
  // retry) never lands after a later one. The device applies them on its main
  // loop, so re-read shortly after.
  let setQueue = Promise.resolve();
  const setGD = (params) => {
    const mine = {};
    for (const k of Object.keys(params)) {
      if (!stateForm[k]) continue;
      mine[k] = held[k] = { v: stateForm[k](String(params[k])), ack: 0 };
    }
    // A league list turns automatic off and an empty one turns it back on.
    // The automatic list itself is the device's to work out, so hold only the flag.
    if (mine.live) {
      mine.live_auto = held.live_auto = { v: !mine.live.v.length, ack: 0 };
      if (!mine.live.v.length) { delete mine.live; delete held.live; }
    }
    if (Object.keys(mine).length) { overlay(); renderAll(); }
    const run = setQueue.then(() => postSet(params)).then((ok) => {
      for (const k of Object.keys(mine)) {
        if (held[k] !== mine[k]) continue;  // a later tap posted this key again
        if (ok) held[k].ack = Date.now();
        else delete held[k];
      }
      if (!ok) { overlay(); renderAll(); }
      scheduleState(APPLY_MS + 100);
      return ok;
    });
    setQueue = run;
    return run;
  };
  const doAction = async (name, params) => {
    try {
      const extra = params ? "&" + qs(params) : "";
      const r = await fetch("/gameday/action?do=" + encodeURIComponent(name) + extra, { method: "POST" });
      if (!r.ok) throw new Error("HTTP " + r.status);
    } catch (e) {
      toast("Device did not respond (" + (e.message || "network") + ")");
    }
  };

  // Every ping calls this; #conn is a live region, so only a flip touches it.
  const setConn = (on) => {
    if (on === connected) return;
    connected = on;
    const c = $("#conn");
    c.classList.toggle("on", on);
    c.querySelector("span").textContent = on ? "live" : "offline";
  };

  // The panel's own miss count can't say it has gone, so the page keeps its
  // own: a state read that gets no answer while the stream is down, or two in
  // a row while the stream still looks open (a panel that lost power).
  let readFails = 0;
  let firstFailAt = 0;
  const renderGone = () => {
    const line = $("#gone");
    line.textContent = restart
      || (downSince ? "Panel not answering since " + new Date(downSince).toLocaleTimeString([], { hour: "numeric", minute: "2-digit" }) : "");
    line.hidden = !line.textContent;
    $("#board").classList.toggle("dim", !line.hidden);
  };
  const answered = () => {
    readFails = 0;
    if (downSince) { downSince = 0; renderGone(); }
  };
  const unanswered = () => {
    if (!readFails++) firstFailAt = Date.now();
    if (downSince || (connected && readFails < 2)) return;
    downSince = firstFailAt;
    setConn(false);
    renderGone();
  };

  // ---- board rendering ---------------------------------------------------
  // ESPN also ends a postponed, suspended or canceled game in POST, without
  // marking it done; its detail says which.
  const endedAs = (g) => (g.done === false && g.detail ? g.detail : "Final");

  const renderTeam = (node, league, id, abbr, rec, timeouts, poss, color) => {
    const img = node.querySelector("img");
    // No id: a side with no team yet, or the demo build's made-up teams.
    const src = id ? logoUrl(league, id, abbr) : "";
    if (img.getAttribute("src") !== src) img.src = src;
    img.style.visibility = src ? "visible" : "hidden";
    const a = node.querySelector(".abbr");
    a.textContent = abbr || "---";
    a.classList.toggle("poss", !!poss);
    a.style.color = "#" + (color || "ffffff");
    node.querySelector(".rec").textContent = rec || "";
    // Timeout pips are football's (other sports send no timeouts).
    node.querySelector(".pips").style.display = timeouts === null ? "none" : "";
    node.querySelectorAll(".pips i").forEach((p, i) => p.classList.toggle("on", i < (timeouts || 0)));
  };
  // The situation line under the clock: down and distance for football, the
  // count and outs for baseball, the latest goal or a playoff series otherwise.
  const situation = (g) => {
    if (!g.sport) return g.d || "";
    if (g.sport === 1 && g.outs >= 0) return (g.sit ? g.sit + ", " : "") + g.outs + (g.outs === 1 ? " out" : " outs");
    return g.sit || "";
  };

  const renderBoard = () => {
    const b = $("#board");
    const g = game;
    const st = $("#gstate");
    const msg = $("#msg");
    // misses is top level in the state document, not in game. Three in a row is
    // where the firmware adds "no update" to the ticker, with or without a game.
    // captive: the panel's Wi-Fi answered with a sign-in page, which the
    // panel can't get past, so say how to fix it rather than "data stale".
    const captive = !!(S && S.captive);
    const stale = $("#stale");
    stale.textContent = captive ? "Wi-Fi needs a sign-in" : "data stale";
    stale.classList.toggle("on", captive || ((S && S.misses) || 0) >= 3);
    // Bowl or playoff name, when ESPN gives the game one
    const gname = $("#gname");
    gname.textContent = (g && g.s !== "NOT_FOUND" && g.gn) || "";
    gname.hidden = !gname.textContent;
    if (!g || g.s === "NOT_FOUND") {
      b.classList.add("empty");
      st.textContent = g ? "NO GAME" : "WAITING";
      st.classList.remove("live");
      msg.hidden = false;
      const mode = S ? String(S.mode || 0) : "0";
      const favMode = mode === "4" && S && (S.favs || []).some(refKey);
      // With no team picked the panel runs the live board, unless Favorites has teams.
      const liveMode = S && !favMode && (mode !== "0" || S.recap || !(S.team && S.team.id));
      // why is the panel's own reason ("Can't reach ESPN", "Getting ready"),
      // sent only when its board says something other than its default.
      msg.textContent = !g ? "Waiting for the first fetch"
        : captive ? "This Wi-Fi needs a sign-in. Use a home network or a hotspot."
        : g.why ? g.why
        : favMode ? "None of your favorites has a game coming up"
          : liveMode ? "No live games right now" : "No upcoming game for this team";
      $("#clock").innerHTML = "&nbsp;";
      $("#down").textContent = "";
      return;
    }
    b.classList.remove("empty");
    msg.hidden = true;
    st.classList.toggle("live", g.s === "IN");
    st.textContent = g.s === "IN" ? "LIVE" : g.s === "POST" ? endedAs(g).toUpperCase() : "UPCOMING";
    const football = !g.sport;
    renderTeam($("#tA"), g.l, g.ti, g.ta, g.tr, football ? g.tt : null, g.p === 1, g.tc);
    renderTeam($("#tB"), g.l, g.oi, g.oa, g.or, football ? g.ot : null, g.p === 2, g.oc);
    $("#sA").textContent = g.ts;
    $("#sB").textContent = g.os;
    const clock = $("#clock");
    if (g.s === "IN") clock.textContent = g.c || "In progress";
    else if (g.s === "POST") clock.textContent = endedAs(g);
    else clock.textContent = g.k || "Upcoming";
    const d = $("#down");
    d.textContent = g.s === "IN" ? situation(g) : g.s === "PRE" && g.tv ? "on " + g.tv : "";
    d.classList.toggle("rz", g.s === "IN" && !!g.rz);
  };

  // ---- mode pills ----------------------------------------------------------------
  // Device modes: 0 my team, 4 favorite teams, 5 live games in the chosen
  // leagues. 1-3 are the older football-only live modes; the page shows them
  // as Live games and a chip tap moves the panel to mode 5.
  const isLive = (m) => m === "1" || m === "2" || m === "3" || m === "5";
  const modeButtons = Array.from(document.querySelectorAll("#modes button"));
  modeButtons.forEach((b) => {
    b.onclick = () => {
      const cur = S ? String(S.mode || 0) : "";
      if (cur === b.dataset.mode || (b.dataset.mode === "5" && isLive(cur))) return;
      setGD({ mode: b.dataset.mode });
      toast(b.textContent);
    };
  });
  const rotateInput = $("#rotate");
  rotateInput.onchange = () => {
    const v = Math.min(30, Math.max(1, Number(rotateInput.value) || 1));
    rotateInput.value = v;
    setGD({ rotate: v });
  };
  // Favorite teams mode settings (mode 4).
  const lockonInput = $("#lockon");
  lockonInput.onchange = () => {
    const v = Math.min(120, Math.max(5, Number(lockonInput.value) || 15));
    lockonInput.value = v;
    setGD({ lockon: v });
  };
  const releaseSelect = $("#release");
  releaseSelect.onchange = () => setGD({ release: releaseSelect.value });
  const collideSelect = $("#collide");
  const favRotate = $("#favrotate");
  const showFavRotate = () => {
    const alt = collideSelect.value === "1";
    favRotate.hidden = !alt;
    $("#favrotateUnit").hidden = !alt;
  };
  collideSelect.onchange = () => {
    showFavRotate();
    setGD({ collide: collideSelect.value });
  };
  favRotate.onchange = () => {
    const v = Math.min(30, Math.max(1, Number(favRotate.value) || 1));
    favRotate.value = v;
    setGD({ rotate: v });
  };
  // Leagues Live mode follows. An old football live mode reads as its leagues.
  const liveKeys = () => {
    const m = String(S.mode || 0);
    if (m === "1") return ["nfl"];
    if (m === "2") return ["ncaa"];
    if (m === "3") return ["nfl", "ncaa"];
    return S.live || [];
  };
  // No leagues picked: the device follows the leagues of the team and the
  // favorites, and that list changes by itself when a favorite is added.
  const liveAuto = () => String(S.mode || 0) === "5" && !!S.live_auto;
  $("#liveAuto").onclick = () => {
    setGD({ live: "" });
    toast("Leagues follow your team and favorites");
  };
  const renderLiveBar = () => {
    const auto = liveAuto();
    // With nothing live the device shows the saved team's card, or with no
    // team a rotation of recent finals, and keeps scanning.
    $("#liveName").textContent = S.fallback ? "No live game right now, showing your team until one starts"
      : S.recap ? "No live game right now, showing recent finals"
        : "Following a random game in progress";
    $("#liveLg").textContent = (S.fallback || S.recap ? "" : "Moves on when it ends, or after Switch every. ")
      + (auto ? "Leagues follow your team and favorites. Tap a league to choose by hand:" : "Leagues:");
    $("#liveChips").classList.toggle("auto", auto);
    $("#liveAuto").hidden = auto || String(S.mode || 0) !== "5";
    renderLiveChips();
  };
  const renderLiveChips = () => {
    const host = $("#liveChips");
    host.innerHTML = "";
    const on = liveKeys();
    for (const lg of Object.keys(LEAGUES)) {
      if (!TEAMS.some((t) => t[0] === lg)) continue;
      const b = el("button", "chip", LEAGUES[lg].tab);
      setOn(b, on.includes(lg), "aria-pressed");
      // From the list as it is now, not as it was when the chips were drawn:
      // a second tap can come before the redraw.
      b.onclick = () => {
        const cur = liveKeys();
        const next = cur.includes(lg) ? cur.filter((k) => k !== lg) : cur.concat([lg]);
        if (!next.length) return toast("Pick at least one league");
        const params = { live: next.join(",") };
        if (String(S.mode || 0) !== "5") params.mode = 5;
        setGD(params);
      };
      host.appendChild(b);
    }
  };
  const renderMode = () => {
    if (!S) return;
    const mode = String(S.mode || 0);
    modeButtons.forEach((b) => setOn(b, b.dataset.mode === mode || (b.dataset.mode === "5" && isLive(mode)), "aria-pressed"));
    const live = isLive(mode);
    if (live) renderLiveBar();
    const favs = mode === "4";
    $("#teambar").hidden = live || favs;
    $("#livebar").hidden = !live;
    $("#favbar").hidden = !favs;
    if (document.activeElement !== rotateInput) rotateInput.value = S.rotate;
    if (document.activeElement !== favRotate) favRotate.value = S.rotate;
    if (document.activeElement !== lockonInput) lockonInput.value = S.lockon || 15;
    if (document.activeElement !== releaseSelect) releaseSelect.value = String(S.release || 30);
    if (document.activeElement !== collideSelect) {
      collideSelect.value = String(S.collide || 0);
      showFavRotate();
    }
    if (favs) {
      const set = (S.next || []).filter((n) => n.t && n.t.id).length;
      const shown = (S.next || []).find((n) => n.slot === S.shown);
      $("#favName").textContent = !set
        ? "No favorites set yet"
        : S.locked && shown
          ? "Locked on " + (shown.t.name || shown.t.abbr)
          : "Cycling through your favorites";
    }
  };

  // ---- toggles and favorites (static controls on the state document) --------
  document.querySelectorAll(".sw[data-set]").forEach((sw) => {
    sw.setAttribute("role", "switch");
    setOn(sw, false, "aria-checked");
    sw.onclick = () => setGD({ [sw.dataset.set]: S && S[sw.dataset.set] ? 0 : 1 });
  });
  // Favorites: one row per team in priority order. Every change posts the
  // whole list (favs=) so the device always sees one ordered list.
  let favKeys = [];
  const favMax = () => (S && S.favmax) || 4;
  // renderSettings() redraws the list from S, which already carries the post.
  const postFavs = (keys) => setGD({ favs: keys.join(",") });
  const renderFavs = () => {
    const host = $("#favCtls");
    host.innerHTML = "";
    favKeys.forEach((key, i) => {
      const t = teamByKey(key);
      const row = el("div", "ctl fav");
      const img = el("img");
      img.loading = "lazy";
      img.alt = "";
      if (t) img.src = logoUrl(t[0], t[1], t[2]);
      row.appendChild(img);
      const name = el("label", null, (i + 1) + ". " + (t ? t[3] : key));
      if (t) name.appendChild(el("small", null, lgInfo(t[0]).name + (i < 4 ? " · remote button " + (i + 1) : "")));
      row.appendChild(name);
      const ctl = el("span", "favmove");
      const mk = (label, title, fn) => {
        const b = el("button", null, label);
        b.title = title;
        b.setAttribute("aria-label", title);
        b.onclick = fn;
        ctl.appendChild(b);
      };
      const swap = (j) => {
        if (j < 0 || j >= favKeys.length) return;
        const next = favKeys.slice();
        next[i] = favKeys[j];
        next[j] = favKeys[i];
        postFavs(next);
      };
      mk("\u25B2", "Move up", () => swap(i - 1));
      mk("\u25BC", "Move down", () => swap(i + 1));
      mk("\u2715", "Remove", () => postFavs(favKeys.filter((_, k) => k !== i)));
      row.appendChild(ctl);
      host.appendChild(row);
    });
    if (!favKeys.length) host.appendChild(el("p", "hint", "No favorites yet."));
    $("#favAdd").hidden = favKeys.length >= favMax();
  };
  const addFav = (t) => {
    const key = keyOf(t);
    if (favKeys.includes(key)) return toast(t[3] + " is already a favorite");
    if (favKeys.length >= favMax()) return toast("That's the most favorites the panel holds");
    postFavs(favKeys.concat([key]));
    toast("Added the " + t[3]);
  };
  const renderSettings = () => {
    if (!S) return;
    document.querySelectorAll(".sw[data-set]").forEach((sw) => setOn(sw, !!S[sw.dataset.set], "aria-checked"));
    const keys = (S.favs || []).map(refKey).filter(Boolean);
    if (keys.join(",") !== favKeys.join(",")) {
      favKeys = keys;
      renderFavs();
    } else {
      $("#favAdd").hidden = favKeys.length >= favMax();
    }
    // A situation switch for each sport the panel follows (team, favorites,
    // Live games leagues picked by hand), all of them before anything is
    // picked. The automatic Live list is no help here: it never comes back
    // empty (NFL on a new panel). Hockey and basketball have no situation
    // line, so no switch.
    const sports = new Set([S.team].concat(S.favs || []).filter((t) => t && t.id).map((t) => t.l)
      .concat(S.live_auto ? [] : S.live || []).map((lg) => lgInfo(lg).path.split("/")[0]));
    document.querySelectorAll("#tickerCtls .ctl[data-sport]").forEach((row) => {
      row.hidden = sports.size > 0 && !sports.has(row.dataset.sport);
    });
    // Firmware that can't install updates (older, or the demo build) leaves it out.
    $("#autoupdRow").hidden = S.autoupd === undefined;
    // Older firmware and the bar and demo builds have no beta to follow.
    $("#betaRow").hidden = S.beta === undefined;
    // The rollback line in the firmware box reads this document, which can
    // land after the update entity does.
    if ((S.update_failed || "") !== fwFailed && ents[byName["Firmware"]]) renderUpdate(ents[byName["Firmware"]]);
    // The bar build: a note, and an email to us with the panel's name in it.
    $("#barRow").hidden = !S.bar;
    if (S.bar) {
      const who = S.name || "this panel";
      $("#barReport").href = "mailto:" + S.bar.report + "?subject=" + encodeURIComponent("Bar panel " + who) +
        "&body=" + encodeURIComponent("Panel: " + who + "\nWhat's wrong:\n");
    }
    $("#ticker").textContent = S.status || "";
    $("#play").textContent = game && game.lp ? "Last play: " + game.lp : "";
    renderMsg();
  };

  // ---- message: the banner and the message card ---------------------------------
  // The boxes take the panel's text when it changes there, never while
  // someone is typing in them.
  const msgSeen = { msg: null, msgurl: null, msgevery: null };
  const fillField = (input, key) => {
    const v = String(S[key] || (key === "msgevery" ? 0 : ""));
    if (v === msgSeen[key] || document.activeElement === input) return;
    msgSeen[key] = v;
    input.value = v;
  };
  const renderMsg = () => {
    // Older firmware has no message card.
    $("#msgCard").hidden = S.msg === undefined;
    if (S.msg === undefined) return;
    fillField($("#msgText"), "msg");
    fillField($("#msgUrl"), "msgurl");
    fillField($("#msgEvery"), "msgevery");
    const read = S.msgread;
    const line = $("#msgRead");
    line.hidden = !S.msgurl;
    if (!S.msgurl) return;
    if (!read || read.status === undefined) line.textContent = "Not read yet. The panel reads it when the card's turn comes.";
    else if (read.status === 0) line.textContent = "The panel couldn't reach that address. It tries again in a few minutes.";
    else if (read.status !== 200) line.textContent = "That address answered with an error (HTTP " + read.status + "). It tries again in a few minutes.";
    else if (read.entries !== undefined) line.textContent = "The list has " + read.entries + (read.entries === 1 ? " entry." : " entries.");
    else line.textContent = "Reading the list a page at a time.";
  };
  $("#sayGo").onclick = () => {
    const text = $("#sayText").value.trim();
    if (!text) return toast("Type the banner first");
    const secs = Math.min(600, Math.max(1, Number($("#saySecs").value) || 10));
    $("#saySecs").value = secs;
    doAction("say", { text, secs });
    toast("Banner up for " + secs + " s");
  };
  $("#sayStop").onclick = () => doAction("say", { text: "" });
  $("#msgSave").onclick = () => {
    const url = $("#msgUrl").value.trim();
    if (url && !/^https?:\/\/\S+$/.test(url)) return toast("The list's address starts with http:// or https://");
    msgSeen.msg = msgSeen.msgurl = msgSeen.msgevery = null;
    setGD({ msg: $("#msgText").value, msgurl: url, msgevery: $("#msgEvery").value });
    toast(url || $("#msgText").value.trim() ? "Message card saved" : "Message card off");
  };
  $("#msgClear").onclick = () => {
    $("#msgText").value = "";
    $("#msgUrl").value = "";
    msgSeen.msg = msgSeen.msgurl = msgSeen.msgevery = null;
    setGD({ msg: "", msgurl: "" });
    toast("Message card off");
  };

  // ---- panel picker ------------------------------------------------------------
  // Picking a layout restarts the device; the page waits it out.
  const renderPanels = () => {
    if (!S || !S.panels) return;
    const cur = String(S.panels);
    document.querySelectorAll("#panels .panelpick").forEach((b) => {
      setOn(b, b.dataset.cols === cur, "aria-pressed");
      b.onclick = () => {
        if (b.dataset.cols === cur) return;
        document.querySelectorAll("#panels .panelpick").forEach((x) => (x.disabled = true));
        setGD({ panels: b.dataset.cols });
        const as = "Restarting as " + (b.dataset.cols === "2" ? "128x64" : "64x64");
        toast(as);
        waitForReboot("", as + ". This page reconnects when the panel is back.");
        toBoard();
      };
    });
  };

  // ---- controls --------------------------------------------------------
  const controlDefs = [
    { name: "Power", into: "#panelCtls", label: "Panel on" },
    { name: "Brightness", into: "#panelCtls", label: "Brightness" },
    { name: "Scroll Speed", into: "#panelCtls", label: "Ticker speed" },
    { name: "Select Page", into: "#panelCtls", label: "Showing" },
    { name: "WizMote Auto-Discovery", into: "#remoteCtls", label: "Discovery" },
    { name: "Clear WizMote Pairing", into: "#remoteActions", label: "Unpair remote", danger: true },
    { name: "Refresh Now", into: "#actions", label: "Refresh scores" },
    { name: "Check for Updates", into: "#actions", label: "Check for updates" },
    // restart: "" waits for the panel to come back; otherwise the panel
    // restarts into setup and this says why.
    { name: "Reboot", into: "#actions", label: "Reboot", danger: true, restart: "" },
    { name: "Reset Wi-Fi", into: "#actions", label: "Reset Wi-Fi", danger: true,
      restart: "The panel is restarting into Wi-Fi setup for 15 minutes, so this page won't reconnect. Its settings are kept, and unplugging it and plugging it back in takes it back to the Wi-Fi it already knows." },
    { name: "Factory Reset", into: "#actions", label: "Factory reset", danger: true,
      restart: "The panel is restarting into setup with everything erased, so this page won't reconnect." },
  ];
  const built = {};

  const buildControl = (def, e) => {
    const domain = domainOf(e);
    const host = $(def.into);
    if (domain === "button") {
      const b = el("button", "btn" + (def.danger ? " danger" : ""), def.label);
      b.onclick = async () => {
        if (def.danger && !confirm(def.label + "?")) return;
        const sent = post(e.id, "press");
        if (def.name === "Check for Updates") { checkForUpdates(b); return; }
        toast(def.label);
        if (def.restart === undefined || !(await sent)) return;
        if (def.restart === "") waitForReboot("", "Restarting. This page reconnects when the panel is back.");
        else intoSetup(def.restart);
        toBoard();
      };
      host.appendChild(b);
      return { update() {} };
    }
    const row = el("div", "ctl");
    const label = el("label", null, def.label);
    const id = "ctl-" + def.name.toLowerCase().replace(/[^a-z0-9]+/g, "-");
    row.appendChild(label);
    if (domain === "switch") {
      const sw = el("button", "sw");
      sw.setAttribute("role", "switch");
      sw.setAttribute("aria-label", def.label);
      setOn(sw, false, "aria-checked");
      sw.onclick = () => {
        const on = !sw.classList.contains("on");
        setOn(sw, on, "aria-checked");
        post(e.id, on ? "turn_on" : "turn_off");
      };
      row.appendChild(sw);
      host.appendChild(row);
      return { update(ev) { setOn(sw, ev.value === true || ev.state === "ON", "aria-checked"); } };
    }
    if (domain === "number") {
      const val = el("span", "val");
      val.setAttribute("aria-hidden", "true");
      const r = el("input");
      r.id = label.htmlFor = id;
      r.type = "range";
      r.min = e.min_value ?? 0;
      r.max = e.max_value ?? 100;
      r.step = e.step ?? 1;
      r.oninput = () => (val.textContent = r.value);
      r.onchange = () => post(e.id, "set", { value: r.value });
      row.appendChild(r);
      row.appendChild(val);
      host.appendChild(row);
      return { update(ev) { if (document.activeElement !== r) { r.value = ev.value; val.textContent = ev.value; } } };
    }
    if (domain === "select") {
      const s = el("select");
      s.id = label.htmlFor = id;
      (e.option || []).forEach((o) => s.appendChild(el("option", null, o)));
      s.onchange = () => post(e.id, "set", { option: s.value });
      row.appendChild(s);
      host.appendChild(row);
      return { update(ev) { if (ev.option) { s.innerHTML = ""; ev.option.forEach((o) => s.appendChild(el("option", null, o))); } s.value = ev.value; } };
    }
    return { update() {} };
  };

  // ---- today's games (fetched by the browser, not the device) -------------
  const ESPN = "https://site.api.espn.com/apis/site/v2/sports/";
  let games = null;        // [{league, id, state, detail, away:{...}, home:{...}}]
  let gamesAt = 0;
  const teamById = (lg, id) => TEAMS.find((t) => t[0] === lg && t[1] === id);
  const easternDate = () => {
    // ESPN's dates parameter is US Eastern
    const p = new Intl.DateTimeFormat("en-US", { timeZone: "America/New_York", year: "numeric", month: "2-digit", day: "2-digit" }).formatToParts(new Date());
    const g = (t) => p.find((x) => x.type === t).value;
    return g("year") + g("month") + g("day");
  };
  const loadGames = async (force) => {
    if (!force && games && Date.now() - gamesAt < 60000) return games;
    const d = easternDate();
    const urls = Object.keys(LEAGUES).map((lg) => [lg, `${ESPN}${LEAGUES[lg].path}/scoreboard?${LEAGUES[lg].scan}dates=${d}`]);
    const out = [];
    await Promise.all(urls.map(async ([lg, u]) => {
      try {
        const j = await (await fetch(u)).json();
        for (const ev of j.events || []) {
          const c = ev.competitions?.[0];
          if (!c) continue;
          const side = (ha) => {
            const x = c.competitors.find((k) => k.homeAway === ha) || {};
            return { id: Number(x.team?.id), abbr: x.team?.abbreviation || "", score: x.score || "0", name: x.team?.shortDisplayName || x.team?.displayName || "" };
          };
          const type = ev.status?.type || {};
          // "off": ended without being completed, so postponed, canceled or suspended
          const state = type.state === "post" && type.completed === false ? "off" : type.state || "pre";
          out.push({ league: lg, id: ev.id, state, detail: type.shortDetail || "", away: side("away"), home: side("home"), date: ev.date });
        }
      } catch (_) { /* one league failing should not hide the other */ }
    }));
    const rank = { in: 0, pre: 1, post: 2, off: 3 };
    out.sort((a, b) => (rank[a.state] - rank[b.state]) || (a.date < b.date ? -1 : 1));
    games = out;
    gamesAt = Date.now();
    return out;
  };

  const renderGames = async () => {
    const tiles = $("#tiles");
    tiles.className = "games";
    tiles.innerHTML = "";
    tiles.appendChild(el("div", "none", "Loading today's games"));
    const list = await loadGames(false);
    if (league !== "now") return;  // the user moved on while we were loading
    tiles.innerHTML = "";
    const q = $("#q").value.trim().toLowerCase();
    const shown = list.filter((g) => !q || (g.away.name + " " + g.home.name + " " + g.away.abbr + " " + g.home.abbr).toLowerCase().includes(q));
    if (!shown.length) {
      tiles.appendChild(el("div", "none", list.length ? "No game matches" : "No games today"));
      return;
    }
    const cur = S ? refKey(S.team) : "";
    let lastState = null;
    for (const g of shown) {
      if (g.state !== lastState) {
        lastState = g.state;
        const heading = { in: "Live now", pre: "Later today", post: "Final", off: "Postponed or canceled" };
        tiles.appendChild(el("div", "gh", heading[g.state] || "Final"));
      }
      const card = el("div", "game" + (g.state === "in" ? " live" : ""));
      const mkSide = (s) => {
        const t = teamById(g.league, s.id);
        const b = el("button", "side" + (t && keyOf(t) === cur ? " on" : ""));
        b.disabled = !t;
        b.title = t ? "Follow the " + t[3] : "Not in the team list";
        const img = el("img");
        img.loading = "lazy";
        img.src = logoUrl(g.league, s.id, s.abbr);
        img.alt = "";
        b.appendChild(img);
        const txt = el("div", "st");
        txt.appendChild(el("div", "n", s.name));
        txt.appendChild(el("div", "a", s.abbr));
        b.appendChild(txt);
        b.appendChild(el("div", "sc", g.state === "pre" || g.state === "off" ? "" : String(s.score)));
        b.onclick = () => {
          if (!t) return;
          if (pickFav) return choose(t);
          setGD({ team: keyOf(t) });
          toast("Now following the " + t[3]);
          $("#modal").classList.remove("open");
        };
        return b;
      };
      card.appendChild(mkSide(g.away));
      const mid = el("div", "gm");
      mid.appendChild(el("div", "at", "@"));
      mid.appendChild(el("div", "det", g.detail));
      card.appendChild(mid);
      card.appendChild(mkSide(g.home));
      tiles.appendChild(card);
    }
  };

  // ---- team chooser ------------------------------------------------------
  // The picker sets My team, or with pickFav adds a favorite.
  let pickFav = false;
  const choose = (t) => {
    if (pickFav) addFav(t);
    else {
      setGD({ team: keyOf(t) });
      toast("Now following the " + t[3]);
    }
    $("#modal").classList.remove("open");
  };
  let league = "now";
  const renderTiles = () => {
    if (league === "now") return renderGames();
    $("#tiles").className = "tiles";
    const q = $("#q").value.trim().toLowerCase();
    const cur = S ? refKey(S.team) : "";
    const tiles = $("#tiles");
    tiles.innerHTML = "";
    const list = TEAMS.filter((t) => (q ? (t[3] + " " + t[2]).toLowerCase().includes(q) : t[0] === league));
    if (!list.length) {
      tiles.appendChild(el("div", "none", "No team matches"));
      return;
    }
    for (const t of list) {
      const tile = el("button", "tile" + (keyOf(t) === cur ? " on" : ""));
      const img = el("img");
      img.loading = "lazy";
      img.src = logoUrl(t[0], t[1], t[2]);
      img.alt = "";
      tile.appendChild(img);
      tile.appendChild(el("div", "n", t[3]));
      tile.appendChild(el("div", "a", lgInfo(t[0]).prefix + " · " + t[2]));
      tile.onclick = () => choose(t);
      tiles.appendChild(tile);
    }
  };
  $("#guardClear").onclick = async () => {
    await doAction("power_ok");
    scheduleState(300);
  };
  $("#pick").onclick = () => openPicker(false);
  $("#favAdd").onclick = () => openPicker(true);
  const openPicker = async (fav) => {
    pickFav = fav;
    const cur = S ? teamByKey(refKey(S.team)) : null;
    league = cur ? cur[0] : "nfl";
    try {
      const list = await loadGames(false);
      if (list.some((g) => g.state === "in")) league = "now";
    } catch (_) {}
    $$tabs(league);
    $("#q").value = "";
    renderTiles();
    $("#modal").classList.add("open");
    setTimeout(() => $("#q").focus(), 50);
  };
  // One tab per league that has teams in this firmware's list.
  for (const lg of Object.keys(LEAGUES)) {
    if (!TEAMS.some((t) => t[0] === lg)) continue;
    const b = el("button", null, LEAGUES[lg].tab);
    b.dataset.lg = lg;
    b.setAttribute("role", "tab");
    b.setAttribute("aria-controls", "tiles");
    $(".tabs").appendChild(b);
  }
  // Only the chosen tab is in the Tab order; the arrow keys move between tabs.
  const tabButtons = Array.from(document.querySelectorAll(".tabs button"));
  const $$tabs = (lg) => tabButtons.forEach((b) => {
    setOn(b, b.dataset.lg === lg, "aria-selected");
    b.tabIndex = b.dataset.lg === lg ? 0 : -1;
  });
  const showTab = (b) => { league = b.dataset.lg; $$tabs(league); $("#q").value = ""; renderTiles(); };
  tabButtons.forEach((b, i) => {
    b.onclick = () => showTab(b);
    b.onkeydown = (ev) => {
      const n = tabButtons.length;
      const j = { ArrowRight: i + 1, ArrowLeft: i - 1 + n, Home: 0, End: n - 1 }[ev.key];
      if (j === undefined) return;
      ev.preventDefault();
      tabButtons[j % n].focus();
      showTab(tabButtons[j % n]);
    };
  });
  $("#q").oninput = renderTiles;
  $("#close").onclick = () => $("#modal").classList.remove("open");
  $("#modal").onclick = (ev) => { if (ev.target.id === "modal") $("#modal").classList.remove("open"); };
  document.addEventListener("keydown", (ev) => { if (ev.key === "Escape") $("#modal").classList.remove("open"); });

  const renderCurrentTeam = () => {
    const t = S ? teamByKey(refKey(S.team)) : null;
    const img = $("#curLogo");
    if (!t) { img.style.visibility = "hidden"; $("#curName").textContent = "No team chosen"; return; }
    img.style.visibility = "visible";
    img.src = logoUrl(t[0], t[1], t[2]);
    $("#curName").textContent = t[3];
    $("#curLg").textContent = lgInfo(t[0]).name;
  };

  // ---- timezone suggestion from the browser --------------------------------
  // Browsers report an IANA zone; the device's list carries one IANA name per
  // entry plus a few common aliases here. Same-zone cities all map to the
  // same POSIX rule, so a near match is still the right choice.
  const TZ_ALIASES = {
    "America/Detroit": "America/New_York", "America/Toronto": "America/New_York", "America/Indiana/Indianapolis": "America/New_York",
    "America/Kentucky/Louisville": "America/New_York", "America/Montreal": "America/New_York", "America/Nassau": "America/New_York",
    "America/Winnipeg": "America/Chicago", "America/Indiana/Knox": "America/Chicago", "America/Menominee": "America/Chicago",
    "America/North_Dakota/Center": "America/Chicago", "America/Matamoros": "America/Chicago",
    "America/Edmonton": "America/Denver", "America/Boise": "America/Denver", "America/Ojinaga": "America/Denver",
    "America/Vancouver": "America/Los_Angeles", "America/Tijuana": "America/Los_Angeles",
    "America/Juneau": "America/Anchorage", "America/Sitka": "America/Anchorage", "America/Nome": "America/Anchorage",
    "Europe/Dublin": "Europe/London", "Europe/Paris": "Europe/Berlin", "Europe/Madrid": "Europe/Berlin", "Europe/Rome": "Europe/Berlin",
    "Europe/Amsterdam": "Europe/Berlin", "Europe/Brussels": "Europe/Berlin", "Europe/Vienna": "Europe/Berlin", "Europe/Zurich": "Europe/Berlin",
    "Europe/Stockholm": "Europe/Berlin", "Europe/Oslo": "Europe/Berlin", "Europe/Copenhagen": "Europe/Berlin", "Europe/Warsaw": "Europe/Berlin",
    "Europe/Prague": "Europe/Berlin", "Europe/Budapest": "Europe/Berlin", "Europe/Helsinki": "Europe/Athens", "Europe/Kiev": "Europe/Athens",
    "Europe/Kyiv": "Europe/Athens", "Europe/Bucharest": "Europe/Athens", "Europe/Sofia": "Europe/Athens",
    "Asia/Hong_Kong": "Asia/Shanghai", "Asia/Taipei": "Asia/Shanghai", "Asia/Manila": "Asia/Singapore", "Asia/Kuala_Lumpur": "Asia/Singapore",
    "Australia/Melbourne": "Australia/Sydney", "Australia/Hobart": "Australia/Sydney", "Australia/Canberra": "Australia/Sydney",
    "Australia/Darwin": "Australia/Adelaide",
  };
  const browserZoneName = () => {
    let iana = "";
    try { iana = Intl.DateTimeFormat().resolvedOptions().timeZone || ""; } catch (_) {}
    if (!iana) return null;
    const target = TZ_ALIASES[iana] || iana;
    const hit = TZS.find((z) => z[1] === target);
    return hit ? hit[0] : null;
  };
  // The panel follows the browser's zone unless someone picked one by hand.
  // The Time card is a single line; the dropdown only appears on request.
  let tzApplied = false;
  const tzsel = $("#tzsel");
  TZS.forEach((z, i) => { const o = el("option", null, z[0]); o.value = String(i); tzsel.appendChild(o); });
  tzsel.onchange = () => { setGD({ tzauto: 0, tz: tzsel.value }); $("#tzpick").hidden = true; };
  const renderTime = () => {
    if (!S) return;
    const cur = S.tz_name || "";
    const auto = !!S.tz_auto;
    const mine = browserZoneName();
    const mineIdx = mine ? TZS.findIndex((z) => z[0] === mine) : -1;
    const line = $("#tzline");
    const pick = $("#tzpick");
    if (document.activeElement !== tzsel) tzsel.value = String(S.tz);
    line.innerHTML = "";
    line.appendChild(el("b", null, cur || "Not set"));
    const link = el("a", "lnk");
    if (auto) {
      line.appendChild(el("span", "muted", mine ? " · set from this browser" : " · this browser's zone is not in the list"));
      link.textContent = "choose manually";
      link.onclick = () => { pick.hidden = !pick.hidden; };
      if (mine && mine !== cur && mineIdx >= 0 && !tzApplied) {
        tzApplied = true;
        setGD({ tz: mineIdx });
        toast("Timezone set to " + mine + " from this browser");
      }
    } else {
      line.appendChild(el("span", "muted", " · chosen by hand"));
      link.textContent = mine ? "follow this browser (" + mine + ")" : "change";
      link.onclick = () => {
        if (mine) { tzApplied = false; setGD({ tzauto: 1 }); pick.hidden = true; }
        else pick.hidden = !pick.hidden;
      };
    }
    line.appendChild(link);
  };

  // ---- up next + splash (from the state document) -------------------------------
  // tbd: ESPN has no kickoff time yet and "kick" is midnight Eastern of the
  // game's day, so show that day in Eastern time and no clock.
  const kickLabel = (epoch, tbd) => {
    const d = new Date(epoch * 1000);
    const opts = { weekday: "short", month: "short", day: "numeric" };
    if (tbd) return d.toLocaleDateString([], { ...opts, timeZone: "America/New_York" }) + " TBD";
    const day = d.toLocaleDateString([], opts);
    const time = d.toLocaleTimeString([], { hour: "numeric", minute: "2-digit" });
    return day + " " + time;
  };
  // Favorite teams mode: one row per favorite, both logos, state or kickoff.
  const renderFavNext = (box) => {
    const list = (S.next || []).filter((n) => n.t && n.t.id);
    box.hidden = !list.length;
    const host = $("#upnextList");
    host.innerHTML = "";
    for (const n of list) {
      const row = el("div", "un fav" + (n.slot === S.shown ? " shown" : ""));
      const img = el("img");
      img.loading = "lazy";
      img.alt = "";
      img.src = logoUrl(n.t.l, n.t.id, n.t.abbr);
      row.appendChild(img);
      const txt = el("div", "ut");
      const name = n.t.abbr + (n.oa ? " vs " + n.oa : "");
      txt.appendChild(el("div", "n", name));
      let when = n.none ? "No game scheduled" : "Not checked yet";
      if (n.s === "IN") when = n.ts + " - " + n.os + (n.detail ? " · " + n.detail : "");
      else if (n.s === "POST") when = endedAs(n) === "Final" ? "Final " + n.ts + " - " + n.os : endedAs(n);
      else if (n.s === "PRE") when = kickLabel(n.kick, n.tbd) + (n.tv ? " · " + n.tv : "");
      txt.appendChild(el("div", "w", when));
      row.appendChild(txt);
      host.appendChild(row);
    }
  };
  const renderUpNext = () => {
    const box = $("#upnext");
    if (S && (S.mode || 0) === 4) { renderFavNext(box); return; }
    if (!S || (S.mode || 0) !== 0) { box.hidden = true; return; }
    const curId = game && game.s !== "NOT_FOUND" ? String(game.id || "") : "";
    const list = (S.next || []).filter((u) => String(u.id) !== curId).slice(0, 3);
    box.hidden = !list.length;
    const host = $("#upnextList");
    host.innerHTML = "";
    const lg = S.team ? S.team.l : "nfl";
    for (const u of list) {
      const row = el("div", "un");
      const img = el("img");
      img.loading = "lazy";
      img.alt = "";
      img.src = logoUrl(lg, u.oi, u.oa);
      row.appendChild(img);
      const txt = el("div", "ut");
      txt.appendChild(el("div", "n", (u.neutral ? "vs " : u.home ? "vs " : "@ ") + (u.on || u.oa)));
      txt.appendChild(el("div", "w", kickLabel(u.kick, u.tbd) + (u.tv ? " · " + u.tv : "")));
      row.appendChild(txt);
      host.appendChild(row);
    }
  };
  let lastSplashKey = "";
  let splashTimer = null;
  const renderSplash = () => {
    if (!S || !S.splash) return;
    // One splash per score change: the same text with the same score is a repeat.
    const key = S.splash + "|" + (game ? game.ts + "-" + game.os : "");
    if (key === lastSplashKey) return;
    lastSplashKey = key;
    const n = $("#splash");
    n.textContent = S.splash;
    n.style.background = "#" + (S.splash_color && S.splash_color !== "000000" ? S.splash_color : "1d4ed8");
    n.classList.add("on");
    clearTimeout(splashTimer);
    splashTimer = setTimeout(() => n.classList.remove("on"), 3000);
  };

  const renderAll = () => {
    renderBoard();
    renderUpNext();
    renderSplash();
    renderCurrentTeam();
    renderMode();
    renderSettings();
    renderPanels();
    renderTime();
    if (S && S.name) $("#hostname").textContent = S.name + ".local";
    renderData();
    renderHealth();
    renderWifi();
  };

  const renderData = () => {
    const kb = S && S.espn_kb;
    if (kb == null) { $("#espnData").textContent = ""; return; }
    const size = kb < 1000 ? kb + " KB" : (kb / 1000).toFixed(kb < 100000 ? 1 : 0) + " MB";
    const reads = S.espn_reads || 0;
    $("#espnData").textContent = size + " in " + reads + (reads === 1 ? " read" : " reads");
  };

  // The health block (components/gameday/health.h). Firmware before it sends
  // none, and the rows stay hidden.
  const RESTARTS = {
    power: "Power on", restart: "Restart", update: "Update", crash: "Crash", watchdog: "Froze (watchdog)",
    brownout: "Power dip (brownout)", usb: "USB reset",
  };
  const restartName = (k) => RESTARTS[k] || "Other";
  // The Wi-Fi history (components/gameday/wifi_history.h). Firmware before it
  // sends none, and the rows stay hidden.
  const WIFI_CAUSES = {
    signal: "signal lost", router: "dropped by the router", password: "sign-in refused",
    roam: "moved to another access point", other: "other",
  };
  const span = (s) => {
    if (s < 60) return s + " s";
    const m = Math.round(s / 60);
    if (m < 60) return m + " min";
    const h = Math.floor(m / 60);
    if (h < 24) return h + " h" + (m % 60 ? " " + (m % 60) + " min" : "");
    return Math.floor(h / 24) + " d" + (h % 24 ? " " + (h % 24) + " h" : "");
  };
  const renderHealth = () => {
    const h = S && S.health;
    $("#health").hidden = !h;
    if (!h) return;
    $("#hStart").textContent = restartName(h.start) + ", " + span(h.up) + " ago";
    $("#hBoots").textContent = h.boots > 1 ? "Start " + h.boots + " since the power came on" : "";
    $("#hFault").textContent = !h.fault ? "None"
      : restartName(h.fault) + (h.fault_ago != null ? ", " + span(h.fault_ago) + " ago" : "");
    $("#hHeap").textContent = Math.round(h.heap_kb) + " KiB, largest piece " + Math.round(h.block_kb) + " KiB";
    $("#hEspn").textContent = h.espn == null ? "None yet"
      : (h.espn === 200 ? "OK" : h.espn === 0 ? "No answer" : "HTTP " + h.espn) + ", " + span(h.espn_ago) + " ago";
    const chan = { stable: "stable", beta: "beta", bar: "bar beta" }[h.channel];
    // The build time comes in Unix seconds; shown in the browser's own time
    const b = h.built ? new Date(h.built * 1000) : null;
    const builtText = b ? b.toLocaleDateString([], { month: "short", day: "numeric" }) + ", " +
      b.toLocaleTimeString([], { hour: "numeric", minute: "2-digit" }) : "";
    $("#hFw").textContent = (h.version || "") + (chan ? " " + chan : "") + (builtText ? ", built " + builtText : "");
    // Live here, so they no longer wait on the RSSI and heap sensors, which
    // are off until someone turns them on in Home Assistant. The Wi-Fi
    // object, when there is one, fills in the signal.
    if (!S.wifi) $("#rssi").textContent = h.rssi != null ? h.rssi + " dBm" : "Not connected";
    $("#psram").textContent = h.psram_kb ? Math.round(h.psram_kb) + " KiB" : "";
    renderGuard(h);
  };

  // Heat and power protection (components/gameday/guard.h). heat is the
  // dimming step, power "capped" after two brownout restarts in a row.
  const HEAT_PERCENT = [100, 70, 45, 25];
  const renderGuard = (h) => {
    const hasGuard = h && h.heat != null;
    $("#hTempRow").hidden = !hasGuard || h.temp == null;
    $("#hPowerRow").hidden = !hasGuard;
    const capped = hasGuard && h.power === "capped";
    const hot = hasGuard && h.heat > 0;
    $("#guard").hidden = !capped && !hot;
    $("#guard").classList.toggle("power", capped);
    if (!hasGuard) return;
    const pct = HEAT_PERCENT[Math.min(h.heat, HEAT_PERCENT.length - 1)];
    if (h.temp != null) $("#hTemp").textContent = Math.round(h.temp) + " C" + (hot ? ", dimmed to " + pct + "%" : "");
    $("#hPower").textContent = (h.brownouts || 0) + (capped ? ", brightness capped" : "");
    $("#guardClear").hidden = !capped;
    if (capped) {
      $("#guardName").textContent = "Check the power supply";
      $("#guardText").textContent = "The panel restarted twice in a row because its power dipped, so its brightness is held down. "
        + "Use the power supply that came with it, not a phone charger or a USB port, then press Clear.";
    } else if (hot) {
      $("#guardName").textContent = "Too warm, dimming";
      $("#guardText").textContent = "The panel is running hot, so it is at " + pct + "% of its brightness until it cools. "
        + "Keep it out of direct sun and give it some air behind it.";
    }
  };
  // The history across the full width (a day at most), newest at the right,
  // gaps where the panel was off Wi-Fi (a red tick along the bottom). -95 to
  // -35 dBm bottom to top.
  const drawSpark = (svg, hist, step) => {
    const W = hist.length, H = 48, NS = "http://www.w3.org/2000/svg";
    const y = (v) => (H - 2 - (Math.min(-35, Math.max(-95, v)) + 95) / 60 * (H - 4)).toFixed(1);
    let d = "", gaps = "", lo = 0, hi = -127, weak = 0, n = 0;
    hist.forEach((v, i) => {
      const x = i;
      if (v == null) { gaps += "M" + x + " " + (H - 3) + "V" + H; return; }
      d += (i > 0 && hist[i - 1] != null ? "L" : "M") + x + " " + y(v);
      if (i === hist.length - 1 || hist[i + 1] == null) d += "h0.01";  // a lone point still shows
      lo = Math.min(lo, v); hi = Math.max(hi, v); n++;
      if (v < -75) weak++;
    });
    const path = (cls, p) => {
      const e = document.createElementNS(NS, "path");
      e.setAttribute("class", cls);
      e.setAttribute("d", p);
      e.setAttribute("vector-effect", "non-scaling-stroke");
      return e;
    };
    svg.setAttribute("viewBox", "0 0 " + (W - 1) + " " + H);
    svg.replaceChildren(path("th", "M0 " + y(-75) + "H" + W));
    if (gaps) svg.appendChild(path("gap", gaps));
    if (d) svg.appendChild(path("ln", d));
    svg.setAttribute("aria-label", n ? "Signal over the last " + span(W * step) + ": " + lo + " to " + hi
      + " dBm" + (weak ? ", weak for " + span(weak * step) : "") : "No signal readings yet");
  };
  const renderWifi = () => {
    const w = S && S.wifi;
    $("#wifiDiag").hidden = !w;
    if (!w) return;
    $("#rssi").textContent = w.rssi != null ? w.rssi + " dBm" : "Not connected";
    $("#wifiWeak").hidden = !w.weak;
    const hist = w.hist || [], step = w.step || 300;
    $("#wifiSparkBox").hidden = hist.length < 2;
    if (hist.length >= 2) {
      drawSpark($("#wifiSpark"), hist, step);
      $("#wifiFrom").textContent = (hist.length * step >= 86400 ? "24 h" : span(hist.length * step)) + " ago";
    }
    // Up less than a day (by the history), every drop since start is in the day.
    const day = hist.length > 0 && hist.length * step < 86400;
    const count = day ? w.drops : w.day;
    $("#wifiDrops").textContent = (count || "None") + (count ? (day ? " since start" : " in 24 h") : "");
    const causes = Object.entries(w.causes || {}).filter(([k]) => k !== "roam");
    let line = "";
    if (w.last) {
      line = "Last: " + (WIFI_CAUSES[w.last] || "other") + ", " + span(w.last_ago) + " ago"
        + (w.last_down != null ? ", back in " + span(w.last_down) : "");
      if (causes.length > 1) line += ". " + causes.map(([k, c]) => c + " " + (WIFI_CAUSES[k] || k)).join(", ");
    }
    $("#wifiLast").textContent = line;
  };

  // After an install the device reboots. The event stream only retries every
  // 30 seconds, so poll the root page instead and reload as soon as it answers.
  let es = null;
  let streamAt = 0;
  const showRestart = (line) => {
    restart = line;
    renderGone();
  };
  // Reboot, the resets and the panel picker are far down the page; the line is on the board.
  const toBoard = () => $("#board").scrollIntoView({ behavior: "smooth", block: "start" });
  const waitForReboot = (fromVersion, line) => {
    const started = Date.now();
    let sawDown = false;
    if (es) { es.close(); es = null; }
    setConn(false);
    showRestart(line);
    // Ask the device which version it runs; reload as soon as that changes.
    // A connection to a rebooting device can hang, so each probe gets 1.5s.
    const tick = async () => {
      const ctl = new AbortController();
      const timer = setTimeout(() => ctl.abort(), 1500);
      try {
        // detail=all carries current_version; the plain state does not
        const r = await fetch("/update/Firmware?detail=all&_=" + Date.now(), { cache: "no-store", signal: ctl.signal });
        if (r.ok) {
          const j = await r.json();
          const changed = j.current_version && fromVersion !== "" && j.current_version !== fromVersion;
          const settled = !String(j.state || "").toUpperCase().includes("INSTALLING");
          // Once the device has been seen down, a settled answer means it is back.
          if (changed || (sawDown && settled)) { location.reload(); return; }
        }
      } catch (_) { sawDown = true; }
      clearTimeout(timer);
      if (Date.now() - started < 5 * 60 * 1000) { setTimeout(tick, 2000); return; }
      // Not back in five minutes: stop saying it's restarting and listen again.
      restart = "";
      renderGone();
      connect();
    };
    setTimeout(tick, 3000);
  };
  // Both resets blank the saved network, so the panel comes back as its own
  // setup hotspot and never on this one. Stop asking it.
  const intoSetup = (why) => {
    stopped = true;
    if (es) { es.close(); es = null; }
    clearInterval(pollTimer);
    clearTimeout(stateTimer);
    setConn(false);
    const tail = S && S.name && S.name.includes("-") ? S.name.slice(S.name.lastIndexOf("-") + 1) : "";
    showRestart(why + " To set it up again, use the Game Day app or join its \"Game Day" + (tail ? " " + tail : "") + "\" Wi-Fi.");
  };

  // ---- check for updates -----------------------------------------------------
  // The device only sends a state event when the result changes, so "no update"
  // after a check would look like nothing happened. Ask for the result directly.
  const checkForUpdates = async (btn) => {
    const uid = byName["Firmware"];
    btn.disabled = true;
    const label = btn.textContent;
    btn.textContent = "Checking";
    $("#fw").hidden = false;
    $("#fwText").textContent = "Checking for updates";
    $("#fwSub").textContent = "";
    $("#fwInstall").hidden = true;
    await new Promise((r) => setTimeout(r, 4000));
    try {
      // detail=all carries current_version; the plain state does not
      const r = await fetch("/update/Firmware?detail=all&_=" + Date.now(), { cache: "no-store" });
      if (r.ok && uid) {
        const j = await r.json();
        onState(Object.assign({ id: uid }, j));
        const st = String(j.state || "").toUpperCase();
        // The firmware row shows the result; only shout when there is something to do
        if (st.includes("AVAILABLE")) toast("Update available: v" + j.value);
        else if (st.includes("INSTALLING")) { /* the install is running; nothing to say */ }
        else if (!st.includes("NO UPDATE")) toast("Could not reach the update server. Try again in a minute.");
      } else if (uid) {
        renderUpdate(ents[uid]);
        toast("Could not read the update status");
      }
    } catch (_) {
      if (uid) renderUpdate(ents[uid]);
      toast("Device did not respond");
    }
    btn.textContent = label;
    btn.disabled = false;
  };

  // State events carry the update state but not current_version; fetch the
  // full detail whenever the state changes so the box shows the running version.
  let fwDetailInflight = false;
  const refreshFirmwareDetail = async (id) => {
    if (fwDetailInflight) return;
    fwDetailInflight = true;
    try {
      const r = await fetch("/update/Firmware?detail=all&_=" + Date.now(), { cache: "no-store" });
      if (r.ok) {
        const j = await r.json();
        ents[id] = Object.assign(ents[id] || {}, j, { id });
        renderUpdate(ents[id]);
      }
    } catch (_) {}
    fwDetailInflight = false;
  };

  // true if a is newer than b, false if older, null if either is unreadable.
  // A beta's build number ("2026.10.0-beta.812") counts as a fourth part, as
  // in the firmware (auto_update.h), so a beta is newer than its plain number.
  const isNewer = (a, b) => {
    const parse = (v) => String(v || "").replace(/^v/, "").replace("-beta.", ".").split(".").map((n) => parseInt(n, 10));
    const pa = parse(a), pb = parse(b);
    if (pa.length < 3 || pb.length < 3 || pa.some(isNaN) || pb.some(isNaN)) return null;
    for (let i = 0; i < 4; i++) { const x = pa[i] || 0, y = pb[i] || 0; if (x !== y) return x > y; }
    return false;
  };

  // ---- firmware update -----------------------------------------------------
  // The manifest carries a one-line summary; the release on GitHub has the
  // real bullet list. Fetch that from the browser and fall back to the summary.
  const notesCache = {};
  const bulletsFrom = (text) => String(text || "").split(/\r?\n/).filter((l) => l.startsWith("- ")).map((l) => l.slice(2).trim());
  const renderNotes = (host, tag, summary) => {
    const list = el("ul", "notes");
    host.appendChild(list);
    const fill = (items) => {
      list.innerHTML = "";
      items.forEach((t) => list.appendChild(el("li", null, t)));
    };
    // Summary first so something shows right away; the manifest joins the bullets with " | ".
    const quick = String(summary || "").split(" | ").map((t) => t.trim()).filter(Boolean);
    if (quick.length) fill(quick);
    if (!tag) return;
    if (notesCache[tag]) { fill(notesCache[tag]); return; }
    fetch("https://api.github.com/repos/gameday-scoreboard/gameday-scoreboard/releases/tags/" + encodeURIComponent(tag), { headers: { Accept: "application/vnd.github+json" } })
      .then((r) => (r.ok ? r.json() : null))
      .then((j) => {
        const items = j ? bulletsFrom(j.body) : [];
        if (items.length) { notesCache[tag] = items; fill(items); }
      })
      .catch(() => {});
  };
  // A release has its own section on the site's changelog, in owner words; a
  // beta has none, so it keeps the manifest's link to its commit.
  const notesLink = (u) =>
    /^\d+(\.\d+)+$/.test(String(u.value || "")) ? "https://install.gameday-scoreboard.com/changelog/#v" + u.value : u.release_url;
  let installing = false;
  let fwFailed = "";     // S.update_failed as the firmware box last drew it
  const renderUpdate = (u) => {
    const box = $("#fw");
    box.hidden = false;
    const cur = u.current_version ? "v" + u.current_version : "";
    const latest = u.value ? "v" + u.value : "";
    const st = String(u.state || "").toUpperCase();
    const btn = $("#fwInstall");
    const sub = $("#fwSub");
    btn.hidden = true;
    fwFailed = (S && S.update_failed) || "";
    if (st.includes("INSTALLING") || installing) {
      $("#fwText").textContent = "Installing " + (latest || "update");
      sub.textContent = "Keep the panel powered. It reboots when done and this page reconnects.";
    } else if (st.includes("AVAILABLE") && isNewer(u.value, u.current_version) === false) {
      // The device offers any different version; a lower one is a downgrade.
      $("#fwText").textContent = "You're ahead of the latest release, " + cur;
      sub.textContent = "The newest published version is " + latest + ". Nothing to install.";
    } else if (st.includes("AVAILABLE")) {
      // The panel rolled back from this very version (a crash in its first
      // minute); it won't install it on its own again, but Install still tries.
      const failed = !!(S && S.update_failed && S.update_failed === u.value);
      if (failed) {
        $("#fwText").textContent = "The update to " + latest + " didn't take and the panel went back to " + (cur || "its old version");
        sub.textContent = "Install tries it again." + (S.autoupd || S.bar ? " It won't install this one on its own." : "");
      } else {
        $("#fwText").textContent = "Update available: " + latest;
        sub.textContent = (cur ? "You have " + cur + "." : "") + (S && (S.autoupd || S.bar) ? " It installs on its own between 3 and 5 AM." : "");
      }
      renderNotes(sub, latest, u.summary);
      btn.hidden = false;
      btn.onclick = () => {
        const ask = failed ? "Try " + latest + " again? It didn't take last time. The panel will reboot." : "Install " + latest + " now? The panel will reboot.";
        if (!confirm(ask)) return;
        installing = true;
        renderUpdate(u);
        post(u.id, "install");
        toast("Installing " + latest);
        waitForReboot(u.current_version, "Installing " + latest + ". This page reloads when the panel is back.");
      };
    } else if (st.includes("NO UPDATE")) {
      $("#fwText").textContent = "You're up to date, " + cur;
      sub.textContent = "";
    } else {
      $("#fwText").textContent = "Firmware " + cur;
      sub.textContent = "Not checked yet. It checks a few seconds after the clock syncs, or tap Check for updates.";
    }
    const notesUrl = notesLink(u);
    if (notesUrl && !st.includes("INSTALLING")) {
      const a = el("a", null, "Release notes");
      a.href = notesUrl;
      a.target = "_blank";
      a.rel = "noopener";
      sub.appendChild(document.createTextNode(sub.textContent ? " " : ""));
      sub.appendChild(a);
    }
  };

  // ---- saved Wi-Fi networks ---------------------------------------------
  // GET /gameday/wifi lists the networks the panel remembers, names only, in
  // the order it tries them. Firmware without the list answers 404 and the
  // section stays hidden.
  const renderNetworks = (j) => {
    const nets = j.networks || [];
    const list = $("#nets");
    list.innerHTML = "";
    nets.forEach((n, i) => {
      const row = el("div", "ctl");
      const label = el("label", null, n.ssid);
      if (n.connected) label.appendChild(el("small", null, "Connected"));
      else if (i === 0) label.appendChild(el("small", null, "Tried first"));
      row.appendChild(label);
      const b = el("button", "btn danger", "Forget");
      b.onclick = async () => {
        if (!confirm("Forget " + n.ssid + "?")) return;
        b.disabled = true;
        try {
          const r = await fetch("/gameday/wifi?forget=" + encodeURIComponent(n.ssid), { method: "POST" });
          toast(r.ok ? "Forgot " + n.ssid + (n.connected ? "; it stays connected until the next restart" : "") : "Could not forget " + n.ssid);
        } catch (_) {
          toast("Could not forget " + n.ssid);
        }
        loadNetworks();
      };
      row.appendChild(b);
      list.appendChild(row);
    });
    $("#netsEmpty").hidden = nets.length > 0;
    $("#netsCard").hidden = false;
  };
  const loadNetworks = async () => {
    try {
      const r = await fetch("/gameday/wifi?_=" + Date.now(), { cache: "no-store" });
      if (!r.ok) { $("#netsCard").hidden = true; return; }
      renderNetworks(await r.json());
    } catch (_) {
      $("#netsCard").hidden = true;
    }
  };

  // ---- event stream ------------------------------------------------------
  const onState = (e) => {
    ents[e.id] = Object.assign(ents[e.id] || {}, e);
    if (e.name) byName[e.name] = e.id;
    const name = ents[e.id].name;
    const def = controlDefs.find((d) => d.name === name);
    if (def) {
      if (!built[e.id]) built[e.id] = buildControl(def, ents[e.id]);
      built[e.id].update(ents[e.id]);
    }
    switch (name) {
      // Sent on every board update: re-read the state document. Its value is
      // Home Assistant's shorter line, so the ticker and last play come from there.
      case "Game Status": scheduleState(150); break;
      case "Firmware": renderUpdate(ents[e.id]); refreshFirmwareDetail(e.id); break;
      case "WizMote Status": $("#wizStatus").textContent = e.value || ""; break;
      case "IP": $("#ip").textContent = e.value || ""; break;
      case "RSSI": if (!(S && (S.wifi || S.health))) $("#rssi").textContent = e.value ? e.value + " dBm" : ""; break;
      case "Free Heap (PSRAM)": if (!(S && S.health)) $("#psram").textContent = e.value ? Math.round(e.value) + " KiB" : ""; break;
    }
  };

  const connect = () => {
    if (es) es.close();
    streamAt = Date.now();
    es = new EventSource("/events");
    es.addEventListener("ping", (ev) => {
      setConn(true);
      answered();
      if (!ev.data) return;
      try {
        const p = JSON.parse(ev.data);
        if (p.title) $("#devname").textContent = p.title;
        if (p.comment) $("#ver").textContent = p.comment;
      } catch (_) {}
    });
    es.addEventListener("state", (ev) => {
      try { onState(JSON.parse(ev.data)); } catch (_) {}
    });
    es.onopen = () => { setConn(true); answered(); fetchState(); };
    // A dropped stream is often only a phone waking up; ask the panel before
    // saying it's gone.
    es.onerror = () => { setConn(false); fetchState(); };
  };
  connect();
  fetchState();
  loadNetworks();
  // Fallback poll in case an event is missed; the ticker event is the fast path.
  const pollTimer = setInterval(fetchState, 15000);
})();
