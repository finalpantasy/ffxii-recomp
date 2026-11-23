R"MODLAB(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Mod Lab</title>
<style>
:root {
  --bg: #0b0f14; --panel: #121821; --panel2: #18202b; --line: #243041; --text: #e6edf3;
  --muted: #8a99ad; --accent: #7cd4ff; --good: #5bd69a; --bad: #ff6b6b; --warn: #ffc857;
  --c: #5aa9ff; --cpp: #b38cff; --luau-native: #36d6c3; --luau-interp: #ffb547; --other: #c8d1dc;
  --mono: ui-monospace, "Cascadia Mono", "SF Mono", Consolas, monospace;
  --sans: "Segoe UI Variable", "Segoe UI", system-ui, -apple-system, sans-serif;
  color-scheme: dark;
}
@media (prefers-color-scheme: light) {
  :root:not([data-theme="dark"]) {
    --bg: #f4f6f9; --panel: #ffffff; --panel2: #eef2f6; --line: #d6dde6; --text: #17202b;
    --muted: #5b6b7f; --accent: #0a6fb0; --good: #13824f; --bad: #c62828; --warn: #a76a00;
    --c: #1f6fd1; --cpp: #7a4fd6; --luau-native: #0f9488; --luau-interp: #c77a00; --other: #56606c;
    color-scheme: light;
  }
}
:root[data-theme="light"] {
  --bg: #f4f6f9; --panel: #ffffff; --panel2: #eef2f6; --line: #d6dde6; --text: #17202b;
  --muted: #5b6b7f; --accent: #0a6fb0; --good: #13824f; --bad: #c62828; --warn: #a76a00;
  --c: #1f6fd1; --cpp: #7a4fd6; --luau-native: #0f9488; --luau-interp: #c77a00; --other: #56606c;
  color-scheme: light;
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--text); font: 14px/1.45 var(--sans); }
.wrap { max-width: 1180px; margin: 0 auto; padding: 20px 16px 48px; }
header { display: flex; align-items: flex-end; justify-content: space-between; gap: 16px; flex-wrap: wrap;
  border-bottom: 1px solid var(--line); padding-bottom: 14px; margin-bottom: 18px; }
.brand { display: flex; align-items: center; gap: 12px; }
.mark { width: 34px; height: 34px; border: 2px solid var(--accent); transform: rotate(45deg); border-radius: 6px;
  display: grid; place-items: center; }
.mark i { width: 10px; height: 10px; background: var(--accent); border-radius: 2px; }
h1 { margin: 0; font: 600 20px/1 var(--sans); letter-spacing: .18em; }
.sub { color: var(--muted); font-size: 12.5px; margin-top: 4px; letter-spacing: .02em; }
.meta { font: 12px var(--mono); color: var(--muted); text-align: right; }
.meta b { color: var(--text); font-weight: 500; }
.grid { display: grid; grid-template-columns: 320px minmax(0, 1fr); gap: 18px; }
.grid > * { min-width: 0; }
@media (max-width: 860px) { .grid { grid-template-columns: 1fr; } }
.panel { background: var(--panel); border: 1px solid var(--line); border-radius: 10px; }
.panel h2 { margin: 0; font: 600 11px/1 var(--sans); letter-spacing: .16em; text-transform: uppercase;
  color: var(--muted); padding: 14px 16px 10px; }
.mods { padding: 0 10px 10px; display: flex; flex-direction: column; gap: 6px; }
.mod { display: grid; grid-template-columns: 18px 1fr auto; gap: 10px; align-items: center; padding: 9px 10px;
  border-radius: 8px; background: var(--panel2); border: 1px solid transparent; cursor: pointer; }
.mod:hover { border-color: var(--line); }
.mod input { accent-color: var(--accent); margin: 0; }
.mod .n { font-weight: 600; font-size: 13.5px; }
.mod .r { color: var(--muted); font: 11.5px var(--mono); }
.chip { font: 600 11px/1 var(--mono); padding: 4px 7px; border-radius: 4px; color: var(--bg);
  background: var(--k, var(--other)); white-space: nowrap; }
