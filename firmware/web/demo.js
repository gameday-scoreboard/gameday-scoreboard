// Demo build only (firmware/pages/demo.yaml): the Demo card, a backup to the
// remote for starting and stopping the looping demo and picking its teams.
// scripts/build_web.py appends this to the device page in bundle-demo.js;
// release firmware's bundle.js does not have it.
//   GET  /gameday/demo   {kind: "off|pro|college", playing, madeup, pro: {home, away}, college: {...}}
//   POST /gameday/demo   do=start&kind=pro|college, do=stop, pro_home=nfl:6, college_away=ncaa:130, madeup=0/1

(() => {
  "use strict";

  const $ = (sel, root = document) => root.querySelector(sel);
  const card = document.createElement("section");
  card.className = "card";
  card.id = "demo";
  card.style.marginTop = "14px";
  card.innerHTML = `
    <h2>Demo</h2>
    <p class="hint">A scripted game that loops until you stop it, with the normal board for a few seconds between runs. On a WizMote: 1 pro, 2 college, 4 back to live scores.</p>
    <div class="ctl"><label>Now</label><span class="val" id="demoNow" style="min-width:0;text-align:left">...</span></div>
    <div class="actions" style="margin:6px 0 4px">
      <button class="btn primary" data-demo="pro">Start pro</button>
      <button class="btn primary" data-demo="college">Start college</button>
      <button class="btn" data-demo="stop">Stop</button>
    </div>
    <div class="sub">Pro football</div>
    <div class="ctl"><label>Home</label><select data-team="pro_home"></select></div>
    <div class="ctl"><label>Away</label><select data-team="pro_away"></select></div>
    <div class="sub">College football</div>
    <div class="ctl"><label>Home</label><select data-team="college_home"></select></div>
    <div class="ctl"><label>Away</label><select data-team="college_away"></select></div>
    <div class="ctl"><label>Made-up teams<small>Invented names and colors, no logos, and no live board between runs. For filming and anything that gets posted.</small></label><button class="sw" id="demoMadeup" aria-label="Made-up teams"></button></div>`;
  $("#board").after(card);

  let D = null;
  const post = async (params) => {
    const qs = Object.entries(params).map(([k, v]) => k + "=" + encodeURIComponent(v)).join("&");
    try {
      const r = await fetch("/gameday/demo?" + qs, { method: "POST" });
      if (!r.ok) throw new Error("HTTP " + r.status);
    } catch (e) {
      $("#demoNow").textContent = "Device did not respond (" + (e.message || "network") + ")";
    }
    setTimeout(load, 400);
  };

  const selects = Array.from(card.querySelectorAll("select[data-team]"));
  selects.forEach((s) => {
    const lg = s.dataset.team.startsWith("college") ? "ncaa" : "nfl";
    for (const t of TEAMS) {
      if (t[0] !== lg) continue;
      const o = document.createElement("option");
      o.value = t[0] + ":" + t[1];
      o.textContent = t[3];
      s.appendChild(o);
    }
    s.onchange = () => post({ [s.dataset.team]: s.value });
  });
  card.querySelectorAll("button[data-demo]").forEach((b) => {
    b.onclick = () => post(b.dataset.demo === "stop" ? { do: "stop" } : { do: "start", kind: b.dataset.demo });
  });
  const madeup = $("#demoMadeup");
  madeup.onclick = () => {
    const on = !madeup.classList.contains("on");
    madeup.classList.toggle("on", on);
    post({ madeup: on ? 1 : 0 });
  };

  const matchup = (side) =>
    D.madeup ? side.madeup_home.name + " vs " + side.madeup_away.name : side.home.name + " vs " + side.away.name;
  const render = () => {
    if (!D) return;
    const side = D[D.kind];
    $("#demoNow").textContent =
      D.kind === "off" ? "Stopped, showing live scores"
        : (D.kind === "pro" ? "Pro: " : "College: ") + matchup(side) + (D.playing ? "" : " (live board between runs)");
    selects.forEach((s) => {
      if (document.activeElement === s) return;
      const [kind, end] = s.dataset.team.split("_");
      const t = D[kind] && D[kind][end];
      if (t) s.value = t.l + ":" + t.id;
    });
    madeup.classList.toggle("on", !!D.madeup);
  };

  let inflight = false;
  const load = async () => {
    if (inflight) return;
    inflight = true;
    try {
      const r = await fetch("/gameday/demo?_=" + Date.now(), { cache: "no-store" });
      if (r.ok) { D = await r.json(); render(); }
    } catch (_) { /* the poll comes back around */ }
    inflight = false;
  };
  load();
  setInterval(load, 5000);
})();
