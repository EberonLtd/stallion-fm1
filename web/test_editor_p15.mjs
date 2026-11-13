// SPDX-License-Identifier: GPL-3.0-only
// Protocol 15 (Stallion, web/EDITOR_PROTOCOL.md "v15") with the editor's own protocol code (editor.html between
// PROTO-BEGIN / END) against the real firmware on the host (tests/editor_bridge.c), and the editor's coverage of the
// FM-1 (specs/stallion R40, R41, R42, R43, R44, R45):
//   node web/test_editor_p15.mjs build/host/editor_bridge
// - R42: INFO 15 + P_ALL, GET / SET / DESC scope 2 of every id, CHANGED scope 2, DUMP / TRACK_DUMP flags; protocol 14's view
//   unchanged (INFO's 127 / 119, DUMP without flags)
// - Stallion: a slider dragged through 100 values in 1 s (LivePacer, as userSet sends): the FM-1 takes values while it
//   moves, never more than ~30 ms apart, and the last one exactly; the TUNING card's TUNE_GET / TUNE_PUT and a Scala
//   file move the pitch the FM-1 plays (measured on its audio); the installer page's counts (web/make_site.py
//   firmware_counts) are the FM-1's own engines and sounds
// - R41: live descriptors (MARE / RODEO's 4th EDIT label by its value, STAMPEDE's WAVE names), the new formats
// - R43: UP_PUT with the device's own values, UP_GET gives them back (the editor's bank / capturePatch), an UP_PUT
//   without them stores the defaults; a sound file's values by place (patchByCount for an older editor)
// - R44: scope 3 GET / SET / DESC / push (GLO > TOUCH, MENU > LIGHTS)
// - R45: every protocol 15 id and every page of the FM-1's ENV, LFO, FX, SEL, EDIT, GLO, ARP layers has a place in the
//   editor (LAYOUT, P_ELSEWHERE, PAGE_PLACE, SX_PAGES), every scope 3 setting is in the settings card
// - R40: the offline editor (web/make_site.py offline_editor) has the same protocol code, layout and settings card
// Exit 1 on any failure.

import { execFileSync, spawn } from "node:child_process";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

const HERE = fileURLToPath(new URL(".", import.meta.url));
const BRIDGE = process.argv[2] || join(HERE, "../build/host/editor_bridge");
let failed = 0;
const ok = (cond, what) => { console.log(`${what.padEnd(72)} ${cond ? "ok" : "FAIL"}`); if (!cond) failed++; };

const html = readFileSync(join(HERE, "editor.html"), "utf8");
const cut = (a, b, src = html) => { const i = src.indexOf(a), j = src.indexOf(b); return i >= 0 && j > i ? src.slice(i, j + b.length) : ""; };
const proto = cut("/*PROTO-BEGIN*/", "/*PROTO-END*/"), layout = cut("/*LAYOUT-BEGIN*/", "/*LAYOUT-END*/");
ok(proto && layout, "editor.html: PROTO and LAYOUT blocks");
const E = vm.runInNewContext(proto + "\n" + layout + `
;({ parse, req, Link, CMD, F, fmtValue, bank, capturePatch, paramKeys, patchByCount, upNx, isV15, LAYOUT, mxSlots, P_ELSEWHERE,
   PAGE_PLACE, SX_PAGES, LivePacer, liveOpt, parseScl, makeMockDevice, MOCK_FW })`, { setTimeout, clearTimeout, setInterval, clearInterval, console, TextEncoder, TextDecoder });

/* ---- the bridge: a line of hex a frame, "." after each line */
const br = spawn(BRIDGE, [], { stdio: ["pipe", "pipe", "inherit"] });
const syncs = [], pushes = [], hzs = [];
let link, buf = "", cover = null;
const js = (x) => JSON.stringify(x);
br.stdout.setEncoding("utf8");
br.stdout.on("data", (d) => {
  buf += d;
  let k;
  while ((k = buf.indexOf("\n")) >= 0) {
    const line = buf.slice(0, k).trim();
    buf = buf.slice(k + 1);
    if (line === ".") { const r = syncs.shift(); if (r) r(); }
    else if (line.startsWith("{")) cover = JSON.parse(line);
    else if (line.startsWith("#hz")) hzs.push(+line.slice(4));
    else if (/^F0/.test(line)) link.receive(Uint8Array.from(line.split(/\s+/).map((x) => parseInt(x, 16))));
  }
});
const put = (line) => new Promise((res) => { syncs.push(res); br.stdin.write(line + "\n"); });
link = new E.Link((f) => { syncs.push(() => {}); br.stdin.write(Array.from(f, (b) => b.toString(16).padStart(2, "0").toUpperCase()).join(" ") + "\n"); },
  { timeout: 2000, onPush: (f) => pushes.push(f) });
const rq = (r, o) => link.request(r, o);