.controls { padding: 12px 16px 16px; border-top: 1px solid var(--line); display: grid; gap: 10px; }
.row { display: flex; gap: 10px; align-items: center; justify-content: space-between; }
label.f { color: var(--muted); font-size: 12.5px; }
select { background: var(--panel2); color: var(--text); border: 1px solid var(--line); border-radius: 6px;
  padding: 5px 8px; font: 12.5px var(--mono); }
button.run { width: 100%; padding: 11px; border-radius: 8px; border: 1px solid var(--accent); cursor: pointer;
  background: linear-gradient(180deg, color-mix(in srgb, var(--accent) 26%, transparent), color-mix(in srgb, var(--accent) 10%, transparent));
  color: var(--text); font: 600 13px var(--sans); letter-spacing: .14em; text-transform: uppercase; }
button.run:disabled { opacity: .55; cursor: wait; }
.hint { color: var(--muted); font-size: 12px; }
.tests { display: grid; gap: 14px; }
.test { padding: 14px 16px 16px; }
.test .hd { display: flex; justify-content: space-between; gap: 12px; align-items: baseline; flex-wrap: wrap; }
.test .t { font-weight: 600; font-size: 15px; }
.test .d { color: var(--muted); font: 12px var(--mono); }
.bars { margin-top: 12px; display: grid; gap: 7px; position: relative; }
.bar { display: grid; grid-template-columns: 110px minmax(0, 1fr) 150px; gap: 12px; align-items: center; }
@media (max-width: 600px) { .bar { grid-template-columns: 100px 1fr 110px; } }
.bar .lbl { font-size: 12.5px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.track { position: relative; height: 18px; background: var(--panel2); border-radius: 4px; overflow: hidden; }
.fill { position: absolute; inset: 0 auto 0 0; background: var(--k, var(--other)); border-radius: 4px;
  transition: width .5s cubic-bezier(.2,.8,.2,1); }
.fill.game { background: repeating-linear-gradient(135deg, var(--muted) 0 4px, transparent 4px 8px); opacity: .55; }
.val { font: 12.5px var(--mono); text-align: right; white-space: nowrap; }
.val small { color: var(--muted); }
.ok { color: var(--good); } .no { color: var(--bad); }
table { width: 100%; border-collapse: collapse; font: 12.5px var(--mono); }
th, td { padding: 8px 10px; text-align: right; border-bottom: 1px solid var(--line); white-space: nowrap; }
th:first-child, td:first-child { text-align: left; }
th { color: var(--muted); font: 600 11px var(--sans); letter-spacing: .08em; text-transform: uppercase; }
.best { color: var(--good); }
.tblwrap { overflow-x: auto; padding: 0 6px 8px; }
.log { font: 12px var(--mono); color: var(--muted); padding: 0 16px 14px; white-space: pre-wrap; max-height: 180px; overflow: auto; }
.empty { padding: 36px 16px; text-align: center; color: var(--muted); }
.foot { margin-top: 14px; color: var(--muted); font: 12px var(--mono); display: flex; gap: 18px; flex-wrap: wrap; }
.err { color: var(--bad); font: 12px var(--mono); padding: 0 16px 12px; }
.theme { background: none; border: 1px solid var(--line); color: var(--muted); border-radius: 6px; padding: 4px 8px;
  font: 11px var(--mono); cursor: pointer; }
</style>
</head>
<body>
<div class="wrap">
  <header>
    <div class="brand">
      <div class="mark"><i></i></div>
      <div>
        <h1>MOD LAB</h1>
        <div class="sub">One test mod per language, the same hooks, timed per call</div>
      </div>
    </div>
    <div class="meta"><div id="cpu">&nbsp;</div><div id="mode">&nbsp;</div>
      <button class="theme" id="theme" title="Switch light and dark">theme</button></div>
  </header>
  <div class="grid">
    <aside class="panel">
      <h2>Mods</h2>
      <div class="mods" id="mods"><div class="hint" style="padding:6px">Loading…</div></div>
      <div class="err" id="listerr"></div>
      <div class="controls">
        <div class="row"><label class="f" for="iters">Calls per test</label>
          <select id="iters"><option>200000</option><option selected>1000000</option><option>5000000</option></select></div>
        <div class="row"><label class="f" for="reps">Repeats (median)</label>
          <select id="reps"><option>3</option><option selected>5</option><option>9</option></select></div>
        <button class="run" id="run">Run</button>
        <div class="hint" id="status">The game does not run here: each mod hooks three stand-in functions through the real mod API and loaders.</div>
      </div>
    </aside>
    <main>
      <div class="tests" id="tests"><div class="panel empty">Pick mods and press Run.</div></div>
      <div class="panel" style="margin-top:14px" id="sumpanel" hidden>
        <h2>Summary · ns per call (median)</h2>
        <div class="tblwrap"><table id="sum"></table></div>
      </div>
      <div class="panel" style="margin-top:14px" id="logpanel" hidden>
        <h2>Mod log</h2>
        <div class="log" id="log"></div>
      </div>
      <div class="foot" id="foot"></div>
    </main>
  </div>
</div>
<script>
const $ = (id) => document.getElementById(id);
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({"&":"&amp;","<":"&lt;",">":"&gt;","\"":"&quot;"}[c]));
let list = null;

function colorOf(e) {
  const l = (e.language || "").toLowerCase(), r = (e.runtime || "").toLowerCase();
  if (r.includes("interpreter")) return "var(--luau-interp)";
  if (l === "luau") return "var(--luau-native)";
  if (l === "c++") return "var(--cpp)";
  if (l === "c") return "var(--c)";
  return "var(--other)";
}
function shortRuntime(e) {
  if (e.runtime === "Luau native code") return "Luau · native";
  if (e.runtime === "Luau interpreter") return "Luau · interp";
  return e.language;
}
function fmt(ns) { return ns < 10 ? ns.toFixed(2) : ns < 100 ? ns.toFixed(1) : ns.toFixed(0); }

try {
  const saved = localStorage.getItem("modlab-theme");
  if (saved) document.documentElement.dataset.theme = saved;
} catch (e) {}
$("theme").onclick = () => {
  const cur = document.documentElement.dataset.theme ||
    (matchMedia("(prefers-color-scheme: light)").matches ? "light" : "dark");
  const next = cur === "light" ? "dark" : "light";
  document.documentElement.dataset.theme = next;
  try { localStorage.setItem("modlab-theme", next); } catch (e) {}
};

async function loadList() {
  const r = await fetch("/api/list");
  list = await r.json();
  $("cpu").innerHTML = "<b>" + esc(list.cpu || "unknown CPU") + "</b>";
  $("mode").textContent = "Luau native code: " + (list.luau_native ? "available" : "not on this host");
  const box = $("mods");
  box.innerHTML = "";
  if (!list.entries.length) box.innerHTML = '<div class="hint" style="padding:6px">No lab mods in ' + esc(list.dir) + "</div>";
  for (const e of list.entries) {
    const row = document.createElement("label");
    row.className = "mod";
    row.innerHTML = '<input type="checkbox" checked value="' + esc(e.key) + '"><div><div class="n">' + esc(e.name) +
      '</div><div class="r">' + esc(e.runtime) + " · " + esc(e.files.join(", ")) + '</div></div><span class="chip" style="--k:' +
      colorOf(e) + '">' + esc(shortRuntime(e)) + "</span>";
    box.appendChild(row);
  }
  $("listerr").textContent = list.errors.join("\n");
}