try {
  const info = E.parse[E.CMD.INFO](await rq(E.req.info()));
  ok(info.proto === 15 && info.pall === 184 && E.isV15(info), "R42 INFO: protocol 15, P_ALL 184");
  ok(info.pcount === 127 && info.pe0 === 119, "R42 INFO: protocol 14's counts unchanged (127, P_E0 119)");
  const d14 = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
  ok(d14.p.length === 127 && d14.g.length === info.gcount, "R42 DUMP without flags: 127 + G_COUNT values (a v14 editor's)");
  const dump = E.parse[E.CMD.DUMP](await rq(E.req.dump(1)), info);
  ok(dump.p.length === 184, "R42 DUMP with flag 1: 184 values");

  const pdesc = [];
  for (let i = 0; i < info.pall; i++) pdesc[i] = E.parse[E.CMD.DESC](await rq(E.req.pdesc(info, i)));
  ok(pdesc.every((d, i) => d && d.id === i && d.scope === 0 && d.label), "R42 DESC scope 2 of every id 0..183 (normalised to scope 0)");
  ok(pdesc[127].label === "SLOPE" && ["UP", "DOWN"].every((l, k) => pdesc[133 + k].label === l) && pdesc[133].fmt === E.F.BRNG,
    "R42 DESC: SLOPE at 127, the BEND page's UP DOWN (BRNG) at 133..");
  ok(E.fmtValue(pdesc[133], -1)[0] === "GLO" && E.fmtValue(pdesc[133], 7).join(" ") === "7 st", "R41 BRNG shown as the FM-1 (GLO, 7 st)");
  {   /* (Stallion) PONY's CHAR (id 181): a bit a quirk, its names the FM-1's; the sets by name */
    const c = pdesc[181];
    ok(c.fmt === E.F.CHAR && c.label === "CHAR" && c.min === 0 && c.max === 0x3FF && c.def === 0x3E && c.names.length === 10 &&
       c.names[0] === "STEPS" && c.names[1] === "HISS" && c.names[9] === "CPU TICK" && E.fmtValue(c, 0)[0] === "CLEAN" &&
       E.fmtValue(c, 0x3E)[0] === "CLASSIC" && E.fmtValue(c, 0x3FF)[0] === "FULL" && E.fmtValue(c, 0x3F)[0] === "CUSTOM",
       `PONY's CHAR: ${c.label} fmt ${c.fmt} ${c.min}..${c.max} def ${c.def}, boxes ${c.names.join(" / ")}`);
  }

  let r = E.parse[E.CMD.SET](await rq(E.req.pset(info, 133, 99)));
  ok(r.scope === 0 && r.id === 133 && r.value === pdesc[133].max, "R42 SET scope 2 (BEND UP 99) clamps to its max");
  r = E.parse[E.CMD.SET](await rq(E.req.pset(info, 133, 5)));
  r = E.parse[E.CMD.GET](await rq(E.req.pget(info, 133)));
  ok(r.value === 5, "R42 GET scope 2 reads it back");
  r = E.parse[E.CMD.GET](await rq(E.req.get(2, 3)));
  ok(r.scope === 0 && r.id === 3 && r.value === dump.p[3], "R42 GET scope 2 of id 3 = scope 0's (ids keep their meaning)");

  /* R41: live labels and names (G_ENGSEL 20: the engine with its defaults) */
  /* the engines by number (core.h ENGI_*: the stores' numbers, which never change; their names may) */
  const core = readFileSync(join(HERE, "../firmware/src/core.h"), "utf8");
  const eng = (k) => { const m = new RegExp(`#define ENGI_${k} (\\d+)u`).exec(core); return m ? +m[1] : -1; };
  ok(eng("MARE") >= 0 && eng("RODEO") >= 0 && eng("STAMPEDE") >= 0 && info.engines[eng("MARE")] === "MARE" &&
    info.engines[eng("RODEO")] === "RODEO" && info.engines[eng("STAMPEDE")] === "STAMPEDE", "R41 MARE, RODEO, STAMPEDE by their engine numbers");
  const e3 = async () => E.parse[E.CMD.DESC](await rq(E.req.pdesc(info, info.pe0 + 3))).label;
  await rq(E.req.set(1, 20, eng("MARE")));
  await rq(E.req.pset(info, info.pe0 + 3, -10)); const mfm = await e3();
  await rq(E.req.pset(info, info.pe0 + 3, 10)); const msy = await e3();
  await rq(E.req.pset(info, info.pe0 + 3, 0)); const msf = await e3();
  ok(mfm === "FM" && msy === "SYNC" && msf === "SY/FM", `R41 MARE's 4th EDIT value by its side (${mfm} ${msy} ${msf})`);
  const mw = E.parse[E.CMD.DESC](await rq(E.req.pdesc(info, info.pe0)));
  await rq(E.req.set(1, 20, eng("RODEO")));
  await rq(E.req.pset(info, info.pe0 + 3, -5)); const ox = await e3();
  await rq(E.req.pset(info, info.pe0 + 3, 5)); const od = await e3();
  const od4 = E.parse[E.CMD.DESC](await rq(E.req.pdesc(info, info.pe0 + 3)));
  ok(ox === "XMOD" && od === "DTUNE" && od4.dyn, `R41 RODEO's DTUNE / XMOD by its value, flagged as following (${ox} ${od})`);
  await rq(E.req.set(1, 20, eng("STAMPEDE")));
  const cw = E.parse[E.CMD.DESC](await rq(E.req.pdesc(info, info.pe0)));
  ok(cw.label === "WAVE" && cw.spread && cw.names[0] === "NOIS" && E.fmtValue(cw, 32)[0] === "SAW" && E.fmtValue(cw, 127)[0] === "RING",
    "R41 STAMPEDE's WAVE as names (an INT split over them)");
  ok(mw.spread && mw.names.length >= 7, "R41 MARE's WAVE as names");
  const src = E.parse[E.CMD.DESC](await rq(E.req.pdesc(info, 60))), dst = E.parse[E.CMD.DESC](await rq(E.req.pdesc(info, 61)));
  ok(["BEND", "POLAT", "CHNAT", "RIBBN"].every((n) => src.names.includes(n)) && ["PIT1", "PIT2", "XMOD", "HPF"].every((n) => dst.names.includes(n)),
    "R41 the matrix's new sources and destinations named");
  /* preset format 2: the matrix's slots 13..24 (ids 141..176) */
  const dsc = async (id) => E.parse[E.CMD.DESC](await rq(E.req.pdesc(info, id)));   /* (now: the DSTs' E1..E8 the engine's) */
  const s13 = await dsc(141), d13 = await dsc(142), d24 = await dsc(175);
  ok(s13.label === "SRC" && d13.label === "DST" && pdesc[143].label === "AMT" && js(s13.names) === js(src.names) && js(d24.names) === js(dst.names),
    "slots 13..24: SRC DST AMT at 141.., as slot 1's descriptors");
  await rq(E.req.pset(info, 175, dst.names.indexOf("E1AMT")));   /* (engine-dests: a code live on STAMPEDE; RING is MARE's alone) */
  r = E.parse[E.CMD.GET](await rq(E.req.pget(info, 175)));
  ok(r.value === dst.names.indexOf("E1AMT") && r.value >= 32, "slot 24's DST: SET / GET scope 2 (a 6-bit code)");
  await rq(E.req.pset(info, 175, 0));
  ok(E.mxSlots(info) === 24 && E.mxSlots({ ...info, pall: 141 }) === 12 && E.mxSlots({ pe0: 89, pcount: 97 }) === 6,
    "mxSlots: 24 from the device's P_ALL (12 on an older Stallion, 6 before Clipper 1.0)");
  const mxp = E.LAYOUT(info.pe0, undefined, info.pall).find((g) => g.title === "MATRIX").pages;
  ok(mxp.length === 24 && js(mxp[12][2]) === js([141, 142, 143]) && js(mxp[23][2]) === js([174, 175, 176]) &&
    E.LAYOUT(info.pe0, undefined, 141).find((g) => g.title === "MATRIX").pages.length === 12,
    "the MATRIX pages: 24 on this device (13: ids 141..143, 24: 174..176), 12 on an older Stallion");
  /* the editor's demo device (makeMockDevice, Stallion: web/gen_mock.mjs's MOCK_FW) is the FM-1 as the website's
     screenshots show it: INFO, every descriptor of scopes 0 (0..126), 2 (0..140), 1 and 3 on every engine and
     track, the labels that follow a value (each value of the INSERT's TYPE, the arp's MODE, LINK, the engines' desc
     hooks), every engine's preset names and page titles, the values a factory preset sets. MOCK_FW == the firmware */
  {
    let gen = "", genOk = true;
    try { gen = execFileSync("node", [join(HERE, "gen_mock.mjs"), "--check", BRIDGE], { encoding: "utf8", timeout: 300000 }).trim(); } catch (e) { genOk = false; gen = String(e.stdout || e.message).trim(); }
    ok(genOk, `the editor's mock: ${gen}`);
    const m = E.makeMockDevice({ clipper: true, auto: false, fast: true });
    const mi = [...m.access.inputs.values()][0], mo = [...m.access.outputs.values()][0];
    const ml = new E.Link((d) => mo.send(d), { timeout: 1000 });
    mi.onmidimessage = (e) => ml.receive(e.data);
    const mq = (r) => ml.request(r);
    const mInfo = E.parse[E.CMD.INFO](await mq(E.req.info()));
    ok(mInfo.version === info.version + " (MOCK)" && mInfo.engines.join() === info.engines.join() && mInfo.pcount === info.pcount &&
      mInfo.pe0 === info.pe0 && mInfo.pall === info.pall && mInfo.gcount === info.gcount && mInfo.proto === info.proto && mInfo.ntrk === info.ntrk,
      `the editor's mock: INFO as the FM-1's (${mInfo.version}, P_COUNT ${mInfo.pcount}, P_E0 ${mInfo.pe0}, P_ALL ${mInfo.pall}, ${mInfo.gcount} globals, protocol ${mInfo.proto})`);
    const sh = (d) => d ? `${d.label} fmt ${d.fmt} ${d.min}..${d.max} def ${d.def} ${d.unit || ""} [${d.names.join(" ")}] ${d.flags | 0}` : "-";
    /* (a side that leaves 20 requests unanswered is not asked again: a mock behind the firmware (an older P_ALL) left
       every newer id without a reply, 2..4 s of timeouts each, and the run took over half an hour instead of failing) */
    const bad = [], seen = new Set(), mute = [0, 0];
    const ask = async (s, f, r) => {
      if (mute[s] >= 20) return null;
      try { return await f(r); } catch (e) {
        if (++mute[s] === 20) bad.push(`${s ? "the mock" : "the FM-1"} left 20 requests unanswered (${String(e.message)}): not asked again`);
        return null;
      }
    };
    const both = async (r) => [await ask(0, rq, r), await ask(1, mq, r)];
    let n = 0;
    const cmpDesc = async (sc, id, where) => {
      const [a, b] = await both(sc === 2 ? E.req.pdesc(info, id) : E.req.desc(sc, id));
      const x = a && sh(E.parse[E.CMD.DESC](a)), y = b && sh(E.parse[E.CMD.DESC](b));
      n++;
      if (x !== y && !seen.has(`${sc}:${id}:${x}`)) { seen.add(`${sc}:${id}:${x}`); bad.push(`${where} ${sc}:${id} FM-1 ${x} / mock ${y}`); }
    };
    for (let i = 0; i < info.gcount; i++) await cmpDesc(1, i, "");
    await put("#cover");                                    /* (the settings: the FM-1's list; the mock's the same ids) */
    if (E.MOCK_FW.xs.map((x) => x[0]).join() !== cover.settings.join()) bad.push("the settings' ids");
    for (const i of cover.settings) await cmpDesc(3, i, "");
    const kept = new Set(E.MOCK_FW.kept);
    let vals = 0, nm = 0;
    const t0 = Date.now();
    for (let e = 0; e < info.nengines; e++) {
      if (process.env.P15_DEBUG) console.log(info.engines[e], Date.now() - t0, n);
      await both(E.req.set(0, 17, 0));                     /* (the arp off: a preset brings its own) */
      await both(E.req.set(1, 20, e));
      for (let i = 0; i < info.pcount; i++) await cmpDesc(0, i, info.engines[e]);
      for (let i = 0; i < info.pall; i++) await cmpDesc(2, i, info.engines[e]);
      const [na, nb] = await both(E.req.names(e));
      if (!na || !nb) { bad.push(`${info.engines[e]} NAMES: no reply`); continue; }
      const x = E.parse[E.CMD.NAMES](na), y = E.parse[E.CMD.NAMES](nb);
      if (x.names.join() !== y.names.join() || x.titles.join() !== y.titles.join()) bad.push(`${info.engines[e]} NAMES`); else nm++;
      /* every factory preset: the values it sets (all but the mix, pattern and key ones) */
      for (let k = 0; k < x.names.length; k++) {
        await both(E.req.set(0, 17, 0));
        await both(E.req.preset(e, k));
        const [da, db] = await both(E.req.dump(1));
        if (!da || !db) { bad.push(`${info.engines[e]} ${x.names[k]} DUMP: no reply`); continue; }
        const pa = E.parse[E.CMD.DUMP](da, info).p, pb = E.parse[E.CMD.DUMP](db, info).p;
        const diff = pa.map((v, i) => (kept.has(i) || v === pb[i] ? "" : `${i}: ${v}/${pb[i]}`)).filter(Boolean);
        if (diff.length) bad.push(`${info.engines[e]} ${x.names[k]} values ${diff.join(" ")}`); else vals++;
      }
      /* the labels that follow a value: each value of every id that drives others */
      const fol = [], drv = [];
      for (let i = 0; i < info.pall; i++) {
        const d = E.parse[E.CMD.DESC](await rq(E.req.pdesc(info, i)));
        if (d.flags & 1) fol.push(i);
        if (d.flags & 2) drv.push([i, d.min, d.max, d.def]);
      }
      for (const [dv, , , df] of drv) await both(E.req.pset(info, dv, df));   /* (each driver from the plain sound) */
      /* (the sweep moves one driver at a time, the others at their defaults; rules that read two drivers get the
         cross-product spot check below) */
      for (const [dv, lo, hi, df] of drv) {
        const vs = [];                                   /* (a wide range: every 4th value and both sides of 0) */
        for (let v = lo; v <= hi; v++) if (hi - lo < 20 || v % 4 === 0 || Math.abs(v) <= 1 || v === hi) vs.push(v);
        for (const v of vs) {
          await both(E.req.pset(info, dv, v));
          for (const i of fol) await cmpDesc(2, i, `${info.engines[e]} ${dv}=${v}`);
        }
        await both(E.req.pset(info, dv, df));
      }
    }
    /* two or three drivers at once: the INSERT's TYPE x the arp's MODE x the filter's LINK (on the first engine) */
    {
      const lab = {}, fol = [];
      for (let i = 0; i < info.pall; i++) {
        const d = E.parse[E.CMD.DESC](await rq(E.req.pdesc(info, i)));
        if (d.flags & 1) fol.push(i);
        for (const l of ["INSRT", "MODE", "LINK"]) if (d.label === l && lab[l] === undefined) lab[l] = i;
      }
      ok(lab.INSRT !== undefined && lab.MODE !== undefined && lab.LINK !== undefined && fol.length > 3, "the spot check: INSERT TYPE, arp MODE, filter LINK found");
      await both(E.req.set(1, 20, 0));
      for (const it of [0, 1, 5, 8]) for (const am of [0, 1, 7, 15]) for (const lk of [0, 1]) {
        await both(E.req.pset(info, lab.INSRT, it));
        await both(E.req.pset(info, lab.MODE, am));
        await both(E.req.pset(info, lab.LINK, lk));
        for (const i of fol) await cmpDesc(2, i, `INSRT=${it} MODE=${am} LINK=${lk}`);
      }
      await both(E.req.pset(info, lab.INSRT, 0));
      await both(E.req.pset(info, lab.MODE, 0));
      await both(E.req.pset(info, lab.LINK, 0));
    }
    /* Stallion's track_select is a no-op (ui.c CLIPPER_SYNTH): TRACK(3) selects nothing, on the FM-1 and in the mock */
    {
      const [a, b] = await both(E.req.track(info.ntrk - 1));
      const x = a ? E.parse[E.CMD.TRACK](a) : {}, y = b ? E.parse[E.CMD.TRACK](b) : {};
      ok(x.sel === 0 && y.sel === 0, `TRACK(${info.ntrk - 1}) does not select (FM-1 track ${x.sel}, mock ${y.sel})`);
    }
    await both(E.req.track(0));
    ml.close(); m.stop();

    /* the demo's simulator (auto on): the Stallion page stays on its showcase sound; ?live=1 (opt.live) moves it */
    {
      const run = (live) => {
        const iv = [];
        const E2 = vm.runInNewContext(proto + "\n" + layout + ";({ makeMockDevice })",
          { setTimeout, clearTimeout, setInterval: (f, ms) => (iv.push({ f, ms, next: ms }), iv.length), clearInterval() {}, console, TextEncoder, TextDecoder });
        const d = E2.makeMockDevice({ clipper: true, live });
        const st = d.state, p0 = st.p.slice(), pre0 = st.preset, tr0 = JSON.stringify(st.tracks.map((t) => t.p)), stp0 = JSON.stringify(st.step);
        for (let t = 1000; t <= 70000; t += 1000) for (const x of iv) while (x.next <= t) { x.f(); x.next += x.ms; }   /* (fake time: 70 s) */
        const same = st.preset === pre0 && JSON.stringify(st.p) === JSON.stringify(p0) && JSON.stringify(st.step) === stp0;
        return { n: iv.length, same, mixSame: JSON.stringify(st.tracks.map((t) => t.p)) === tr0 };
      };
      const still = run(false), live = run(true);
      ok(still.same && still.mixSame && live.n > still.n && !live.same && !live.mixSame,
        `the demo's simulator: 70 s on, the preset, steps and mix stay (${still.n} timers); ?live=1 moves them (${live.n} timers)`);
    }
    ok(n > 10000 && nm === info.nengines && vals > 500 && !bad.length, `the editor's mock describes ${n} values (every scope, engine, spot-checked drivers) ` +
      `and ${vals} factory presets as the FM-1` + (bad.length ? ": " + bad.slice(0, 12).join("; ") + (bad.length > 12 ? ` .. (${bad.length})` : "") : ""));
  }

  /* R43: user presets with the device's own values */
  const all = E.parse[E.CMD.DUMP](await rq(E.req.dump(1)), info);
  /* (preset format 2: every value exact; slots 13..24 at 141..; their DSTs live on this engine (engine-dests: the
     matrix's FLTFM / RING codes are MARE's alone, a slot into them is cleared elsewhere): LFO1, E1AMT, by name) */
  const want = { 127: 2, 128: 3, 132: 1, 133: 7, 134: 12, 137: -30, 138: 1, 139: 12, 140: -24, 177: 85, 178: 2, 179: 80, 180: 1, 181: 0x2A5, 182: 30, 183: 2,
    141: 30, 142: dst.names.indexOf("LFO1"), 143: -64, 159: 7, 160: 2, 174: 25, 175: dst.names.indexOf("E1AMT"), 176: 63 };
  ok(want[142] > 0 && want[175] >= 32, "R43 the slots' DSTs: LFO1, E1AMT (a 6-bit code)");
  const pt = { name: "P15 TEST", engine: all.engine, p: all.p.slice(), pattern: [] };
  for (const [k, v] of Object.entries(want)) pt.p[k] = Math.max(pdesc[k].min, Math.min(pdesc[k].max, v));
  const nx = E.upNx(info, pdesc);
  ok(nx && nx.from === 127 && nx.n === 57, "R43 upNx: ids 127..183");
  ok((await E.bank.put(rq, 3, pt, nx)) === 0, "R43 UP_PUT with the device's own values: rc 0");
  const back = await E.bank.get(rq, info, 3);
  if (process.env.P15_DEBUG) console.log(Object.keys(want).map((k) => `${k}: ${pt.p[k]}/${back.p[k]}`).join(" "), back.p.length);
  ok(back.p.length === 184 && Object.keys(want).every((k) => back.p[k] === pt.p[k]), "R43 UP_GET gives them back (SLOPE, FILTER 2, LINK, BEND, VCO3, VCO4, slots 13..24, WAV34, PONY's RANGE KYBD VCA CHAR 0x2A5: both bytes, RODEO's KYBD PW0)");
  ok((await E.bank.put(rq, 4, { ...pt, p: pt.p.slice(0, 127) })) === 0, "R43 UP_PUT without them (protocol 14's): rc 0");
  const b4 = await E.bank.get(rq, info, 4);
  ok(b4.p.slice(127).every((v, k) => v === pdesc[127 + k].def), "R43 .. those values at their defaults");
  await rq(E.req.upLoad(3));
  const cap = await E.capturePatch(rq, info, "CAP");
  ok(Object.keys(want).every((k) => cap.patch.p[k] === pt.p[k]), "R43 the loaded preset: capturePatch (the library) holds them");
  const keys = E.paramKeys(pdesc, info.pe0, info.pall);
  ok(keys.length === 184 && keys[119] === "E0" && keys[126] === "E7" && keys[127] === "SLOPE" && new Set(keys).size === 184,
    "R43 library keys: the device's own by label, unique");
  const old = E.patchByCount(cap.patch.p, 119, 127, 119);
  ok(old.length === 127 && old.every((v, i) => v === cap.patch.p[i]), "R43 a 184-value sound file in an older editor: its 127 by place");

  /* R42 pushes: WATCH bit 2 */
  const w = E.parse[E.CMD.WATCH](await rq(E.req.watch(7)));
  ok((w.on & 5) === 5, "R42 WATCH 7: bit 2 known");
  pushes.length = 0;
  await put("#p 133 9");
  await put("#tick 5");
  const ch = pushes.filter((f) => f.cmd === E.CMD.CHANGED).map((f) => E.parse[E.CMD.CHANGED](f.a));
  ok(ch.some((c) => c.scope === 0 && c.id === 133 && c.value === 9), "R42 CHANGED scope 2 of BEND UP (133) pushed");

  /* R44: the FM-1's own settings */
  const s57 = E.parse[E.CMD.DESC](await rq(E.req.desc(3, 57))), s96 = E.parse[E.CMD.DESC](await rq(E.req.desc(3, 96)));
  ok(s57.scope === 3 && s57.id === 57 && s57.names.length >= 2 && s96.label && s96.max >= 8, `R44 DESC scope 3: ${s57.label}, ${s96.label}`);
  r = E.parse[E.CMD.SET](await rq(E.req.set(3, 96, 3)));
  const g96 = E.parse[E.CMD.GET](await rq(E.req.get(3, 96)));
  ok(r.scope === 3 && r.value === 3 && g96.value === 3, "R44 SET / GET scope 3 of LIGHTS");
  r = E.parse[E.CMD.SET](await rq(E.req.set(3, 57, 99)));
  ok(r.value === s57.max, "R44 SET scope 3 clamps (KEY TOUCH)");
  {   /* GLO > TOUCH 3 > LEVER: OFF, SPRNG, HOLD, LATCH (the default) as the settings card shows them */
    const s65 = E.parse[E.CMD.DESC](await rq(E.req.desc(3, 65))), g65 = E.parse[E.CMD.GET](await rq(E.req.get(3, 65)));
    ok(s65.label === "LEVER" && s65.max === 3 && s65.names.join() === "OFF,SPRNG,HOLD,LATCH" && g65.value === 3 &&
      E.fmtValue(s65, 2)[0] === "HOLD", `touch: DESC scope 3 LEVER: ${s65.names.join(" ")}, LATCH by default`);
    r = E.parse[E.CMD.SET](await rq(E.req.set(3, 65, 2)));
    const h65 = E.parse[E.CMD.GET](await rq(E.req.get(3, 65)));
    await rq(E.req.set(3, 65, 3));
    ok(r.value === 2 && h65.value === 2, "touch: SET / GET scope 3 LEVER HOLD");
  }
  pushes.length = 0;
  await put("#x 96 5");
  await put("#tick 5");
  ok(pushes.some((f) => f.cmd === E.CMD.CHANGED && E.parse[E.CMD.CHANGED](f.a).scope === 3 && E.parse[E.CMD.CHANGED](f.a).id === 96),
    "R44 CHANGED scope 3 pushed for a setting changed on the FM-1");
  let none = false;
  try { await rq(E.req.desc(3, 60), { timeout: 300, retries: 0 }); } catch (e) { none = /timeout/.test(e.message); }
  ok(none, "R44 scope 3 id 60 (TOUCH CAL, the FM-1's) gets no reply");

  /* Stallion: a slider dragged through 100 values in 1 s, sent as userSet does (LivePacer, liveOpt): the values the
     FM-1 took (its answers) while it moved, at most ~30 ms apart, the last one exactly. A gap is the pacer's wait
     (<= 16 ms) and the bridge's round trip: 17..18 ms on an idle machine, the limit 34. On a loaded one the process is
     not scheduled for a while and the gaps reached 36..42 ms with nothing wrong. So a probe timer runs beside the drag
     and records when this process was not running (each 2 ms sleep's lateness); each gap is judged without the time
     the probe was held up inside it, against 34 ms plus the bridge's own round trip measured right after (GETs, 90th
     percentile, the other process's share of the load), and a drag over it is tried once more. A pacer that waits
     too long (a 150 ms timeout, a retry, a backlog, a longer interval) waits on timers, so the probe stays on time and
     the gap counts in full: it fails both tries */
  {
    const id = 3;                                      /* (SUS: 0..127) */
    const drag = async (k) => {
      const seen = [], pace = new E.LivePacer(16), held = [], rt = [];
      let want = 0, probing = true;
      const probe = (async () => {                     /* [from, to, ms late]: this process held up */
        while (probing) {
          const a = performance.now(); await new Promise((res) => setTimeout(res, 2)); const b = performance.now();
          if (b - a > 3) held.push([a, b, b - a - 2]);
        }
      })();
      await rq(E.req.pset(info, id, 0));
      const send = async (fin) => {
        const v = want, r = E.parse[E.CMD.SET](await rq(E.req.pset(info, id, v), E.liveOpt(k, fin)));
        seen.push({ t: performance.now(), v: r.value });
      };
      for (let i = 0; i < 100; i++) {
        want = 20 + i;                                  /* (20 .. 119) */
        pace.push(k, send, i === 99);
        await new Promise((res) => setTimeout(res, 10));
      }
      while (pace.pending(k)) await new Promise((res) => setTimeout(res, 5));
      probing = false;
      await probe;
      let g;
      for (let i = 0; i < 10; i++) { const a = performance.now(); g = E.parse[E.CMD.GET](await rq(E.req.pget(info, id))); rt.push(performance.now() - a); }
      rt.sort((a, b) => a - b);
      const trip = rt[Math.floor(rt.length * 0.9)], limit = 34 + trip;
      const lost = (a, b) => held.reduce((s, [x, y, l]) => s + Math.min(l, Math.max(0, Math.min(b, y) - Math.max(a, x))), 0);
      const gaps = seen.slice(1).map((x, i) => x.t - seen[i].t), net = seen.slice(1).map((x, i) => x.t - seen[i].t - lost(seen[i].t, x.t));
      const mid = new Set(seen.map((x) => x.v).filter((v) => v > 20 && v < 119)), worst = Math.max(...net);
      return { pass: mid.size >= 30 && worst <= limit && seen[seen.length - 1].v === 119 && g.value === 119,
               txt: `${seen.length} SETs, ${mid.size} between, gaps <= ${worst.toFixed(0)} ms held up time aside (raw ${Math.max(...gaps).toFixed(0)}, limit ${limit.toFixed(0)}), last ${g.value}` };
    };
    let dr = await drag("0:" + id);
    const first = dr.pass ? "" : ` (tried again; first: ${dr.txt})`;
    if (!dr.pass) dr = await drag("0:" + id + "b");
    ok(dr.pass, `drag 100 values in 1 s: ${dr.txt}${first}`);
  }

  /* Stallion: the TUNING card (TUNE_GET / TUNE_PUT as tuSend sends them) moves the pitch the FM-1 plays: ANALOG's first
     preset (a plain saw), E4 measured on its audio (editor_bridge #hz) */
  {
    const hz = async (key) => { await put(`#note ${key} 100`); await put("#hz 3000"); await put(`#note ${key} 0`); await put("#tick 60"); return hzs.pop(); };
    const cents = (a, b) => 1200 * Math.log2(a / b);
    await rq(E.req.preset(0, 0));
    await put("#tick 30");
    const tu = E.parse[E.CMD.TUNE_GET](await rq(E.req.tuneGet()));
    ok(tu.rc === 0 && tu.sys === 0 && tu.a4 === 440, `TUNING: TUNE_GET (system ${tu.sys}, A4 ${tu.a4})`);
    const e4 = await hz(64), a4 = await hz(69);
    ok(Math.abs(a4 - 440) < 0.5 && Math.abs(cents(e4, 329.628)) < 1, `TUNING: EQUAL plays E4 ${e4.toFixed(2)} Hz, A4 ${a4.toFixed(2)} Hz`);
    let r = E.parse[E.CMD.TUNE_PUT](await rq(E.req.tunePut(1, 0, tu.deg, tu.period, tu.a4)));
    const just = await hz(64);
    ok(r.rc === 0 && Math.abs(cents(just, e4) + 13.7) < 1.5, `TUNING: JUST on C: E4 ${cents(just, e4).toFixed(1)} cents (a pure third: -13.7)`);
    r = E.parse[E.CMD.TUNE_PUT](await rq(E.req.tunePut(1, 4, tu.deg, tu.period, tu.a4)));
    const onE = await hz(64);
    ok(r.rc === 0 && Math.abs(cents(onE, e4)) < 1, `TUNING: JUST on E: E4 the root again (${cents(onE, e4).toFixed(1)} cents)`);
    r = E.parse[E.CMD.TUNE_PUT](await rq(E.req.tunePut(0, 0, tu.deg, tu.period, 432)));
    const a432 = await hz(69);
    ok(r.rc === 0 && Math.abs(a432 - 432) < 0.5, `TUNING: A4 432 Hz: A4 plays ${a432.toFixed(2)} Hz`);
    const sc = E.parseScl("! test.scl\nmajor scale on the keys\n7\n!\n200.0\n400.0\n500.0\n700.0\n900.0\n1100.0\n2/1\n");
    r = E.parse[E.CMD.TUNE_PUT](await rq(E.req.tunePut(13, 0, sc.deg, sc.period, 440)));
    const k1 = await hz(61), k7 = await hz(67);
    ok(r.rc === 0 && Math.abs(cents(k1, 293.665)) < 1.5 && Math.abs(cents(k7, 523.251)) < 1.5,
      `TUNING: a .scl (7 notes): C#4 plays D4 (${k1.toFixed(2)} Hz), G4 plays C5 (${k7.toFixed(2)} Hz)`);
    const back = E.parse[E.CMD.TUNE_GET](await rq(E.req.tuneGet()));
    ok(back.sys === 13 && back.deg.length === 7 && back.period === sc.period, "TUNING: TUNE_GET gives the USER scale back");
    pushes.length = 0;
    await rq(E.req.tunePut(2, 7, tu.deg, tu.period, 442));
    await put("#tick 10");
    ok(!pushes.some((f) => f.cmd === E.CMD.CHANGED && E.parse[E.CMD.CHANGED](f.a).scope === 3), "TUNING: the editor's own TUNE_PUT not pushed back (scope 3)");
    await put("#x 47 0");
    await put("#tick 10");
    ok(pushes.some((f) => f.cmd === E.CMD.CHANGED && E.parse[E.CMD.CHANGED](f.a).scope === 3 && E.parse[E.CMD.CHANGED](f.a).id === 47),
      "TUNING: GLO > TUNING turned on the FM-1: CHANGED scope 3 (the card follows)");
    await rq(E.req.tunePut(0, 0, tu.deg, tu.period, 440));
  }

  /* Stallion: the installer page's counts (make_site.py firmware_counts) are the FM-1's own (the engines with a name:
     a retired ID, 1.0's 3, is none) */
  try {
    const c = JSON.parse(execFileSync(process.env.PYTHON || "python3", ["-c",
      "import sys, json; sys.path.insert(0, sys.argv[1]); import make_site; print(json.dumps(make_site.firmware_counts()))", HERE]).toString());
    let sounds = 0;
    for (let e = 0; e < info.nengines; e++) sounds += E.parse[E.CMD.NAMES](await rq(E.req.names(e))).names.length;
    ok(c.ENGINES === info.engines.filter(Boolean).length && c.SOUNDS === sounds && c.FILTERS >= 8 && c.TUNINGS === 13,
      `the installer's counts are the FM-1's: ${c.ENGINES} engines, ${c.SOUNDS} sounds, ${c.FILTERS} filters, ${c.TUNINGS} tunings`);
  } catch (e) { ok(false, "the installer's counts: " + e.message); }

  /* R45: every id and page has a place */
  await put("#cover");
  ok(!!cover && cover.pall === 184 && cover.params.length === 184, "R45 the firmware's cover list");
  const L = E.LAYOUT(cover.pe0), ids = new Set(), titles = new Set();
  for (const g of L) for (const [t, s, x] of g.pages) { titles.add(t.replace(/ \d+$/, (m) => (/^MATRIX/.test(t) ? "" : m))); if (!s) x.forEach((i) => ids.add(i)); }
  const noId = cover.params.filter((p) => !ids.has(p.id) && !(p.id in E.P_ELSEWHERE));
  ok(!noId.length, `R45 every parameter id has a place (${noId.map((p) => p.id + " " + p.label).join(", ") || "all"})`);
  ok(Object.keys(E.P_ELSEWHERE).every((k) => html.includes(`id="${E.P_ELSEWHERE[k]}"`)), "R45 P_ELSEWHERE's elements exist");
  const pages = cover.pages.filter((p) => ["ENV", "LFO", "FX", "SEL", "EDIT", "GLO", "ARP"].includes(p.layer));
  const noPage = pages.filter((p) => !titles.has(p.title) && !(p.title in E.PAGE_PLACE));
  ok(pages.length > 30 && !noPage.length, `R45 every page of ENV LFO FX SEL EDIT GLO ARP has a place (${noPage.map((p) => p.title).join(", ") || pages.length + " pages"})`);
  const badPlace = Object.entries(E.PAGE_PLACE).filter(([, el]) => !html.includes(`id="${el}"`));
  ok(!badPlace.length, `R45 PAGE_PLACE's elements exist (${badPlace.map((x) => x[0]).join(", ") || "all"})`);
  const missPage = pages.filter((p) => p.scope === "track" || p.scope === "engine").filter((p) => p.ids.some((i) => i < 184 && !ids.has(i) && !(i in E.P_ELSEWHERE)));
  ok(!missPage.length, `R45 the track pages' ids all shown (${missPage.map((p) => p.title).join(", ") || "all"})`);
  const sxIds = new Set(E.SX_PAGES.flatMap((p) => p[1]));
  const noSx = (cover.settings || []).filter((i) => !sxIds.has(i) && !(i >= 47 && i <= 49));
  ok(cover.settings && cover.settings.length > 20 && !noSx.length, `R45 every scope 3 setting in the settings card (TUNING: its own card) ${noSx.join(" ")}`);
  const sxPages = pages.filter((p) => p.scope === "syn" && p.title in E.PAGE_PLACE && E.PAGE_PLACE[p.title] === "sxcard");
  ok(sxPages.every((p) => p.ids.every((i) => sxIds.has(i) || i === 60)), "R45 the GLO > MIDI / TOUCH pages' values all in the card");
} catch (e) {
  console.log("error:", e && e.stack || e);
  failed++;
}

/* R40: the offline editor */
try {
  const off = execFileSync(process.env.PYTHON || "python3", ["-c",
    "import sys; sys.path.insert(0, sys.argv[1]); import make_site; sys.stdout.write(make_site.offline_editor(open(sys.argv[2], encoding='utf-8').read()))",
    HERE, join(HERE, "editor.html")], { maxBuffer: 1 << 26 }).toString("utf8");
  ok(cut("/*PROTO-BEGIN*/", "/*PROTO-END*/", off) === proto && cut("/*LAYOUT-BEGIN*/", "/*LAYOUT-END*/", off) === layout,
    "R40 the offline editor: the same protocol code and layout, byte for byte");
  ok(off.includes('id="sxcard"') && off.includes("function sxRead") && off.includes("function dynLater"), "R40 the offline editor: the settings card and v15 code");
} catch (e) { console.log("error:", e.message); failed++; }

br.stdin.end();
link.close();
console.log(failed ? `${failed} FAILED` : "all ok");
process.exit(failed ? 1 : 0);