function render(res) {
  const tests = list.tests;
  const box = $("tests");
  box.innerHTML = "";
  for (const t of tests) {
    const game = res.vanilla[t.id];
    const rows = res.entries.filter((e) => e.ok).map((e) => ({ e, r: e.tests.find((x) => x.test === t.id) })).filter((x) => x.r);
    const max = Math.max(game.ns, ...rows.map((x) => x.r.ns)) || 1;
    const best = Math.min(...rows.map((x) => x.r.ns));
    const p = document.createElement("section");
    p.className = "panel test";
    let html = '<div class="hd"><div class="t">' + esc(t.title) + '</div><div class="d">' + esc(t.detail) + '</div></div><div class="bars">';
    html += '<div class="bar"><div class="lbl" style="color:var(--muted)">game alone</div><div class="track"><div class="fill game" style="width:' +
      (100 * game.ns / max).toFixed(2) + '%"></div></div><div class="val">' + fmt(game.ns) + " ns</div></div>";
    for (const { e, r } of rows) {
      const over = r.ns - game.ns;
      const check = r.mismatches === 0 ? '<span class="ok">✓</span>' : '<span class="no">✗ ' + r.mismatches + "</span>";
      html += '<div class="bar"><div class="lbl" title="' + esc(e.name) + '">' + esc(shortRuntime(e)) + '</div><div class="track"><div class="fill" style="--k:' +
        colorOf(e) + ";width:" + (100 * r.ns / max).toFixed(2) + '%"></div></div><div class="val">' +
        (r.ns === best ? '<span class="best">' + fmt(r.ns) + "</span>" : fmt(r.ns)) + " ns <small>" + (over >= 0 ? "+" : "") + fmt(over) + "</small> " + check + "</div></div>";
    }
    html += "</div>";
    p.innerHTML = html;
    box.appendChild(p);
  }
  // Summary table.
  let h = "<tr><th>Mod</th><th>Runtime</th><th>Load ms</th>";
  for (const t of tests) h += "<th>" + esc(t.id) + "</th>";
  h += "<th>Results</th></tr>";
  const bestOf = {};
  for (const t of tests) bestOf[t.id] = Math.min(...res.entries.filter((e) => e.ok).map((e) => (e.tests.find((x) => x.test === t.id) || {ns: Infinity}).ns));
  h += '<tr><td style="color:var(--muted)">game alone</td><td>–</td><td>–</td>';
  for (const t of tests) h += "<td>" + fmt(res.vanilla[t.id].ns) + "</td>";
  h += "<td></td></tr>";
  for (const e of res.entries) {
    h += '<tr><td><span class="chip" style="--k:' + colorOf(e) + '">' + esc(e.language) + "</span> " + esc(e.name) + "</td><td>" + esc(e.runtime) + "</td><td>" +
      (e.load_ms < 10 ? e.load_ms.toFixed(2) : e.load_ms.toFixed(1)) + "</td>";
    if (!e.ok) { h += '<td colspan="' + tests.length + '" class="no">' + esc(e.error || "failed") + "</td><td></td></tr>"; continue; }
    let bad = 0, calls = 0;
    for (const t of tests) {
      const r = e.tests.find((x) => x.test === t.id);
      bad += r.mismatches; calls += r.calls;
      const ratio = r.ns / bestOf[t.id];
      h += "<td" + (r.ns === bestOf[t.id] ? ' class="best"' : "") + ">" + fmt(r.ns) + (ratio > 1.05 ? ' <small style="color:var(--muted)">×' + ratio.toFixed(1) + "</small>" : "") + "</td>";
    }
    h += "<td>" + (bad === 0 ? '<span class="ok">✓ ' + calls.toLocaleString() + "</span>" : '<span class="no">✗ ' + bad.toLocaleString() + " wrong</span>") + "</td></tr>";
  }
  $("sum").innerHTML = h;
  $("sumpanel").hidden = false;
  const logs = res.entries.flatMap((e) => e.log);
  $("log").textContent = logs.length ? logs.join("\n") : "(no log lines)";
  $("logpanel").hidden = false;
  $("foot").innerHTML = "<span>" + Number(res.iters).toLocaleString() + " calls × " + res.reps + " repeats per test</span><span>run " +
    res.seconds.toFixed(1) + " s</span><span>machine CPU load " + (res.machine_load >= 0 ? res.machine_load.toFixed(0) + "%" : "unknown") +
    "</span><span>" + esc(res.cpu) + "</span>";
}

$("run").onclick = async () => {
  const only = [...document.querySelectorAll("#mods input:checked")].map((i) => i.value);
  if (!only.length) { $("status").textContent = "Pick at least one mod."; return; }
  $("run").disabled = true;
  $("status").textContent = "Running… other work on this PC changes the numbers; the machine load is shown with the results.";
  try {
    const r = await fetch("/api/run?iters=" + $("iters").value + "&reps=" + $("reps").value + "&only=" + encodeURIComponent(only.join(",")));
    const res = await r.json();
    render(res);
    $("status").textContent = "Done. Bars are ns per hook call (median of repeats); the small number is the cost over the game function alone.";
  } catch (e) {
    $("status").textContent = "Run failed: " + e;
  }
  $("run").disabled = false;
};

loadList().catch((e) => { $("mods").textContent = "Cannot reach the lab: " + e; });
</script>
</body>
</html>
)MODLAB"
