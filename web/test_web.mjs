// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
//
// Node checks of the web pages' JS (no browser, no hardware). Run from the repo root:
//   node web/test_web.mjs
// - editor.html: the protocol section (between PROTO-BEGIN/END) against its mock device (v1 commands,
//   the user preset bank / librarian, library files, live pushes, older-firmware fallback, the v3 tracks
//   and the mixer), its tab layout and ja/en strings,
//   and the user-sample pipeline byte for byte against tools/sampleio.py
// - Stallion (editor UX): a slider dragged through 100 values in 1 s goes to the mock while it moves (LivePacer: at
//   most ~30 ms apart, the last value exactly, never a backlog), no card waits for its slider to stop, the version
//   shown without "FELUCCA ", the envelope's grab points (ENVG) map back onto ATK DEC SUS REL, each language of the
//   editor and the installer only its own words, the installer's counts filled from the firmware
// - fm1pkg.js: productOf and logicalImage on build/felucca.fwsc (skipped without a build)
// - fm1ota.js: a full install and an unplug during the write against a simulated FM-1
// - smkota.js: Chrome's MIDI let go while the keyboard restarts (MidiSession, sloop-pro #3)
// - the demo page (?mock=1): the editor's own connect sequence reads the Stallion mock to "ready", timers slowed as a
//   browser slows a page not in front
// - the patch bay: every matrix code once, drag and drop into the slots, refusals, CLEAR ALL
// - the patch bay on every engine with a matrix: MARE's extras only there, 6 or 12 slots, only the inputs the
//   engine uses (the device's INFO masks, checked against eng_*.c), the modules closed up round them, no warnings
// - the connected-device banner: FM-1 / SMK-37 Pro from the device's identity, demo, not connected, a change said
// - the Sound tab's header: SOUND list, NAME, MODE; multi sounds (links by name, B's settings, the 47-byte
//   record, the device's MULTI_* with a missing link and without them, the library, files, offline)

import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, readdirSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";
import { blockCount, logicalImage, productOf, validateStockPackage } from "./fm1pkg.js";
import { Updater, pack7, unpack7 } from "./fm1ota.js";

let failed = 0;
const ok = (cond, what) => { console.log(`${what.padEnd(64)} ${cond ? "ok" : "FAIL"}`); if (!cond) failed++; };
const eq = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const PYTHON = process.env.PYTHON || (process.platform === "win32" ? "python" : "python3");
const py = (code, ...args) => execFileSync(PYTHON, ["-c", code, ...args], { maxBuffer: 1 << 26 });
const HERE = fileURLToPath(new URL(".", import.meta.url));

/* ------------------------------------------------------------ editor protocol --- */
const html = readFileSync(join(HERE, "editor.html"), "utf8");
const proto = html.slice(html.indexOf("/*PROTO-BEGIN*/"), html.indexOf("/*PROTO-END*/"));
const E = vm.runInNewContext(proto + `
;({ frame, unframe, parse, req, Link, parseWav, resample, normalize, rootFromName, buildSlot, LOOP, makeMockDevice, CMD, SMP,
   CHOP, chopNovelty, chopHits, chopSnap, chopGrid, chopEqual, chopList, chopPick, chopFit, chopZones, wavFile, zipStore, crc32,
   UP, bank, capturePatch, auditionPatch, startWatch, libraryFile, readLibraryFile, paramKeys, patternFromSteps, stepsFromPattern, upName,
   mixer, GM_DRUM, drumName, parseNotes, fmtValue, F, DRUM_LANES, LV_NAMES, emptyDrum, lockable, MICRO, FC, fillGet, fillSet, pack7, unpack7,
   backupCapture, backupRestore, backupObjects, BK, TOUCH, b64enc, b64dec, FM6, KIT, kitPad, kitZones, kitLen, kitBytes, kitFit, kitSplit, kitLaneOf, kitPlace, zipRead, ROLL, SMF, ARR, mockSettings, fm6CartWrite, fm6CartPreset, fm6CartSlots, fm6CartPresets, DSYN, DSYN_MOCK_NAMES, DSYN_MOCK_KITS, HY, FX, WT, SetGate, patchByCount, bkProjFormat, bkDevFormat, bkFwAtLeast, FM1_PORT_ED: FM1_PORT, fm1Ports, parseScl, PAT, LivePacer, liveOpt, fwShown, ENVG, ENVT, LFOG })`,
{ setTimeout, clearTimeout, setInterval, clearInterval, console, TextEncoder, TextDecoder, Blob, Response, DecompressionStream });

async function editorMock() {
  const m = E.makeMockDevice();
  const inp = [...m.access.inputs.values()][0], out = [...m.access.outputs.values()][0];
  const link = new E.Link((d) => out.send(d), { timeout: 300 });
  inp.onmidimessage = (e) => link.receive(e.data);
  const rq = async (r, o) => link.request(r, o);
  const info = E.parse[E.CMD.INFO](await rq(E.req.info()));
  ok(info.nengines === 12 && info.engines[9] === "FM6" && info.engines[10] === "PHYS" && info.engines[11] === "NOISE" && info.engines[5] === "VOICE" && info.engines[6] === "TRIO" && info.engines[7] === "WHEEL" && info.engines[8] === "GRAIN" && info.pcount === 61 && info.pe0 === 53 && info.engines[4] === "SAMPLE",
    "editor: INFO");
  let descs = 0;
  for (let i = 0; i < info.pcount; i++) if (E.parse[E.CMD.DESC](await rq(E.req.desc(0, i))).label) descs++;
  ok(descs === info.pcount, "editor: DESC for every parameter");
  {
    /* the SLICER (core.h P_SLCR..P_SLDEPTH = 45..48, just before P_E0): the mock as params.c has it,
       and a factory preset turns it off as ui.c apply_preset_to does */
    const pc = readFileSync(join(HERE, "../firmware/src/params.c"), "utf8");
    const sd = [];
    for (let i = 45; i < 49; i++) sd.push(E.parse[E.CMD.DESC](await rq(E.req.desc(0, i))));
    ok(sd.map((d) => d.label).join() === "SLCR,PAT,RATE,DEPTH" && sd[0].names.join() === "OFF,GATE,STUT"
      && sd[2].names.join() === "1/8,1/16,1/32,8T,16T,32T" && sd[2].def === 1 && sd[1].min === 1 && sd[1].max === 16 && sd[3].def === 127
      && /\[P_SLCR\] = PE\("SLCR", N_SLCR, 0\)/.test(pc) && /\[P_SLPAT\] = PD\("PAT", F_INT, 1, 16, 1\)/.test(pc)
      && /\[P_SLRATE\] = PE\("RATE", N_SLDIV, 1\)/.test(pc) && /\[P_SLDEPTH\] = PD\("DEPTH", F_PCT, 0, 127, 127\)/.test(pc)
      && /N_SLDIV\[\] = \{"1\/8", "1\/16", "1\/32", "8T", "16T", "32T"\}/.test(pc),
      "editor: SLICER parameters 45..48 (mock == params.c)");
    await rq(E.req.set(0, 45, 2));
    await rq(E.req.set(0, 46, 7));
    const on = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
    await rq(E.req.preset(0, 1));
    const off = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
    ok(on.p[45] === 2 && on.p[46] === 7 && off.p[45] === 0 && off.p[46] === 1, "editor: a factory preset turns the SLICER off");
  }
  const scale = E.parse[E.CMD.DESC](await rq(E.req.desc(0, 26)));
  const scaleNames = ["CHR", "MAJ", "MIN", "DOR", "MIX", "PEN", "MPEN", "HARM", "PHRY", "LYD", "LOC", "MEL", "BLUES", "WHOLE", "DIMHW", "DIMWH"];
  ok(scale.label === "SCL" && scale.max === 15 && eq(scale.names, scaleNames), "editor: all 16 scale names exposed");
  const scaleSet = E.parse[E.CMD.SET](await rq(E.req.set(0, scale.id, 15)));
  ok(scaleSet.value === 15, "editor: new scale selection is not clamped to the old range");
  const dump = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
  ok(dump.p.length === info.pcount && dump.g.length === info.gcount, "editor: DUMP");
  const set = E.parse[E.CMD.SET](await rq(E.req.set(0, 3, 500)));
  ok(set.value === 127, "editor: SET clamps to the range");
  const st = E.parse[E.CMD.STEP_SET](await rq(E.req.stepSet(5, { n: 2, notes: [60, 64], time: 0, flags: 1, vel: 100 })));
  ok(st.n === 2 && st.notes[1] === 64 && st.vel === 100, "editor: STEP_SET");
  const pj = E.parse[E.CMD.PROJECT](await rq(E.req.project(1, 2), { timeout: 4000, retries: 0 }));
  ok(pj.used === 1, "editor: PROJECT save");
  /* sample upload as smpUpload() does it */
  const s = Int16Array.from({ length: 3000 }, (_, i) => Math.round(8000 * Math.sin(i / 7)));
  const { hdr, data } = E.buildSlot("test", [{ s, root: 60 }]);
  let rc = E.parse[E.CMD.SMP_BEGIN](await rq(E.req.smpBegin(1), { timeout: 1000, retries: 0 })).rc;
  for (let off = 0; off < data.length && !rc; off += 256) {
    rc = E.parse[E.CMD.SMP_WRITE](await rq(E.req.smpWrite(1, E.SMP.DATA_OFF + off, data.subarray(off, off + 256)), { timeout: 1000 })).rc;
  }
  rc = rc || E.parse[E.CMD.SMP_END](await rq(E.req.smpEnd(1, hdr), { timeout: 2000, retries: 0 })).rc;
  const si = E.parse[E.CMD.SMP_INFO](await rq(E.req.smpInfo()));
  ok(rc === 0 && si.slots[1].zones === 1 && si.slots[1].name === "TEST", "editor: sample upload (CRC checked by the mock)");
  /* a device that never answers */
  const dead = new E.Link(() => {}, { timeout: 30 });
  const err = await dead.request(E.req.info(), { retries: 1 }).then(() => null, (e) => e.message);
  ok(/^timeout/.test(err || ""), "editor: no reply -> timeout after the retries");
  link.close();
  m.stop();
}

/* ------------- editor mock: the value names it shares with the FM-1 == params.c (TP, GP; the Stallion build's) --- */
/* Each enum of the mock's sound (ids 0..52, before Clipper's own) and globals that has the same label on the device must
   name the same values as params.c (the first definition: the CLIPPER_SYNTH one where the two builds differ). The mock
   is the editor's demo and its tests: ALLOC without POLY2 or a TIME that stops at 1/16D shows values the FM-1 does not */
async function editorMockNames() {
  const ch = readFileSync(join(HERE, "../firmware/src/core.h"), "utf8").replace(/\/\*[\s\S]*?\*\//g, "");
  const pc = readFileSync(join(HERE, "../firmware/src/params.c"), "utf8").replace(/#if FELUCCA_SLICE\n[\s\S]*?#endif\n/g, "");   /* (core.h: 0) */
  const ids = (first, last) => { const i = ch.indexOf(first), j = ch.indexOf(last, i); return ch.slice(i, j).split(",").map((x) => x.trim()).filter(Boolean); };
  const pIds = ids("P_LEVEL,", "P_L2RATE"), gIds = ids("G_BPM,", "G_COUNT");
  const arr = (n) => { const m = new RegExp(`\\b${n}\\[\\] = \\{([\\s\\S]*?)\\};`).exec(pc); return m ? [...m[1].replace(/\/\*[\s\S]*?\*\//g, "").matchAll(/"([^"]*)"/g)].map((x) => x[1]) : null; };
  const pe = (id) => { const m = new RegExp(`\\[${id}\\] = PE\\("([^"]*)", (N_\\w+)`).exec(pc); return m ? { label: m[1], names: arr(m[2]), arr: m[2] } : null; };
  const { rq, done } = attachMock({ clipper: true });
  const C = E.CMD, bad = [];
  let n = 0;
  for (const [scope, list] of [[0, pIds], [1, gIds]]) {
    for (let i = 0; i < list.length; i++) {
      const f = pe(list[i]);
      if (!f || !f.names) continue;
      const d = E.parse[C.DESC](await rq(E.req.desc(scope, i)));
      if (!d || d.label !== f.label) continue;   /* (a slot the Stallion build uses for something else: test_editor_p15 compares every id) */
      n++;
      if (d.names.join() !== f.names.join()) bad.push(`${list[i]} ${f.arr}: mock ${d.names.join(" ")}`);
    }
  }
  done();
  ok(n >= 20 && !bad.length, `editor mock: ${n} shared enums name the values as params.c` + (bad.length ? " (" + bad.join("; ") + ")" : ""));
  /* the two the audit found, by name: POLY2 and the free delay times */
  ok(!!pe("P_ALLOC") && pe("P_ALLOC").names.join() === "ROT,REUSE,POLY2" && pe("G_DTIME").names.slice(8).join() ===
    "35MS,50MS,75MS,100MS,150MS,200MS,300MS,400MS,500MS,600MS,700MS,780MS", "editor mock: params.c ALLOC has POLY2, TIME the free 35..780 ms");
}

/* ------------- the demo page (?mock=1): the editor's own connect sequence reads the mock to the end --- */
/* The page's script runs in a sandbox with a DOM that takes anything; every timer at least 1 s and on a whole second
   (as a browser slows a page that is not in front: an offscreen one making the website's screenshots stalled at
   "Reading 17/94" when the mock answered by a timer). The demo answers at once: INFO, every descriptor of the
   Stallion layout, the names, steps, mixer and bank read, "ready" within the first seconds */
async function editorDemoPage() {
  const code = html.slice(html.indexOf('<script>\n"use strict";') + 8, html.indexOf("</script>", html.indexOf('<script>\n"use strict";')));
  const anchor = 'if (!MOCK && !navigator.requestMIDIAccess) sayK("nomidi");\n})();';
  const any = () => {
    const store = {};
    return new Proxy(function () {}, {
      get(t, k) {
        if (k === Symbol.iterator) return function* () {};
        if (k === Symbol.toPrimitive) return (h) => (h === "number" ? 0 : "");
        if (k === "then") return undefined;
        if (k in store) return store[k];
        if (["length", "width", "height", "offsetWidth", "clientWidth", "scrollTop"].includes(k)) return 0;
        if (["value", "textContent", "innerHTML", "className"].includes(k)) return "";
        return (store[k] = any());
      },
      set(t, k, v) { store[k] = v; return true; },
      apply() { return any(); },
      construct() { return any(); },
    });
  };
  const said = [], errs = [], ls = new Map();
  const slowTimer = (f, ms, ...a) => setTimeout(f, Math.ceil((Date.now() + Math.max(ms | 0, 1000)) / 1000) * 1000 - Date.now(), ...a);
  const timers = [];
  const sb = {
    console: { log() {}, debug() {}, info() {}, warn: (...a) => errs.push(a.join(" ")), error: (...a) => errs.push(a.join(" ")) },
    setTimeout: (...a) => { const h = slowTimer(...a); timers.push(h); return h; }, clearTimeout,
    setInterval: (f, ms) => { const h = setInterval(f, Math.max(ms, 1000)); timers.push(h); return h; }, clearInterval,
    queueMicrotask, TextEncoder, TextDecoder, URLSearchParams, URL, Blob, Response, DecompressionStream,
    location: { search: "?mock=1", hash: "", pathname: "/editor.html", href: "http://localhost/editor.html?mock=1", protocol: "file:" },
    navigator: { userAgent: "node", language: "en", languages: ["en"] }, history: { replaceState() {}, pushState() {} },
    localStorage: { getItem: (k) => (ls.has(k) ? ls.get(k) : null), setItem: (k, v) => ls.set(k, String(v)), removeItem: (k) => ls.delete(k) },
    document: any(), requestAnimationFrame: (f) => slowTimer(f, 16), cancelAnimationFrame: clearTimeout,
    matchMedia: () => ({ matches: false, addEventListener() {}, addListener() {} }),
    ResizeObserver: class { observe() {} disconnect() {} }, MutationObserver: class { observe() {} disconnect() {} },
    performance, fetch: () => Promise.reject(new Error("no fetch")), getComputedStyle: () => any(), addEventListener() {}, removeEventListener() {},
    __said: said,
  };
  sb.window = sb; sb.self = sb; sb.globalThis = sb;
  let d = null, err = "";
  try {
    if (!code.includes(anchor)) throw new Error("the page script's last lines moved (test anchor)");
    vm.runInNewContext(code.replace(anchor, anchor.replace("})();", `
sayK = ((o) => (...a) => { globalThis.__said.push(a.join("")); try { return o(...a); } catch (e) { /* (the sandbox's DOM) */ } })(sayK);
globalThis.__go = async () => { await connect(); return dev; };
globalThis.__hdr = { devbarPart, libSaveCurrent, libRows, libFilter, libRename, libSetTags, libDuplicate, libDelete, libNorm, renderLib, libExportFile,
  libImportMultis, libSel: () => libSel, LIB_MULTI, setLang: (l) => { lang = l; }, getLang: () => lang, bkStoreCurrent, libList: () => lib, brCats, brItems, brCatOf, sndValue, hdrShown, lySet, lyMode, modeSet, sndChoose, linkChoose, multiSaveDev, multiList, mlib, MULTI, soundFileMake, soundFileApply,
  soundFileCheck, hdr, MOCK_FW, CMD, t, TEXT, DB, dev: () => dev, mock: () => mock, busy: () => busy, live: () => live,
  offline: async (on) => { if (dev) disconnect(); access = null; OFFLINE = on; await connect(); return dev; }, offlineOn: () => OFFLINE };
})();`)), sb, { filename: "editor.html" });
    d = await Promise.race([sb.__go(), new Promise((r) => setTimeout(() => r(null), 20000))]);
  } catch (e) { err = String(e && e.message); }
  timers.forEach((h) => { clearTimeout(h); clearInterval(h); });
  const info = d && d.info, last = said[said.length - 1];
  ok(!!d && last === "ready" && info.proto === 15 && /STALLION/.test(info.version),
    `demo page: connects and reads to "ready" with slow timers (${last || err}, ${said.filter((x) => /^reading\d/.test(x)).length} reads)`);
  if (!d) return;
  const np = info.pall, pd = d.pdesc.slice(0, np), gd = d.gdesc.slice(0, info.gcount);
  ok(info.pcount === 127 && np > info.pcount && pd.every(Boolean) && gd.every(Boolean) && d.names.length === info.nengines && d.names.every((n, e) => !info.engines[e] || (n && n.length)),
    `demo page: the Stallion layout, every descriptor read (${np} sound, ${info.gcount} globals), every engine's presets`);
  const mx = [0, 1, 2, 3].filter((k) => d.dump.p[60 + 3 * k] > 0 && d.dump.p[61 + 3 * k] > 0 && d.dump.p[62 + 3 * k]).length;   /* (SRC, DST, AMT) */
  ok(info.engines[d.dump.engine] === "MARE" && mx === 4 && d.steps.some((x) => x && x.n) && !errs.length,
    `demo page: track 1 a MARE sound with ${mx} matrix cables and a pattern` + (errs.length ? ` (${errs.slice(0, 3).join("; ")})` : ""));
  await editorHeader(sb.__hdr, errs, said);
}

/* ------------- the Sound tab's header: SOUND, NAME, MODE; multi sounds (their design) ------- */
/* On the demo page as connected above (the editor's own code, the Stallion mock): one SOUND list (INIT, multi sounds,
   single sounds); the NAME is the sound's own (a link chosen never changes it); INIT then MODE; MODE OFF a single sound
   with every card, else a multi sound: only its links (single sounds, by name) and part B's settings; the 47-byte
   record; the device's MULTI_* (a missing link: nothing loaded; a firmware without them: the library and files); the
   library; files; offline */
async function editorHeader(H, errs, said) {
  if (!H) { ok(false, "header: the page's functions"); return; }
  const until = async (f, ms = 20000) => { const t0 = Date.now(); while (!f() && Date.now() - t0 < ms) await sleep(20); return f(); };
  const idle = () => until(() => !H.busy() && ![29, 30, 31, 32, 0, 39, 50].some((id) => H.live().pending("ly:" + id)));
  const T = () => H.mock().state.tracks, js = JSON.stringify, d = () => H.dev();
  const fv = (eng, p) => `f:${d().info.engines.indexOf(eng)}:${p}`;
  const nameOf = (eng, p) => d().names[d().info.engines.indexOf(eng)][p];

  /* the MODE's rule */
  const sh = [0, 1, 2, 3].map((m) => H.hdrShown(m));
  ok(sh[0].cards && !sh[0].links && !sh[0].ctl.length && [1, 2, 3].every((m) => !sh[m].cards && sh[m].links),
    "header: MODE OFF a single sound (every card); LAYER / SPLIT / MULTI a multi sound (its links, no card)");
  ok(js(sh[1].ctl) === js([30, 0, 39, 50]) && js(sh[2].ctl) === js([32, 30, 0, 39, 50]) && js(sh[3].ctl) === js([29, 30, 0, 39, 50]),
    "header: part B's settings: LAYER OCT LEVEL PAN FILT; SPLIT the split point too; MULTI B's MIDI channel too");
  ok(/\$\("p-sound"\)\.classList\.toggle\("multi", sh\.b\)/.test(html) && /#p-sound\.multi > :not\(#hdr\) \{ display: none !important; \}/.test(html) &&
     !/id="partbar"|id="lypanel"|Editing A \| B/.test(html) && /id="hdrsnd"[^>]*>/.test(html) &&
     html.indexOf('id="hdrsnd"') < html.indexOf('id="hdrname"') && html.indexOf('id="hdrname"') < html.indexOf('id="hdrmode"') &&
     html.indexOf('id="hdrmode"') < html.indexOf('id="hdrparts"'),
    "header: SOUND, then NAME, then MODE, then the links; a multi sound hides every card; no A | B editing");

  /* the record: 47 bytes of 0..127, back as it was */
  const m0 = { name: "PAD + BASS", mode: 2, a: { kind: 0, engine: 23, name: "HIGH STRINGS" }, b: { kind: 1, engine: 1, name: "MY BASS" },
    split: 52, oct: 1, level: 99, pan: -64, filt: 63, ch: 16 };
  const rec = H.MULTI.encode(m0), back = H.MULTI.decode(rec);
  ok(rec.length === 47 && rec.every((b) => Number.isInteger(b) && b >= 0 && b <= 127) && js(back) === js(m0) &&
     js(rec.slice(0, 12)) === js([..."PAD + BASS  "].map((c) => c.charCodeAt(0))) && rec[12] === 2 && rec[13] === 0 && rec[27] === 1 && rec[44] === 0 && rec[45] === 127,
    "header: a multi sound's record: 47 bytes (name 12, mode, two links of 14, B's six), decoded back the same");

  /* a single sound from the list: part A, MODE OFF, its name */
  ok(await H.sndChoose(fv("PALOMINO", 3)) && T()[0].engine === d().info.engines.indexOf("PALOMINO") && T()[0].preset === 3 &&
     H.hdr.name === nameOf("PALOMINO", 3) && !H.hdr.multi && T()[1].p[31] === 0,
    `header: SOUND list: a single sound onto part A, MODE OFF, its name (${H.hdr.name})`);
  ok(await H.sndChoose("init") && H.hdr.name === "INIT" && T()[0].preset === 0 && H.lyMode() === 0,
    "header: SOUND list: INIT, a new sound named INIT (its MODE next)");

  /* the browsers: two columns, the left one filters the right one */
  const cats = H.brCats(true), cl = H.brCats(false), eng = d().info.engines;
  ok(cats[0][0] === "multi" && cats[0][1] === "MULTI" && cats[1][0] === "user" && cats[1][1] === "USER" &&
     js(cats.slice(2).map((c) => c[1])) === js(eng.filter((_, e) => (d().names[e] || []).length)) && js(cats.slice(2).map((c) => c[0])) === js(eng.map((_, e) => `e:${e}`).filter((_, e) => (d().names[e] || []).length)),
    `header: browser's left column: MULTI, USER, then ${cats.length - 2} engines in the device's order`);
  const pal = eng.indexOf("PALOMINO"), ip = H.brItems(`e:${pal}`, true);
  ok(ip.length === d().names[pal].length && ip[0][1] === "INIT" && ip.every(([v], p) => v === `f:${pal}:${p}`),
    `header: ... an engine: its sounds only, INIT first (PALOMINO: ${ip.length})`);
  const iu = H.brItems("user", true);
  ok(iu.length === d().bank.slots.filter((x) => x && x.used).length && iu.length > 0 && iu.every(([v]) => /^u:\d+$/.test(v)), `header: ... USER: the user presets (${iu.length})`);
  ok(H.brItems("multi", true)[0][0] === "newmulti" && H.brItems("multi", true)[0][1] === "NEW MULTI" && !H.brItems("multi", false).length &&
     cl[0][0] === "user" && !cl.some((c) => c[0] === "multi"),
    "header: ... MULTI: NEW MULTI first; the A and B pickers: no MULTI (single sounds only)");
  ok(/id="hdrsndcat"/.test(html) && /id="hdrlinkacat"/.test(html) && /id="hdrlinkbcat"/.test(html) && /brRender\("hdrsnd", true,/.test(html) &&
     /brRender\("hdrlinka", false,/.test(html) && /brRender\("hdrlinkb", false,/.test(html) && /e\.key === "ArrowRight"/.test(html) && /e\.key === "ArrowLeft"/.test(html) &&
     !/function sndGroups/.test(html),
    "header: three two-column browsers (SOUND, A, B), left / right between the columns; no merged list");
  ok(H.brCatOf(H.sndValue()) === `e:${T()[0].engine}`, "header: ... the left column follows the sound shown (an engine's)");
  const u0 = d().bank.slots.findIndex((x) => x && x.used);
  ok(await H.sndChoose(`u:${u0}`) && H.hdr.name === d().bank.slots[u0].name && H.brCatOf(H.sndValue()) === "user",
    `header: ... a user preset: USER on the left, its name (${H.hdr.name})`);
  ok(await H.sndChoose("newmulti") && H.hdr.name === "NEW MULTI" && H.hdr.multi && H.hdr.multi.mode === 1 && T()[1].p[31] === 1 &&
     H.brCatOf(H.sndValue()) === "multi",
    "header: NEW MULTI: a multi sound, MODE LAYER, named NEW MULTI, MULTI on the left");
  await H.sndChoose("init");

  /* INIT -> MODE: a multi sound; the name is the sound's own, a link chosen never changes it */
  H.hdr.name = "MY SPLIT";
  await H.modeSet(2);
  await idle();
  ok(T()[1].p[31] === 2 && H.hdr.multi && H.hdr.multi.mode === 2 && H.hdr.name === "MY SPLIT", "header: MODE SPLIT: a multi sound (track 2's MODE), the name kept");
  await H.linkChoose(0, fv("PALOMINO", 3));
  await H.linkChoose(1, fv("DIGITAL", 1));
  await idle();
  ok(T()[0].engine === d().info.engines.indexOf("PALOMINO") && T()[0].preset === 3 && T()[1].engine === d().info.engines.indexOf("DIGITAL") &&
     T()[1].preset === 1 && H.hdr.name === "MY SPLIT" && H.hdr.multi.a.name === nameOf("PALOMINO", 3) && H.hdr.multi.b.name === nameOf("DIGITAL", 1),
    `header: links chosen: A ${H.hdr.multi.a.name} on track 1, B ${H.hdr.multi.b.name} on track 2 (PRESET part 1); the name still MY SPLIT`);
  H.lySet(32, 50); H.lySet(0, 90);
  await idle();
  ok(T()[1].p[32] === 50 && T()[1].p[0] === 90 && H.hdr.multi.split === 50 && H.hdr.multi.level === 90, "header: part B's settings: track 2's, and the multi sound's");

  /* the device's multi slots (MULTI_*) */
  ok(Array.isArray(d().multi) && d().multi.length === 16, "header: the device's 16 multi slots (MULTI_LIST)");
  ok(await H.multiSaveDev(3) && H.brItems("multi", true).some(([v]) => v === "dm:3") && d().multi[3].used && d().multi[3].name === "MY SPLIT" &&
     js(H.MULTI.decode(H.mock().state.multis[3])) === js({ ...H.hdr.multi, name: "MY SPLIT" }),
    "header: Save on the device: slot M04 (MULTI_PUT), the record as the multi sound");
  await H.sndChoose("init");
  await idle();
  ok(await H.sndChoose("dm:3") && T()[0].preset === 3 && T()[1].engine === d().info.engines.indexOf("DIGITAL") && T()[1].p[32] === 50 &&
     T()[1].p[31] === 2 && H.hdr.name === "MY SPLIT" && H.hdr.multi && H.hdr.multi.mode === 2,
    "header: the device's multi sound from the list (MULTI_LOAD): both parts, B's settings, its name");
  /* the banner's line: the sound the header shows (its NAME; a multi sound's MODE and links; a single's engine), at once */
  const ln = (e, p) => `${e} ${nameOf(e, p)}`;
  ok(H.devbarPart() === `MY SPLIT  ·  SPLIT  ·  A: ${ln("PALOMINO", 3)}  ·  B: ${ln("DIGITAL", 1)}`,
    `header: the banner after a multi sound loads: "${H.devbarPart()}"`);
  await H.linkChoose(1, fv("DIGITAL", 2));
  ok(H.devbarPart() === `MY SPLIT  ·  SPLIT  ·  A: ${ln("PALOMINO", 3)}  ·  B: ${ln("DIGITAL", 2)}`, `header: ... after a link changes: "${H.devbarPart()}"`);
  H.hdr.name = "JG SPLIT SUR";
  ok(H.devbarPart().startsWith("JG SPLIT SUR  ·  SPLIT") && /\$\("hdrname"\)\.addEventListener\("input", \(\) => \{ hdr\.name = hdrClean\(\$\("hdrname"\)\.value\); devbarRender\(\); \}\)/.test(html) &&
     /ly\.sig = ""; \}\n  devbarRender\(\);/.test(html),
    "header: ... after a NAME edit (the banner drawn with the header, and as the NAME is typed)");
  await H.sndChoose(fv("PONY", 2));
  ok(H.devbarPart().startsWith(`${nameOf("PONY", 2)}  ·  PONY`) && !/Track|track/.test(H.devbarPart()),
    `header: ... after a single sound loads: "${H.devbarPart()}" (no track on the synth)`);
  await H.sndChoose("dm:3");

  /* a link missing: said, nothing loaded */
  H.mock().state.multis[4] = H.MULTI.encode({ ...m0, name: "BROKEN", a: { kind: 0, engine: 1, name: nameOf("DIGITAL", 2) }, b: { kind: 0, engine: 1, name: "NO SUCH" } });
  await H.multiList();
  const t0 = js([T()[0].engine, T()[0].preset, T()[1].engine, T()[1].preset]);
  said.length = 0;
  ok(!(await H.sndChoose("dm:4")) && js([T()[0].engine, T()[0].preset, T()[1].engine, T()[1].preset]) === t0 &&
     said.some((x) => /NO SUCH/.test(x)) && H.hdr.name === "MY SPLIT",
    `header: a multi sound with a missing link: nothing loaded, the missing sound named (${said.find((x) => /NO SUCH/.test(x))})`);
  /* the library (this browser) */
  H.mlib.put({ ...H.hdr.multi, name: "LIB SPLIT" });
  H.mlib.put({ ...m0, name: "LIB BROKEN", b: { kind: 1, engine: 1, name: "NOBODY" } });
  const lib = H.mlib.read(), li = lib.findIndex((m) => m.name === "LIB SPLIT"), lb = lib.findIndex((m) => m.name === "LIB BROKEN");
  await H.sndChoose("init");
  ok(H.brItems("multi", true).some(([v, n]) => v === `lm:${li}` && /LIB SPLIT/.test(n)) && await H.sndChoose(`lm:${li}`) && H.hdr.name === "LIB SPLIT" && T()[1].p[31] === 2 && T()[0].preset === 3, "header: the library's multi sound from the list");
  const t1 = js([T()[0].engine, T()[0].preset, T()[1].engine, T()[1].preset]);
  ok(!(await H.sndChoose(`lm:${lb}`)) && js([T()[0].engine, T()[0].preset, T()[1].engine, T()[1].preset]) === t1,
    "header: ... one with a missing user preset: nothing loaded");
  /* a firmware without MULTI_*: no device slots, the library and files */
  H.mock().opt.noMulti = true;
  ok((await H.multiList()) === null && d().multi === null, "header: a firmware without MULTI_* (no reply): no device slots");
  H.mock().opt.noMulti = false;
  await H.multiList();

  /* saving the current sound (the Library tab, the device's store): a multi sound as a multi sound under its NAME (never
     part A's sound or name); a single sound under the NAME */
  await H.sndChoose("dm:3");
  const nLib = H.libList().length, aName = H.hdr.multi.a.name;
  const r1 = await H.libSaveCurrent();
  const lm = H.mlib.read().find((m) => m.name === "MY SPLIT");
  ok(r1 && r1.multi && lm && lm.mode === 2 && lm.a.name === aName && lm.split === 50 && H.libList().length === nLib && !H.libList().some((p) => p.name === aName),
    "header: Library > Save current sound with a multi sound: a multi entry named MY SPLIT (links, MODE, B's settings), no patch of part A");
  await editorLibraryMultis(H, r1, nameOf);
  const bk0 = js(H.mock().state.bank);
  const r2 = await H.bkStoreCurrent(5);
  ok(r2 && Number.isInteger(r2.multiSlot) && js(H.mock().state.bank) === bk0 && H.MULTI.decode(H.mock().state.multis[r2.multiSlot]).name === "MY SPLIT",
    `header: ... the device's store with a multi sound: its multi slot M${r2 && r2.multiSlot + 1} (MY SPLIT), no user preset of part A`);
  await H.sndChoose(fv("DIGITAL", 2));
  H.hdr.name = "RENAMED";
  const r3 = await H.libSaveCurrent();
  ok(r3 && r3.patch && r3.patch.name === "RENAMED" && H.libList().some((p) => p.name === "RENAMED"), "header: a renamed single sound saved under its new name (the library)");
  const r4 = await H.bkStoreCurrent(5);
  ok(r4 && r4.slot === 5 && H.mock().state.bank[5] && H.mock().state.bank[5].name === "RENAMED" && H.hdr.name === "RENAMED",
    "header: ... and on the device (user preset slot U06 named RENAMED)");
  ok(/name: obj\.name \|\| obj\.presetName/.test(html) && !/def = upName\(\(dev\.names\[dev\.dump\.engine\]/.test(html),
    "header: a sound file into the library under its NAME; no save path names a sound after its preset");

  /* files: a multi sound (links, MODE, B's settings, name); a single sound (MODE OFF, its name) */
  await H.sndChoose("dm:3");
  const fm = JSON.parse(js(await H.soundFileMake()));
  ok(fm.format === "stallion-multi" && fm.multi.name === "MY SPLIT" && fm.multi.split === 50 && fm.multi.b.name === nameOf("DIGITAL", 1) && !("p" in fm.multi),
    "header: Save to file: the multi sound (links by name, MODE, B's settings, its name; no values)");
  await H.sndChoose("init");
  H.soundFileCheck(fm);
  ok(await H.soundFileApply(fm) && T()[1].p[31] === 2 && T()[1].p[32] === 50 && T()[0].preset === 3 && H.hdr.name === "MY SPLIT",
    "header: Load file: the multi sound back");
  await H.sndChoose(fv("DIGITAL", 2));
  H.hdr.name = "MY KEYS";
  const fs1 = JSON.parse(js(await H.soundFileMake()));
  await H.sndChoose("dm:3");
  ok(fs1.format === "felucca-patch" && fs1.name === "MY KEYS" && await H.soundFileApply(fs1) && H.lyMode() === 0 && T()[1].p[31] === 0 &&
     H.hdr.name === "MY KEYS" && !H.hdr.multi,
    "header: a single sound's file: MODE OFF, its name");

  /* the header's words: never "FM-1" for the device; the connected one's name ({d}) */
  const keys = Object.keys(H.TEXT.en).filter((k) => /^(hdr|ly\d|multi|dvOffline|workOffline|leaveOffline|offline|stSnapHelp)/.test(k));
  const fm1 = keys.filter((k) => /FM-1/.test(H.TEXT.en[k]) || /FM-1|本体/.test(H.TEXT.ja[k]));
  const cur = H.DB.cur;
  H.DB.cur = { ...cur, kind: "smk", label: "SMK-37 Pro", model: "SMK-37 Pro" };
  const smk = H.t("hdrDevSave"), smk2 = H.t("hdrStore");
  H.DB.cur = cur;
  ok(keys.length > 25 && !fm1.length && smk === "Save on the SMK-37 Pro" && smk2 === "Store on the SMK-37 Pro" && H.t("hdrDevSave") === "Save on the FM-1",
    `header: no "FM-1" in its words (${keys.length} strings${fm1.length ? ": " + fm1.join(" ") : ""}); an SMK-37: "${smk}"`);

  /* every text of both languages on an
     SMK-37 Pro: none says "FM-1" but those naming both instruments; on an FM-1 they say FM-1 */
  const BOTH = new Set(["dvPress", "fm1", "nodevice"]), lang0 = H.getLang();
  H.DB.cur = { ...cur, kind: "smk", label: "SMK-37 Pro", model: "SMK-37 Pro" };
  const leak = [];
  let dn = 0;
  for (const l of ["en", "ja"]) {
    H.setLang(l);
    for (const k of Object.keys(H.TEXT[l])) {
      const v = H.t(k);
      if (typeof v === "string" && /FM-1/.test(v) && !BOTH.has(k)) leak.push(`${l}.${k}`);
      if (typeof H.TEXT[l][k] === "string" && H.TEXT[l][k].includes("{d}") && /SMK-37 Pro/.test(v)) dn++;
    }
  }
  H.setLang("en");
  const smkTitle = H.t("sxTitle"), smkStop = H.t("bkStop");
  H.DB.cur = cur;
  const fmTitle = H.t("sxTitle");
  H.setLang(lang0);
  ok(!leak.length && dn >= 80 && smkTitle === "SMK-37 Pro settings" && smkStop === "Stop the SMK-37 Pro (PLAY), then restore again." && fmTitle === "FM-1 settings",
    `device name: on an SMK-37 Pro no text says FM-1 (${dn} texts name it${leak.length ? "; FM-1 in " + leak.slice(0, 8).join(" ") : ""}); on an FM-1: "${fmTitle}"`);

  /* offline: no device; a multi sound made on the editor's own description, saved to a file, loaded into a device later */
  const od = await H.offline(true);
  ok(!!od && H.offlineOn() && /\(OFFLINE\)$/.test(od.info.version) && H.DB.state === "offline" && d().multi === null && H.mock().state.bank.every((x) => !x),
    `header: Work offline: a fresh synth, no device slots (the library and files), the banner OFFLINE (${od && od.info.version})`);
  await H.sndChoose("init");
  H.hdr.name = "OFF MULTI";
  await H.modeSet(3);
  await H.linkChoose(1, fv("PONY", 1));
  H.lySet(29, 5);
  await idle();
  const fo = JSON.parse(js(await H.soundFileMake()));
  ok(fo.format === "stallion-multi" && fo.multi.mode === 3 && fo.multi.ch === 5 && fo.multi.b.name === nameOf("PONY", 1) && fo.multi.name === "OFF MULTI",
    "header: offline: Save to file holds the multi sound");
  await H.offline(false);
  ok(!H.offlineOn() && H.DB.state === "demo", "header: ... leave offline, a device connected (the demo's)");
  ok(await H.soundFileApply(fo) && T()[1].engine === d().info.engines.indexOf("PONY") && T()[1].preset === 1 && T()[1].p[31] === 3 && T()[1].p[29] === 5 &&
     H.hdr.name === "OFF MULTI",
    "header: ... its file loaded into the device: B's sound by PRESET part 1, MULTI, B's channel, the name");
  await H.sndChoose("init");
  await idle();
  ok(!errs.length, "header: no warnings from the page" + (errs.length ? ` (${errs.slice(0, 3).join("; ")})` : ""));
}

/* ------------- the Library tab: multi sounds are rows like sounds (filtering by engine
   filters them too; a sound just saved to the library is selected, ready for tags). r1: libSaveCurrent's result for the
   multi sound MY SPLIT (B a DIGITAL sound) */
async function editorLibraryMultis(H, r1, nameOf) {
  const id = r1.multi && r1.multi.id, row = () => H.libRows().find((r) => r.id === id);
  const names = (o) => H.libFilter(H.libRows(), o).map((r) => r.name);
  ok(!!id && H.libSel() === id && row() && row().kind === "m" && row().eng.includes("DIGITAL") && !!row().created && !!row().modified,
    `library: a multi sound saved is a row of the list, selected (${H.libSel()}), its engines ${row() && row().eng.join("/")}, dated`);
  ok(H.libSetTags(id, "pad, live, pad") && js(row().tags) === js(["pad", "live"]) && H.libFilter(H.libRows(), { ft: "live" }).some((r) => r.id === id),
    "library: ... its tags set (pad, live) and the tag filter finds it");
  ok(names({ fe: H.LIB_MULTI }).includes("MY SPLIT") && H.libFilter(H.libRows(), { fe: H.LIB_MULTI }).every((r) => r.kind === "m") &&
     names({ fe: "DIGITAL" }).includes("MY SPLIT") && !names({ fe: "PONY" }).includes("MY SPLIT") &&
     H.libFilter(H.libRows(), { fe: "PONY" }).every((r) => r.kind === "p" ? r.x.engineName === "PONY" : r.eng.includes("PONY")),
    "library: engine MULTI lists the multi sounds only; an engine A or B plays finds it; another engine does not");
  ok(names({ q: "my spl" }).includes("MY SPLIT") && names({ q: "LIVE" }).includes("MY SPLIT") && names({ q: nameOf("DIGITAL", 1).toLowerCase() }).includes("MY SPLIT") &&
     !names({ q: "zzz-none" }).length && !names({ ft: "nosuchtag" }).length,
    "library: search finds it by name, tag and a link's name; another word or tag does not");
  ok(H.libRename(id, "my layer") && row().name === "MY LAYER" && H.libFilter(H.libRows(), { sort: "name" }).length === H.libRows().length,
    "library: ... renamed (MY LAYER); the sorts keep every row");
  const dup = await H.libDuplicate(id);
  ok(dup && dup.id !== id && dup.name === "MY LAYER 2" && js(dup.tags) === js(["pad", "live"]) && H.libSel() === dup.id && await H.libDelete(dup.id) && !H.libRows().some((r) => r.id === dup.id),
    "library: ... duplicated (MY LAYER 2, its tags, selected), the copy deleted");
  const ex = JSON.parse(js(H.libExportFile()));
  ok(Array.isArray(ex.multis) && ex.multis.some((m) => m.name === "MY LAYER" && !("id" in m)) && Array.isArray(ex.patches),
    "library: Export library holds the multi sounds (no id)");
  H.mlib.del(id);
  ok(H.libImportMultis(ex) >= 1 && H.libRows().some((r) => r.kind === "m" && r.name === "MY LAYER" && r.tags.includes("live")) && H.libImportMultis({ patches: [] }) === 0,
    "library: ... imported back with its tags; an older file without multis: none, no error");
  /* entries missing fields: never a throw; no date sorts last */
  H.mlib.write([...H.mlib.read(), { name: "OLD MULTI", mode: 1, a: { kind: 0, engine: 1, name: "X" }, b: { kind: 0, engine: 1, name: "Y" } }]);
  const lib = H.libList(), bare = { id: "bare-1", name: "BARE", engine: 0, p: [] };
  lib.push(bare);
  let threw = "";
  try { H.renderLib(); ["name", "engine", "created", "modified"].forEach((sort) => H.libFilter(H.libRows(), { sort, q: "o", ft: "x" })); } catch (e) { threw = e.message; }
  const mod = H.libFilter(H.libRows(), { sort: "modified" }).map((r) => r.name);
  ok(!threw && js(bare.tags) === "[]" && mod.indexOf("OLD MULTI") > mod.indexOf("MY LAYER") && H.libRows().find((r) => r.name === "OLD MULTI").id,
    `library: an entry missing tags / dates: no throw (${threw || "none"}), given an id, sorted last by date`);
  lib.splice(lib.indexOf(bare), 1);
  H.mlib.write(H.mlib.read().filter((m) => m.name !== "OLD MULTI"));
}

/* ------------------------------------- editor protocol v2: librarian + live --- */
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const js = (x) => JSON.stringify(x);
/* a mock + Link pair; counts frames sent per cmd and timeouts reported */
function attachMock(opt, linkOpt = {}) {
  const m = E.makeMockDevice({ auto: false, ...opt });
  const inp = [...m.access.inputs.values()][0], out = [...m.access.outputs.values()][0];
  const sent = {}, ev = { timeouts: 0, unknown: [], pushes: [] };
  const link = new E.Link((d) => { sent[d[4]] = (sent[d[4]] || 0) + 1; out.send(d); }, {
    timeout: 300, onTimeout: () => ev.timeouts++, onUnknown: (f) => ev.unknown.push(f),
    onPush: (f) => ev.pushes.push({ ...f, pending: link.cur ? link.cur.cmd : 0 }), ...linkOpt });
  inp.onmidimessage = (e) => link.receive(e.data);
  const rq = (r, o) => link.request(r, o);
  return { m, link, rq, sent, ev, done: () => { link.close(); m.stop(); } };
}
const emptyStep = { n: 0, notes: [0, 0, 0, 0], time: 2, flags: 0, vel: 0 };

async function editorLibrarian() {
  const { m, rq, done } = attachMock({});
  const C = E.CMD;
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const pdesc = [];
  for (let i = 0; i < info.pcount; i++) pdesc.push(E.parse[C.DESC](await rq(E.req.desc(0, i))));
  const keys = E.paramKeys(pdesc, info.pe0, info.pcount);
  ok(keys[6] === "PIT" && keys[13] === "PIT#2" && keys[info.pe0] === "E0" && new Set(keys).size === keys.length, "librarian: parameter keys unique (label#n, E0..E7)");

  const b = await E.bank.list(rq);
  ok(b.total === 32 && b.slots.length === 32 && b.slots[1].used && b.slots[1].name === "GLASS BELL" && !b.slots[3].used
    && b.slots[31].slot === 31, "librarian: UP_LIST, 32 slots in 2 frames");

  const cap = (await E.capturePatch(rq, info, "acid test")).patch;
  ok(cap.engine === 0 && cap.p.length === info.pcount && cap.pattern && cap.pattern[0][0] === 45 && cap.pattern[0][1] === 1,
    "librarian: capture = DUMP + first 16 steps");
  let rc = await E.bank.put(rq, 10, cap);
  const g = await E.bank.get(rq, info, 10);
  ok(rc === 0 && g.used && g.name === "acid test" && g.engine === 0 && eq(g.p, cap.p) && js(g.pattern) === js(cap.pattern),
    "librarian: UP_PUT -> UP_GET round trip");
  const bad = E.parse[C.UP_PUT](await rq(E.req.upPut(60, cap), { timeout: 2500, retries: 0 }));
  ok(bad.rc === 1, "librarian: UP_PUT to a slot past the bank -> rc 1");
  ok(E.upName("") === "PATCH" && E.upName("abcdefghijklmnop") === "abcdefghijkl" && E.upName("Bäss") === "Bss", "librarian: device names (ASCII, 1..12)");

  await rq(E.req.set(0, 1, 33));
  rc = await E.bank.store(rq, 11, "STORED");
  const g2 = await E.bank.get(rq, info, 11);
  const d1 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  ok(rc === 0 && g2.name === "STORED" && g2.engine === d1.engine && eq(g2.p, d1.p) && g2.p[1] === 33, "librarian: UP_STORE keeps the current sound");

  /* another engine, empty sequencer, then UP_LOAD brings the sound back, not the pattern (LIVE) */
  await rq(E.req.preset(2, 0));
  for (let i = 0; i < info.nstep; i++) await rq(E.req.stepSet(i, emptyStep));
  rc = await E.bank.load(rq, 10);
  const d2 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const st = [];
  for (let i = 0; i < 16; i++) st.push(E.parse[C.STEP_GET](await rq(E.req.stepGet(i))));
  ok(rc === 0 && d2.engine === 0 && eq(d2.p, cap.p) && st.every((x) => !x.n), "librarian: UP_LOAD applies the sound, the sequencer stays empty (LIVE)");

  rc = await E.bank.erase(rq, 11);
  const b2 = await E.bank.list(rq);
  const rcEmpty = await E.bank.load(rq, 11);
  ok(rc === 0 && !b2.slots[11].used && b2.slots[10].used && rcEmpty === 1, "librarian: UP_ERASE, UP_LOAD of an empty slot -> rc 1");

  /* audition: a PHASE patch with a pattern into an empty sequencer: the sound only (as UP_LOAD in SLOOP), never
     the pattern, and the mix / pattern / key parameters stay (ui.c param_kept) */
  const bass = await E.bank.get(rq, info, 2);   /* PHASE RESO, with the ACID pattern */
  for (let i = 0; i < info.nstep; i++) await rq(E.req.stepSet(i, emptyStep));
  const KEPT = [0, 25, 26, 27, 29, 30, 31, 32, 39, 40, 49];
  for (const [id, v] of [[0, 77], [25, 5], [29, 12], [39, -10], [49, 2]]) await rq(E.req.set(0, id, v));
  const d2b = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const flashBefore = js(m.state.bank);
  let a = await E.auditionPatch(rq, info, bass, { gEng: 20 });
  const d3 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const st3 = [];
  for (let i = 0; i < 16; i++) st3.push(E.parse[C.STEP_GET](await rq(E.req.stepGet(i))));
  const soundOk = (d, pt) => d.p.every((v, i) => (KEPT.includes(i) ? v === d2b.p[i] : v === pt.p[i]));
  ok(!a.wrote && d3.engine === bass.engine && soundOk(d3, bass) && st3.every((s) => !s.n) && js(m.state.bank) === flashBefore
    && d3.p[0] === 77 && d3.p[25] === 5 && d3.p[29] === 12 && d3.p[49] === 2,
    "librarian: audition = G_ENGSEL + SETs of the sound; no pattern, mix, LEN or key; no flash write");
  /* notes in the sequencer stay too */
  await rq(E.req.stepSet(20, { n: 1, notes: [50, 0, 0, 0], time: 0, flags: 0, vel: 90 }));
  const bell = await E.bank.get(rq, info, 1);
  a = await E.auditionPatch(rq, info, { ...bell, pattern: [[72, 0], [74, 0]] }, { gEng: 20 });
  const s0 = E.parse[C.STEP_GET](await rq(E.req.stepGet(0))), s20 = E.parse[C.STEP_GET](await rq(E.req.stepGet(20)));
  const d4 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  ok(!a.wrote && !s0.n && s20.notes[0] === 50 && d4.engine === 1 && soundOk(d4, bell), "librarian: audition keeps the sequencer as it is");

  /* ties as the firmware keeps them: no note; a rest has no flags */
  const tied = [[60, 1], [61, 4], [0, 4], [0, 3], [64, 2]];
  const norm = E.patternFromSteps(E.stepsFromPattern(tied));
  rc = await E.bank.put(rq, 12, { ...cap, name: "TIES", pattern: tied });
  const g3 = await E.bank.get(rq, info, 12);
  ok(rc === 0 && js(norm.slice(0, 5)) === js([[60, 1], [0, 4], [0, 4], [0, 0], [64, 2]]) && js(g3.pattern) === js(norm),
    "librarian: pattern ties / rests normalised like the firmware");
  rc = await E.bank.store(rq, 13, "");
  ok(rc === 0 && (await E.bank.get(rq, info, 13)).name === `${info.engines[d4.engine]} 14`, "librarian: UP_STORE with no name -> automatic name");

  /* library files */
  const ctx = { keys, engines: info.engines, firmware: info.version, pe0: info.pe0 };
  const pts = [cap, { ...bass, engineName: info.engines[bass.engine], tags: ["bass", "device"] }];
  const file = JSON.parse(JSON.stringify(E.libraryFile("library", pts, ctx)));
  ok(file.format === "felucca-library" && file.version === 1 && file.pCount === 61 && file.paramLabels.length === 61 && file.engines.length === 12,
    "library file: versioned, with P_COUNT, labels and engines");
  const back = E.readLibraryFile(file, ctx);
  ok(back.patches.length === 2 && !back.skipped && eq(back.patches[0].p, cap.p) && eq(back.patches[1].p, bass.p)
    && js(back.patches[1].pattern) === js(bass.pattern) && back.patches[1].tags.join() === "bass,device" && back.patches[1].engineName === "PHASE",
    "library file: write -> read round trip");
  /* a future firmware: one more parameter at id 5, engines in another order and one of them gone */
  const keys2 = [...keys.slice(0, 5), "NEW", ...keys.slice(5)];
  const eng2 = ["PHASE", "ANALOG", "SAMPLE"];
  const fut = E.readLibraryFile(file, { keys: keys2, engines: eng2 });
  const p0 = fut.patches[0].p;
  ok(fut.patches.length === 2 && p0.length === 62 && p0[5] === null && p0[6] === cap.p[5] && p0[61] === cap.p[60]
    && fut.patches[0].engine === 1 && fut.patches[1].engine === 0, "library file: other ids / engine order mapped by label and name");
  const lost = E.readLibraryFile({ ...file, patches: [{ ...file.patches[0], engineName: "WAVETABLE" }] }, ctx);
  ok(lost.patches.length === 0 && lost.skipped === 1, "library file: a patch for an unknown engine is skipped");
  /* Stallion R4, R5: a file from before the renames names its engine the old way; a device from before, too */
  const oldName = ((ren) => (n) => Object.keys(ren).find((k) => ren[k] === n))(
    vm.runInNewContext("(" + /\nconst ENG_RENAMED = (\{[^}]*\});/.exec(html)[1] + ")"));   /* (the old names: the editor's table) */
  const renamed = E.readLibraryFile({ ...file, patches: [{ ...file.patches[0], engine: 3, engineName: oldName("PALOMINO") }] },
    { keys, engines: ["ANALOG", "PHASE", "PONY", "PALOMINO"] });
  const toOld = E.readLibraryFile({ ...file, patches: [{ ...file.patches[0], engine: 3, engineName: "PALOMINO" }] },
    { keys, engines: ["ANALOG", "PHASE", oldName("PONY"), oldName("PALOMINO")] });
  ok(renamed.patches.length === 1 && renamed.patches[0].engine === 3 && renamed.patches[0].engineName === "PALOMINO"
    && toOld.patches.length === 1 && toOld.patches[0].engine === 3,
    "library file: an engine's old name reads as its new one (PALOMINO), both ways");
  const bankFile = E.libraryFile("bank", [{ ...g, engineName: "ANALOG", slot: 10 }], ctx);
  ok(bankFile.kind === "bank" && bankFile.patches[0].slot === 10 && E.readLibraryFile(bankFile, ctx).patches[0].slot === 10, "library file: bank export keeps slot numbers");
  const old = E.readLibraryFile({ format: "felucca-patch", version: 1, engine: 0, preset: 4, engineName: "ANALOG", presetName: "ACID", p: d2.p, steps: E.stepsFromPattern(cap.pattern) }, ctx);
  ok(old.patches.length === 1 && eq(old.patches[0].p, d2.p) && js(old.patches[0].pattern) === js(cap.pattern), "library file: reads the old \"Save to file\" format");
  let threw = false;
  try { E.readLibraryFile({ format: "something" }, ctx); } catch (e) { threw = true; }
  ok(threw, "library file: unknown format -> error");
  done();
}

async function editorLive() {
  const C = E.CMD;
  const { m, link, rq, sent, ev, done } = attachMock({ watchMs: 250 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  ok(await E.startWatch(rq), "live: WATCH on");

  /* a push between a request and its reply */
  const pend = rq(E.req.dump());
  const kn = m.sim.knob(9, 4);
  const dump = E.parse[C.DUMP](await pend, info);
  const ch = ev.pushes.find((f) => f.cmd === C.CHANGED);
  const cv = ch && E.parse[C.CHANGED](ch.a);
  ok(dump.p.length === 61 && ch && ch.pending === C.DUMP && cv.scope === 0 && cv.id === 9 && cv.value === kn.value && !ev.unknown.length,
    "live: CHANGED while DUMP waits -> push handler, reply still matched");
  const rl = m.sim.reload();
  m.sim.step(3);
  const pend2 = rq(E.req.stepGet(7));
  const s7 = E.parse[C.STEP_GET](await pend2);
  await sleep(10);
  const r = ev.pushes.find((f) => f.cmd === C.RELOAD), sc = ev.pushes.find((f) => f.cmd === C.STEP_CHANGED);
  ok(s7.index === 7 && r && E.parse[C.RELOAD](r.a).preset === rl.preset && sc && E.parse[C.STEP_CHANGED](sc.a).index === 3,
    "live: RELOAD and STEP_CHANGED routed");
  const nr = ev.pushes.filter((f) => f.cmd === C.RELOAD).length;
  await rq(E.req.preset(1, 2));
  await rq(E.req.stepSet(9, { n: 1, notes: [62, 0, 0, 0], time: 0, flags: 0, vel: 90 }));
  await sleep(10);
  ok(ev.pushes.filter((f) => f.cmd === C.RELOAD).length === nr + 1 && !ev.pushes.some((f) => f.cmd === C.STEP_CHANGED && f.a[0] === 9),
    "live: RELOAD after an editor PRESET too, nothing after its STEP_SET");

  /* PING keeps the watch on; without requests it ends */
  for (let i = 0; i < 4; i++) { await sleep(120); await rq(E.req.ping()); }
  let n0 = ev.pushes.length;
  m.sim.knob();
  await sleep(10);
  ok(ev.pushes.length === n0 + 1, "live: PING keeps WATCH on");
  await sleep(320);
  n0 = ev.pushes.length;
  m.sim.knob();
  await sleep(10);
  ok(ev.pushes.length === n0, "live: WATCH ends by itself without requests");

  /* a slider drag: 40 values at once -> one SET in flight + one coalesced, the last value wins */
  const before = sent[C.SET] || 0;
  const all = [];
  for (let v = 0; v < 40; v++) all.push(rq(E.req.set(0, 9, v * 3), { key: "0:9" }));
  await Promise.all(all);
  ok((sent[C.SET] || 0) - before === 2 && m.state.p[9] === 117 && link.idle, "live: drag SETs coalesce (2 frames for 40 values, latest kept)");
  ok(ev.timeouts === 0, "live: no timeouts");
  done();

  /* older firmware: no reply to WATCH -> false without a "no reply" message; no bank */
  const o = attachMock({ legacy: true });
  E.parse[C.INFO](await o.rq(E.req.info()));
  const w = await o.rq(E.req.watch(1), { timeout: 60, retries: 0, quiet: true }).then(() => true, () => false);
  const sw = await E.startWatch(o.rq);
  const bl = await E.bank.list((rr, oo) => o.rq(rr, { ...oo, timeout: 60, quiet: true })).then(() => "listed", (e) => e.message);
  ok(!w && !sw && /^timeout/.test(bl) && o.ev.timeouts === 0, "live: older firmware -> WATCH unanswered (fall back to polling), no bank");
  o.done();
}

/* ------------------------------------------------------ editor protocol v3: tracks --- */
async function editorTracks() {
  const C = E.CMD;
  const { m, rq, ev, done } = attachMock({ watchMs: 1000 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const tr = E.parse[C.TRACK](await rq(E.req.track()));
  ok(info.ntrk === 4 && tr.sel === 0 && tr.ntrk === 4 && tr.tracks[0].engine === 0 && tr.tracks[1].engine === 1
    && tr.tracks[3].engine === info.nengines, "tracks: INFO NTRK, TRACK lists 4 (track 4 = drums, engine NENGINES)");
  /* the v1 commands follow the selected track */
  const d0 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const t1 = E.parse[C.TRACK](await rq(E.req.track(1)));
  const d1 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  await rq(E.req.set(0, 1, 77));
  const td0 = E.parse[C.TRACK_DUMP](await rq(E.req.trackDump(0)), info);
  const td1 = E.parse[C.TRACK_DUMP](await rq(E.req.trackDump(1)), info);
  ok(t1.sel === 1 && d1.engine === 1 && d0.engine === 0 && td1.p[1] === 77 && td0.p[1] === d0.p[1] && td0.p[1] !== 77,
    "tracks: TRACK selects; DUMP / SET act on it, TRACK_DUMP reads any track");
  /* steps of a track that is not selected */
  const w = E.parse[C.TRACK_STEP](await rq(E.req.trackStep(2, 5, { n: 2, notes: [60, 67, 0, 0], time: 0, flags: 1, vel: 99 })));
  const g2 = E.parse[C.TRACK_STEP](await rq(E.req.trackStep(2, 5)));
  const s1 = E.parse[C.STEP_GET](await rq(E.req.stepGet(5)));
  ok(w.track === 2 && g2.n === 2 && g2.notes[1] === 67 && g2.vel === 99 && s1.n === 0, "tracks: TRACK_STEP set / get on another track");
  /* level / mute; the drum level is GLO > DRUMS LEVEL */
  const mx = E.parse[C.TRACK_MIX](await rq(E.req.trackMix(0, 90, 1)));
  const mxd = E.parse[C.TRACK_MIX](await rq(E.req.trackMix(3, 64, 0)));
  const mx2 = E.parse[C.TRACK_MIX](await rq(E.req.trackMix(0)));
  const tr2 = E.parse[C.TRACK](await rq(E.req.track()));
  ok(mx.level === 90 && mx.mute === 1 && mx2.level === 90 && mxd.level === 64 && m.state.g[25] === 64
    && tr2.tracks[0].level === 90 && tr2.tracks[0].mute === 1, "tracks: TRACK_MIX level / mute (drums: G_DRLVL)");
  /* the drum track: no sound to store or load */
  await rq(E.req.track(3));
  const dd = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const us = E.parse[C.UP_STORE](await rq(E.req.upStore(20, "X"), { timeout: 2500, retries: 0 }));
  const ul = E.parse[C.UP_LOAD](await rq(E.req.upLoad(1), { timeout: 2500, retries: 0 }));
  ok(dd.engine === info.nengines && us.rc === 1 && ul.rc === 1, "tracks: drum track selected -> DUMP engine NENGINES, UP_STORE / UP_LOAD rc 1");
  /* pushes carry the selected track */
  await rq(E.req.track(0));
  ok(await E.startWatch(rq), "tracks: WATCH on");
  m.sim.track(2);
  m.sim.step(4);
  await sleep(10);
  const rl = ev.pushes.find((f) => f.cmd === C.RELOAD), sc = ev.pushes.find((f) => f.cmd === C.STEP_CHANGED);
  ok(rl && E.parse[C.RELOAD](rl.a).track === 2 && sc && E.parse[C.STEP_CHANGED](sc.a).track === 2 && E.parse[C.STEP_CHANGED](sc.a).index === 4,
    "tracks: RELOAD / STEP_CHANGED carry the selected track");
  /* projects keep all four tracks */
  await rq(E.req.project(1, 3), { timeout: 4000, retries: 0 });
  await rq(E.req.trackStep(2, 5, { n: 0, notes: [0, 0, 0, 0], time: 2, flags: 0, vel: 0 }));
  await rq(E.req.track(0));
  await rq(E.req.project(0, 3), { timeout: 4000, retries: 0 });
  const back = E.parse[C.TRACK_STEP](await rq(E.req.trackStep(2, 5)));
  const sel = E.parse[C.TRACK](await rq(E.req.track())).sel;
  ok(back.n === 2 && back.notes[0] === 60 && sel === 2, "tracks: PROJECT save / load keeps every track and the selection");
  /* older firmware: no NTRK in INFO, no RELOAD track byte */
  const o = attachMock({ legacy: true });
  const oi = E.parse[C.INFO](await o.rq(E.req.info()));
  ok(oi.ntrk === 0 && E.parse[C.RELOAD]([0, 4]).track === 0 && E.parse[C.STEP_CHANGED]([7]).track === 0, "tracks: older firmware parses (no tracks)");
  o.done();
  done();
}

/* ------------------------------------------------ editor v3: the mixer (Tracks tab) --- */
async function editorMixer() {
  const C = E.CMD;
  const { m, rq, ev, done } = attachMock({ watchMs: 1000 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const PAN = 39, MUTE = 40;
  const m0 = await E.mixer.read(rq, info, { pan: PAN });
  ok(m0.ntrk === 4 && m0.tracks.length === 4 && m0.tracks[1].pan === -24 && m0.tracks[2].pan === 20 && m0.tracks[3].engine === info.nengines
    && m0.tracks.every((x) => Number.isInteger(x.level) && (x.mute === 0 || x.mute === 1)), "mixer: read = TRACK + pan of every track (TRACK_DUMP)");
  /* level / mute of a track that is not selected, and of the drum track (G_DRLVL) */
  const a = await E.mixer.setMix(rq, 2, 70, 1);
  const b = await E.mixer.setMix(rq, 3, 200, 0);
  const m1 = await E.mixer.read(rq, info, { pan: PAN });
  const td2 = E.parse[C.TRACK_DUMP](await rq(E.req.trackDump(2)), info);
  ok(a.level === 70 && a.mute === 1 && b.level === 127 && m.state.g[25] === 127 && m1.tracks[2].level === 70 && m1.tracks[2].mute === 1
    && td2.p[0] === 70 && td2.p[MUTE] === 1 && m1.sel === 0, "mixer: TRACK_MIX level / mute round trip (drums: G_DRLVL, clamped)");
  /* pan of another track: selected for the SET, the selection put back, no RELOAD pushed */
  ok(await E.startWatch(rq), "mixer: WATCH on");
  const pushes = ev.pushes.length;
  /* (the v3 path: firmware 0.8 has no TRACK_PARAM) */
  const p2 = await E.mixer.setPan(rq, 2, 0, PAN, -40);
  const p0 = await E.mixer.setPan(rq, 0, 0, PAN, 99);
  await sleep(10);
  const m2 = await E.mixer.read(rq, info, { pan: PAN });
  const d0 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  ok(p2 === -40 && p0 === 63 && m2.sel === 0 && m2.tracks[2].pan === -40 && m2.tracks[0].pan === 63 && d0.p[PAN] === 63 && m2.tracks[1].pan === -24
    && ev.pushes.length === pushes, "mixer: pan of any track via SET (other track selected for a moment, then back; no push)");
  /* the device's TRACKS page: level of the selected track pushes CHANGED; REC arm shows in TRACK */
  m.sim.level(33);
  m.sim.arm(1);
  await sleep(10);
  const ch = ev.pushes.filter((f) => f.cmd === C.CHANGED).map((f) => E.parse[C.CHANGED](f.a)).pop();
  const m3 = await E.mixer.read(rq, info, { pan: PAN });
  ok(ch && ch.scope === 0 && ch.id === 0 && ch.value === 33 && m3.tracks[0].level === 33 && m3.tracks[1].armed === 1 && m3.tracks[0].armed === 0,
    "mixer: device-side level (CHANGED push) and REC arm read back");
  await rq(E.req.track(3));
  m.sim.level(90);
  await sleep(10);
  const chd = E.parse[C.CHANGED](ev.pushes.filter((f) => f.cmd === C.CHANGED).pop().a);
  ok(chd.scope === 1 && chd.id === 25 && chd.value === 90, "mixer: drum level on the device pushes G_DRLVL");
  /* the drum track's steps: GM notes, shown and typed by name */
  const st = [];
  for (let i = 0; i < 16; i++) st.push(E.parse[C.TRACK_STEP](await rq(E.req.trackStep(3, i))));
  const names = st[0].notes.slice(0, st[0].n).map(E.drumName).join(" ");
  ok(names === "KICK CHH" && st[4].notes.slice(0, 2).map(E.drumName).join(" ") === "SNARE CHH" && E.drumName(20) === "20",
    "mixer: the mock drum track holds a GM pattern (KICK CHH ...)");
  ok(JSON.stringify(E.parseNotes("kick CHH 49")) === "[36,42,49]" && JSON.stringify(E.parseNotes("C4 SNARE")) === "[60,38]" && E.parseNotes("KICKS") === null
    && Object.keys(E.GM_DRUM).length === 47 && new Set(Object.values(E.GM_DRUM)).size === 47, "mixer: GM drum names parse (unique, 35..81)");
  done();
}

/* ------------------------------------- editor v4: TRACK_PARAM and TRACK_CHANGED --- */
async function editorTrackParam() {
  const C = E.CMD, PAN = 39, MUTE = 40;
  const { m, rq, sent, ev, done } = attachMock({ watchMs: 1000 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const w1 = E.parse[C.WATCH](await rq(E.req.watch(1)));
  m.sim.param(2, PAN, 11);
  await sleep(10);
  ok(w1.on === 1 && !ev.pushes.some((f) => f.cmd === C.TRACK_CHANGED), "v4: WATCH 1 answers 1 as before (no TRACK_CHANGED pushes)");
  ok(await E.startWatch(rq) === 3, "v4: WATCH 3 -> 3 (TRACK_PARAM / TRACK_CHANGED known)");
  const tracks0 = sent[C.TRACK] || 0, pushes = ev.pushes.length;
  const g = E.parse[C.TRACK_PARAM](await rq(E.req.trackParam(1, PAN)));
  const p2 = await E.mixer.setPan(rq, 2, 0, PAN, -40, true);
  const p1 = await E.mixer.setPan(rq, 1, 0, PAN, 99, true);
  const alg = E.parse[C.TRACK_PARAM](await rq(E.req.trackParam(1, info.pe0, 50)));   /* track 2 is DIGITAL: ALG 0..7 */
  const lv = E.parse[C.TRACK_PARAM](await rq(E.req.trackParam(3, 0, -5)));
  const sel = E.parse[C.TRACK](await rq(E.req.track()));
  const td2 = E.parse[C.TRACK_DUMP](await rq(E.req.trackDump(2)), info);
  await sleep(10);
  ok(g.track === 1 && g.id === PAN && g.value === -24 && p2 === -40 && p1 === 63 && alg.value === 7 && lv.value === 0 && td2.p[PAN] === -40
    && sel.sel === 0 && (sent[C.TRACK] || 0) === tracks0 + 1 && ev.pushes.length === pushes,
    "v4: TRACK_PARAM get / set on other tracks (clamped as SET, selection kept, no push)");
  const bad = await rq(E.req.trackParam(4, PAN), { timeout: 60, retries: 0, quiet: true }).then(() => "reply", () => "none");
  ok(bad === "none", "v4: TRACK_PARAM of track 5: no reply");
  /* device-side changes: CHANGED for the selected track, TRACK_CHANGED for the others */
  m.sim.param(2, PAN, 30);
  m.sim.param(3, MUTE, 1);
  m.sim.param(0, PAN, -7);
  await sleep(10);
  const tc = ev.pushes.filter((f) => f.cmd === C.TRACK_CHANGED).map((f) => E.parse[C.TRACK_CHANGED](f.a));
  const ch = ev.pushes.filter((f) => f.cmd === C.CHANGED).map((f) => E.parse[C.CHANGED](f.a)).pop();
  ok(tc.length === 2 && tc[0].track === 2 && tc[0].id === PAN && tc[0].value === 30 && tc[1].track === 3 && tc[1].id === MUTE && tc[1].value === 1
    && ch && ch.scope === 0 && ch.id === PAN && ch.value === -7 && !ev.unknown.length, "v4: TRACK_CHANGED pushes for the other tracks, CHANGED for the selected one");
  done();
  /* firmware 0.8 (v3): WATCH 3 answers 1, TRACK_PARAM unanswered: the editor keeps the select / restore path */
  const o = attachMock({ v3: true, watchMs: 1000 });
  E.parse[C.INFO](await o.rq(E.req.info()));
  const on = await E.startWatch(o.rq);
  const tp = await o.rq(E.req.trackParam(1, PAN), { timeout: 60, retries: 0, quiet: true }).then(() => "reply", () => "none");
  const pv = await E.mixer.setPan(o.rq, 2, 0, PAN, 5, false);
  ok(on === 1 && tp === "none" && pv === 5 && o.ev.timeouts === 0 && !o.ev.unknown.length, "v4: v3 firmware -> WATCH 1, no TRACK_PARAM (pan by select / restore)");
  o.done();
}

/* ------------------------------------- editor v9 (SLOOP 2.4): FM6 patches --- */
async function editorV9() {
  const C = E.CMD, F = E.FM6;
  /* the editor's factory patches == the firmware's (build/gen/felucca_fm6.h, tools/gen_fm6_patches.py) */
  const gp = join(HERE, "../build/gen/felucca_fm6.h");
  if (existsSync(gp)) {
    const h = readFileSync(gp, "utf8").replace(/\/\*[^*]*\*\//g, "");
    const arr = (name) => (new RegExp(`${name}\\[[^\\]]*\\](?:\\[[^\\]]*\\])?\\s*=\\s*\\{([\\s\\S]*?)\\};`).exec(h) || [])[1] || "";
    const nums = (x) => (x.match(/\d+/g) || []).map(Number);
    const fac = nums(arr("FM6_FACTORY"));
    ok(js(nums(arr("FM6_INIT"))) === js(Array.from(F.INIT_PK)) && fac.length === F.FACTORY_PK.length * 128
      && F.FACTORY_PK.every((pk, i) => js(Array.from(pk)) === js(fac.slice(i * 128, i * 128 + 128))),
    "v9: the editor's init and factory patches == felucca_fm6.h");
  }
  /* pack / unpack: every voice survives, every value into its range */
  let rt = true;
  for (let n = 0; n < 50; n++) {
    const v = F.sanitize(Uint8Array.from({ length: F.SIZE }, (_, i) => (i * 37 + n * 101 + (i * n) % 13) & 127));
    if (js(Array.from(F.unpack(F.pack(v)))) !== js(Array.from(v))) rt = false;
  }
  ok(rt, "v9: FM6 pack / unpack round trip (50 voices, sanitized)");
  /* SysEx: one voice (163 bytes) and a 32-voice bank (4104 bytes), names, checksums */
  const v1 = F.factory(0), one = F.singleSysex(v1), p1 = F.parseSysex(one);
  ok(one.length === 163 && one[0] === 0xF0 && one[162] === 0xF7 && p1.voices.length === 1 && !p1.badSum
    && js(Array.from(p1.voices[0].v)) === js(Array.from(v1)), "v9: single-voice SysEx: 163 bytes, read back the same");
  const voices = Array.from({ length: 32 }, (_, k) => { const v = F.factory(k % F.FACTORY_PK.length); F.setName(v, `VOICE ${k + 1}`); return v; });
  const bank = F.bankSysex(voices), pb = F.parseSysex(bank);
  ok(bank.length === 4104 && pb.voices.length === 32 && !pb.badSum && pb.voices[31].name.trim() === "VOICE 32"
    && pb.voices.every((x, k) => js(Array.from(x.v)) === js(Array.from(F.unpack(F.pack(voices[k]))))),
  "v9: 32-voice bank SysEx: 4104 bytes, 32 voices with their names");
  const badb = Uint8Array.from(bank); badb[4102] ^= 1;
  ok(F.parseSysex(badb).badSum, "v9: a bank with a wrong checksum is read but flagged");
  /* damaged and unusual files (2.4, after Felucca 1.0.3) */
  const cut = bank.subarray(0, 6 + 128 * 10 + 50);               /* cut inside voice 11 */
  const pc = F.parseSysex(cut);
  ok(pc.voices.length === 10 && pc.short === 1 && pc.voices[9].name.trim() === "VOICE 10", "v9: a bank cut short: its whole voices, flagged");
  const two = Uint8Array.from([...bank, ...one]), p2 = F.parseSysex(two);
  ok(p2.voices.length === 33 && !p2.badSum && !p2.short, "v9: a bank and a voice in one file: 33 voices");
  const nof7 = Uint8Array.from([...bank.subarray(0, 4102), ...one]);   /* the bank with no checksum / F7 */
  ok(F.parseSysex(nof7).voices.length === 33, "v9: a message with no end: the next one still read");
  const raw2 = new Uint8Array(8192); raw2.set(bank.subarray(6, 4102), 0); raw2.set(bank.subarray(6, 4102), 4096);
  ok(F.parseSysex(raw2).voices.length === 64, "v9: raw data, two 4096-byte banks: 64 voices");
  const fm4 = Uint8Array.from([0xF0, 0x43, 0x00, 0x04, 0x20, 0x00, ...new Array(4096).fill(0), 0, 0xF7]), p4 = F.parseSysex(fm4);
  ok(!p4.voices.length && p4.kinds.includes("fm4") && p4.sysex, "v9: 4-operator voices: none read, said so");
  const roland = F.parseSysex(Uint8Array.from([0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7]));
  ok(!roland.voices.length && roland.kinds[0] === "maker:41", "v9: another maker's SysEx: its ID");
  ok(!F.parseSysex(Uint8Array.from([1, 2, 3])).sysex, "v9: not SysEx at all: said so");
  /* the device: FM6_LIST / GET / PUT / ERASE */
  const { m, rq, done } = attachMock({});
  const info = E.parse[C.INFO](await rq(E.req.info()));
  ok(info.proto === 10 && info.nengines === 12 && info.engines[9] === "FM6", "v9: INFO ends with 10 (v10 includes v9), FM6 is engine 9");
  const L = E.parse[C.FM6_LIST](await rq(E.req.fm6List()));
  if (!(L.factory === F.FACTORY_PK.length && L.bank === 27 && L.slots.length === F.FACTORY_PK.length + 27)) console.log("  FM6_LIST", L.factory, L.bank, L.slots.length, js(L.slots.slice(0, 10)));
  ok(L.factory === F.FACTORY_PK.length && L.bank === 27 && L.slots.length === F.FACTORY_PK.length + 27 && L.slots[0].used, "v9: FM6_LIST: the factory patches, 27 bank slots");
  const mine = F.factory(3); F.setName(mine, "MY BELL");
  let r = E.parse[C.FM6_PUT](await rq(E.req.fm6Put(F.TARGET.TRACK, 1, F.pack(mine))));
  const g = E.parse[C.FM6_GET](await rq(E.req.fm6Get(F.TARGET.TRACK, 1)));
  ok(r.rc === 0 && g.rc === 0 && F.name(F.unpack(g.packed)).trim() === "MY BELL", "v9: FM6_PUT to track 2, FM6_GET reads it back");
  r = E.parse[C.FM6_PUT](await rq(E.req.fm6Put(F.TARGET.BANK, 4, F.pack(mine)), { timeout: 3000 }));
  const L2 = E.parse[C.FM6_LIST](await rq(E.req.fm6List()));
  ok(r.rc === 0 && L2.slots[F.FACTORY_PK.length + 4].used && L2.slots[F.FACTORY_PK.length + 4].name.trim() === "MY BELL", "v9: stored in bank slot B5, listed by name");
  m.sim.play(true);
  r = E.parse[C.FM6_PUT](await rq(E.req.fm6Put(F.TARGET.BANK, 5, F.pack(mine)), { timeout: 3000 }));
  const re = E.parse[C.FM6_ERASE](await rq(E.req.fm6Erase(4), { timeout: 3000 }));
  ok(r.rc === 3 && re.rc === 3, "v9: bank writes refused while the song plays (rc 3)");
  m.sim.play(false);
  const e1 = E.parse[C.FM6_ERASE](await rq(E.req.fm6Erase(4), { timeout: 3000 }));
  const ge = E.parse[C.FM6_GET](await rq(E.req.fm6Get(F.TARGET.BANK, 4)));
  const bad = E.parse[C.FM6_GET](await rq(E.req.fm6Get(F.TARGET.FACTORY, 30)));
  ok(e1.rc === 0 && ge.rc === 2 && bad.rc === 1, "v9: FM6_ERASE empties the slot (GET rc 2), a bad index is rc 1");
  done();
  const old = attachMock({ v8: true });
  const oi = E.parse[C.INFO](await old.rq(E.req.info()));
  let none = false;
  try { await old.rq(E.req.fm6List(), { timeout: 200, retries: 0 }); } catch (e) { none = true; }
  ok(oi.proto === 8 && none, "v9: a v8 device says 8 and does not answer FM6_LIST (the editor hides the FM6 panel)");
  old.done();
}

/* ------------------------------------- editor v6: backup / restore --- */
async function editorBackup() {
  const C = E.CMD;
  const { rq, done } = attachMock({});
  const info = E.parse[C.INFO](await rq(E.req.info()));
  ok(info.proto >= 6, "backup: INFO protocol v6 or later");
  const ec = readFileSync(join(HERE, "../firmware/src/editor.c"), "utf8");
  ok(/ED_BK_IDS\[\] = \{0, 1, 2, 3, 4, 5, 6, 7, 8, 9,\n#if CLIPPER_SYNTH\n\s*14,\n#endif\n\s*32, 33, 34, 35\}/.test(ec), "backup: the object ids == editor.c ED_BK_IDS (35: USR4, 9: SYN kits, 14: the projects' blocks; preset format 2 has no 12 / 13)");
  await rq(E.req.upStore(3, "BACKUP ME"));
  await rq(E.req.project(1, 2), { timeout: 4000, retries: 0 });
  const s = Int16Array.from({ length: 3000 }, (_, i) => Math.round(8000 * Math.sin(i / 7)));
  const { hdr, data } = E.buildSlot("test", [{ s, root: 60 }]);
  await rq(E.req.smpBegin(1), { timeout: 1000, retries: 0 });
  for (let off = 0; off < data.length; off += 256) await rq(E.req.smpWrite(1, E.SMP.DATA_OFF + off, data.subarray(off, off + 256)), { timeout: 1000 });
  await rq(E.req.smpEnd(1, hdr), { timeout: 2000, retries: 0 });
  const A = await E.backupCapture(rq, info);
  const obj = (id) => A.objects.find((o) => o.id === id);
  ok(A.format === "sloop-backup" && A.objects.map((o) => o.id).join() === "0,1,2,3,4,5,6,7,8,9,32,33,34,35" && obj(9).len === 1464 && obj(33).len > 512 && obj(35).len === 0
    && obj(4).len > 0 && obj(3).len === 0 && obj(32).len === 0, "backup: LIST + GET: 14 objects (project in C, sample in USR2, B empty, the FM6 bank, the SYN kits, USR4)");
  await rq(E.req.upErase(3));
  await rq(E.req.smpErase(1), { timeout: 2500, retries: 0 });
  await rq(E.req.set(0, 3, 5));
  await rq(E.req.project(1, 1), { timeout: 4000, retries: 0 });
  await E.backupRestore(rq, JSON.parse(JSON.stringify(A)));   /* as written to disk and read back */
  const B = await E.backupCapture(rq, info);
  ok(js(B.objects) === js(A.objects), "backup: restore -> the same bytes again (presets, projects, samples, settings)");
  const bad = JSON.parse(JSON.stringify(A));
  bad.objects[2].data = E.b64enc(E.b64dec(bad.objects[2].data).map((x, i) => i === 3 ? x ^ 1 : x));
  let caught = "";
  try { E.backupObjects(bad); } catch (e) { caught = e.code; }
  ok(caught === "bkBad", "backup: a damaged file is refused before anything is written");
  done();
  const old = attachMock({ noBackup: true });
  const oi = E.parse[C.INFO](await old.rq(E.req.info()));
  ok(oi.proto === 5, "backup: firmware without it says protocol 5 (the editor hides backup)");
  old.done();
}


/* ------------------- Stallion: GLO > TOUCH and TOUCH CAL in the backup (object 15, specs/touch R28, R37) --- */
async function backupTouch() {
  const C = E.CMD;
  const pc = readFileSync(join(HERE, "../firmware/src/project.c"), "utf8"), ec = readFileSync(join(HERE, "../firmware/src/editor.c"), "utf8");
  const th = readFileSync(join(HERE, "../firmware/src/touch.h"), "utf8");
  ok(/TOUCH_STORE_MAGIC 0x31484354u/.test(pc) && /sizeof\(touch_store_t\) == 148u/.test(pc) && /TOUCH_BODY_OFF == 12u/.test(pc)
    && /offsetof\(touch_store_t, x\) == 116u/.test(pc) && /offsetof\(touch_store_t, ka\) == 120u/.test(pc)
    && /offsetof\(touch_store_t, pa\) == 132u/.test(pc) && E.TOUCH.MAGIC === 0x31484354 && E.TOUCH.SIZE === 148 && E.TOUCH.BODY_OFF === 12
    && E.TOUCH.EXT_OFF === 116 && E.TOUCH.KAT_OFF === 120 && E.TOUCH.PAT_OFF === 132,
    "touch backup: object 15 == project.c touch_store_t (TCH1, 148 bytes, the LEVER's mode at 116, KEY AT at 120, PAD AT at 132)");
  {   /* KEY AT and PAD AT: the editor's defaults == touch.h KAT_CFG_DEFAULTS, pat_cfg.h PAT_CFG_DEFAULTS */
    const pa = readFileSync(join(HERE, "../firmware/src/pat_cfg.h"), "utf8"), m = E.TOUCH.make(), mv = new DataView(m.buffer);
    ok(/KAT_CFG_DEFAULTS \{1, 50, 70, 40, 64, 0, 120, 400, 3000\}/.test(th) && /PAT_CFG_DEFAULTS \{60, SH_SLOW, SH_EASE, 0, 80, 800, 250, 120, 120, 0\}/.test(pa)
      && /enum \{ SH_LIN, SH_EASE, SH_SLOW, SH_FAST, SH_N \}/.test(pa)
      && [1, 50, 70, 40, 64, 0].every((b, i) => m[120 + i] === b) && mv.getUint16(126, true) === 120 && mv.getUint16(128, true) === 400
      && mv.getUint16(130, true) === 3000 && [60, 2, 1, 0].every((b, i) => m[132 + i] === b)
      && [80, 800, 250, 120, 120, 0].every((w, i) => mv.getUint16(136 + 2 * i, true) === w) && E.TOUCH.ok(m),
      "touch backup: the editor's KEY AT / PAD AT defaults == touch.h KAT_CFG_DEFAULTS, pat_cfg.h PAT_CFG_DEFAULTS");
  }
  ok(/TOUCH_CFG_DEFAULTS \{KT_OFF, 40, AT_OUT_OFF, AT_LIN, 0, 1, 1, 1, 24, 12, 0, 0, 120, 150, 400\}/.test(th)
    && /enum \{ LV_SPRNG, LV_HOLD, LV_LATCH, LV_MODES \}/.test(th) && /TOUCH_EXT_DEFAULTS \{LV_LATCH, 0, \{0, 0\}\}/.test(th)
    && E.TOUCH.DEF.leverMode === 2 && E.TOUCH.make()[116] === 2 && E.TOUCH.make()[117] === 0
    && E.TOUCH.ok(E.TOUCH.make()), "touch backup: the editor's defaults == touch.h TOUCH_CFG_DEFAULTS, TOUCH_EXT_DEFAULTS (LATCH)");
  ok(/ED_BK_IDS2\[\] = \{15\}/.test(ec) && /a\[0\] & 1u\) \? \(uint32_t\)sizeof ED_BK_IDS2/.test(ec) && E.req.bkList()[1][0] === 1,
    "touch backup: 15 listed only to a LIST with bit 0 (editor.c ED_BK_IDS2; the editor sets it)");
  ok(E.BK.RESTORE.indexOf(15) > E.BK.RESTORE.indexOf(1) && E.BK.OPTIONAL.includes(15), "touch backup: restored after the settings, skipped by a device without it");
  const { rq, done } = attachMock({ touch: true });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  /* an older editor's LIST (no argument) never sees it: its own files stay ones it can restore */
  const L0 = E.parse[C.BK_LIST](await rq([C.BK_LIST, []]));
  ok(!L0.items.some((x) => x.id === 15), "touch backup: an older editor's LIST does not list object 15");
  const mine = E.TOUCH.make({ keyTouch: 2, atSens: 55, atCurve: 1, atOffset: -3, lever: 0, motionRec: 0, ribRange: 7, atAttack: 120, ribReturn: 900, leverMode: 1 });
  mine[12 + 20 + 80] = 1;                               /* (touch_cal_t cal: a calibration ran; its ends are zero) */
  new DataView(mine.buffer).setUint32(8, E.crc32(mine.subarray(12)), true);
  await rq(E.req.bkBegin(15, mine.length, E.crc32(mine))); await rq(E.req.bkData(15, 0, mine)); await rq(E.req.bkCommit(15));
  const A = await E.backupCapture(rq, info);
  const o15 = A.objects.find((o) => o.id === 15);
  ok(o15 && o15.len === 148 && o15.len === E.TOUCH.SIZE && eq(E.b64dec(o15.data), mine), "touch backup: a backup holds GLO > TOUCH and TOUCH CAL as set (object 15)");
  const other = E.TOUCH.make();
  await rq(E.req.bkBegin(15, other.length, E.crc32(other))); await rq(E.req.bkData(15, 0, other)); await rq(E.req.bkCommit(15));
  await E.backupRestore(rq, JSON.parse(JSON.stringify(A)));
  let B = await E.backupCapture(rq, info);
  ok(js(B.objects) === js(A.objects), "touch backup: restore -> the same bytes again (object 15 with the rest)");
  /* a backup made before (no object 15): restores, and leaves GLO > TOUCH as it is */
  const old = JSON.parse(JSON.stringify(A));
  old.objects = old.objects.filter((o) => o.id !== 15);
  await E.backupRestore(rq, old);
  B = await E.backupCapture(rq, info);
  ok(eq(E.b64dec(B.objects.find((o) => o.id === 15).data), mine), "touch backup: a backup without object 15 restores, GLO > TOUCH kept");
  /* a backup made before the LEVER's mode (a 104-byte body, 116 bytes): valid as it is, restored and kept byte for byte
   * (the device reads LATCH: editor_repro 5); this editor's 120-byte one passes an older editor's ok() (it checks the
   * length the object gives, not 116) */
  {
    const o = E.TOUCH.make({ keyTouch: 1, lever: 1 }).slice(0, 116), ov = new DataView(o.buffer);
    ov.setUint16(4, 104, true); ov.setUint32(8, E.crc32(o.subarray(12)), true);
    const F = JSON.parse(JSON.stringify(A)), f15 = F.objects.find((x) => x.id === 15);
    f15.len = o.length; f15.crc = E.crc32(o); f15.data = E.b64enc(o);
    let c0 = "";
    try { await E.backupRestore(rq, F); } catch (e) { c0 = e.code || String(e); }
    B = await E.backupCapture(rq, info);
    ok(c0 === "" && E.TOUCH.ok(o) && eq(E.b64dec(B.objects.find((x) => x.id === 15).data), o),
      "touch backup: an object 15 made before the LEVER's mode (116 bytes) restores");
    const html = readFileSync(join(HERE, "editor.html"), "utf8");
    ok(/TOUCH\.BODY_OFF \+ len === d\.length/.test(html) && !/len === 104|length === 116/.test(html),
      "touch backup: ok() takes the body's own length (an older editor reads a 120-byte object 15)");
    await E.backupRestore(rq, JSON.parse(JSON.stringify(A)));
  }
  /* a backup without KEY AT and PAD AT (a 108-byte body, 120 bytes): valid as it is, restored byte for byte
   * (the device gives the rest its defaults) */
  {
    const o = E.TOUCH.make({ keyTouch: 1 }).slice(0, 120), ov = new DataView(o.buffer);
    ov.setUint16(4, 108, true); ov.setUint32(8, E.crc32(o.subarray(12)), true);
    const F = JSON.parse(JSON.stringify(A)), f15 = F.objects.find((x) => x.id === 15);
    f15.len = o.length; f15.crc = E.crc32(o); f15.data = E.b64enc(o);
    let c0 = "";
    try { await E.backupRestore(rq, F); } catch (e) { c0 = e.code || String(e); }
    B = await E.backupCapture(rq, info);
    ok(c0 === "" && E.TOUCH.ok(o) && eq(E.b64dec(B.objects.find((x) => x.id === 15).data), o),
      "touch backup: an object 15 made before KEY AT / PAD AT (120 bytes) restores");
    await E.backupRestore(rq, JSON.parse(JSON.stringify(A)));
  }
  /* a damaged object 15 (its own CRC wrong, the file's right) is refused by the device */
  const bad = mine.slice(); bad[13] ^= 1;
  await rq(E.req.bkBegin(15, bad.length, E.crc32(bad))); await rq(E.req.bkData(15, 0, bad));
  ok(E.parse[C.BK_PUT](await rq(E.req.bkCommit(15))).rc === 2, "touch backup: object 15 with its body's CRC wrong refused (rc 2)");
  /* a later firmware's object this editor does not know: passed over, not refused */
  const fut = JSON.parse(JSON.stringify(A)), xd = Uint8Array.from([1, 2, 3]);
  fut.objects.push({ id: 99, len: 3, crc: E.crc32(xd), data: E.b64enc(xd) });
  let caught = "";
  try { E.backupObjects(fut); await E.backupRestore(rq, fut); } catch (e) { caught = e.code || String(e); }
  ok(caught === "" && !E.backupObjects(fut).has(99), "touch backup: an object a later firmware adds is passed over");
  done();
  /* a device without it (SLOOP / Clipper / Stallion before): not listed, a Stallion file restores, object 15 skipped */
  const pre = attachMock({});
  const pi = E.parse[C.INFO](await pre.rq(E.req.info()));
  const P = await E.backupCapture(pre.rq, pi);
  ok(!P.objects.some((o) => o.id === 15), "touch backup: a device without object 15 lists none to a new editor");
  caught = "";
  try { await E.backupRestore(pre.rq, JSON.parse(JSON.stringify(A))); } catch (e) { caught = e.code || String(e); }
  ok(caught === "", "touch backup: a file with object 15 restores into a device without it (skipped)");
  pre.done();
}

/* ------------------------------------------- Clipper 0.2: a wavetable for WAVE (editor.html WT) --- */
async function wavetables() {
  /* a Serum-style WAV: two frames of 2048 points, a sine then a saw (16-bit PCM) */
  const N = 2048, x = new Int16Array(2 * N);
  for (let n = 0; n < N; n++) {
    x[n] = Math.round(16000 * Math.sin(2 * Math.PI * n / N));
    let s = 0;
    for (let h = 1; h <= 200; h++) s += Math.sin(2 * Math.PI * h * n / N) / h;
    x[N + n] = Math.round(12000 * s * 2 / Math.PI);
  }
  const body = new Uint8Array(x.buffer), wav = new Uint8Array(44 + body.length), v = new DataView(wav.buffer);
  wav.set([..."RIFF"].map((c) => c.charCodeAt(0)), 0); v.setUint32(4, 36 + body.length, true);
  wav.set([..."WAVEfmt "].map((c) => c.charCodeAt(0)), 8); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 1, true);
  v.setUint32(24, 44100, true); v.setUint32(28, 88200, true); v.setUint16(32, 2, true); v.setUint16(34, 16, true);
  wav.set([..."data"].map((c) => c.charCodeAt(0)), 36); v.setUint32(40, body.length, true); wav.set(body, 44);
  const wt = E.WT.fromWave(E.parseWav(wav.buffer), wav);
  const at = (f, h, k) => wt.spec[(f * E.WT.NH + h - 1) * 2 + k];
  ok(wt.nsrc === 2 && wt.len === 2048, "WT: a Serum-style WAV read as 2 frames of 2048 points");
  ok(Math.abs(at(0, 1, 1) - Math.round(0.9 * 32767)) < 40 && Math.abs(at(0, 2, 1)) < 20 && Math.abs(at(0, 1, 0)) < 20,
    "WT: frame 1 (the sine) is its first harmonic alone, at 0.9");
  ok(Math.abs(at(15, 2, 1) / at(15, 1, 1) - 0.5) < 0.02 && Math.abs(at(15, 3, 1) / at(15, 1, 1) - 1 / 3) < 0.02,
    "WT: frame 16 (the saw): harmonics at 1 / h");
  const data = E.WT.data(wt.spec), hdr = E.WT.header("MYTABLE", data), hv = new DataView(hdr.buffer);
  ok(data.length === 8192 && hdr.length === E.SMP.HDR_LEN && hv.getUint32(0, true) === 0x42545746 && hdr[6] === 16 && hdr[7] === 128 &&
    hv.getUint32(16, true) === 8192 && hv.getUint32(20, true) === E.crc32(data), "WT: the FWTB header (16 x 128, 8192 bytes, CRC)");
  /* through the mock: BEGIN, WRITE x 32, END -> SMP_INFO says 127 zones (a wavetable) and its name */
  const m = E.makeMockDevice({ clipper: true });
  const inp = [...m.access.inputs.values()][0], out = [...m.access.outputs.values()][0];
  const link = new E.Link((d) => out.send(d), { timeout: 2000 });
  inp.onmidimessage = (e) => link.receive(e.data);
  const rq = async (r, o) => link.request(r, o);
  let r = E.parse[E.CMD.SMP_BEGIN](await rq(E.req.smpBegin(1)));
  for (let off = 0; off < data.length && !r.rc; off += 256) r = E.parse[E.CMD.SMP_WRITE](await rq(E.req.smpWrite(1, E.SMP.DATA_OFF + off, data.subarray(off, off + 256))));
  const end = E.parse[E.CMD.SMP_END](await rq(E.req.smpEnd(1, hdr)));
  const info = E.parse[E.CMD.SMP_INFO](await rq(E.req.smpInfo()));
  ok(!r.rc && !end.rc && info.slots[1].zones === E.WT.ZONES && info.slots[1].name === "MYTABLE", "WT: sent to USR2 (the mock): 127 zones, MYTABLE");
  const bad = E.WT.header("X", data); bad[7] = 64;
  E.parse[E.CMD.SMP_BEGIN](await rq(E.req.smpBegin(2)));
  const e2 = E.parse[E.CMD.SMP_END](await rq(E.req.smpEnd(2, bad)));
  ok(e2.rc === 2, "WT: a wrong header refused (rc 2)");
}

/* ------------------------------------- Clipper: the HYBRID patch (v11), editor.html HY --- */
function hybridPatch() {
  const H = E.HY;
  /* the factory patches as the firmware has them (tests/hybrid_editor_test.c writes them; skipped without it) */
  const fp = join(HERE, "../build/host/hy_factory.json");
  if (!existsSync(fp)) { console.log("HYBRID: skipped (run tests/hybrid_editor_test first)"); return; }
  const fx = JSON.parse(readFileSync(fp, "utf8"));
  ok(fx.size === H.SIZE && fx.factory.length === H.NFACTORY, "HYBRID: 128 bytes a patch, as many factory patches as the firmware");
  ok(fx.factory.every((b) => eq(Array.from(H.pack(H.unpack(Uint8Array.from(b)))), b)), "HYBRID: every factory patch unpacks and packs back byte for byte");
  const c = H.unpack(Uint8Array.from(fx.factory[fx.names.indexOf("STALLION")]));
  ok(c.name === "STALLION" && H.OSC[c.osc[0].type] === "SUPER" && c.osc[2].coarse === -12 && c.osc[2].reset && H.FLT[c.flt[0].type] === "LP24"
    && H.SRC[c.mod[0].src] === "M1" && H.DST[c.mod[0].dst] === "CUT" && c.mod[0].amt === 50 && H.SRC[c.mod[3].via] === "MW",
    "HYBRID: STALLION reads as hybrid_patches.h writes it (signed values, flags, the matrix)");
  ok(fx.names.every((n, i) => H.unpack(Uint8Array.from(fx.factory[i])).name === n), "HYBRID: every name");
  ok(H.MOCK_FACTORY.length === fx.factory.length && H.MOCK_FACTORY.every((b, i) => eq(Array.from(b), fx.factory[i])) && eq(H.MOCK_NAMES, fx.names),
    "HYBRID: the mock's factory patches are the firmware's");
  const junk = H.pack({ osc: [{ type: 99, coarse: -100, fine: 200, level: 300 }], route: 7, flt: [{ type: -1, env: -99 }],
    mod: [{ src: 50, via: 2, dst: 99, amt: 100 }], bend: 99, name: "naïve name too long" });
  const j = H.unpack(junk);
  ok(j.osc[0].type === 0 && j.osc[0].coarse === -48 && j.osc[0].fine === 63 && j.osc[0].level === 127 && j.route === 0 && j.flt[0].env === -64
    && j.mod[0].src === 0 && j.mod[0].dst === 0 && j.mod[0].amt === 63 && j.bend === 24 && j.name === "NA VE NAME", "HYBRID: values out of range are put into range");
  const r = E.req.hyPut(2, H.pack(c));
  ok(r[0] === E.CMD.HY_PUT && r[1][0] === 0 && r[1][1] === 2 && r[1].length === 2 + 147 && r[1].every((x) => x < 128)
    && eq(Array.from(E.unpack7(Uint8Array.from(r[1].slice(2)))).slice(0, 128), fx.factory[fx.names.indexOf("STALLION")]), "HYBRID: HY_PUT is pack7 of the 128 bytes, 7-bit clean");
  const g = E.parse[E.CMD.HY_GET]([1, 3, 0, ...E.pack7(Uint8Array.from(fx.factory[3]))]);
  ok(g.target === 1 && g.index === 3 && !g.rc && eq(Array.from(g.bytes), fx.factory[3]), "HYBRID: HY_GET parses back");
  ok(H.cutText(0) === "30" && H.cutText(127) === "16k" && H.timeText(0) === "1ms" && H.rateText({ rate: 55, sync: true }) === "1/4",
    "HYBRID: readable values (cutoff, times, a synced rate)");
}

/* ------------------------------------- Clipper 0.6: the FMX patch (v12), editor.html FX --- */
async function fmxPatch() {
  const X = E.FX;
  const fp = join(HERE, "../build/host/fmx_factory.json");
  if (!existsSync(fp)) { console.log("FMX: skipped (run tests/fmx_test first)"); return; }
  const fx = JSON.parse(readFileSync(fp, "utf8"));
  ok(fx.size === X.SIZE && fx.factory.length === X.NFACTORY && eq(fx.waves, X.WAVE), "FMX: 128 bytes a patch, as many factory patches as the firmware, the waves as eng_fmx.c names them");
  ok(fx.alg.length === X.ALG.length && fx.alg.every((r, i) => eq(r, X.ALG[i])), "FMX: the 40 templates == fmx_tables.h");
  ok(fx.factory.every((b) => eq(Array.from(X.pack(X.unpack(Uint8Array.from(b)))), b)), "FMX: every factory patch unpacks and packs back byte for byte");
  const ep = X.unpack(Uint8Array.from(fx.factory[0])), init = X.unpack(Uint8Array.from(fx.factory[fx.factory.length - 1]));
  ok(ep.name === "FMX EP" && ep.op[1].coarse === 14 && ep.mod[0][1] === 100 && ep.out[0] === 100 && ep.alg === 5 && init.name === "INIT" && init.peg === 64
    && X.unpack(Uint8Array.from(fx.factory[11])).op[0].wave === 7, "FMX: FMX EP, INIT and PULSE CLAV read as eng_fmx.c writes them");
  const a = X.applyAlg(X.init(), 1);
  ok(a.mod[0][1] === 100 && a.mod[5][5] === 60 && a.mod[2][3] === 100 && a.out[0] === 110 && a.out[2] === 100 && !a.out[1] && a.alg === 1,
    "FMX: template 1 fills the routing (2 > 1, 6 > 5 > 4 > 3, 6's feedback; carriers 1 and 3, an output level there kept)");
  const j = X.unpack(X.pack({ op: [{ wave: 99, mode: 7, coarse: 99, fine: 500, dtn: 40 }], mod: [[300]], out: [-5], alg: 77, name: "toolongname!" }));
  ok(j.op[0].wave === X.WAVE.length - 1 && j.op[0].mode === 2 && j.op[0].coarse === 31 && j.op[0].fine === 99 && j.op[0].dtn === 14
    && j.mod[0][0] === 127 && j.out[0] === 0 && j.alg === 40 && j.name === "toolongnam", "FMX: values out of range are put into range");
  const r = E.req.fmxStore(4, X.pack(ep));
  ok(r[0] === E.CMD.FMX_STORE && r[1][0] === 4 && r[1].every((x) => x < 128) && eq(Array.from(E.unpack7(Uint8Array.from(r[1].slice(1)))).slice(0, 128), fx.factory[0]),
    "FMX: FMX_STORE is the slot then pack7 of the 128 bytes, 7-bit clean");
  /* through the mock (Clipper 0.6: protocol 12; the Stallion mock 15): put, get back, store, read the slot */
  const m = E.makeMockDevice({ clipper: true });
  const inp = [...m.access.inputs.values()][0], out = [...m.access.outputs.values()][0];
  const link = new E.Link((d) => out.send(d), { timeout: 2000 });
  inp.onmidimessage = (e) => link.receive(e.data);
  const rq = async (q) => link.request(q);
  const info = E.parse[E.CMD.INFO](await rq(E.req.info()));
  const put = E.parse[E.CMD.FMX_PUT](await rq(E.req.fmxPut(0, X.pack(ep))));
  const got = E.parse[E.CMD.FMX_GET](await rq(E.req.fmxGet(0, 0)));
  const empty = E.parse[E.CMD.FMX_GET](await rq(E.req.fmxGet(2, 6)));
  const st = E.parse[E.CMD.FMX_STORE](await rq(E.req.fmxStore(6, X.pack(X.unpack(Uint8Array.from(fx.factory[10]))))));
  const back = E.parse[E.CMD.FMX_GET](await rq(E.req.fmxGet(2, 6)));
  ok(info.proto === 15 && info.engines.includes("FMX") && !put.rc && eq(Array.from(got.bytes), fx.factory[0]) && empty.rc === 2 && !st.rc
    && eq(Array.from(back.bytes), fx.factory[10]), "FMX: through the mock: PUT, GET back, an empty slot (rc 2), STORE, the slot read");
  /* Clipper 0.8 (protocol 13): PONY and VECTOR, the MOD SEQ's steps (editor_fmx.c ed_mseq_handle) */
  const q = E.req.mseqPut(1, Array.from({ length: 64 }, (_, i) => (i % 16) - 8));
  ok(q[0] === E.CMD.MSEQ_PUT && q[0] === 94 && q[1].length === 65 && q[1][0] === 1 && q[1][1] === 0 && q[1][16] === 15 && q[1].every((x) => x < 128),
    "MOD SEQ: MSEQ_PUT is the part, then 64 steps + 8 (0..15)");
  const g0 = E.parse[E.CMD.MSEQ_GET](await rq(E.req.mseqGet(0)));
  const p1 = E.parse[E.CMD.MSEQ_PUT](await rq(q));
  const g1 = E.parse[E.CMD.MSEQ_GET](await rq(E.req.mseqGet(1)));
  const bad = E.parse[E.CMD.MSEQ_GET](await rq(E.req.mseqGet(5)));
  ok(info.engines.includes("PONY") && info.engines.includes("VECTOR") && !g0.rc && g0.steps.length === 64 && g0.steps[0] === -8 && g0.steps[15] === 7
    && !p1.rc && !g1.rc && g1.steps[16] === -8 && g1.steps[31] === 7 && bad.rc === 1,
    "MOD SEQ through the mock: PONY, VECTOR listed; the shapes to start from; PUT, GET back; a part out of range (rc 1)");
  /* Clipper 0.9 (protocol 14): the TUNING (editor_fmx.c ED_TUNE_GET / ED_TUNE_PUT) and Scala .scl files */
  const scl = E.parseScl("! test.scl\n!\nQuarter-tone test\n 5\n!\n 150.0\n 6/5\n 500.\n 3/2 comment\n 2/1\n");
  ok(scl && scl.desc === "Quarter-tone test" && scl.deg.length === 5 && scl.deg[0] === 0 && scl.deg[1] === 2400
    && scl.deg[2] === 5050 && scl.deg[3] === 8000 && scl.deg[4] === 11231 && scl.period === 19200,
    ".scl: description, count, cents and ratios (1/16 cent), the period the last line");
  ok(!E.parseScl("x\n 3\n 100.0\n 50.0\n 2/1\n") && !E.parseScl("x\n 2\n 100.0\n") && !E.parseScl("x\n 49\n" + " 10.0\n".repeat(49))
    && E.parseScl("x\n 1\n 3/1\n").period === 30431, ".scl: falling, short or too long refused; 1 note: the period alone (tritave)");
  const tq = E.req.tunePut(13, 2, scl.deg, scl.period);
  ok(tq[0] === E.CMD.TUNE_PUT && tq[0] === 96 && tq[1].length === 6 + 3 * 4 && tq[1].every((x) => x < 128), "TUNING: TUNE_PUT is sys, root, n, period, n-1 degrees (21 bits each)");
  const t0 = E.parse[E.CMD.TUNE_GET](await rq(E.req.tuneGet()));
  const tp = E.parse[E.CMD.TUNE_PUT](await rq(tq));
  const t1 = E.parse[E.CMD.TUNE_GET](await rq(E.req.tuneGet()));
  const tb = E.parse[E.CMD.TUNE_PUT](await rq(E.req.tunePut(1, 0, [0, 3000, 2000], 19200)));
  ok(!t0.rc && t0.sys === 0 && t0.deg.length === 12 && t0.deg[11] === 17600 && t0.period === 19200 && !tp.rc && !t1.rc && t1.sys === 13 && t1.root === 2
    && t1.deg.join() === scl.deg.join() && t1.period === 19200 && tb.rc === 1,
    "TUNING through the mock: 12-EDO to start; PUT a .scl, GET it back; degrees not rising refused (rc 1)");
  {   /* Clipper 1.0: A4 in Hz (one more byte both ways; without it, A4 as it is) */
    const p432 = E.parse[E.CMD.TUNE_PUT](await rq(E.req.tunePut(0, 0, [0, 1600, 3200, 4800, 6400, 8000, 9600, 11200, 12800, 14400, 16000, 17600], 19200, 432)));
    const g1 = E.parse[E.CMD.TUNE_GET](await rq(E.req.tuneGet()));
    const p2 = E.parse[E.CMD.TUNE_PUT](await rq(E.req.tunePut(1, 0, [0, 1600, 3200, 4800, 6400, 8000, 9600, 11200, 12800, 14400, 16000, 17600], 19200)));
    const g2 = E.parse[E.CMD.TUNE_GET](await rq(E.req.tuneGet()));
    const fc = readFileSync(join(HERE, "../firmware/src/editor_fmx.c"), "utf8");
    ok(t0.a4 === 440 && !p432.rc && g1.a4 === 432 && g1.sys === 0 && !p2.rc && g2.a4 === 432 && g2.sys === 1
      && E.req.tunePut(0, 0, [0], 19200, 415)[1].length === 7 && E.req.tunePut(0, 0, [0], 19200)[1].length === 6
      && /ed_b\(\(uint32_t\)\(clamp\(tune_cfg\.a4, -40, 40\) \+ 40\)\)/.test(fc),
      "TUNING 1.0: A4 in Hz (432 sent, read back; a PUT without it keeps it; the firmware's GET ends with it)");
  }
}

/* ------------------------------------- editor v5 (SLOOP 2.0): lanes, levels, ratchets --- */
async function editorV5() {
  const C = E.CMD;
  const { m, rq, ev, done } = attachMock({ watchMs: 1000 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  ok(info.proto === 10 && /SLOOP/.test(info.version) && info.pcount === 61 && info.gcount === 33 && info.pe0 === 53, "v5..v10: INFO ends with the protocol version (10: SYN kits); 33 globals (2.5: drum DLY)");
  /* the firmware says the same: ED_DRUM_STEP is command 33, INFO sends 5, P_CHORD / the master globals as the mock has them */
  const ec = readFileSync(join(HERE, "../firmware/src/editor.c"), "utf8"), pc = readFileSync(join(HERE, "../firmware/src/params.c"), "utf8");
  const en = (/enum \{ ED_INFO = 1,([^}]*)\}/.exec(ec) || [])[1] || "";
  const names = ["ED_INFO", ...en.replace(/\/\*[^*]*\*\//g, "").split(",").map((x) => x.trim()).filter(Boolean)];
  ok(names.indexOf("ED_DRUM_STEP") + 1 === C.DRUM_STEP && names.indexOf("ED_TRACK_CHANGED") + 1 === C.TRACK_CHANGED
    && names.indexOf("ED_BK_LIST") + 1 === C.BK_LIST && names.indexOf("ED_BK_PUT") + 1 === C.BK_PUT
    && names.indexOf("ED_LOCK_GET") + 1 === C.LOCK_GET && names.indexOf("ED_LOCK_SET") + 1 === C.LOCK_SET
    && names.indexOf("ED_MICRO_GET") + 1 === C.MICRO_GET && names.indexOf("ED_MICRO_SET") + 1 === C.MICRO_SET
    && names.indexOf("ED_FILL_GET") + 1 === C.FILL_GET && names.indexOf("ED_FILL_SET") + 1 === C.FILL_SET
    && /ED_FM6_GET = 68, ED_FM6_PUT, ED_FM6_LIST, ED_FM6_ERASE/.test(ec) && C.FM6_GET === 68 && C.FM6_ERASE === 71
    && /#define ED_PROTO \(CLIPPER_SYNTH \? 15u : 11u\)/.test(ec) && /ed_b\(ED_PROTO\);/.test(ec)
    && /enum \{ ED_FMX_GET = 90, ED_FMX_PUT, ED_FMX_STORE \};/.test(readFileSync(join(HERE, "../firmware/src/editor_fmx.c"), "utf8"))
    && C.FMX_GET === 90 && C.FMX_STORE === 92
    && /enum \{ ED_MSEQ_GET = 93, ED_MSEQ_PUT, ED_TUNE_GET, ED_TUNE_PUT \};/.test(readFileSync(join(HERE, "../firmware/src/editor_fmx.c"), "utf8"))
    && C.MSEQ_GET === 93 && C.MSEQ_PUT === 94 && C.TUNE_GET === 95 && C.TUNE_PUT === 96
    && /enum \{ ED_HY_GET = 77, ED_HY_PUT \};/.test(readFileSync(join(HERE, "../firmware/src/editor_hybrid.c"), "utf8")) && C.HY_GET === 77 && C.HY_PUT === 78
    && /enum \{ ED_DSYN_LIST = 72, ED_DSYN_GET, ED_DSYN_PUT, ED_DSYN_STORE, ED_DSYN_PLAY \};/.test(readFileSync(join(HERE, "../firmware/src/editor_dsyn.c"), "utf8"))
    && C.DSYN_LIST === 72 && C.DSYN_PLAY === 76, "v5..v13: command numbers and INFO == editor.c / editor_dsyn.c / editor_hybrid.c / editor_fmx.c");
  const enumNames = (id) => (new RegExp(`${id}\\[\\] = \\{([^}]*)\\}`).exec(pc) || [])[1].split(",").map((x) => x.trim().replace(/"/g, ""));
  const chord = E.parse[C.DESC](await rq(E.req.desc(0, 49)));
  const gd = [];
  for (let i = 27; i < 32; i++) gd.push(E.parse[C.DESC](await rq(E.req.desc(1, i))));
  ok(chord.label === "CHORD" && chord.names.join() === enumNames("N_CHORD").join() && /\[P_CHORD\] = PE\("CHORD", N_CHORD, 0\)/.test(pc)
    && gd.map((d) => d.label).join() === "DUST,DUCK,FILT,ROLL,NEW" && gd[3].names.join() === enumNames("N_ROLL").join() && gd[3].def === 1
    && /\[G_DUST\] = PD\("DUST", F_PCT, 0, 127, 0\)/.test(pc) && /\[G_FILT\] = PD\("FILT", F_FILT, -64, 63, 0\)/.test(pc)
    && /\[G_ROLL\] = PE\("ROLL", N_ROLL, 1\)/.test(pc) && /\[G_NEWPRJ\] = PE\("NEW", N_GO, 0\)/.test(pc),
    "v5: CHORD, DUST, DUCK, FILT, ROLL, NEW (mock == params.c)");
  /* the value formats (params.c fmt_value) */
  const fv = (fmt, v, max = 127, min = 0) => E.fmtValue({ fmt, min, max, names: [] }, v).join("");
  ok(fv(E.F.SWING, 0, 100) === "0" && fv(E.F.SWING, 50, 100) === "50" && fv(E.F.SWING, 100, 100) === "100"
    && fv(E.F.FILT, 0, 63, -64) === "OFF" && fv(E.F.FILT, -64, 63, -64) === "LP100%" && fv(E.F.FILT, -32, 63, -64) === "LP50%" && fv(E.F.FILT, 63, 63, -64) === "HP100%"
    && fv(E.F.PCT, 127) === "100%" && fv(E.F.PCT, 64) === "50%" && fv(E.F.PCT, 60, 120) === "50%", "v5: SWING 0..100 (2.5), FILT LP / OFF / HP, PCT of the range");
  /* a synth step keeps its levels and ratchets; an old-style write clears them (as the firmware) */
  const w = E.parse[C.STEP_SET](await rq(E.req.stepSet(3, { n: 2, notes: [60, 67, 0, 0], time: 0, flags: 0, vel: 100, lvl: 0b11000110, rat: 0b10000011 })));
  const g = E.parse[C.STEP_GET](await rq(E.req.stepGet(3)));
  const w2 = E.parse[C.STEP_SET](await rq(E.req.stepSet(3, { n: 1, notes: [62, 0, 0, 0], time: 0, flags: 0, vel: 90 })));
  ok(w.lvl === 0b11000110 && g.lvl === 0b11000110 && g.rat === 0b10000011 && w2.lvl === 0 && w2.rat === 0 && E.req.stepSet(1, { n: 0, notes: [], time: 2, flags: 0, vel: 0 })[1].length === 9,
    "v5: STEP_SET / STEP_GET level and ratchet bytes (8 bits each), none for an old step");
  /* (1.0) x5..x8: flags bit 2 + bits 3..6, kept through STEP_SET / STEP_GET; without bit 2 only ACC and SLIDE */
  const w8 = E.parse[C.STEP_SET](await rq(E.req.stepSet(4, { n: 2, notes: [60, 64, 0, 0], time: 0, flags: 1 | 4 | 8 | 16, vel: 100, lvl: 0, rat: 0b0111 })));
  const g8 = E.parse[C.STEP_GET](await rq(E.req.stepGet(4)));
  const w9 = E.parse[C.STEP_SET](await rq(E.req.stepSet(4, { n: 1, notes: [60, 0, 0, 0], time: 0, flags: 2 | 8, vel: 100, lvl: 0, rat: 0 })));
  ok(w8.flags === (1 | 4 | 8 | 16) && g8.flags === w8.flags && g8.rat === 0b0111 && w9.flags === 2
    && E.req.stepSet(4, { n: 1, notes: [60, 0, 0, 0], time: 0, flags: 0x7F, vel: 1 })[1].every((b) => b >= 0 && b < 128),
    "1.0: ratchet x5..x8 (flags 4 + a bit per note) through STEP_SET / STEP_GET; without bit 4 only ACC and SLIDE stay");
  /* the drum track: 16 lanes with their level and ratchet; lane 15 uses the top bits */
  const d = E.emptyDrum();
  d.on = (1 << 0) | (1 << 4) | (1 << 15); d.lvl[0] = 3; d.lvl[4] = 1; d.rat[4] = 3; d.lvl[15] = 2; d.rat[15] = 1;
  const ds = E.parse[C.DRUM_STEP](await rq(E.req.drumStep(5, d)));
  const dg = E.parse[C.DRUM_STEP](await rq(E.req.drumStep(5)));
  ok(ds.index === 5 && dg.on === d.on && dg.lvl.join() === d.lvl.join() && dg.rat.join() === d.rat.join()
    && E.req.drumStep(5, d)[1].every((b) => b >= 0 && b < 128) && E.req.drumStep(5, d)[1].length === 14, "v5: DRUM_STEP round trip (16 lanes, levels, ratchets)");
  /* old commands see the lanes as GM notes (the first four, ACC when one is hard); an old write lands on lanes */
  const old = E.parse[C.TRACK_STEP](await rq(E.req.trackStep(3, 5)));
  await rq(E.req.trackStep(3, 6, { n: 2, notes: [38, 46, 0, 0], time: 0, flags: 1, vel: 100 }));
  const d6 = E.parse[C.DRUM_STEP](await rq(E.req.drumStep(6)));
  ok(old.n === 3 && old.notes.slice(0, 3).join() === "36,42,56" && old.flags === 1 && d6.on === ((1 << 2) | (1 << 5)) && d6.lvl[2] === 3 && d6.lvl[5] === 3,
    "v5: the drum track through TRACK_STEP: lanes <-> GM notes");
  ok(E.DRUM_LANES.length === 16 && E.DRUM_LANES[0][0] === 36 && E.DRUM_LANES[15][0] === 56 && E.LV_NAMES.join() === "NORM,GHOST,SOFT,HARD",
    "v5: the 16 lanes (kick .. cowbell), 4 levels");
  const dc = readFileSync(join(HERE, "../firmware/src/drums.c"), "utf8");
  const lanes = ((/LANE_NOTE\[DRUM_LANES\] = \{([^}]*)\}/.exec(dc) || [])[1] || "").split(",").map((x) => +x);
  ok(lanes.join() === E.DRUM_LANES.map((x) => x[0]).join(), "v5: lane notes == drums.c LANE_NOTE");
  /* the kit: the drum track's P_E0 */
  await rq(E.req.track(3));
  const kit = E.parse[C.DESC](await rq(E.req.desc(0, info.pe0)));
  const dk = (/DRUM_KIT_NAMES\[\] = \{([^}]*)\}/.exec(dc) || [])[1] || "";
  const gen = join(HERE, "../build/gen/felucca_drumkits.h");
  const dsList = existsSync(gen) ? ((/#define DS_KIT_NAME_LIST (.*)/.exec(readFileSync(gen, "utf8")) || [])[1] || "") : null;
  const fwKits = dsList == null ? null : dk.replace("DS_KIT_NAME_LIST", dsList).split(",").map((x) => x.trim().replace(/"/g, ""));
  ok(kit.label === "KIT" && kit.names[5] === "THUMP" && kit.names.length === kit.max + 1 && (!fwKits || fwKits.join() === kit.names.join()),
    `v5: the drum track's KIT (${kit.names.length} kits${fwKits ? ", == drums.c" : ""})`);
  /* TRACK ends with the solo mask */
  m.state.solo = 0b0101;
  const tr = E.parse[C.TRACK](await rq(E.req.track()));
  ok(tr.solo === 5 && tr.sel === 3 && tr.tracks.length === 4, "v5: TRACK reports the soloed tracks");
  ok(!ev.unknown.length, "v5: no unmatched replies");
  done();
  /* firmware 0.8 (v3): no DRUM_STEP, no protocol byte, steps without the extra bytes */
  const o = attachMock({ v3: true });
  const oi = E.parse[C.INFO](await o.rq(E.req.info()));
  const os = E.parse[C.STEP_GET](await o.rq(E.req.stepGet(0)));
  const nd = await o.rq(E.req.drumStep(0), { timeout: 60, retries: 0, quiet: true }).then(() => "reply", () => "none");
  ok(oi.proto === 0 && os.lvl === undefined && nd === "none", "v5: older firmware -> proto 0, no DRUM_STEP (the editor keeps GM notes)");
  o.done();
}

/* ----------------------------- editor protocol v7: nudges and parameter locks (SLOOP 2.4) --- */
async function editorV7() {
  const C = E.CMD;
  const { m, rq, ev, done } = attachMock({ watchMs: 1000 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  ok(info.proto === 10, "v7: INFO ends with 10 (v10 includes v7)");
  /* the lockable set == seq.c p_lockable (the ids as core.h names them, the groovebox's: Clipper 0.4's LFO 2, ENV 2 and
     matrix, #if CLIPPER_SYNTH, move P_E0 to 78: lockable(id, 78)) */
  const sc = readFileSync(join(HERE, "../firmware/src/seq.c"), "utf8"), ch = readFileSync(join(HERE, "../firmware/src/core.h"), "utf8");
  const ids = {}; let n = 0;
  for (const nm of (/enum \{\s*\/\* per-track parameters \*\/([^}]*)\}/.exec(ch) || [])[1].replace(/#if CLIPPER_SYNTH[\s\S]*?#endif/g, "").replace(/\/\*[\s\S]*?\*\//g, "").split(",").map((x) => x.trim()).filter(Boolean)) ids[nm] = n++;
  const lockableC = (id) => id <= ids.P_LD_AMP || id === ids.P_SGATE || (id >= ids.P_DIST && id <= ids.P_REV) || id === ids.P_GLIDE || id === ids.P_PAN
    || id === ids.P_DETUNE || (id >= ids.P_SLCR && id <= ids.P_SLDEPTH) || (id >= ids.P_E0 && id <= ids.P_E7) || id === ids.P_TFLT;
  let same = ids.P_COUNT === 61;
  for (let id = 0; id < 61; id++) same = same && !!E.lockable(id) === !!lockableC(id);
  ok(same && /id <= P_LD_AMP \|\| id == P_SGATE \|\| \(id >= P_DIST && id <= P_REV\) \|\| id == P_GLIDE \|\| id == P_PAN/.test(sc)
    && !E.lockable(ids.P_SLEN) && !E.lockable(ids.P_AMODE) && !E.lockable(ids.P_VOICE) && !E.lockable(ids.P_CHORD) && E.lockable(ids.P_E7),
    "v7: the lockable parameters (editor == seq.c p_lockable; LEN, ARP, VOICE, CHORD are not)");
  {   /* Clipper 0.4 (core.h with CLIPPER_SYNTH): LFO 2 at 53, ENV 2 at 56, the matrix at 60, P_E0 78, P_COUNT 86 (editor
         EXPECT_04, LAYOUT, MX_PAGES); its EDIT values lockable, the 25 new ones not (seq.c p_lockable) */
    const sy = {}; let k = 0;
    for (const nm of (/enum \{\s*\/\* per-track parameters \*\/([^}]*)\}/.exec(ch) || [])[1].replace(/#(if|endif)[^\n]*/g, "").replace(/\/\*[\s\S]*?\*\//g, "").split(",").map((x) => x.trim()).filter(Boolean)) sy[nm] = k++;
    ok(sy.P_L2RATE === 53 && sy.P_E2ATK === 56 && sy.P_M1SRC === 60 && sy.P_M6AMT === 77 && sy.P_ITYPE === 78 && sy.P_IMIX === 82
      && sy.P_MSRATE === 83 && sy.P_MSKEY === 86 && sy.P_SPICE === 87 && sy.P_DICE === 88 && sy.P_VINT === 89 && sy.P_SLOOP === 90
      && sy.P_SPREAD === 91 && sy.P_L3RATE === 92 && sy.P_E2LOOP === 95 && sy.P_MAC1 === 96 && sy.P_MAC4 === 99 && sy.P_M7SRC === 100
      && sy.P_M12AMT === 117 && sy.P_PTGT === 118 && sy.P_FSLOPE === 119 && sy.P_F2MODEL === 120 && sy.P_F2LINK === 124
      && /#define P_EDH0 \(\(uint32_t\)P_FSLOPE\)/.test(ch)
      && sy.P_E0 - (sy.P_E0 - sy.P_FSLOPE) === 119 && sy.P_COUNT - (sy.P_E0 - sy.P_FSLOPE) === 127   /* (the protocol's: the
         device's own values from P_FSLOPE on passed over, core.h P_EDH0) */
      && E.lockable(119, 119) && E.lockable(126, 119) && !E.lockable(sy.P_L2RATE, 119) && !E.lockable(sy.P_M6AMT, 119)
      && !E.lockable(sy.P_ITYPE, 119) && !E.lockable(sy.P_IMIX, 119) && !E.lockable(sy.P_MSRATE, 119) && !E.lockable(sy.P_DICE, 119)
      && !E.lockable(sy.P_SLOOP, 119) && !E.lockable(sy.P_SPREAD, 119) && !E.lockable(sy.P_MAC1, 119) && !E.lockable(sy.P_M12AMT, 119)
      && !E.lockable(sy.P_PTGT, 119),
      "v7: Clipper's layout (LFO 2 53, ENV 2 56, MATRIX 60, INSERT 78, 0.8's MOD SEQ 83, SPICE 87, 0.9's VINTAGE 89 LOOP 90, 1.0's SPREAD 91, LFO 3 92, ENV 2 LOOP 95, MACROS 96, slots 7..12 100, then P>VCO 118, P_E0 119; SLOPE and after the device's own): its EDIT values lockable, the rest not");
  }
  {   /* (after SLOOP 2.6) SetGate: a value set again is sent once the last request is answered */
    const g = new E.SetGate();
    const a = g.user("0:30", 6, false, 1), b = g.user("0:30", 6, false, 2);
    g.get("0:30").sent = 6;
    const c = g.user("0:30", 6, true, 3), d = g.user("0:30", 7, true, 4), e = g.user("0:30", 6, false, 5);
    g.clear();
    ok(a && b && !c && d && e && g.get("0:30") === undefined,
      "SetGate: the same value skipped only while its request is pending; answered, sent again; clear() forgets");
  }
  {   /* patchByCount: SLOOP 2.6 (71, pE0 63) / 2.5 (61, 53) / Clipper 0.4 (86, 78) */
    const s26 = Array.from({ length: 71 }, (_, i) => i), c04 = Array.from({ length: 86 }, (_, i) => 1000 + i);
    const a = E.patchByCount(s26, 63, 86, 78), b = E.patchByCount(c04, 78, 71, 63), c = E.patchByCount(c04, 78, 86, 78);
    const d = E.patchByCount(s26.slice(0, 61), undefined, 86, 78);
    ok(a[52] === 52 && a[53] === null && a[77] === null && a[78] === 63 && a[85] === 70
      && b[52] === 1052 && b[53] === null && b[63] === 1078 && b[70] === 1085 && c[60] === 1060 && d[78] === 53 && d[77] === null,
      "patchByCount: the 53 shared values by place, the engine's 8 by theirs; SLOOP 2.6's and Clipper's own left out");
    const e = E.patchByCount(c04, 78, 91, 83), f = E.patchByCount(Array.from({ length: 91 }, (_, i) => 2000 + i), 83, 86, 78);
    ok(e[77] === 1077 && e[78] === null && e[82] === null && e[83] === 1078 && e[90] === 1085
      && f[77] === 2077 && f[78] === 2083 && f[85] === 2090,
      "patchByCount: Clipper 0.4 / 0.5 <-> 0.6: LFO 2, ENV 2, the matrix by place, 0.6's INSERT left as the device has it");
  }
  {   /* the backup check: a SLOOP 2.6 project (FUN6) into Clipper 0.4 stops before writing; 0.4.1 reads it */
    const f6 = Uint8Array.of(0x36, 0x4E, 0x55, 0x46), f5 = Uint8Array.of(0x35, 0x4E, 0x55, 0x46);
    ok(E.bkProjFormat(f6) === 6 && E.bkProjFormat(f5) === 5 && E.bkProjFormat(Uint8Array.of(1, 2, 3, 4)) === 0
      && E.bkDevFormat({ version: "CLIPPER 0.4", pcount: 86 }) === 5 && E.bkDevFormat({ version: "CLIPPER 0.4.1", pcount: 86 }) === 6
      && E.bkDevFormat({ version: "SLOOP 2.6", pcount: 71 }) === 6 && E.bkDevFormat({ version: "FELUCCA SLOOP 2.5", pcount: 61 }) === 5,
      "backup: the project format each firmware reads (Clipper 0.4.1 reads SLOOP 2.6's FUN6)");
  }
  /* MICRO: 64 bytes offset by 64; a set clamps to -32..31 */
  const mg = E.parse[C.MICRO_GET](await rq(E.req.microGet(0)));
  ok(mg.track === 0 && mg.micro.length === 64 && mg.micro.every((x) => x === 0), "v7: MICRO_GET: 64 nudges, all 0");
  const ms = E.parse[C.MICRO_SET](await rq(E.req.microSet(0, 4, -8)));
  const ms2 = E.parse[C.MICRO_SET](await rq(E.req.microSet(0, 5, 100)));
  const ms3 = E.parse[C.MICRO_SET](await rq(E.req.microSet(0, 6, -100)));
  const mg2 = E.parse[C.MICRO_GET](await rq(E.req.microGet(0)));
  ok(ms.step === 4 && ms.micro === -8 && ms2.micro === 31 && ms3.micro === -32 && mg2.micro[4] === -8 && mg2.micro[5] === 31 && mg2.micro[6] === -32
    && E.req.microSet(0, 4, -8)[1].join() === "0,4,56" && E.req.microSet(0, 4, 31)[1][2] === 95 && E.req.microSet(0, 4, -32)[1][2] === 32,
    "v7: MICRO_SET (sent + 64: -8 -> 56, 31 -> 95, -32 -> 32), clamped to -32..31");
  /* LOCK: set, move, list, delete; refused on a non-lockable parameter, bounded */
  const l1 = E.parse[C.LOCK_SET](await rq(E.req.lockSet(0, 2, ids.P_E0, 1)));
  const l2 = E.parse[C.LOCK_SET](await rq(E.req.lockSet(0, 2, ids.P_DIST, 500)));
  const l3 = E.parse[C.LOCK_SET](await rq(E.req.lockSet(0, 2, ids.P_SLEN, 8)));
  const l4 = E.parse[C.LOCK_SET](await rq(E.req.lockSet(0, 70, ids.P_E0, 1)));
  const lg = E.parse[C.LOCK_GET](await rq(E.req.lockGet(0)));
  ok(l1.rc === 0 && l1.has === 1 && l1.value === 1 && l2.rc === 0 && l2.value === 127 && l3.rc === 2 && !l3.has && l3.value === null && l4.rc === 1
    && lg.track === 0 && lg.locks.length === 2 && lg.locks[0].step === 2 && lg.locks[0].param === ids.P_E0 && lg.locks[1].value === 127
    && E.req.lockSet(0, 2, 5, -30)[1].length === 5 && E.req.lockSet(0, 2, 5)[1].length === 3,
    "v7: LOCK_SET makes and clamps a lock (DIST 500 -> 127), refuses LEN (rc 2) and step 70 (rc 1); LOCK_GET lists them");
  const l5 = E.parse[C.LOCK_SET](await rq(E.req.lockSet(0, 2, ids.P_E0, 3)));
  const l6 = E.parse[C.LOCK_SET](await rq(E.req.lockSet(0, 2, ids.P_E0)));
  const lg2 = E.parse[C.LOCK_GET](await rq(E.req.lockGet(0)));
  ok(l5.value === 3 && l6.rc === 0 && l6.has === 0 && lg2.locks.length === 1 && lg2.locks[0].param === ids.P_DIST,
    "v7: LOCK_SET again moves the lock; without a value it deletes it");
  for (let i = 0; i < 23; i++) await rq(E.req.lockSet(0, i, ids.P_LEVEL, 100));   /* 24 in all */
  const full = E.parse[C.LOCK_SET](await rq(E.req.lockSet(0, 40, ids.P_LEVEL, 100)));
  ok(full.rc === 3 && E.parse[C.LOCK_GET](await rq(E.req.lockGet(0))).locks.length === 24, "v7: the 25th lock is refused (rc 3, 24 a track)");
  /* another track is untouched; a device-side nudge pushes STEP_CHANGED */
  const lg3 = E.parse[C.LOCK_GET](await rq(E.req.lockGet(1)));
  await rq(E.req.watch(1));
  m.sim.nudge(7, 12);
  await sleep(30);
  const sc7 = ev.pushes.find((f) => f.cmd === C.STEP_CHANGED && f.a[0] === 7);
  ok(lg3.locks.length === 0 && !!sc7 && E.parse[C.MICRO_GET](await rq(E.req.microGet(0))).micro[7] === 12,
    "v7: other tracks untouched; a nudge on the device -> STEP_CHANGED of its step");
  ok(!ev.unknown.length, "v7: no unmatched replies");
  done();
  /* a v6 device (SLOOP 2.3): INFO 6, no reply to the v7 commands */
  const o = attachMock({ noBackup: true });
  const oi = E.parse[C.INFO](await o.rq(E.req.info()));
  const nl = await o.rq(E.req.lockGet(0), { timeout: 60, retries: 0, quiet: true }).then(() => "reply", () => "none");
  ok(oi.proto === 5 && nl === "none", "v7: a device without the commands: no reply (the editor hides the step detail)");
  o.done();
}

/* ----------------------------- editor protocol v8: fill conditions (SLOOP 2.4) --- */
async function editorV8() {
  const C = E.CMD;
  const { m, rq, ev, done } = attachMock({ watchMs: 1000 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  ok(info.proto === 10, "v8: INFO ends with 10 (v10 includes v8)");
  /* the condition codes == core.h FC_*; the bit layout of a 2-bit step field */
  const ch = readFileSync(join(HERE, "../firmware/src/core.h"), "utf8"), sc = readFileSync(join(HERE, "../firmware/src/seq.c"), "utf8");
  ok(/enum \{ FC_NORM, FC_FILL, FC_NOFILL \}/.test(ch) && /uint8_t fill\[NSTEP \/ 4\];/.test(ch)
    && /t->fill\[idx \/ 4u\] >> \(2u \* \(idx % 4u\)\)\) & 3u/.test(sc) && E.FC.FILL === 1 && E.FC.NOFILL === 2,
    "v8: FC codes and the 2-bit layout == core.h / seq.c step_fill");
  const g0 = E.parse[C.FILL_GET](await rq(E.req.fillGet(0)));
  ok(g0.track === 0 && g0.fill.length === 16 && g0.fill.every((x) => x === 0), "v8: FILL_GET: 16 bytes, every step normal");
  const s1 = E.parse[C.FILL_SET](await rq(E.req.fillSet(0, 4, E.FC.FILL)));
  const s2 = E.parse[C.FILL_SET](await rq(E.req.fillSet(0, 3, E.FC.NOFILL)));   /* (bits 6..7 of byte 0: the top bit crosses pack7) */
  const s3 = E.parse[C.FILL_SET](await rq(E.req.fillSet(0, 63, 3)));            /* 3: normal */
  const g1 = E.parse[C.FILL_GET](await rq(E.req.fillGet(0)));
  ok(s1.step === 4 && s1.cond === 1 && s2.cond === 2 && s3.cond === 0 && g1.fill[1] === 1 && g1.fill[0] === 0x80 && g1.fill[15] === 0
    && E.fillGet(g1.fill, 4) === 1 && E.fillGet(g1.fill, 3) === 2 && E.fillGet(g1.fill, 5) === 0
    && E.req.fillSet(0, 4, 1)[1].join() === "0,4,1" && E.req.fillGet(2)[1].join() === "2",
    "v8: FILL_SET sets a step (3 -> normal); FILL_GET returns the bytes as stored (0x80 through pack7)");
  {
    const b = new Uint8Array(16); E.fillSet(b, 7, 2); E.fillSet(b, 0, 1); E.fillSet(b, 7, 0);
    const rt = E.unpack7(E.pack7(Uint8Array.from([0x80, 0xFF, 0x01, 0x7F, 0x00, 0xC3, 0x55, 0xAA, 0x01, 0x80, 0, 0, 0, 0, 0, 0x40])));
    ok(E.fillGet(b, 0) === 1 && E.fillGet(b, 7) === 0 && b[1] === 0 && rt.length === 16 && rt[0] === 0x80 && rt[1] === 0xFF && rt[7] === 0xAA && rt[15] === 0x40,
      "v8: fillSet / fillGet on the client; pack7 keeps the top bits of 16 bytes (3 groups)");
  }
  /* another track untouched; a device-side change (SEQ + step + OCT+) pushes STEP_CHANGED */
  const g2 = E.parse[C.FILL_GET](await rq(E.req.fillGet(1)));
  await rq(E.req.watch(1));
  m.sim.fill(9, 2);
  await sleep(30);
  const sc9 = ev.pushes.find((f) => f.cmd === C.STEP_CHANGED && f.a[0] === 9);
  ok(g2.fill.every((x) => x === 0) && !!sc9 && E.fillGet(E.parse[C.FILL_GET](await rq(E.req.fillGet(0))).fill, 9) === 2,
    "v8: other tracks untouched; a condition set on the device -> STEP_CHANGED of its step");
  ok(!ev.unknown.length, "v8: no unmatched replies");
  done();
  /* a v7 device: INFO 7, no reply to the v8 commands */
  const o = attachMock({ v7: true });
  const oi = E.parse[C.INFO](await o.rq(E.req.info()));
  const nf = await o.rq(E.req.fillGet(0), { timeout: 60, retries: 0, quiet: true }).then(() => "reply", () => "none");
  const ml = await o.rq(E.req.microGet(0), { timeout: 200, retries: 0, quiet: true }).then(() => "reply", () => "none");
  ok(oi.proto === 7 && nf === "none" && ml === "reply", "v8: a v7 device: INFO 7, locks yes, no reply to FILL_GET (the editor hides the condition)");
  o.done();
}

/* ------------------------------------------------- 2.4: the drum kit page --- */
await (async () => {
  const lanes = { "BD Kick 808.wav": 0, "Snare_01.wav": 2, "HH closed.wav": 4, "OH open.wav": 5, "Crash cymbal.wav": 11, "Clap.wav": 3,
    "Low Tom.wav": 9, "Hi Tom.wav": 10, "rimshot.aif": 7, "Cowbell 1.wav": 15, "shaker.wav": 13, "Conga hi.wav": 14, "ride.wav": 12,
    "pedal hat.wav": 6, "piano C4.wav": -1, "kick2.wav": 1, "Snare 2.wav": 8 };
  const got = Object.entries(lanes).filter(([n, l]) => E.kitLaneOf(n) !== l).map(([n]) => `${n}->${E.kitLaneOf(n)}`);
  ok(!got.length, "drum kit: file names sorted onto the lanes" + (got.length ? ` (${got.join(", ")})` : ""));
  const s = (n, a = 20000) => Int16Array.from({ length: n }, (_, i) => Math.round(a * Math.sin(i / 7)));
  const pads = Array(16).fill(null);
  pads[0] = { s: s(8000), fname: "kick.wav", tune: 0, gain: 0, len: 0 };
  ok(E.kitPlace(pads, "BD second kick.wav") === 1 && E.kitPlace(pads, "piano.wav") === 1 && E.kitPlace(pads, "x.wav", 9) === 9,
    "drum kit: a taken lane gives way (KICK -> KICK 2), unknown names the first free pad, a drop its pad");
  pads[2] = { s: s(6000), fname: "snare.wav", tune: 12, gain: -6, len: 0 };
  pads[5] = { s: s(20000), fname: "oh.wav", tune: 0, gain: 0, len: 4410 };
  const p2 = E.kitPad(pads[2]), p5 = E.kitPad(pads[5]);
  const pk = (x) => x.reduce((a, v) => Math.max(a, Math.abs(v)), 0);
  ok(Math.abs(p2.length - 3000) <= 2 && Math.abs(pk(p2) / 20000 - 0.501) < 0.03 && E.kitLen(pads[2]) - p2.length <= 1,
    "drum kit: +12 st plays it twice as fast (half the length), -6 dB half the level");
  ok(p5.length === 4410 && Math.abs(p5[4409]) < 400 && pk(p5.subarray(0, 4000)) > 15000, "drum kit: LENGTH cuts it, with a 4 ms fade (no click)");
  const zs = E.kitZones(pads), b = E.buildSlot("KIT", zs);
  const hz = (j) => { const o = 32 + j * 28; return [b.hdr[o + 25], b.hdr[o + 26], new DataView(b.hdr.buffer).getInt16(o + 20, true) / 16]; };
  ok(zs.length === 3 && b.hdr[6] === 3 && [0, 1, 2].every((j) => { const [lo, hi, root] = hz(j); return lo === hi && hi === root; })
    && [hz(0)[0], hz(1)[0], hz(2)[0]].join() === "36,38,46",
    "drum kit: one zone per pad, lo = hi = root = the lane's note (drums.c LANE_NOTE: KICK 36, SNARE 38, OPEN HAT 46)");
  const big = Array(16).fill(null).map((_, l) => ({ s: s(30000), fname: l + ".wav", tune: 0, gain: 0, len: 0 }));
  ok(E.kitBytes(big) > 81408 && E.kitFit(big, 81408 - 64) && E.kitBytes(big) <= 81408 - 64 && E.kitBytes(big) > 81408 - 64 - 64,
    "drum kit: FIT shortens the longest pads to fill the slot, no more");
  {   /* KIT USR3+4: a big kit shared out over two slots, each pad whole in one */
    const R = 81408 - 64, mk = (secs) => ({ s: s(Math.round(secs * 22050)), fname: "x.wav", tune: 0, gain: 0, len: 0 });
    const kit = Array(16).fill(null);
    [0.6, 0.3, 0.3, 0.15, 2.5, 1.0, 0.2, 0.2, 0.3, 0.6, 0.6, 3.0, 2.5, 0.3, 0.4, 0.3].forEach((x, l) => { kit[l] = mk(x); });
    const tot = E.kitBytes(kit), sp = E.kitSplit(kit, R);
    const load = (ls) => ls.reduce((a, l) => a + ((E.kitLen(kit[l]) + 1) >> 1), 0);
    ok(tot > R && tot <= 2 * R && sp && sp[0].length + sp[1].length === 16 && load(sp[0]) <= R && load(sp[1]) <= R
      && new Set([...sp[0], ...sp[1]]).size === 16,
      `drum kit: USR3+4 takes a ${(tot * 2 / 22050).toFixed(1)} s kit no slot could, every pad whole in one of the two`);
    const huge = Array(16).fill(null).map(() => mk(1.5));
    ok(!E.kitSplit(huge, R) && E.kitFit(huge, R, 2) && E.kitSplit(huge, R) && E.kitBytes(huge) > 2 * R * 0.9,
      "drum kit: ... FIT for the pair fills both slots (no more than they hold)");
    const one = Array(16).fill(null); one[0] = mk(9);
    ok(!E.kitSplit(one, R) && E.kitFit(one, R, 2) && E.kitSplit(one, R) && E.kitSplit(one, R)[1].length === 0,
      "drum kit: ... a sound longer than a slot is cut to one slot");
  }
  const files = [{ name: "KIT/01 KICK.wav", data: new Uint8Array([1, 2, 3]) }, { name: "KIT/03 SNARE.wav", data: new Uint8Array(1000).fill(7) }];
  const back = await E.zipRead(E.zipStore(files));
  ok(back.length === 2 && back[0].name === "KIT/01 KICK.wav" && back[1].data.length === 1000 && back[1].data[999] === 7, "drum kit: a kit ZIP reads back (zipRead)");
  const dfl = await (async () => {                     /* a deflated entry, as zip tools write them */
    const raw = new Uint8Array(await new Response(new Blob([new Uint8Array(500).fill(65)]).stream().pipeThrough(new CompressionStream("deflate-raw"))).arrayBuffer());
    const z = E.zipStore([{ name: "a.wav", data: raw }]);
    z[8] = 8; z[raw.length + 30 + 5 + 10] = 8;          /* method 8 in the local header and in the directory */
    return (await E.zipRead(z))[0];
  })();
  ok(dfl.data.length === 500 && dfl.data[499] === 65, "drum kit: ... deflated ZIP entries too");
})();

/* ------------------------------ SLOOP 2.5: length, piano roll, MIDI files, song --- */
async function editor25() {
  const { ROLL, SMF, ARR } = E, R = ROLL.rest, len = 16;
  const apply = (st, m) => { const o = st.map((x) => ({ ...x })); for (const [i, v] of m) o[i] = v; return o; };
  const shape = (st) => st.slice(0, len).map((x) => (x.time === 0 && x.n ? "N" + x.notes.slice(0, x.n).join("+") : x.time === 1 ? "-" : ".")).join(" ");
  /* the piano roll */
  let st = Array.from({ length: 64 }, R);
  ok(ROLL.steps(1 / 4, 2) === 4 && ROLL.steps(1, 2) === 16 && ROLL.steps(1 / 16, 7) === 1 && ROLL.steps(1 / 2, 4) === 6 && ROLL.steps(1 / 8, 8) === 1,
    "roll: a note length in steps (1/4 at 1/16 = 4, 1BAR = 1, at least 1, triplets)");
  st = apply(st, ROLL.place(st, len, 0, 60, 4));
  st = apply(st, ROLL.place(st, len, 0, 64, 1));            /* same step: into the chord, its length kept */
  st = apply(st, ROLL.place(st, len, 8, 67, 2));
  ok(shape(st) === "N60+64 - - - . . . . N67 - . . . . . ." && ROLL.chords(st, len).map((c) => `${c.at}:${c.dur}`).join() === "0:4,8:2",
    "roll: place a note, a chord on its first step, a second note");
  const full = apply(st, new Map([[0, { ...st[0], n: 4, notes: [60, 64, 67, 71] }]]));
  ok(ROLL.place(full, len, 0, 72, 1).size === 0 && ROLL.place(st, len, 0, 60, 1).size === 0, "roll: a fifth note, or the same note twice: nothing changes");
  let s2 = apply(st, ROLL.place(st, len, 2, 50, 4));         /* inside the held chord: it ends there */
  ok(shape(s2) === "N60+64 - N50 - - - . . N67 - . . . . . ." && ROLL.owner(s2, 5) === 2 && ROLL.owner(s2, 7) === -1, "roll: a note inside a held chord ends it; owner()");
  s2 = apply(st, ROLL.place(st, len, 6, 48, 6));             /* stops before the next chord */
  ok(shape(s2) === "N60+64 - - - . . N48 - N67 - . . . . . .", "roll: a long note stops before the next chord");
  s2 = apply(st, ROLL.resize(st, len, 0, 7));
  ok(shape(s2) === "N60+64 - - - - - - . N67 - . . . . . ." && shape(apply(st, ROLL.resize(st, len, 0, 12))) === "N60+64 - - - - - - - N67 - . . . . . ."
    && shape(apply(st, ROLL.resize(st, len, 0, 1))) === "N60+64 . . . . . . . N67 - . . . . . ." && shape(apply(st, ROLL.resize(st, len, 8, 99))) === "N60+64 - - - . . . . N67 - - - - - - -",
    "roll: drag a length: longer (ties, up to the next chord or the end), shorter (rests)");
  s2 = apply(st, ROLL.remove(st, len, 0, 64));
  const s3 = apply(s2, ROLL.remove(s2, len, 0, 60));
  ok(shape(s2) === "N60 - - - . . . . N67 - . . . . . ." && shape(s3) === ". . . . . . . . N67 - . . . . . ." && ROLL.remove(st, len, 0, 99).size === 0,
    "roll: remove a chord's note; the last one leaves rests (ties too)");
  /* MIDI files */
  const steps = apply(st, new Map([[8, { ...st[8], flags: 1 }], [0, { ...st[0], vel: 80, lvl: 1 << 2 }]]));
  const notes = SMF.fromSteps(steps, len, 2);
  ok(notes.length === 3 && notes[0].t === 0 && notes[0].dur === 4 * 24 - 1 && notes[0].vel === 80 && notes[1].vel === 42 && notes[2].t === 8 * 24 && notes[2].vel === 127,
    "midi: a track's steps -> notes (ties as lengths; velocity, level, accent as the device plays them)");
  const file = SMF.write(notes, { bpm: 97, ch: 2, name: "SLOOP track 3" });
  const back = SMF.read(file);
  ok(back.format === 0 && back.ppq === 96 && back.bpm === 97 && back.tracks.length === 1 && back.tracks[0].name === "SLOOP track 3"
    && js(back.tracks[0].notes.map((n) => [n.t, n.dur, n.key, n.vel, n.ch])) === js(notes.slice().sort((a, b) => a.t - b.t || a.key - b.key).map((n) => [n.t, n.dur, n.key, n.vel, 2])),
    "midi: write -> read: the same notes, tempo, name, channel");
  const plan = SMF.toSteps(back.tracks[0].notes, back.ppq, 2, 64);
  ok(shape(plan.steps) === shape(steps) && plan.len === 10 && plan.steps[0].vel === 80 && plan.steps[8].vel === 127 && js(Object.values(plan.report)) === js([3, 10, 0, 0, 0, 0, 0]),
    "midi: read back onto the grid: the same chords and lengths, a clean report");
  const py2 = py(`import struct,sys
def vlq(n):
    b=[n&127]; n>>=7
    while n: b.insert(0,128|(n&127)); n>>=7
    return bytes(b)
ev=[(0,b'\\xff\\x51\\x03\\x07\\xa1\\x20')]
def note(t,d,k,v,ch=0): ev.extend([(t,bytes([0x90|ch,k,v])),(t+d,bytes([0x80|ch,k,0]))])
for k in (60,62,64,65,67): note(0,240,k,30+k)          # 5 notes at once: one is cut
note(480,96,72,100); note(520,96,72,90)                 # 520: off the grid (a quarter step = 30) -> the same step, merged
note(960,960,74,100); note(1200,96,76,100)              # cut short by the next chord
note(480*33,96,40,100)                                  # past 64 steps at 1/16
ev.sort(key=lambda e:e[0]); t=0; tr=b''
for at,b in ev: tr+=vlq(at-t)+b; t=at
tr+=b'\\x00\\xff\\x2f\\x00'
hd=b'MThd'+struct.pack('>IHHH',6,1,2,480)
t1=b'MTrk'+struct.pack('>I',4)+b'\\x00\\xff\\x2f\\x00'
sys.stdout.buffer.write(hd+t1+b'MTrk'+struct.pack('>I',len(tr))+tr)`);
  const f2 = SMF.read(new Uint8Array(py2)), p2 = SMF.toSteps(f2.tracks[1].notes, f2.ppq, 2, 64);
  ok(f2.format === 1 && f2.bpm === 120 && f2.tracks.length === 2 && !f2.tracks[0].notes.length && f2.tracks[1].notes.length === 10
    && js(p2.report) === js({ notes: 10, steps: 11, moved: 1, chordCut: 1, overlap: 1, dropped: 1, merged: 1 })
    && p2.steps[0].n === 4 && js(p2.steps[0].notes) === js([62, 64, 65, 67]) && p2.steps[0].vel === 97 && shape(p2.steps).startsWith("N62+64+65+67 - . . N72 . . . N74 - N76 . ."),
    "midi: a format 1 file from Python (480 ppq): 4 notes a step (loudest), merged, moved, cut short, past the end");
  let caught = "";
  try { SMF.read(Uint8Array.from([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14])); } catch (e) { caught = e.message; }
  ok(caught === "not a MIDI file" && (() => { try { SMF.read(file.slice(0, 20)); return true; } catch (e) { return false; } })(), "midi: a non-MIDI file is refused; a cut one reads what is there");
  const ds = Array.from({ length: 64 }, E.emptyDrum);
  ds[0].on = 1 | (1 << 4); ds[0].lvl[4] = 1; ds[4].on = 1 << 2; ds[4].lvl[2] = 3; ds[6].on = 1 << 15; ds[6].lvl[15] = 2;
  const dn = SMF.fromDrums(ds, 16, 2), dp = SMF.toSteps(SMF.read(SMF.write(dn, { ch: 9 })).tracks[0].notes, 96, 2, 64, true);
  ok(dn.map((n) => n.key).join() === "36,42,38,56" && dp.len === 7 && [0, 4, 6].every((i) => dp.dsteps[i].on === ds[i].on && js(dp.dsteps[i].lvl) === js(ds[i].lvl))
    && SMF.laneOf(36) === 0 && SMF.laneOf(35) === 1 && SMF.laneOf(57) === 11 && SMF.laneOf(20) === 0 && SMF.laneOf(90) === 13,
    "midi: the drum track: lanes as GM notes and back, levels ghost..hard kept (drums.c LANE_OF_GM)");
  const dc = readFileSync(join(HERE, "../firmware/src/drums.c"), "utf8");
  const tbl = (/LANE_OF_GM\[81 - 35 \+ 1\] = \{([^}]*)\}/.exec(dc) || [, ""])[1].replace(/\/\*[^*]*\*\//g, "").split(",").map((x) => +x.trim());
  ok(js(tbl) === js(SMF.LANE_OF_GM), "midi: LANE_OF_GM == drums.c");
  /* the song order */
  const sb = E.mockSettings(), song = ARR.decode(sb);
  ok(ARR.ok(sb) && sb.length === 88 && !song.loop && js(song.entries) === js([0, 1, 2, 3].map((scene) => ({ scene, bars: 4 }))), "song: the settings' song order (PER3, arr_defaults)");
  const rows = ARR.group([{ scene: 0, bars: 4 }, { scene: 0, bars: 4 }, { scene: 1, bars: 8 }, { scene: 0, bars: 4 }]);
  ok(js(rows) === js([{ scene: 0, bars: 4, times: 2 }, { scene: 1, bars: 8, times: 1 }, { scene: 0, bars: 4, times: 1 }]) && ARR.expand(rows).length === 4,
    "song: rows (a section n times) <-> entries");
  const enc = ARR.encode(sb, { loop: true, entries: ARR.expand(rows) });
  ok(js(ARR.decode(enc)) === js({ loop: true, entries: ARR.expand(rows) }) && enc.subarray(0, 48).every((v, i) => v === sb[i]) && enc.subarray(84).every((v, i) => v === sb[84 + i]),
    "song: encode keeps the rest of the settings");
  ok(!ARR.valid({ entries: [] }) && !ARR.valid({ entries: Array(17).fill({ scene: 0, bars: 1 }) }) && !ARR.valid({ entries: [{ scene: 4, bars: 1 }] })
    && !ARR.valid({ entries: [{ scene: 0, bars: 65 }] }) && ARR.valid({ entries: Array(16).fill({ scene: 3, bars: 64 }) }), "song: valid == arranger.h arr_valid (1..16 entries, A..D, 1..64 bars)");
  const pc = readFileSync(join(HERE, "../firmware/src/project.c"), "utf8"), ah = readFileSync(join(HERE, "../firmware/src/arranger.h"), "utf8");
  const pt = (/typedef struct \{([^}]*)\} persist_t;/.exec(pc) || [, ""])[1];
  ok(/PERSIST_MAGIC 0x50455233u/.test(pc) && /arr_config_t arrangement;/.test(pt) && /ARR_STEPS 16u/.test(ah)
    && /if \(\(song\.playing \|\| transport_req\) && memcmp\(&p\.arrangement, &arrangement, sizeof arrangement\)\)\s*return 3;/.test(pc),
    "song: PER3, persist_t has the arrangement, 16 steps; a new order refused while playing (project.c)");
  /* read and send it through the backup objects, as the Song page does */
  const { m, rq, done } = attachMock({});
  const get1 = async () => { const L = E.parse[E.CMD.BK_LIST](await rq(E.req.bkList())); const it = L.items.find((x) => x.id === 1);
    return E.parse[E.CMD.BK_GET](await rq(E.req.bkGet(1, 0, it.len))).data; };
  const put1 = async (d) => {
    await rq(E.req.bkBegin(1, d.length, E.crc32(d)));
    await rq(E.req.bkData(1, 0, d));
    return E.parse[E.CMD.BK_PUT](await rq(E.req.bkCommit(1))).rc;
  };
  const cur = await get1();
  const mine = ARR.encode(cur, { loop: true, entries: [{ scene: 2, bars: 8 }, { scene: 3, bars: 2 }] });
  const rc1 = await put1(mine), now = await get1();
  const bad = ARR.encode(cur, { loop: false, entries: [{ scene: 0, bars: 4 }] }); bad[ARR.OFF + 5] = 0;
  const rc2 = await put1(bad);
  m.sim.play(true);
  const rc3 = await put1(ARR.encode(cur, { loop: false, entries: [{ scene: 1, bars: 1 }] }));
  const rc4 = await put1(now);                                 /* the same order while playing: fine */
  m.sim.play(false);
  ok(ARR.ok(cur) && rc1 === 0 && js(ARR.decode(now)) === js({ loop: true, entries: [{ scene: 2, bars: 8 }, { scene: 3, bars: 2 }] }) && rc2 === 2 && rc3 === 3 && rc4 === 0
    && js(ARR.decode(await get1())) === js(ARR.decode(now)), "song: mock: read, send (rc 0), a bad one (rc 2), a new one while playing (rc 3)");
  done();
  /* the page */
  ok(/id="lenval" type="number" min="1" max="64"/.test(html) && /if \(!s && id === pSlen\(\)\) lenChanged\(\);/.test(html) && /if \(!\$\("roll"\)\.hidden\) renderRoll\(\);/.test(html),
    "editor: the LEN control (1..64), the grid and roll redrawn when LEN changes");
}

/* ------------------------------------- SLOOP 2.5: the DRUM SYNTH page (v10) --- */
async function editorDsyn() {
  const { DSYN } = E, C = E.CMD;
  const dsc = readFileSync(join(HERE, "../firmware/src/drum_synth.c"), "utf8");
  const st = (/typedef struct \{([\s\S]*?)\} dsnd_t;/.exec(dsc) || [, ""])[1].replace(/\/\*[\s\S]*?\*\//g, "");
  const fields = [...st.matchAll(/(?:u?int8_t)\s+([^;]+);/g)].flatMap((m) => m[1].split(",").map((x) => x.trim()));
  ok(js(fields) === js(DSYN.FIELDS) && DSYN.SIZE === 22, "dsyn: the 22 fields in dsnd_t's order (drum_synth.c)");
  const ln = ((/DS_LANE_NOTE\[DS_LANES\] = \{([^}]*)\}/.exec(dsc) || [, ""])[1]).split(",").map((x) => +x.trim());
  ok(js(ln) === js(DSYN.LANE_NOTE) && DSYN.LANES_N.length === 16, "dsyn: the lanes' notes == DS_LANE_NOTE");
  ok(/sizeof\(dsu_bank_t\) == 1464u/.test(dsc) && /DSU_MAGIC 0x31555344u/.test(dsc) && DSYN.BANK_SIZE === 1464, "dsyn: backup object 9 == dsu_bank_t (1464 bytes, DSU1)");
  /* the mock's factory kits == the firmware's (when built) */
  const raw = E.b64dec(E.DSYN_MOCK_KITS);
  let hdr = null;
  try { hdr = readFileSync(join(HERE, "../build/gen/felucca_drumkits.h"), "utf8"); } catch (e) { /* (not built) */ }
  if (hdr) {
    const kits = [...hdr.matchAll(/\{"([^"]*)", "[^"]*", (0x[0-9A-Fa-f]+|\d+), \{([\s\S]*?)\}\},/g)];
    const bytes = kits.flatMap((k) => [Number(k[2]), ...[...k[3].matchAll(/\{([^{}]*)\},/g)].flatMap((r) => r[1].split(",").map((x) => +x & 255))]);
    ok(kits.length === 32 && js(kits.map((k) => k[1])) === js(E.DSYN_MOCK_NAMES) && js(Array.from(raw)) === js(bytes),
      "dsyn: the mock's 32 kits == build/gen/felucca_drumkits.h");
  }
  /* viewing never changes a value: decode / encode give every factory sound back */
  let same = true;
  for (let i = 0; i < 32 * 16; i++) {
    const at = Math.floor(i / 16) * 353 + 1 + (i % 16) * 22, b = raw.slice(at, at + 22);
    same = same && js(Array.from(DSYN.encode(DSYN.decode(b)))) === js(Array.from(b));
  }
  const wild = DSYN.decode(DSYN.encode({ wave: 9, noise: 7, clap: 1, pitch: 300, fine: 40, bend: 200, btime: 999, hold: 999, decay: -5,
    tlev: 500, t2: 999, t2lev: 999, click: 999, nlev: 999, nhold: 999, ndec: 999, fmode: 7, fall: 1, res: 99, fcut: 999, fenv: -999, hpf: 999, chip: 999, drive: 999, level: 999 }));
  ok(same && wild.wave === 5 && wild.noise === 4 && wild.clap === 1 && wild.pitch === 127 && wild.fine === 15 && wild.bend === 96 && wild.decay === 0
    && wild.fmode === 3 && wild.res === 31 && wild.fenv === -128 && wild.level === 255 && wild.drive === 127,
    "dsyn: decode / encode: all 512 factory sounds unchanged; out-of-range values into the firmware's ranges");
  ok(Math.round(DSYN.hz(69)) === 440 && Math.round(DSYN.decayMs(127)) === 4000 && Math.round(DSYN.cutHz(0)) === 30 && DSYN.db(132) === 1,
    "dsyn: the units (Hz, ms, cutoff, dB) as tools/gen_tables.py");
  /* files and the backup object */
  const kit = { name: "MY 808", crush: 0x21, src: 0, sounds: Array.from({ length: 16 }, (_, l) => raw.slice(1 + l * 22, 23 + l * 22)) };
  const back = DSYN.fromFile(JSON.parse(JSON.stringify(DSYN.toFile(kit))));
  let bad = "";
  try { DSYN.fromFile({ format: "sloop-drumsynth", sounds: [[1, 2]] }); } catch (e) { bad = e.message; }
  const bank = DSYN.bankEncode([kit, kit, kit, kit]), dec = DSYN.bankDecode(bank);
  ok(back.name === "MY 808" && back.crush === 0x21 && js(back.sounds.map((x) => Array.from(x))) === js(kit.sounds.map((x) => Array.from(x))) && /not a SLOOP/.test(bad)
    && bank.length === 1464 && dec && dec[3].name === "MY 808" && dec[2].crush === 0x21 && js(Array.from(dec[1].sounds[15])) === js(Array.from(kit.sounds[15]))
    && DSYN.bankDecode(bank.slice(1)) === null, "dsyn: a kit file and the backup object, there and back; a wrong file refused");
  /* the device (mock): LIST / GET / PUT / STORE / PLAY */
  const { m, rq, done } = attachMock({});
  let L = E.parse[C.DSYN_LIST](await rq(E.req.dsynList()));
  const g0 = E.parse[C.DSYN_GET](await rq(E.req.dsynGet(DSYN.USER)));
  const f5 = E.parse[C.DSYN_GET](await rq(E.req.dsynGet(5)));
  ok(L.factory === 32 && L.user === 4 && L.stored && L.names[0] === "THUMP" && L.kits.map((k) => k.name).join() === "THUMP,CLUB,TRAP,TECHNO"
    && g0.rc === 0 && g0.name === "THUMP" && g0.src === 0 && js(g0.sounds.map((x) => Array.from(x))) === js(kit.sounds.map((x) => Array.from(x)))
    && f5.rc === 0 && f5.name === "TRAP", "dsyn: LIST (32 + 4, stored), GET SYN1 = the 808, GET a factory kit");
  const o = DSYN.decode(g0.sounds[0]); o.decay = 20; o.wave = 4;
  const p1 = E.parse[C.DSYN_PUT](await rq(E.req.dsynSound(0, 0, DSYN.encode(o))));
  const p2 = E.parse[C.DSYN_PUT](await rq(E.req.dsynHead(0, "BOOM", 0x10, 0)));
  const p3 = E.parse[C.DSYN_PUT](await rq(E.req.dsynCopy(3, 25)));
  const pl = E.parse[C.DSYN_PLAY](await rq(E.req.dsynPlay(0, 0, 100)));
  const g1 = E.parse[C.DSYN_GET](await rq(E.req.dsynGet(DSYN.USER)));
  L = E.parse[C.DSYN_LIST](await rq(E.req.dsynList()));
  ok(p1.rc === 0 && p2.rc === 0 && p3.rc === 0 && pl.rc === 0 && g1.name === "BOOM" && g1.crush === 0x10 && DSYN.decode(g1.sounds[0]).decay === 20
    && DSYN.decode(g1.sounds[0]).wave === 4 && L.kits[3].name === "CHIP" && !L.stored && m.state.dsynPlayed.length === 1 && m.state.dsynPlayed[0].sound[7] === 20,
    "dsyn: PUT a sound, the name and crush, a factory copy; PLAY hears the edit; LIST: not stored");
  m.sim.play(true);
  const s3 = E.parse[C.DSYN_STORE](await rq(E.req.dsynStore()));
  m.sim.play(false);
  const s0 = E.parse[C.DSYN_STORE](await rq(E.req.dsynStore()));
  L = E.parse[C.DSYN_LIST](await rq(E.req.dsynList()));
  const bp = E.parse[C.DSYN_PLAY](await rq(E.req.dsynPlay(0, 16)));
  ok(s3.rc === 3 && s0.rc === 0 && L.stored && bp.rc === 1, "dsyn: STORE while playing: rc 3; stopped: stored; PLAY of no lane: rc 1");
  /* a backup carries the kits, a restore puts them back */
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const A = await E.backupCapture(rq, info);
  await rq(E.req.dsynCopy(0, 1));
  await E.backupRestore(rq, JSON.parse(JSON.stringify(A)));
  const g2 = E.parse[C.DSYN_GET](await rq(E.req.dsynGet(DSYN.USER)));
  ok(A.objects.some((x) => x.id === 9 && x.len === 1464) && g2.name === "BOOM", "dsyn: a backup holds the SYN kits (object 9); a restore puts them back");
  done();
  const old = attachMock({ v9: true });
  const oi = E.parse[C.INFO](await old.rq(E.req.info()));
  const B = await E.backupCapture(old.rq, oi);
  ok(oi.proto === 9 && !B.objects.some((x) => x.id === 9), "dsyn: a v9 device says 9 (the page says it needs 2.5), no object 9");
  await E.backupRestore(old.rq, JSON.parse(JSON.stringify(A)));    /* a 2.5 backup into a v9 device: object 9 skipped */
  ok(true, "dsyn: a 2.5 backup restores into a v9 device (object 9 skipped)");
  old.done();
}

/* ------------------------------------------- 2.5: a DX7 cartridge into the bank --- */
async function editorCart() {
  const C = E.CMD, F = E.FM6;
  const fc = readFileSync(join(HERE, "../firmware/src/fm6_bank.c"), "utf8");
  const voices = Array.from({ length: 32 }, (_, k) => { const v = F.factory(k % F.FACTORY_PK.length); F.setName(v, `CART ${k + 1}`); return v; });
  const pb = F.parseSysex(F.bankSysex(voices));
  const slots = new Array(27).fill(null); slots[0] = F.pack(voices[0]); slots[26] = F.pack(voices[5]);
  const o = F.bankObj(slots), u = F.bankUnobj(o);
  const bad = Uint8Array.from(o); bad[16 + 3] = 200;
  const wrong = Uint8Array.from(o); wrong[6] = 32;
  ok(/sizeof\(fm6_bank_t\) == 3472u/.test(fc) && /FM6_BANK_MAGIC 0x42364D46u/.test(fc) && o.length === 3472
    && js(Array.from(o.subarray(0, 12))) === js([0x46, 0x4D, 0x36, 0x42, 1, 0, 27, 0, 1, 0, 0, 4])
    && u[0] && u[26] && !u[1] && js(Array.from(u[26])) === js(Array.from(slots[26])) && F.bankUnobj(new Uint8Array(0)).every((x) => x === null)
    && F.bankUnobj(bad) === null && F.bankUnobj(wrong) === null && F.bankUnobj(o.subarray(1)) === null,
  "cart: the bank object == fm6_bank_t (3472 bytes, FM6B, 27 slots); a bad one is refused");
  const { m, rq, sent, done } = attachMock({});
  const mine = F.factory(3); F.setName(mine, "MY BELL");
  await rq(E.req.fm6Put(F.TARGET.BANK, 4, F.pack(mine)), { timeout: 3000 });     /* B5 */
  await rq(E.req.fm6Put(F.TARGET.BANK, 1, F.pack(mine)), { timeout: 3000 });     /* B2 */
  const packs = [0, 1, 2].map((k) => F.pack(pb.voices[k].v));
  m.sim.play(true);
  const r3 = await E.fm6CartWrite(rq, packs);
  let L = E.parse[C.FM6_LIST](await rq(E.req.fm6List()));
  ok(r3 === 3 && L.slots[F.FACTORY_PK.length + 1].name.trim() === "MY BELL" && L.slots[F.FACTORY_PK.length].name.trim() === "MY TINES", "cart: while the song plays: rc 3, the bank unchanged");
  m.sim.play(false);
  const p0 = sent[C.BK_PUT] || 0, r0 = await E.fm6CartWrite(rq, packs), puts = sent[C.BK_PUT] - p0;
  L = E.parse[C.FM6_LIST](await rq(E.req.fm6List()));
  const g = E.parse[C.FM6_GET](await rq(E.req.fm6Get(F.TARGET.BANK, 2)));
  ok(r0 === 0 && L.slots.slice(F.FACTORY_PK.length, F.FACTORY_PK.length + 3).map((x) => x.name.trim()).join() === "CART 1,CART 2,CART 3" && L.slots[F.FACTORY_PK.length + 4].name.trim() === "MY BELL"
    && !L.slots[F.FACTORY_PK.length + 3].used && js(Array.from(F.unpack(g.packed))) === js(Array.from(pb.voices[2].v)) && puts === 2 + Math.ceil(3472 / 256),
  "cart: 3 voices -> B1-B3 in one write (B2 replaced, B5 kept, B4 still empty)");
  const all = pb.voices.slice(0, 27).map((x) => F.pack(x.v));
  const rAll = await E.fm6CartWrite(rq, all);
  L = E.parse[C.FM6_LIST](await rq(E.req.fm6List()));
  let tooMany = "";
  try { await E.fm6CartWrite(rq, pb.voices.map((x) => F.pack(x.v))); } catch (e) { tooMany = e.message; }
  ok(rAll === 0 && L.slots.slice(F.FACTORY_PK.length).every((x, k) => x.used && x.name.trim() === `CART ${k + 1}`) && /32 voices/.test(tooMany),
    "cart: the first 27 of a 32-voice cartridge fill B1-B27; 32 at once refused");
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const A = await E.backupCapture(rq, info);
  ok(A.objects.some((x) => x.id === 8 && x.len === 3472), "cart: a backup holds the filled bank (object 8)");
  /* 2.5: also as user presets, named after the voices, each on its bank slot */
  const fake = { total: 32, slots: Array.from({ length: 32 }, (_, s) => ({ slot: s, used: s === 0 || s === 2 ? 1 : 0, name: s ? "X" : "MINE" })) };
  ok(js(E.fm6CartSlots(fake, 3, "free")) === js([1, 3, 4]) && js(E.fm6CartSlots(fake, 3, 29)) === js([29, 30, 31])
    && E.fm6CartSlots(fake, 3, 30) === null && E.fm6CartSlots(fake, 31, "free") === null && E.fm6CartSlots(fake, 30, "free").length === 30,
  "cart: preset slots: the first free ones, or from a slot; null when they do not fit");
  const base = Array.from({ length: info.pcount }, (_, i) => i % 3);
  const pr = E.fm6CartPreset(info, base, 8, 4, "  BRASS 1   "), pr0 = E.fm6CartPreset(info, base, 8, 0, "");
  ok(pr.engine === 9 && pr.name === "BRASS 1" && pr.p.length === 61 && pr.p[info.pe0 + 7] === 12 && pr.p[info.pe0 + 6] === base[info.pe0 + 6]
    && pr.p[0] === base[0] && pr0.name === "FM6 B1" && pr0.p[info.pe0 + 7] === 8, "cart: a preset = FM6, the voice's name, PTCH = its B slot, the rest as given");
  const ul = await E.bank.list(rq);
  const free = E.fm6CartSlots(ul, 3, "free");
  const ups = free.map((s, i) => ({ slot: s, pt: E.fm6CartPreset(info, base, 8, i, pb.voices[i].name) }));
  m.sim.play(true);
  const u3 = await E.fm6CartPresets(rq, ups);
  m.sim.play(false);
  const u0 = await E.fm6CartPresets(rq, ups);
  const ul2 = await E.bank.list(rq), g2 = await E.bank.get(rq, info, free[2]);
  ok(u3 === 3 && u0 === 0 && free.every((s, i) => ul2.slots[s].used && ul2.slots[s].name === `CART ${i + 1}` && ul2.slots[s].engine === 9)
    && g2.engine === 9 && g2.p[info.pe0 + 7] === 10 && ul.slots.filter((x) => x.used).every((x) => ul2.slots[x.slot].name === x.name),
  "cart: user presets CART 1-3 in the free slots (FM6, PTCH B1-B3), the others kept; refused while the song plays");
  done();
  const old = attachMock({ v8: true });
  ok(await old.rq(E.req.info()) && (await E.fm6CartWrite(old.rq, packs)) === "old", "cart: a device before v9: 'old' (the page says it needs 2.4)");
  old.done();
}

/* ------------------------------------------ Clipper 1.0: the pattern tools (editor.html PAT) --- */
async function patternTools() {
  const { PAT } = E;
  const shape = (P, n = P.len) => P.steps.slice(0, n).map((x) => (x.time === 0 && x.n ? x.notes.slice(0, x.n).join("+") + (x.flags & 1 ? ">" : "") + (x.flags & 2 ? "~" : "") : x.time === 1 ? "-" : ".")).join(" ");
  const blank = (len = 16) => ({ len, steps: Array.from({ length: 64 }, PAT.rest), micro: new Array(64).fill(0), fill: new Array(64).fill(0), locks: [] });
  const from = (txt, len) => { const P = blank(len); txt.split(" ").forEach((t, i) => {
    P.steps[i] = t === "." ? PAT.rest() : t === "-" ? PAT.tie() : PAT.note(t.split("+").map(Number)); }); return P; };
  /* make <-> the editor's state; diff */
  {
    const fb = new Array(16).fill(0);
    E.fillSet(fb, 3, 1); E.fillSet(fb, 5, 2);
    const st = Array.from({ length: 64 }, (_, i) => (i === 2 ? { n: 1, notes: [60, 0, 0, 0], time: 0, flags: 9, vel: 100, lvl: 0, rat: 0 } : PAT.rest()));
    const P = PAT.make(st, new Array(64).fill(0).map((_, i) => (i === 2 ? -5 : 0)), fb, [{ step: 2, param: 5, value: 40 }], 16);
    ok(P.len === 16 && P.fill[3] === 1 && P.fill[5] === 2 && P.micro[2] === -5 && P.steps[2].flags === 1 && P.locks.length === 1,
      "pattern: the editor's steps, nudges, packed fills and locks as one value");
    const Q = PAT.clone(P);
    Q.steps[4] = PAT.note([62]); Q.micro[2] = 3; Q.fill[3] = 0; Q.locks = [{ step: 2, param: 5, value: 41 }, { step: 4, param: 6, value: 1 }]; Q.len = 32;
    const d = PAT.diff(P, Q);
    ok(d.steps.join() === "4" && d.micro.join() === "2" && d.fill.join() === "3" && d.lockSet.length === 2 && !d.lockDel.length && d.len === 32 && d.n === 6
      && PAT.diff(Q, P).lockDel.map((l) => l.step + ":" + l.param).join() === "4:6" && PAT.diff(P, P).n === 0 && P.steps[4].time === 2,
      "pattern: diff = what to send (and the input is never changed)");
  }
  /* transformations */
  {
    const P = from("60 - . 64 . . 67 - - . . . 72 . . 71", 16);
    P.micro[3] = 4; P.locks = [{ step: 3, param: 5, value: 9 }];
    ok(shape(PAT.transpose(P, 12)) === "72 - . 76 . . 79 - - . . . 84 . . 83" && shape(PAT.transpose(P, -1, 0, 4)) === "59 - . 63 . . 67 - - . . . 72 . . 71"
      && PAT.transpose(from("120 5", 2), 12).steps.slice(0, 2).map((s) => s.notes[0]).join() === "120,17", "pattern: transpose (the range; octaves fold back into 0..127)");
    const r1 = PAT.rotate(P, 1);
    ok(shape(r1) === "71 60 - . 64 . . 67 - - . . . 72 . ." && r1.micro[4] === 4 && r1.locks[0].step === 4 && shape(PAT.rotate(P, -1)) === "- . 64 . . 67 - - . . . 72 . . 71 60"
      && shape(PAT.rotate(P, 1, 0, 4)) === "64 60 - . . . 67 - - . . . 72 . . 71", "pattern: shift round, nudges and locks move with their steps");
    const rv = PAT.reverse(P);
    ok(shape(rv) === "71 . . 72 . . . 67 - - . . 64 . 60 -" && rv.micro[12] === 4 && rv.locks[0].step === 12, "pattern: reverse keeps each chord's length");
    ok(shape(PAT.reverse(P, 0, 8)) === "67 - . . 64 . 60 - . . . . 72 . . 71", "pattern: reverse a range");
    const r2 = PAT.reverse(P, 6, 8);                     /* the chord 6..8 held past the range: cut */
    ok(shape(r2) === "60 - . 64 . . 67 - . . . . 72 . . 71", "pattern: reverse a range, a chord held past it stops at its end");
    const dbl = PAT.double(P);
    ok(dbl.len === 32 && shape(dbl) === shape(P) + " " + shape(P) && dbl.micro[19] === 4 && dbl.locks.map((l) => l.step).join() === "3,19"
      && PAT.double({ ...P, len: 40 }, 64).len === 64, "pattern: double the length (the copy after, up to 64 steps)");
    const clip = PAT.copy(P, 3, 7);
    const ps = PAT.paste(P, clip, 12);
    ok(clip.len === 4 && shape(ps) === "60 - . 64 . . 67 - - . . . 64 . . 67" && ps.micro[12] === 4 && ps.locks.map((l) => l.step).join() === "3,12"
      && shape(PAT.paste(P, PAT.copy(P, 0, 2), 6)) === "60 - . 64 . . 60 - . . . . 72 . . 71", "pattern: copy / paste (cut at LEN, an old chord's ties after it end)");
    ok(shape(PAT.clear(P, 0, 4)) === ". . . . . . 67 - - . . . 72 . . 71" && !PAT.clear(P, 0, 4).locks.length && shape(PAT.clear(P, 6, 7)) === "60 - . 64 . . . . . . . . 72 . . 71",
      "pattern: clear a range (its locks go; ties of a chord cleared go too)");
    const many = blank(16);
    many.locks = Array.from({ length: 20 }, (_, k) => ({ step: k % 16, param: 5 + Math.floor(k / 16), value: k }));
    const dm = PAT.double(many);
    ok(dm.locks.length === 24 && dm.dropped === 16, "pattern: at most 24 locks (the device's), what did not fit is counted");
  }
  /* fine editing */
  {
    const P = from("60 . 64 -", 4);
    let Q = PAT.setLane(P, "rat", 3);
    ok(Q.steps[0].rat === 0xAA && Q.steps[1].rat === 0 && PAT.laneGet(Q, "rat", 0) === 3, "pattern: ratchet x3 on the notes (all four of a step)");
    const Q8 = PAT.setLane(P, "rat", 8), Q6 = PAT.setLane(Q8, "rat", 6), Q2 = PAT.setLane(Q6, "rat", 2), Q9 = PAT.setLane(P, "rat", 12);
    ok(PAT.laneGet(Q8, "rat", 0) === 8 && Q8.steps[0].flags === (4 | 0x78) && Q8.steps[0].rat === 0xFF && PAT.laneGet(Q6, "rat", 0) === 6
      && PAT.laneGet(Q2, "rat", 0) === 2 && Q2.steps[0].flags === 0 && PAT.laneGet(Q9, "rat", 0) === 8 && PAT.LANES.rat[1] === 8
      && PAT.same(PAT.cstep(Q8.steps[0]), Q8.steps[0]) && !PAT.same(Q8.steps[0], Q2.steps[0]),
      "pattern: ratchet x1..x8 (1.0: x5..x8 in the flags, as the firmware), past x8: x8");
    Q = PAT.setLane(P, "lvl", 1);
    ok(Q.steps[0].lvl === PAT.uni(1) && PAT.laneGet(Q, "lvl", 0) === 1 && PAT.laneGet(P, "lvl", 0) === 3 && PAT.laneGet(PAT.setLane(P, "lvl", 4), "lvl", 2) === 4,
      "pattern: level GHOST..HARD (1..4 by loudness)");
    ok(PAT.laneGet(PAT.setLane(P, "vel", 300), "vel", 0) === 127 && PAT.laneGet(P, "vel", 1) === 96 && PAT.laneGet(PAT.setLane(P, "nudge", -99), "nudge", 2) === -32,
      "pattern: velocity and nudge lanes (clamped)");
    ok(shape(PAT.chord(P, "maj")) === "60+64+67 . 64+68+71 -" && shape(PAT.chord(P, "m7", 2, 3)) === "60 . 64+67+71+74 -"
      && shape(PAT.chord(P, "key3", 0, 4, { root: 0, scale: "MAJ" })) === "60+64+67 . 64+67+71 -"
      && shape(PAT.chord(P, "key4", 0, 1, { root: 9, scale: "MIN" })) === "60+64+67+71 . 64 -" && shape(PAT.chord(PAT.chord(P, "maj"), "single")) === "60 . 64 -",
      "pattern: chords (fixed or in the key) from each step's first note");
    ok(shape(PAT.flag(P, 1, "on")) === "60> . 64> -" && shape(PAT.flag(PAT.flag(P, 2, "on"), 2, "off", 0, 1)) === "60 . 64~ -", "pattern: accents / slides on a range");
  }
  /* generators */
  {
    const eu = (k, n, r) => PAT.euclid(k, n, r).map((x) => (x ? "x" : ".")).join("");
    ok(eu(3, 8) === "x..x..x." && eu(5, 8) === "x.x.xx.x" && eu(4, 16) === "x...x...x...x..." && eu(3, 8, 1) === ".x..x..x" && eu(0, 4) === "...." && eu(9, 8) === "xxxxxxxx",
      "pattern: euclidean rhythms (tresillo, cinquillo, rotation)");
    const g = PAT.genEuclid(from("60 - - - 62 62 62 62", 16), { k: 3, n: 8, notes: [36, 43], accent: true });
    ok(shape(g) === "36+43> . . 36+43 . . 36+43 . 36+43> . . 36+43 . . 36+43 ." && shape(PAT.genEuclid(from("50 . 51 . . . . 52", 8), { k: 2, n: 4, notes: [40], merge: true })) === "40 . 40 . 40 . 40 52",
      "pattern: euclid over the range (replace or merge)");
    const r1 = PAT.genRandom(blank(), { seed: 7, root: 2, scale: "DOR", base: 48, oct: 2, density: 80, ties: 30, accents: 20, slides: 20, chords: 0 });
    const r2 = PAT.genRandom(blank(), { seed: 7, root: 2, scale: "DOR", base: 48, oct: 2, density: 80, ties: 30, accents: 20, slides: 20, chords: 0 });
    const notes = r1.steps.slice(0, 16).filter(PAT.isNote).flatMap((s) => s.notes.slice(0, s.n));
    ok(shape(r1) === shape(r2) && shape(r1) !== shape(PAT.genRandom(blank(), { seed: 8, root: 2, scale: "DOR" })) && notes.length >= 6
      && notes.every((n) => n >= 48 && n <= 72 && PAT.SCALES.DOR.includes((n - 2 + 120) % 12)) && r1.steps[0].time === 0 && r1.steps[16].time === 2,
      "pattern: random in the key (the same seed, the same pattern; only the range)");
    const dense = PAT.genRandom(blank(), { seed: 3, density: 100, ties: 0, chords: 100, scale: "MAJ" });
    ok(dense.steps.slice(0, 16).every((s) => PAT.isNote(s) && s.n === 3), "pattern: random chords at 100 %");
    const h = PAT.humanize(from("60 60 . 60", 4), { seed: 1, vel: 20, nudge: 6 });
    ok(h.steps[0].vel !== 96 && h.steps.slice(0, 4).every((s, i) => i === 2 || Math.abs(s.vel - 96) <= 20) && h.micro.slice(0, 4).some((m) => m) && h.micro[2] === 0
      && h.micro.every((m) => m >= -6 && m <= 6), "pattern: humanize velocity and nudge (notes only, within the amounts)");
    const key = { root: 9, scale: "MIN", base: 36 };
    ok(PAT.fromText("1 3 5 8 1' 3, - . 1+3+5>~", key).map((s) => (s.time === 0 ? s.notes.slice(0, s.n).join("+") + s.flags : s.time)).join(" ")
      === "450 480 520 570 570 360 1 2 45+48+523" && PAT.fromText("1 x", key) === null && PAT.fromText("1+2+3+4+5", key) === null,
      "pattern: pattern text (degrees of the key, ' , octaves, + chords, > accent, ~ slide)");
    let good = 0;
    for (const [, , txt] of PAT.PRESETS) { const s = PAT.fromText(txt, key); if (s && s.length === 16) good++; }
    ok(good === PAT.PRESETS.length && PAT.PRESETS.length >= 20, "pattern: every ready-made pattern reads (16 steps)");
    const gt = PAT.genText(blank(32), "- 1 . 5", { root: 0, scale: "MAJ", base: 48 }, 0, 6);
    ok(shape(gt, 8) === ". 48 . 55 - 48 . ." && PAT.genText(blank(), "1 q", key) === null, "pattern: a text over the range, again and again");
  }
  /* files and the bank */
  {
    const P = from("60+64 - . 67", 4);
    P.micro[3] = -7; P.fill[0] = 2; P.locks = [{ step: 3, param: 12, value: -200 }, { step: 9, param: 5, value: 1 }];
    P.steps[3].rat = 0x55; P.steps[3].lvl = 0xFF;
    const f = JSON.parse(JSON.stringify(PAT.toFile(P, "My bass")));
    const Q = PAT.fromFile(f, 64);
    ok(f.format === "clipper-pattern" && f.steps.length === 4 && f.locks.length === 1 && Q.len === 4 && shape(Q) === shape(P) && Q.micro[3] === -7 && Q.fill[0] === 2
      && Q.steps[3].rat === 0x55 && Q.steps[3].lvl === 0xFF && Q.locks.length === 1 && Q.locks[0].value === -200 && Q.steps[4].time === 2,
      "pattern: a .clipper-pattern.json file there and back");
    let bad = 0;
    for (const o of [null, {}, { format: "clipper-pattern", steps: [] }, { format: "x", steps: [[1, [60], 0, 0, 96]] }, { format: "clipper-pattern", steps: [[1, [60], 0, 0, "q"]] }, { format: "clipper-pattern", steps: [5] }])
      try { PAT.fromFile(o); } catch (e) { bad++; }
    const big = PAT.fromFile({ format: "clipper-pattern", len: 999, steps: [[9, [300, -4, 0, 0], 7, 9, 999]], micro: [99] }, 64);
    ok(bad === 6 && big.len === 64 && big.steps[0].n === 4 && big.steps[0].notes[0] === 127 && big.steps[0].notes[1] === 0 && big.steps[0].time === 2 && big.micro[0] === 31,
      "pattern: a bad file is refused, odd values are clamped");
    const old = PAT.fitTo(Q, { micro: null, fill: null, locks: null });
    ok(old.micro === null && old.locks === null && PAT.diff(PAT.fitTo(Q, { micro: [], fill: [], locks: [] }), Q).n === 0, "pattern: fit to older firmware (no nudge / fill / lock)");
    let bank = PAT.bankPut([], "b", f, 1);
    bank = PAT.bankPut(bank, "a", f, 2);
    bank = PAT.bankPut(bank, " b ", f, 3);
    ok(bank.map((x) => x.name + x.at).join() === "a2,b3" && bank[0].pat.name === "a", "pattern: the bank (by name, saving again replaces)");
  }
  /* against the mock: a pattern sent with STEP_SET / MICRO_SET / FILL_SET / LOCK_SET reads back the same */
  {
    const m = E.makeMockDevice();
    const inp = [...m.access.inputs.values()][0], out = [...m.access.outputs.values()][0];
    const link = new E.Link((d) => out.send(d), { timeout: 300 });
    inp.onmidimessage = (e) => link.receive(e.data);
    const rq = async (r) => link.request(r);
    const info = E.parse[E.CMD.INFO](await rq(E.req.info()));
    const steps = [];
    for (let i = 0; i < info.nstep; i++) steps.push(E.parse[E.CMD.STEP_GET](await rq(E.req.stepGet(i))));
    const mic = E.parse[E.CMD.MICRO_GET](await rq(E.req.microGet(0))).micro, fil = E.parse[E.CMD.FILL_GET](await rq(E.req.fillGet(0))).fill;
    const lk = E.parse[E.CMD.LOCK_GET](await rq(E.req.lockGet(0))).locks;
    const A = PAT.make(steps, mic, fil, lk, 16);
    let B = PAT.genText(A, "1+3+5> - 3 5~ . 8", { root: 0, scale: "MAJ", base: 48 });
    B = PAT.setLane(PAT.humanize(B, { seed: 5, vel: 10, nudge: 5 }), "rat", 2, 0, 4);
    B.fill[2] = 1; B.locks = [{ step: 0, param: 5, value: 20 }];
    const d = PAT.diff(A, B);
    for (const i of d.steps) await rq(E.req.stepSet(i, B.steps[i]));
    for (const i of d.micro) await rq(E.req.microSet(0, i, B.micro[i]));
    for (const i of d.fill) await rq(E.req.fillSet(0, i, B.fill[i]));
    for (const l of d.lockDel) await rq(E.req.lockSet(0, l.step, l.param, null));
    for (const l of d.lockSet) await rq(E.req.lockSet(0, l.step, l.param, l.value));
    const s2 = [];
    for (let i = 0; i < info.nstep; i++) s2.push(E.parse[E.CMD.STEP_GET](await rq(E.req.stepGet(i))));
    const C = PAT.make(s2, E.parse[E.CMD.MICRO_GET](await rq(E.req.microGet(0))).micro, E.parse[E.CMD.FILL_GET](await rq(E.req.fillGet(0))).fill,
      E.parse[E.CMD.LOCK_GET](await rq(E.req.lockGet(0))).locks, 16);
    ok(d.n > 10 && PAT.diff(B, C).n === 0, "pattern: written to the mock with the device's commands, read back the same");
    link.close && link.close();
  }
}

/* ------------------------------------------------- Stallion: the editor's UX --- */
async function stallionUx() {
  /* the version as a user reads it */
  ok(E.fwShown("FELUCCA STALLION 1.0") === "STALLION 1.0" && E.fwShown("FELUCCA CLIPPER 1.3") === "CLIPPER 1.3" &&
     E.fwShown("FELUCCA SLOOP 2.6") === "FELUCCA SLOOP 2.6" && E.fwShown(undefined) === "",
     "Stallion: the version shown without FELUCCA (STALLION 1.0)");
  const shown = [...html.matchAll(/\$\("version"\)\.textContent = ([^;]+);|\["firmware", ([^\]]+)\]/g)].map((m) => m[1] || m[2]);
  ok(shown.filter((x) => x !== '""').length === 2 && shown.filter((x) => x !== '""').every((x) => /^fwShown\(/.test(x)),
    "Stallion: the device card and the System card show fwShown(version)");

  /* a slider dragged through 100 values in 1 s against the mock (4..16 ms an answer, one frame at a time). A gap
     between two answers is the pacer's wait (<= 16 ms) and the mock's answer (<= 16 ms), so on an idle machine <= 34
     ms. On a loaded one (the suite beside other builds) the process is not scheduled for a while and the gaps reached
     36..42 ms (133 under a stress load) with nothing wrong. So a probe timer runs beside the drag and records when
     this process was not running (each 2 ms sleep's lateness); each gap is judged without the time the probe was
     held up inside it, against the same 34 ms, and a drag over it is tried once more. A pacer that waits too long
     (a 150 ms timeout, a retry, a backlog, a longer interval) waits on timers, so the probe stays on time and the gap
     counts in full: it fails both tries */
  const m = E.makeMockDevice({ clipper: true, auto: false });
  const inp = [...m.access.inputs.values()][0], out = [...m.access.outputs.values()][0];
  const link = new E.Link((d) => out.send(d), { timeout: 300 });
  let seen = [];
  inp.onmidimessage = (e) => {
    const d = e.data;
    if (d[4] === E.CMD.SET && d[5] === 0 && d[6] === 3) seen.push({ t: performance.now(), v: (d[7] | (d[8] << 7)) - 8192 });   /* (an answer: the value it took) */
    link.receive(d);
  };
  const rq = (r, o) => link.request(r, o);
  E.parse[E.CMD.INFO](await rq(E.req.info()));
  const drag = async (k) => {
    const pace = new E.LivePacer(16), held = [];
    let want = 0, probing = true;
    const probe = (async () => {                       /* [from, to, ms late]: this process held up */
      while (probing) { const a = performance.now(); await sleep(2); const b = performance.now(); if (b - a > 3) held.push([a, b, b - a - 2]); }
    })();
    await rq(E.req.set(0, 3, 0));
    seen = [];
    for (let i = 0; i < 100; i++) {
      want = 20 + i;
      pace.push(k, (fin) => rq(E.req.set(0, 3, want), E.liveOpt(k, fin)), i === 99);
      await sleep(10);
    }
    while (pace.pending(k)) await sleep(5);
    probing = false;
    await probe;
    const lost = (a, b) => held.reduce((s, [x, y, l]) => s + Math.min(l, Math.max(0, Math.min(b, y) - Math.max(a, x))), 0);
    const gaps = seen.slice(1).map((x, i) => x.t - seen[i].t), net = seen.slice(1).map((x, i) => x.t - seen[i].t - lost(seen[i].t, x.t));
    const mid = new Set(seen.map((x) => x.v).filter((v) => v > 20 && v < 119)), worst = Math.max(...net);
    const g = E.parse[E.CMD.GET](await rq(E.req.get(0, 3)));
    if (process.env.UX_DEBUG) console.log(gaps.map((x, i) => `${x.toFixed(0)}/${net[i].toFixed(0)}`).join(" "));
    return { pass: mid.size >= 25 && worst <= 34 && seen[seen.length - 1].v === 119 && g.value === 119,
             txt: `${seen.length} SETs, ${mid.size} between, gaps <= ${worst.toFixed(0)} ms held up time aside (raw ${Math.max(...gaps).toFixed(0)}), last ${g.value}` };
  };
  let dr = await drag("0:3");
  const first = dr.pass ? "" : ` (tried again; first: ${dr.txt})`;
  if (!dr.pass) dr = await drag("0:3b");
  ok(dr.pass, `Stallion: a drag of 100 values in 1 s: ${dr.txt}${first}`);
  /* .. a burst at once: one send now, the latest one after (never a backlog); a send that fails: the next one still goes */
  let sent = [];
  const p2 = new E.LivePacer(16);
  for (let v = 0; v < 50; v++) p2.push("x", () => { sent.push(v); return sleep(5); });
  while (p2.pending("x")) await sleep(5);
  ok(sent.length === 2 && sent[0] === 0 && sent[1] === 49, `Stallion: 50 moves at once: 2 sends (${sent.join(" ")})`);
  sent = [];
  p2.push("y", () => Promise.reject(new Error("timeout")));
  p2.push("y", (fin) => { sent.push(fin); return Promise.resolve(); }, true);
  while (p2.pending("y")) await sleep(5);
  ok(sent.length === 1 && sent[0] === true, "Stallion: after a lost answer the final value still goes, as final");
  ok(js(E.liveOpt("k", false)) === js({ key: "k", front: true, retries: 0, timeout: 150, quiet: true }) && js(E.liveOpt("k", true)) === js({ key: "k", front: true }),
    "Stallion: live SETs ahead of reads, quick to give up; the final one with retries");
  link.close();
  m.stop && m.stop();
  /* no card waits for its slider to stop (a debounce of 80 ms sent only when it stopped) */
  ok(!/clearTimeout\((hyTimer|fxTimer|msTimer|DS\.timer)\)/.test(html) && /live\.push\("hy"/.test(html) && /live\.push\("fx"/.test(html) &&
     /live\.push\("ms"/.test(html) && /live\.push\("ds"/.test(html) && /input\.addEventListener\("input", \(\) => sxSet\(id, \+input\.value, false\)\)/.test(html),
     "Stallion: HYBRID, FMX, MOD SEQ, DRUM SYNTH and the settings send while their sliders move");

  /* the envelope's grab points: each lands where the path has it, and dragging it there gives the values back */
  let worst = 0, mono = true;
  for (let n = 0; n < 300; n++) {
    const v = [0, 1, 2, 3].map(() => Math.floor(Math.random() * 128)), p = E.ENVG.points(...v);
    const a = E.ENVG.drag("A", ...p.A, v), ds = E.ENVG.drag("DS", ...p.DS, v), r = E.ENVG.drag("R", ...p.R, v);
    worst = Math.max(worst, Math.abs(a[0] - v[0]), Math.abs(ds[1] - v[1]), Math.abs(ds[2] - v[2]), Math.abs(r[3] - v[3]));
  }
  for (let x = 0, last = -1; x <= 100; x++) { const a = E.ENVG.drag("A", x, 6, [0, 0, 0, 0])[0]; mono = mono && a >= last; last = a; }
  const p0 = E.ENVG.points(0, 0, 127, 0), p1 = E.ENVG.points(127, 127, 0, 127);
  ok(worst <= 1 && mono && p0.DS[1] === E.ENVG.TOP && p1.DS[1] === E.ENVG.BOT && p1.R[0] <= E.ENVG.W && E.ENVG.drag("DS", 0, -50, [0, 0, 0, 0])[2] === 127 &&
     E.ENVG.drag("A", 9999, 0, [0, 0, 0, 0])[0] === 127 && E.ENVG.drag("R", -9999, 0, [0, 0, 0, 0])[3] === 0,
     `Stallion: the envelope's grab points map back onto ATK DEC SUS REL (worst ${worst}), clamped, inside the picture`);
  ok(/class="envh"|"envh"/.test(html) && /addEventListener\("pointerdown"/.test(html) && /setPointerCapture/.test(html) && /touch-action: none/.test(html) &&
     /\.envh:hover/.test(html) && /\.envh\.on/.test(html) && /envtip/.test(html), "Stallion: grab points with pointer events (mouse, touch, pen), hover / active, the value shown");
  envPlayhead();
  lfoGraph();

  /* each language its own words: the editor's and the installer's tables, the installer's sections */
  const cjk = /[\u3040-\u30ff\u3400-\u9fff\uff00-\uffef]/, latin = /[A-Za-z]/;
  const table = (src) => { const tb = src.slice(src.indexOf("const TEXT = {"), src.indexOf("\n};", src.indexOf("const TEXT = {")) + 2); return vm.runInNewContext(tb.replace("const TEXT =", "(") + ")"); };
  const pkg = readFileSync(join(HERE, "index_pkg.html"), "utf8");
  const TE = table(html), TP = table(pkg);
  const bad = [];
  for (const [nm, T] of [["editor", TE], ["installer", TP]])
    for (const l of Object.keys(T)) for (const [k, v] of Object.entries(T[l])) {
      const sv = Array.isArray(v) ? v.join(" ") : String(v);
      if (l !== "ja" && cjk.test(sv)) bad.push(`${nm} ${l}.${k}`);
    }
  const sect = [...pkg.matchAll(/<section[^>]*data-l="(en|fr)"[^>]*>([\s\S]*?)<\/section>/g)].filter((x) => cjk.test(x[2])).map((x) => x[1]);
  ok(!bad.length && !sect.length && !latin.test(TE.ja.other) && !latin.test(TP.ja.other) && !cjk.test(TE.en.other) && /English/.test(TE.en.other) &&
     /日本語/.test(TE.ja.other) && /français/.test(TP.fr.other) && /<html lang="en">/.test(html),
     `Stallion: each language only its own words (the language button: the one shown)${bad.length ? " " + bad.join(" ") : ""}${sect.length ? " sections " + sect : ""}`);
  /* the installer's counts: no number typed by hand, every one filled by make_site.py */
  const typed = /\b\d+ (synthesis engines|sounds|moteurs de synthèse|sons)\b|\d+ の(音源エンジン|音色)|<b>\d+<\/b><span data-t="t(Eng|Snd|Hy|Mx)"/.test(pkg);
  let filled = "";
  try { filled = py("import sys; sys.path.insert(0, sys.argv[1]); import make_site; print(make_site.fill_counts(open(sys.argv[2], encoding='utf-8').read(), {'ENGINES': 27, 'SOUNDS': 517, 'FILTERS': 9, 'TUNINGS': 13}).count('{{'))", HERE, join(HERE, "index_pkg.html")).toString().trim(); } catch (e) { filled = e.message; }
  ok(!typed && /\{\{ENGINES\}\} synthesis engines and \{\{SOUNDS\}\} sounds/.test(pkg) && filled === "0",
    `Stallion: the installer's counts come from the firmware (make_site.py firmware_counts), none typed (${filled})`);
}

/* ------------------------------------------- the connected-device banner --- */
/* The banner's code (editor.html between DEVBAR-BEGIN / END) in a small stand-in page, against the mock device answering
   the update handshake (cmd 0x11) as the firmware does (ota.c ota_reply_identity: "FM-1_104200", "SMK-37 Pro_104200") */
function dvHarness() {
  const blk = html.slice(html.indexOf("/*DEVBAR-BEGIN*/"), html.indexOf("/*DEVBAR-END*/"));
  const tb = html.slice(html.indexOf("const TEXT = {"), html.indexOf("\n};", html.indexOf("const TEXT = {")) + 2);
  const TEXT = vm.runInNewContext(tb.replace("const TEXT =", "(") + ")");
  const els = {};
  const fake = () => { const cl = new Set(); return { hidden: false, textContent: "", value: "", className: "", dataset: {}, kids: [], attrs: {},
    setAttribute(k, v) { this.attrs[k] = v; }, replaceChildren(...k) { this.kids = k; }, addEventListener() {},
    classList: { add: (c) => cl.add(c), remove: (c) => cl.delete(c), contains: (c) => cl.has(c), toggle: (c, on) => (on ?? !cl.has(c)) ? cl.add(c) : cl.delete(c) } }; };
  const timers = [];
  const ctx = {
    console, Date, Promise, Map, Set, Uint8Array, String, Math,
    setTimeout: (f, ms) => { if (ms >= 1000) { timers.push(f); return timers.length; } return setTimeout(f, ms); }, clearTimeout,
    $: (id) => (els[id] ||= fake()),
    el: (tag, a = {}) => ({ tag, value: a.value, text: a.text }),
    t: (k) => TEXT.en[k] ?? k,
    fwShown: E.fwShown, fm1Ports: E.fm1Ports, localStorage: { getItem: () => null, setItem: () => {} },
    dev: null, MOCK: false,
  };
  vm.createContext(ctx);
  vm.runInContext(blk + `
;Object.assign(globalThis, { DB, devIdParse, devIdReply, devIdQuery, devEntry, devIdentify, devbarOn, devbarOff, devbarSet, devbarInfo,
  devbarRender, devMockBoard, devOrder, devChoose, devSig, DEV_PORT, devProbeOrder, devRelease, devNumber, devOurs });`, ctx);
  return { ctx, els, TEXT, timers };
}
async function editorDevbar() {
  if (html.indexOf("/*DEVBAR-BEGIN*/") < 0) { ok(false, "device banner: DEVBAR block in editor.html"); return; }
  const H = dvHarness(), X = H.ctx, T = H.TEXT.en, name = () => H.els.dvname.textContent, cls = () => H.els.devbar.className;
  /* the identity as the firmware sends it (ota.c: 00 59 11 len 27 bytes ~sum, packed 7 bits LSB first) */
  const wire = (txt) => { const id = Array.from({ length: 27 }, (_, i) => (i < txt.length ? txt.charCodeAt(i) : 0));
    const body = [0, 0x59, 0x11, 27, 0, 0, ...id, ~id.reduce((a, b) => a + b, 0) & 255]; return [0xF0, ...pack7(body), 0xF7]; };
  const fm = X.devIdParse(wire("FM-1_104200")), smk = X.devIdParse(wire("SMK-37 Pro_104200")), ld = X.devIdParse(wire("ota-FM-1_104200"));
  ok(fm && fm.model === "FM-1" && smk && smk.model === "SMK-37 Pro" && ld && ld.model === "FM-1" && X.devIdParse(wire("FM-1_104200").slice(0, 20)) === null
    && eq(X.devIdReply("SMK-37 Pro_104200"), wire("SMK-37 Pro_104200")), "device banner: the identity the firmware reports (FM-1_104200, SMK-37 Pro_104200) read");
  const fwsrc = readFileSync(join(HERE, "../firmware/src/felucca.c"), "utf8") + readFileSync(join(HERE, "../firmware/hal/fm1_cc.h"), "utf8");
  ok(/#define FELUCCA_ID "SMK-37 Pro_104200"/.test(fwsrc) && /#define FELUCCA_ID "FM-1_104200"/.test(fwsrc) && /#define SMK_MODEL "SMK-37 Pro"/.test(fwsrc),
    "device banner: ... the identities felucca.c / fm1_cc.h build in");
  ok(X.devOurs(104210) && X.devOurs(104200) && X.devOurs(900) && X.devOurs(913) && !X.devOurs(15) && !X.devOurs(10001400) && !X.devOurs(1500),
    "device banner: ours: Stallion's own number (1042XY), the 9XX family (Stallion's 900, Clipper, SLOOP); not the stock or another number");
  /* the mock answers the handshake as each board; the editor's protocol still goes through the same port */
  const mk = (txt, n) => { const m = E.makeMockDevice({ auto: false }); X.devMockBoard(m, txt, n); return m; };
  const port = (m, k) => [...m.access[k].values()][0];
  const mf = mk("FM-1_104200", 0), ms = mk("SMK-37 Pro_104200", 1), mo = { access: {   /* firmware without the answer */
    inputs: new Map([["old-in", { id: "old-in", name: "Stallion", type: "input", onmidimessage: null }]]),
    outputs: new Map([["old-out", { id: "old-out", name: "Stallion", type: "output", send: () => {} }]]) } };
  const qf = await X.devIdQuery(port(mf, "inputs"), port(mf, "outputs")), qs = await X.devIdQuery(port(ms, "inputs"), port(ms, "outputs"));
  const t0 = Date.now(), qo = await X.devIdQuery(port(mo, "inputs"), port(mo, "outputs")), slow = Date.now() - t0;
  const l = new E.Link((d) => port(ms, "outputs").send(d), { timeout: 300 });
  port(ms, "inputs").onmidimessage = (e) => l.receive(e.data);
  const info = E.parse[E.CMD.INFO](await l.request(E.req.info()));
  ok(qf && qf.model === "FM-1" && qs && qs.model === "SMK-37 Pro" && qo === null && slow < 1500 && info.nengines > 0,
    `device banner: the mock FM-1 and SMK-37 Pro answer the handshake, old firmware does not (${slow} ms), INFO still answered`);
  /* the banner: FM-1, unplugged, then the SMK-37 Pro: its name, its colour, the change said */
  const pf = { input: port(mf, "inputs"), output: port(mf, "outputs") }, ps = { input: port(ms, "inputs"), output: port(ms, "outputs") };
  X.devbarSet("off");
  ok(name() === T.dvOff && /\bst-off\b/.test(cls()) && H.els.dvnote.hidden && /USB/.test(H.els.dvsub.textContent),
    `device banner: not connected said in words, red (${name()})`);
  X.devbarSet("connecting");
  ok(name() === T.dvConnecting && /\bst-connecting\b/.test(cls()), "device banner: connecting");
  X.devbarOn(X.devEntry(pf, qf));
  X.devbarInfo({ version: "FELUCCA STALLION 1.0" });
  ok(name() === "FM-1" && /\bst-on\b/.test(cls()) && /\bk-fm1\b/.test(cls()) && !/\bflash\b/.test(cls()) && H.els.dvnote.hidden
    && /STALLION 1\.0/i.test(H.els.dvsub.textContent) && !/FELUCCA/.test(H.els.dvsub.textContent) && H.els.devname.textContent === "FM-1",
    `device banner: FM-1 from the device's identity, firmware and version (${H.els.dvsub.textContent})`);
  X.devbarOff("lost");
  ok(name() === T.dvOff && /\bst-off\b/.test(cls()) && H.els.dvsub.textContent.includes("FM-1"), `device banner: unplugged: not connected, which one was (${H.els.dvsub.textContent})`);
  X.devbarOn(X.devEntry(ps, qs));
  ok(name() === "SMK-37 Pro" && /\bk-smk\b/.test(cls()) && /\bflash\b/.test(cls()) && !H.els.dvnote.hidden && H.els.dvnote.textContent === T.dvNow + "SMK-37 Pro"
    && T.dvNow === "Now connected: ", `device banner: the other device: "${H.els.dvnote.textContent}", highlighted`);
  H.timers.splice(0).forEach((f) => f());
  ok(!/\bflash\b/.test(cls()) && H.els.dvnote.hidden && name() === "SMK-37 Pro", "device banner: ... the highlight ends, the name stays");
  X.devbarOn(X.devEntry(ps, qs));
  ok(!/\bflash\b/.test(cls()), "device banner: the same device again: no change said");
  /* old firmware: the port name, marked as such */
  X.devbarOn(X.devEntry({ input: port(mo, "inputs"), output: port(mo, "outputs") }, null));
  ok(name() === "Stallion" && /\bk-other\b/.test(cls()) && H.els.dvsub.textContent.includes(T.dvFromPort), "device banner: old firmware: the USB port's name, marked as such");
  /* the demo */
  X.MOCK = true;
  X.devbarOn(X.devEntry(pf, qf));
  ok(name() === T.dvDemo && T.dvDemo === "DEMO — no device" && /\bst-demo\b/.test(cls()) && H.els.dvsub.textContent.includes("FM-1"),
    `device banner: demo mode labelled "${name()}"`);
  X.MOCK = false;
  /* two devices: each its own port pair, both listed, one picked */
  const two = { inputs: new Map([...mf.access.inputs, ...ms.access.inputs]), outputs: new Map([...mf.access.outputs, ...ms.access.outputs]) };
  const pairs = E.fm1Ports(two);
  ok(pairs.length === 2 && pairs[0].input === pf.input && pairs[0].output === pf.output && pairs[1].input === ps.input && pairs[1].output === ps.output,
    "device banner: two devices of one port name: each input with its own output");
  const idn = await X.devIdentify(pairs, pairs[0]);
  X.DB.list = idn.list;
  X.devbarOn(idn.cur);
  const opts = H.els.dvpick.kids;
  ok(idn.list.length === 2 && idn.cur.model === "FM-1" && !H.els.dvpickw.hidden && opts.length === 2 && /FM-1/.test(opts[0].text) && /SMK-37 Pro/.test(opts[1].text)
    && H.els.dvmany.textContent.includes("2"), `device banner: two devices said, a picker (${opts.map((o) => o.text).join(" | ")})`);
  X.DB.pick = ps.input.id;
  ok(X.devOrder(pairs)[0].input === ps.input, "device banner: the picked one is tried first");
  X.DB.list = [idn.cur];
  X.devbarRender();
  ok(H.els.dvpickw.hidden, "device banner: one device: no picker");
  /* (fix 1) only Stallion ports are asked: generic names other gear uses get no handshake, unless that pair answered INFO */
  const isHs = (d) => d.length === 10 && [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7].every((b, i) => b === d[i]);
  const spy = (id, name) => { const o = { id: id + "-out", name, type: "output", sent: [], closed: 0, send(d) { this.sent.push(Array.from(d)); }, close() { this.closed++; return Promise.resolve(); } };
    return { input: { id: id + "-in", name, type: "input", onmidimessage: null, closed: 0, close() { this.closed++; return Promise.resolve(); } }, output: o }; };
  const gUsb = spy("usb", "USB-MIDI"), gIac = spy("iac", "IAC Driver Bus 1");
  const hsTo = (pr) => pr.output.sent.filter(isHs).length;
  await X.devIdentify([pf, gUsb, gIac], pf);
  await X.devIdentify([gUsb, gIac], null);
  ok(hsTo(gUsb) === 0 && hsTo(gIac) === 0 && X.DEV_PORT.test("Stallion") && X.DEV_PORT.test("SMK-37 Pro") && !X.DEV_PORT.test("USB-MIDI"),
    `device banner: "USB-MIDI" and "IAC Driver" ports get no handshake (${hsTo(gUsb)}, ${hsTo(gIac)})`);
  const gCur = spy("cur", "USB-MIDI");
  await X.devIdentify([gCur, gUsb], gCur);
  ok(hsTo(gCur) > 0 && hsTo(gUsb) === 0, "device banner: ... the connected pair (it answered INFO) is asked whatever its name");
  /* (fix 6) the pairs not used are let go, the connected one kept */
  const rCur = spy("rcur", "Stallion"), rOther = spy("roth", "Stallion");
  await X.devIdentify([rCur, rOther], rCur, X.devEntry(rCur, qf));
  ok(rOther.input.closed === 1 && rOther.output.closed === 1 && rCur.input.closed === 0 && rCur.output.closed === 0 && gUsb.input.closed > 0,
    "device banner: after asking, the other pairs' ports are closed, the connected pair's kept open");
  /* the connected output shared by two pairs (fm1Ports fallbacks) is never closed */
  {
    const shared = spy("sh", "FM-1"), usb = spy("shu", "USB-MIDI");
    usb.output = shared.output;
    await X.devIdentify([shared, usb], shared, X.devEntry(shared, qf));
    ok(shared.output.closed === 0 && shared.input.closed === 0 && usb.input.closed === 1,
      `device banner: a shared output of the connected pair is never closed (${shared.output.closed})`);
  }
  /* the INFO probe order: Stallion-named pairs first, generic names after */
  {
    const g = spy("pg", "USB-MIDI"), s1 = spy("ps", "Stallion"), s2 = spy("pf", "FM-1");
    const ord = X.devProbeOrder([g, s1, s2]).map((p) => p.input.name).join();
    ok(ord === "Stallion,FM-1,USB-MIDI", `device banner: INFO tried on Stallion-named ports first (${ord})`);
  }
  /* (fix 2) a device in update mode (the loader, "ota-FM-1_104200"): said, kept out of the picker, never the pick */
  const ml = mk("ota-FM-1_104200", 3), pl = { input: port(ml, "inputs"), output: port(ml, "outputs") };
  ok(ld.loader === true && fm.loader === false, "device banner: the loader's identity marked as update mode");
  const idl = await X.devIdentify([pf, pl], pf);
  X.DB.list = idl.list; X.DB.loaders = idl.loaders;
  X.devbarOn(idl.cur);
  ok(idl.list.length === 1 && idl.loaders.length === 1 && H.els.dvpickw.hidden && !H.els.dvnote.hidden
    && H.els.dvnote.textContent.includes("FM-1 — UPDATE MODE: finish it in the installer") && name() === "FM-1",
    `device banner: a second device in update mode said, not in the picker (${H.els.dvnote.textContent})`);
  let stored = 0;
  X.localStorage = { getItem: () => null, setItem: () => { stored++; } };
  const pick0 = X.DB.pick;
  X.devChoose(pl.input.id);
  ok(stored === 0 && X.DB.pick === pick0, "device banner: ... a device in update mode is never stored as the pick");
  X.DB.list = []; X.DB.loaders = (await X.devIdentify([pl], null)).loaders;
  X.devbarOff("noreply");
  ok(name() === T.dvOff && H.els.dvnote.textContent === "FM-1" + T.dvUpdate && !H.els.dvnote.hidden,
    `device banner: only a device in update mode: not connected, "${H.els.dvnote.textContent}"`);
  const msl = mk("ota-SMK-37 Pro_104200", 4);
  X.DB.loaders = (await X.devIdentify([{ input: port(msl, "inputs"), output: port(msl, "outputs") }], null)).loaders;
  X.devbarRender();
  ok(H.els.dvnote.textContent === "SMK-37 Pro — UPDATE MODE: finish it in the installer", `device banner: ... the SMK-37 Pro too (${H.els.dvnote.textContent})`);
  X.DB.loaders = [];
  {
    const la = mk("ota-FM-1_104200", 6), lb = mk("ota-FM-1_104200", 7);
    X.DB.loaders = (await X.devIdentify([{ input: port(la, "inputs"), output: port(la, "outputs") }, { input: port(lb, "inputs"), output: port(lb, "outputs") }], null)).loaders;
    X.devbarRender();
    ok(H.els.dvnote.textContent === "FM-1 #1 — UPDATE MODE: finish it in the installer  ·  FM-1 #2 — UPDATE MODE: finish it in the installer",
      `device banner: two loaders of one model numbered (${H.els.dvnote.textContent})`);
    X.DB.loaders = [];
  }
  /* (fix 3) two of one model: numbered by port order, in the banner and the picker */
  const mf2 = mk("FM-1_104200", 5), pf2 = { input: port(mf2, "inputs"), output: port(mf2, "outputs") };
  const id2 = await X.devIdentify([pf, pf2], pf2);
  X.DB.list = id2.list;
  X.devbarOn(id2.cur);
  const o2 = H.els.dvpick.kids.map((o) => o.text);
  ok(name() === "FM-1 #2" && id2.list[0].label === "FM-1 #1" && /^FM-1 #1 /.test(o2[0]) && /^FM-1 #2 /.test(o2[1]) && H.els.devname.textContent === "FM-1 #2",
    `device banner: two FM-1s numbered by port order (${name()}; ${o2.join(" | ")})`);
  const id3 = await X.devIdentify([pf, ps], pf);
  ok(id3.list.map((e) => e.label).join() === "FM-1,SMK-37 Pro", "device banner: ... different models not numbered");
  X.DB.list = [];
  /* (fix 5) version 000: the recovery firmware */
  const rec = X.devIdParse(wire("FM-1_000"));
  X.devbarOn(X.devEntry(pf, rec));
  ok(rec && rec.ver === 0 && !rec.loader && H.els.dvsub.textContent.includes(T.dvRecovery) && T.dvRecovery === "RECOVERY MODE" && !H.els.dvsub.textContent.includes(T.dvOtherFw),
    `device banner: FM-1_000 said as recovery mode (${H.els.dvsub.textContent})`);
  /* the page: large type, colours with words, the connect path */
  const css = html.slice(html.indexOf("<style>"), html.indexOf("</style>"));
  const fs = +((/#dvname\s*\{[^}]*font-size:\s*(\d+)px/.exec(css) || [])[1] || 0);
  ok(fs >= 28 && /#dvname\s*\{[^}]*font-weight:\s*(700|bold)/.test(css) && /\.devbar[\w.-]*\.k-fm1 \{ --dvc: var\(--dvfm1\)/.test(css) && /\.devbar[\w.-]*\.k-smk \{ --dvc: var\(--dvsmk\)/.test(css) && /\.devbar\.st-off/.test(css)
    && /\.devbar\.st-demo/.test(css) && /prefers-reduced-motion/.test(css), `device banner: the device's name ${fs} px bold, a colour per device and state`);
  /* (fix 4) the details, the note, "N devices found" and the picker large enough to read: at least 20 px (24 / 28) */
  const px = (re) => +((re.exec(css) || [])[1] || 0);
  const sz = { sub: px(/#dvsub\s*\{[^}]*font-size:\s*(\d+)px/), note: px(/#dvnote\s*\{[^}]*font-size:\s*(\d+)px/),
    many: px(/#dvmany\s*\{[^}]*font-size:\s*(\d+)px/), pick: px(/\.dvpick select\s*\{[^}]*font-size:\s*(\d+)px/) };
  const lh = px(/#dvsub\s*\{[^}]*line-height:\s*(\d+)px/);
  ok(Object.values(sz).every((v) => v >= 20) && lh >= sz.sub, `device banner: details ${sz.sub}, note ${sz.note}, count ${sz.many}, picker ${sz.pick} px (line ${lh} px)`);
  const conn = html.slice(html.indexOf("async function connect()"), html.indexOf("function onState("));
  ok(/devIdentify\(/.test(conn) && /devbarOn\(/.test(conn) && /devbarOff\(/.test(conn) && /devOrder\(/.test(conn) && /devMockBoard\(/.test(conn)
    && /<header class="top">\s*<div class="devbar[^"]*" id="devbar"/.test(html), "device banner: at the top of the page, set by connect / disconnect");
}

/* ------------------------------------------------- editor tabs and strings --- */
function editorTabs() {
  const tabs = [...html.matchAll(/<button role="tab" data-tab="(\w+)"/g)].map((x) => x[1]);
  const panels = [...html.matchAll(/<section class="panel" id="p-(\w+)" data-tab="(\w+)"/g)].map((x) => [x[1], x[2]]);
  const TABS = JSON.parse((/const TABS = (\[[^\]]*\]);/.exec(html) || [])[1] || "[]");
  ok(tabs.length === 10 && js(tabs) === js(TABS) && js(panels.map((x) => x[1])) === js(TABS) && panels.every(([a, b]) => a === b),
    `editor: ${tabs.length} tabs, one panel each (${tabs.join(" ")})`);
  ok(/localStorage\.setItem\(TAB_KEY/.test(html) && /try \{ localStorage/.test(html) && /history\.replaceState\([^)]*"#" \+ name\)/.test(html)
    && /addEventListener\("hashchange"/.test(html), "editor: last tab in localStorage (try/catch) and in the URL hash");
  /* every string key in both languages */
  const tb = html.slice(html.indexOf("const TEXT = {"), html.indexOf("\n};", html.indexOf("const TEXT = {")) + 2);
  const TEXT = vm.runInNewContext(tb.replace("const TEXT =", "(") + ")");
  const ja = new Set(Object.keys(TEXT.ja)), en = new Set(Object.keys(TEXT.en));
  const used = new Set([...html.matchAll(/data-t="(\w+)"|\bt\("(\w+)"\)|sayK\("(\w+)"|hint = "(\w+)"/g)].map((x) => x[1] || x[2] || x[3] || x[4]));
  for (const k of ["needDevice", "smpNone", "bankConnect", "bankNone", "selectedTrack", "selectTrack", "drumHelp", "notesHelp", "live", "polling"]) used.add(k);
  const miss = [...used].filter((k) => !ja.has(k) || !en.has(k));
  const odd = [...ja].filter((k) => !en.has(k)).concat([...en].filter((k) => !ja.has(k)));
  ok(!miss.length && !odd.length, `editor: every string in ja and en (${used.size} used${miss.length ? ", missing " + miss : ""}${odd.length ? ", one language only " + odd : ""})`);
  /* the page script parses (the browser's view of it) */
  const script = html.slice(html.indexOf("<script>") + 8, html.lastIndexOf("</script>"));
  let err = null;
  try { new vm.Script(script); } catch (e) { err = e.message; }
  ok(!err, "editor: page script compiles" + (err ? ` (${err})` : ""));
  {   /* (1.0) a function or a top-level const declared twice in the page: the later one silently wins */
    const names = [...script.matchAll(/^(?:async )?function (\w+)|^(?:const|let) (\w+)/gm)].map((m) => m[1] || m[2]);
    const twice = [...new Set(names.filter((n, i) => names.indexOf(n) !== i))];
    ok(!twice.length, "editor: no top-level name declared twice" + (twice.length ? ` (${twice.join(" ")})` : ""));
  }
  {   /* 2.4: the device's palette as tokens (:root), used through var(--...); outside them only #000 (text on a colour) */
    const css = html.slice(html.indexOf("<style>"), html.indexOf("</style>")).replace(/:root\s*\{[^}]*\}/g, "");
    const hex = [...css.matchAll(/#[0-9a-f]{3,6}\b/gi)].map((m) => m[0].toLowerCase()).filter((c) => c !== "#000");
    ok(!hex.length && /--t1: #287cff; --t2: #1ecc70; --t3: #ffc618; --t4: #ff621a;/.test(html),
      "editor: the track colours of the device (ui_studio.c TE_COL), colours only as tokens" + (hex.length ? ` (${hex.join(" ")})` : ""));
    const te = readFileSync(join(HERE, "../firmware/src/ui_studio.c"), "utf8");
    ok(/TE_COL\[4\] = \{RGB\(40, 124, 255\), RGB\(30, 204, 112\), RGB\(255, 198, 24\), RGB\(255, 98, 26\)\}/.test(te),
      "editor: ... the same four as the firmware's TE_COL");
    ok(/SLOOP-FONT\*\/url\(data:font\/ttf;base64,[A-Za-z0-9+\/=]{20000,}\)/.test(html), "editor: the device's Terminus font inlined (tools/gen_webfont.py)");
  }
}

/* ------------------------------- editor: Clipper's pages == the device's (params.c PAGES, CLIPPER_SYNTH) --- */
function editorClipperPages() {
  const lay = html.slice(html.indexOf("const EXPECT = {"), html.indexOf("/* globals that are actions"));
  const syn = html.slice(html.indexOf("const INS_BITS"), html.indexOf("/* the INSERT's TYPE or the arp's MODE changed"));
  const X = vm.runInNewContext(lay + syn + ";({ LAYOUT, layoutKnown, synDesc, INS_DESC, INS_RATE, INS_NTYPE, INS_ROW, INS_SEMI, INS_DMS, AM_TURING })", { F: E.F, dev: null });
  const ch = readFileSync(join(HERE, "../firmware/src/core.h"), "utf8"), pc = readFileSync(join(HERE, "../firmware/src/params.c"), "utf8");
  /* the synth build's ids (core.h, CLIPPER_SYNTH 1) and its aliases */
  const P = {}; let k = 0;
  for (const nm of (/enum \{\s*\/\* per-track parameters \*\/([^}]*)\}/.exec(ch) || [])[1].replace(/#(if|endif)[^\n]*/g, "").replace(/\/\*[\s\S]*?\*\//g, "").split(",").map((x) => x.trim()).filter(Boolean)) P[nm] = k++;
  const G = {}; k = 0;
  for (const nm of (/enum \{\s*\/\* global parameters \*\/([^}]*)\}/.exec(ch) || [])[1].replace(/#(if|endif)[^\n]*/g, "").replace(/\/\*[\s\S]*?\*\//g, "").split(",").map((x) => x.trim()).filter(Boolean)) G[nm] = k++;
  for (const [, a, b] of ch.matchAll(/#define ([PG]_\w+) ([PG]_\w+)/g)) (a[0] === "P" ? P : G)[a] = (b[0] === "P" ? P : G)[b];
  /* PAGES as the synth build has it: #if CLIPPER_SYNTH kept, its #else and #if !CLIPPER_SYNTH dropped */
  const src = pc.slice(pc.indexOf("static const page_t PAGES[]"), pc.indexOf("#define NPAGES"));
  let keep = [true]; const lines = [];
  for (const l of src.split("\n")) {
    if (/^#if !CLIPPER_SYNTH/.test(l)) keep.push(false);
    else if (/^#if CLIPPER_SYNTH/.test(l)) keep.push(true);
    else if (/^#else/.test(l)) keep[keep.length - 1] = !keep[keep.length - 1];
    else if (/^#endif/.test(l)) keep.pop();
    else if (keep.every(Boolean)) lines.push(l);
  }
  const pages = [...lines.join("\n").matchAll(/\{"([^"]+)", [\w ?:]+, (SC_TRACK|SC_GLOBAL), GR_\w+, \{([^}]*)\}\}/g)]
    .map(([, t, sc, ids]) => [t, sc, ids.split(",").map((x) => x.trim()).filter((x) => x !== "0xFF").map((x) => (sc === "SC_TRACK" ? P : G)[x])]);
  /* the values from P_FSLOPE to P_E0 are the device's own (core.h P_EDH0: the editor protocol passes over them): the
     protocol's ids are core.h's without them, P_E0 .. P_E7 right after P_M12AMT as in 1.0 */
  const HN = P.P_E0 - P.P_FSLOPE, PE0 = P.P_E0 - HN, PCOUNT = P.P_COUNT - HN;
  const L = X.LAYOUT(PE0), on = (s) => new Set(L.flatMap((g) => g.pages.filter((x) => x[1] === s).flatMap((x) => x[2])));
  const p0 = on(0), g1 = on(1), all0 = L.flatMap((g) => g.pages.filter((x) => !x[1]).flatMap((x) => x[2]));
  const devP = new Set(pages.filter((x) => x[1] === "SC_TRACK").flatMap((x) => x[2]).filter((id) => id < P.P_FSLOPE || id >= P.P_E0)
    .map((id) => (id >= P.P_E0 ? id - HN : id)));
  const devG = pages.filter((x) => x[1] === "SC_GLOBAL").flatMap((x) => x[2]).filter((id) => ![G.G_INFO, G.G_SLOT, G.G_LOAD, G.G_SAVE, G.G_CLRSEQ, G.G_INITSND, G.G_NEWPRJ].includes(id));
  const missP = [...devP].filter((id) => !p0.has(id)), missG = devG.filter((id) => !g1.has(id));
  const notOn = Array.from({ length: PCOUNT }, (_, i) => i).filter((i) => !p0.has(i));
  ok(pages.length > 20 && PE0 === 119 && PCOUNT === 127 && G.G_COUNT === 35 && G.G_DMODE === 33 && G.G_RSHIM === 34
    && pages.some((x) => x[0] === "FILTER" && x[2].includes(P.P_FSLOPE))
    && pages.some((x) => x[0] === "FILTER 2" && x[2].every((id) => id >= P.P_FSLOPE && id < P.P_E0))
    && X.layoutKnown({ pcount: 127, pe0: 119, gcount: G.G_COUNT }) && X.layoutKnown({ pcount: 126, pe0: 118, gcount: G.G_COUNT })
    && X.layoutKnown({ pcount: 126, pe0: 118, gcount: 34 })
    && X.layoutKnown({ pcount: 99, pe0: 91, gcount: 33 })
    && !missP.length && !missG.length && js(notOn) === js([P.P_MUTE]) && all0.length === new Set(all0).size,
    `editor: Clipper 1.0's pages hold every value of the device's (params.c PAGES; not on one: MUTE${missP.length || missG.length ? `; missing P ${missP} G ${missG}` : ""}${notOn.length !== 1 ? `; off the pages ${notOn}` : ""})`);
  const title = (id, s) => (L.flatMap((g) => g.pages).find((x) => x[1] === s && x[2].includes(id)) || [])[0];
  ok(title(P.P_FMODEL, 0) === "FILTER" && title(P.P_VELF, 0) === "MOD" && title(P.P_VINT, 0) === "VOICE 2" && title(P.P_SLOOP, 0) === "SPICE"
    && title(G.G_RTYPE, 1) === "REVERB" && title(G.G_RPRE, 1) === "REVERB" && title(G.G_EQLOW, 1) === "EQ+COMP" && title(G.G_SYNC, 1) === "SYSTEM"
    && !L.some((g) => g.pages.some((x) => x[0] === "SLICER" || x[0] === "DRUMS"))
    && title(P.P_SPREAD, 0) === "VOICE 3" && title(P.P_L3MODE, 0) === "LFO 3" && title(P.P_E2LOOP, 0) === "ENV 2 LOOP"
    && title(P.P_MAC3, 0) === "MACROS" && title(P.P_M7SRC, 0) === "MATRIX 7" && title(P.P_M12AMT, 0) === "MATRIX 12"
    && title(G.G_DMODE, 1) === "DLY 2" && title(G.G_RSHIM, 1) === "REVERB 2" && title(P.P_PTGT, 0) === "MOD"
    && X.LAYOUT(91).flatMap((g) => g.pages).filter((x) => /^MATRIX/.test(x[0])).length === 6,
    "editor: ... grouped as the device (FILTER MODL, LFO > MOD, VOICE 2 VINTG, SPICE LOOP, REVERB TYPE PRE, EQ+COMP, SYSTEM; 1.0: VOICE 3 SPREAD, LFO 3, ENV 2 LOOP, MACROS, MATRIX 7..12, DLY 2 MODE, REVERB 2 SHIM; 0.9's: 6 slots)");
  /* MARE's EDIT 3 (FLTFM, RING: v15 ids 139, 140) only on a firmware that has them: an older Stallion says
     pe0 119 too, with 139 ids */
  const ed3 = (eng, pall) => (X.LAYOUT(119, eng, pall).flatMap((g) => g.pages).find((x) => x[0] === "EDIT 3") || [0, 0, []])[2].join(",");
  ok(ed3("MARE", 141) === "139,140" && ed3("MARE", 178) === "139,140,177" && ed3("MARE", 139) === "" && ed3("MARE", 127) === ""
    && ed3("ANALOG", 141) === "" && ed3(undefined, undefined) === "139,140,177",
    "editor: EDIT 3 (VCO3 VCO4, WAV34 from 178 ids; FLTFM RING before) on MARE with 141 ids; not with 139 (an older Stallion, pe0 119 too), 127, nor on another engine");
  ok(ed3("PONY", 182) === "178,179,180,181" && ed3("PONY", 178) === "" && ed3("ANALOG", 182) === "" && ed3("MARE", 182) === "139,140,177",
    "editor: PONY's EDIT 3 (RANGE KYBD VCA STEPS, v15 ids 178..181) on PONY with 182 ids, not before, nor on another engine");
  ok(ed3("RODEO", 184) === "182,183" && ed3("RODEO", 182) === "" && ed3("PONY", 184) === "178,179,180,181" && ed3("ANALOG", 184) === "",
    "editor: RODEO's EDIT 3 (KYBD PW0, v15 ids 182 183) on RODEO with 184 ids, not before, nor on another engine");
  const fil = (eng) => (X.LAYOUT(119, eng, 182).flatMap((g) => g.pages).find((x) => x[0] === "FILTER") || [0, 0, []])[2].join(",");
  ok(fil("PONY") === "50,46,127,132" && fil("ANALOG") === "50,46,127,132" && fil("FM6") === "50,127,132" && fil(undefined) === "50,46,127,132",
    "editor: FX > FILTER's MODL on the engines with a MODEL filter (ANALOG WAVE VECTOR PONY), hidden on the others (FM6)");
  /* the labels the device shows and DESC does not (params.c track_desc) */
  const pcs = (re) => ((re.exec(pc) || [])[1] || "").match(/"[^"]*"/g).map((x) => x.slice(1, -1));
  const amode = pcs(/N_AMODE\[\] = \{([^}]*)\}/), itype = pcs(/N_ITYPE\[\] = \{([^}]*)\}/), irate = pcs(/N_IRATE\[\] = \{([^}]*)\}/);
  const insc = [...((/INS_DESC\[\d+\]\[3\] = \{([\s\S]*?)\n\};/.exec(pc) || [])[1] || "").matchAll(/PU\("([^"]+)"/g)].map((x) => x[1]);
  const irow = (((/INS_ROW\[\] = \{([^}]*)\}/.exec(pc) || [])[1]) || "").split(",").map(Number);
  const isemi = pcs(/N_ISEMI\[\] = \{([^}]*)\}/), idms = pcs(/N_IDMS\[\] = \{([^}]*)\}/);
  ok(amode.indexOf("TURNG") === X.AM_TURING && itype.length === X.INS_NTYPE && itype[9] === "BBD1" && js(irate) === js(X.INS_RATE)
    && js(insc) === js(X.INS_DESC.flat().map((x) => x[0])) && js(irow) === js(X.INS_ROW) && itype[16] === "LOCMP"
    && js(isemi) === js(X.INS_SEMI) && js(idms) === js(X.INS_DMS),
    "editor: TURNG, the INSERT's types, CRUSH's RATE names and the A B C labels as params.c (1.1: each type's row, PITCH's semitones, DELAY's ms)");
  const info = { pe0: 91 }, pd = [];
  pd[91] = { label: "CUT" }; pd[92] = { label: "-" }; pd[98] = { label: "PTCH" };
  const dst = { label: "DST", fmt: E.F.ENUM, min: 0, max: 14, def: 0, names: ["OFF", "PITCH", "CUT", "SHAPE", "AMP", "PAN", "LFO2", "E1", "E2", "E3", "E4", "E5", "E6", "E7", "E8"] };
  const insA = { label: "INS A", fmt: E.F.PCT, min: 0, max: 127, def: 64, names: [] }, spice = { label: "SPICE", fmt: E.F.PCT, min: 0, max: 127, def: 0, names: [] };
  const p = new Array(99).fill(0);
  const d1 = X.synDesc(61, dst, info, p, pd);
  p[78] = 5; const crB = X.synDesc(79, insA, info, p, pd), crR = X.synDesc(80, insA, info, p, pd);
  p[78] = 10; const ju = X.synDesc(79, insA, info, p, pd);
  p[78] = 12; const pi = X.synDesc(79, insA, info, p, pd);
  p[78] = 15; const dl = X.synDesc(79, insA, info, p, pd);
  p[78] = 0; const off = X.synDesc(79, insA, info, p, pd);
  p[17] = 15; const mu = X.synDesc(87, spice, info, p, pd), sd = X.synDesc(88, { ...spice, label: "DICE" }, info, p, pd);
  p[17] = 14; const ch14 = X.synDesc(87, spice, info, p, pd);
  ok(d1.names[7] === "CUT" && d1.names[8] === "E2" && d1.names[14] === "PTCH" && dst.names[7] === "E1"
    && crB.label === "BITS" && E.fmtValue(crB, 0)[0] === "1" && E.fmtValue(crB, 127)[0] === "16" && E.fmtValue(crB, 64)[0] === "9"
    && crR.label === "RATE" && E.fmtValue(crR, 127).join() === "44k,Hz" && ju.label === "WIDTH" && ju.def === 127 && off === insA
    && pi.label === "PITCH" && E.fmtValue(pi, 95).join() === "+12,st" && E.fmtValue(pi, 64).join() === "0,st"
    && dl.label === "TIME" && E.fmtValue(dl, 127).join() === "92,ms"
    && mu.label === "MUTAT" && sd.label === "SEED" && ch14 === spice && X.synDesc(87, spice, { pe0: 53 }, p, pd) === spice
    && X.synDesc(101, dst, info, p, pd) === dst && (() => { const q1 = []; q1[118] = { label: "CUT" };
      return X.synDesc(101, dst, { pe0: 118 }, new Array(126).fill(0), q1).names[7] === "CUT" && X.synDesc(116, dst, { pe0: 118 }, new Array(126).fill(0), q1).names[7] === "CUT"; })(),
    "editor: Clipper's labels: a matrix DST as the engine's values, the INSERT's A B C by TYPE (CRUSH BITS 1..16), TURNG's MUTAT SEED");
  /* a Clipper 0.8 sound (97 values, P_E0 89) into 0.9 (99, 91): VINTAGE and LOOP left as the device has them */
  const v8 = Array.from({ length: 97 }, (_, i) => 3000 + i), q = E.patchByCount(v8, 89, 99, 91), r = E.patchByCount(q.map((x) => x ?? 0), 91, 97, 89);
  ok(q[88] === 3088 && q[89] === null && q[90] === null && q[91] === 3089 && q[98] === 3096 && r[88] === 3088 && r[89] === 3089 && r[96] === 3096,
    "patchByCount: Clipper 0.8 <-> 0.9: up to DICE by place, the engine's 8 by theirs, VINTAGE and LOOP left out");
  /* a Clipper 0.9 sound (99 values, P_E0 91) into 1.0 (126, 118): SPREAD .. slot 12 left as the device has them */
  const v9 = Array.from({ length: 99 }, (_, i) => 4000 + i), q9 = E.patchByCount(v9, 91, 126, 118), r9 = E.patchByCount(q9.map((x) => x ?? 0), 118, 99, 91);
  ok(q9[90] === 4090 && q9[91] === null && q9[117] === null && q9[118] === 4091 && q9[125] === 4098 && r9[90] === 4090 && r9[91] === 4091 && r9[98] === 4098,
    "patchByCount: Clipper 0.9 <-> 1.0: up to LOOP by place, the engine's 8 by theirs, 1.0's 27 left out");
}

/* ------------------------------------------------- editor icons (Fukiai) --- */
function editorIcons() {
  const blk = html.slice(html.indexOf("const GLYPH = {"), html.indexOf("};", html.indexOf("const GLYPH = {")));
  const names = new Set([...blk.matchAll(/(\w+): 0x[0-9A-F]{4}/g)].map((m) => m[1]));
  const used = new Set([...html.matchAll(/data-ic="(\w+)"|ic: "(\w+)"|: "((?:waveform|function|symbol|control|port|ui|note)_\w+)"/g)].map((m) => m[1] || m[2] || m[3]));
  const missing = [...used].filter((n) => !names.has(n));
  ok(names.size > 0 && !missing.length, `editor: every icon name is in GLYPH (${used.size} used${missing.length ? ", missing " + missing : ""})`);
  const ttf = existsSync(join(HERE, "fukiai.ttf")) && readFileSync(join(HERE, "fukiai.ttf"));
  ok(ttf && ttf.readUInt32BE(0) === 0x00010000 && existsSync(join(HERE, "FUKIAI-LICENSE.txt")) && html.includes('href="FUKIAI-LICENSE.txt"'),
    "editor: fukiai.ttf and FUKIAI-LICENSE.txt next to editor.html");
  ok(/html:not\(\.fk\) \.ic \{ display: none; \}/.test(html) && html.includes('classList.add("fk")'), "editor: icons hidden until the font has loaded");
}

/* ------------------------------------------------ the patch bay --- */
/* The PATCH BAY card's code (editor.html between PB-BEGIN / END) in a small stand-in page: a sound (MARE unless
   named) on a protocol 15 device (or opt.pe0's), its descriptors those of params.c (N_MSRC, N_MDST), userSet recorded as the card calls it */
/* each engine's EDIT labels (eng_*.c's edit[]: "-" an unused slot) */
const PB_EDITS = {
  MARE: ["WAVE", "VCO2", "DTUNE", "SY/FM", "CUT", "RES", "SPACE", "MODE"], ANALOG: ["WAVE", "DTN", "MIX", "NOIS", "CUT", "RES", "DRV", "KTR"],
  PALOMINO: ["W1", "W2", "VCO2", "XMOD", "MIX", "CUT", "RES", "MODE"], FM6: ["ALG", "FB", "MLVL", "MRAT", "MEG", "VMOD", "DTUN", "PTCH"],
  SAMPLE: ["SET", "TUNE", "BITS", "LOOP", "CUT", "-", "DRV", "-"], FMX: ["PTCH", "ALG", "MOD", "FB", "TIME", "DTN", "VEL", "KTRK"],
  STAMPEDE: ["WAVE", "FEET", "DTUNE", "BAL", "HPF", "LPF", "RES", "IL"],
  WHEEL: ["REG", "SUB", "BODY", "TOP", "PERC", "CLICK", "DRV", "ROTR"], HYBRID: ["FILT", "CUT", "RES", "FENV", "PTCH", "M1", "M2", "M3"],
};
/* opt: pe0 (119: protocol 15, 12 slots (pall24: P_ALL 177, preset format 2's 24); 91: Clipper 0.9, 6 slots; 53: the FM-1's own layout, no matrix), f2 (FILTER 2's
   MODL, id 128: 0 OFF), drum (the drum track selected), lang ("en" or "ja"), engines (the device's engine names), store
   (a stand-in localStorage: an object; none: a browser without one), ctm (the SVG's screen matrix {a, d, e, f}: the
   card's scale and place; none: 1 unit = 1 px at 0, 0) */
/* the engines' unused destinations as the device sends them in INFO (editor.html MOCK_FW, written from the firmware by
   web/gen_mock.mjs; test_editor_p15.mjs checks it is the firmware's) */
let pbMockFw = null;
const pbMock = () => pbMockFw ||= vm.runInNewContext("(" + /\nconst MOCK_FW = (\{[^]*?\n\});/.exec(html)[1] + ")");
function pbHarness(engine = "MARE", opt = {}) {
  const pc = readFileSync(join(HERE, "../firmware/src/params.c"), "utf8");
  const arr = (nm) => [...((new RegExp(`N_${nm}\\[\\] = \\{([\\s\\S]*?)\\};`).exec(pc) || [])[1] || "").replace(/\/\*[\s\S]*?\*\//g, "").matchAll(/"([^"]*)"/g)].map((m) => m[1]);
  const MSRC = arr("MSRC"), MDST = arr("MDST");
  const lay = html.slice(html.indexOf("/*LAYOUT-BEGIN*/"), html.indexOf("/*LAYOUT-END*/"));
  const syn = html.slice(html.indexOf("const INS_BITS"), html.indexOf("/* the INSERT's TYPE or the arp's MODE changed"));
  const pb = html.slice(html.indexOf("/*PB-BEGIN*/"), html.indexOf("/*PB-END*/"));
  const tb = html.slice(html.indexOf("const TEXT = {"), html.indexOf("\n};", html.indexOf("const TEXT = {")) + 2);
  const TEXT = vm.runInNewContext(tb.replace("const TEXT =", "(") + ")");
  const els = {};
  const m = opt.ctm || { a: 1, d: 1, e: 0, f: 0 };   /* (screen = a * x + e, d * y + f; its inverse as a DOMMatrix gives it) */
  const ctm = { ...m, b: 0, c: 0, inverse: () => ({ a: 1 / m.a, b: 0, c: 0, d: 1 / m.d, e: -m.e / m.a, f: -m.f / m.d }) };
  const fake = () => { const cl = new Set(), on = {}, at = {}; return { hidden: true, innerHTML: "", textContent: "", value: "", style: {}, dataset: {}, on, tabIndex: 0,
    addEventListener: (ty, f) => { (on[ty] ||= []).push(f); }, setPointerCapture: () => {},
    setAttribute: (k, v) => { at[k] = String(v); }, getAttribute: (k) => at[k] ?? null,
    querySelector: (q) => (q === "svg" ? { getScreenCTM: () => ctm } : null), contains: () => true,
    classList: { add: (c) => cl.add(c), remove: (c) => cl.delete(c), contains: (c) => cl.has(c), toggle: (c, on) => (on ?? !cl.has(c)) ? cl.add(c) : cl.delete(c) } }; };
  const writes = [], said = [];
  let asks = 0, answer = true;
  const asked = [];
  const engines = opt.engines || ["ANALOG", "PALOMINO", "MARE", "FM6", "SAMPLE", "FMX", "STAMPEDE", "WHEEL", "HYBRID"];
  const pdesc = [];
  const pe0 = opt.pe0 ?? 119, pcount = { 119: 127, 91: 99, 53: 61 }[pe0], pall = pe0 >= 119 ? (opt.pall24 ? 177 : 141) : pcount;
  const store = opt.store;
  /* a stand-in document: the element with the focus (activeElement), its listeners (pointerup / cancel) */
  const doc = { activeElement: null, on: {}, addEventListener(ty, f) { (this.on[ty] ||= []).push(f); } };
  const ctx = {
    F: E.F, console, Set, Map, document: doc,
    ...(store ? { localStorage: { getItem: (k) => (k in store ? store[k] : null), setItem: (k, v) => { store[k] = String(v); } } } : {}),
    $: (id) => (els[id] ||= fake()),
    t: (k) => TEXT[opt.lang || "en"][k] ?? k,
    sayK: (k, a = "") => said.push(a !== "" && TEXT.en[k].includes("{n}") ? TEXT.en[k].replace("{n}", a) : TEXT.en[k] + a),
    confirm: (q) => { asks++; asked.push(q); return answer; },
    userSet: (s, id, v, fin) => { writes.push([s, id, v, fin]); ctx.dev.dump.p[id] = v; },
    dev: { info: { proto: pe0 >= 119 ? 15 : 13, pcount, pall, pe0, gcount: 35, engines, nengines: engines.length,
      noDst: opt.nomask || pe0 < 119 ? null : engines.map((n) => pbMock().dd[pbMock().eng.indexOf(n)] || []) },
      dump: { engine: opt.drum ? engines.length : engines.indexOf(engine), p: new Array(Math.max(141, pall)).fill(0) }, pdesc },
  };
  vm.createContext(ctx);
  const ren = (/\nconst ENG_RENAMED = \{[^}]*\};/.exec(html) || [""])[0];   /* (the engines' old names: pbEng) */
  vm.runInContext(lay + "\n" + syn + "\n" + ren + "\n" + pb + `
;Object.assign(globalThis, { mxId, LAYOUT, SX_PAGES, PAGE_PLACE, devPageTag, sxPages, SIG_COL, pbSrcType, pbShow, renderPb, pbDrop, pbClear, pbDragStart,
  pbFinish, pbSelect, pbAmount, pbRemove, pbCables, pbOn, pbWire, pbAt, pbLay, pbWords, PB, pbState: () => pb,
  pbSlots, mxSlots, pbLive, PB_ENG, PB_OUT, PB_IN, pbView, pbMxSet, renderMx, pbExtras, pbZoomTo });`, ctx);
  pdesc[ctx.mxId(1)] = { label: "SRC", fmt: E.F.ENUM, min: 0, max: MSRC.length - 1, names: MSRC };
  pdesc[ctx.mxId(1) + 1] = { label: "DST", fmt: E.F.ENUM, min: 0, max: MDST.length - 1, names: MDST };
  (PB_EDITS[engine] || PB_EDITS.MARE).forEach((l, k) => { pdesc[pe0 + k] = { label: l, names: [] }; });
  if (engine === "MARE") pdesc[pe0 + 7].names = ["STER", "PARA", "SERI", "STR+N", "PAR+N", "SER+N", "SPLT", "SPL+N"];
  if (pe0 >= 119) {
    pdesc[127] = { label: "MODL", names: [] };
    pdesc[128] = { label: "MODL", names: ["OFF", "SVF", "LADDR"] };
    ctx.dev.dump.p[128] = opt.f2 ?? 0;
    pdesc[139] = { label: "FLTFM", names: [] };
    pdesc[140] = { label: "RING", names: [] };
  }
  return { ctx, els, doc, writes, said, asked, MSRC, MDST, ask:(a) => { answer = a; const n = asks; asks = 0; return n; } };
}
/* the patch bay on every engine whose part has a matrix: the same jacks, the EDIT strip the engine's; MARE's
   normals, nodes and routes only on MARE; an input that does nothing on the engine dashed with a short label and why; a
   cable into one dashed with a warning, never hidden; 6 or 12 slots as the device has */
function pbFirmwareEngines() {                     /* eng_*.c's engine_t: name -> its fields (positional or designated) */
  const ch = readFileSync(join(HERE, "../firmware/src/core.h"), "utf8").replace(/\/\*[\s\S]*?\*\//g, "");
  const body = (/typedef struct \{([^]*?)\} engine_t;/.exec(ch.slice(ch.indexOf("struct track;"))) || [])[1] || "";
  const fields = [];
  for (const decl of body.split(";").map((x) => x.trim()).filter(Boolean)) {
    const fp = /\(\*(\w+)\)/.exec(decl);
    if (fp) { fields.push(fp[1]); continue; }
    const rest = decl.replace(/^(const\s+)?[\w\s]*?\b(char\s*\*|u?int\d+_t|param_desc_t|preset_t\s*\*|preset_t)\s*/, "");
    for (const n of rest.split(",")) fields.push(n.trim().replace(/^\*+/, "").replace(/\[.*$/, ""));
  }
  const out = {};
  const dir = join(HERE, "../firmware/src");
  for (const f of readdirSync(dir).filter((n) => /^eng_\w+\.c$/.test(n))) {
    const src = readFileSync(join(dir, f), "utf8").replace(/\/\*[\s\S]*?\*\//g, "").replace(/\/\/.*$/gm, "").replace(/^\s*#.*$/gm, "");
    const m = /static const engine_t ENG_\w+ = \{/.exec(src);
    if (!m) continue;
    const items = [];
    let depth = 1, cur = "", q = false;
    for (let i = m.index + m[0].length; i < src.length; i++) {
      const c = src[i];
      if (c === '"' && src[i - 1] !== "\\") q = !q;
      if (!q && (c === "{" || c === "(")) depth++;
      if (!q && (c === "}" || c === ")")) depth--;
      if (depth === 0) break;
      if (!q && c === "," && depth === 1) { items.push(cur.trim()); cur = ""; } else cur += c;
    }
    if (cur.trim()) items.push(cur.trim());
    const v = {};
    let k = 0;
    for (const it of items) {
      const d = /^\.(\w+)\s*=\s*([^]*)$/.exec(it);
      if (d) { k = fields.indexOf(d[1]); v[d[1]] = d[2].trim(); } else v[fields[k]] = it;
      k++;
    }
    if (v.name) out[JSON.parse(v.name)] = Object.assign(v, { src });   /* (src: the render code, comments out) */
  }
  return { fields, out };
}
function editorPatchBayGeneric() {
  const show = (eng, opt) => { const H = pbHarness(eng, opt); H.ctx.pbShow(); return H; };
  const svgOf = (H) => H.els.pbsvg.innerHTML;
  const njacks = (H) => (svgOf(H).match(/data-jack="/g) || []).length;
  const edits = (H) => [...svgOf(H).matchAll(/class="pbmt"[^>]*>([^<]*)</g)].map((m) => m[1]).filter((x) => /^EDIT\b/.test(x));
  const lab = (H, k) => { const m = new RegExp(`data-for="${k}">[^]*?<text class="pbl"[^>]*>([^<]*)</text>`).exec(svgOf(H)); return m && m[1]; };
  const jg = (H, k) => new RegExp(`<g class="([^"]*)" data-jack="${k}"[^>]*><title>([^<]*)</title>`).exec(svgOf(H)) || [];
  const engs = ["MARE", "ANALOG", "PALOMINO", "FM6", "SAMPLE", "FMX", "STAMPEDE", "WHEEL", "HYBRID"];
  const all = Object.fromEntries(engs.map((e) => [e, show(e)]));
  ok(engs.every((e) => all[e].els.pbpanel.hidden === false && all[e].ctx.pbOn() && njacks(all[e]) >= 30 + 34 - 7),
    `patch bay generic: shown on every synth engine (${engs.map((e) => `${e} ${njacks(all[e])}`).join(", ")} jacks: what each uses, below)`);
  ok(engs.every((e) => edits(all[e]).length >= 1 && edits(all[e]).every((x) => x === `EDIT ${e}`)) && lab(all.ANALOG, "i:7") === "WAVE" &&
     lab(all.FM6, "i:7") === "ALG" && lab(all.PALOMINO, "i:10") === "XMOD" && lab(all.SAMPLE, "i:11") === "CUT" && lab(all.SAMPLE, "i:12") === null,
    `patch bay generic: the EDIT strip titled EDIT <ENGINE>, the engine's names (${edits(all.FM6).join(", ")}; FM6 E1 ${lab(all.FM6, "i:7")})`);
  ok(/pbTitle: "PATCH BAY"/.test(html) && !/pbTitle: "[^"]*MARE/.test(html) && (html.match(/pbTitle: "/g) || []).length === 2,
    "patch bay generic: the card is titled PATCH BAY (no engine), in both languages");
  /* hidden where the part has no matrix */
  const drum = show("ANALOG", { drum: true }), old = show("ANALOG", { pe0: 53 }), nod = pbHarness("ANALOG");
  delete nod.ctx.dev.pdesc[nod.ctx.mxId(1)];
  nod.ctx.pbShow();
  ok(drum.els.pbpanel.hidden && !drum.ctx.pbOn() && old.els.pbpanel.hidden && !old.ctx.pbOn() && nod.els.pbpanel.hidden && !nod.ctx.pbOn(),
    "patch bay generic: hidden on the drum track, on a layout with no matrix, without the slot descriptors");
  /* MARE's extras only on MARE */
  const extras = (H) => { const s = svgOf(H); return [/data-normal=/.test(s), /data-route=/.test(s), /class="pbnode"/.test(s),
    />(VCO 1|VCO 2|MIXER|VCF 1|VCF 2)</.test(s), /data-cap="route"/.test(s)]; };
  for (const e of engs) { const X = all[e].ctx; X.dev.dump.p[139] = 40; X.dev.dump.p[140] = 40;   /* (FLTFM, RING: an older
    device's values; cables into them, below) */
    X.dev.dump.p[X.mxId(24) + 1] = 33, X.dev.dump.p[X.mxId(24) + 2] = 20, X.dev.dump.p[X.mxId(23) + 1] = 34, X.dev.dump.p[X.mxId(23) + 2] = 20; X.renderPb(); }
  ok(extras(all.MARE).every(Boolean) && engs.filter((e) => e !== "MARE").every((e) => !extras(all[e]).some(Boolean)),
    `patch bay generic: normals, VCO / MIXER / VCF nodes, FM / RING / SPLIT routes on MARE only (MARE ${extras(all.MARE).join(" ")}; ANALOG ${extras(all.ANALOG).join(" ")})`);
  ok(engs.every((e) => /data-link="sh"/.test(svgOf(all[e]))), "patch bay generic: the S&H clock link (LFO 2) on every engine");
  /* only the inputs the engine uses: the device's mask (INFO), nothing dashed, no UNUSED, no warning (36
     destinations, with SEMI and SEM34) */
  const jk = (H, k) => new RegExp(`data-jack="${k}"`).test(svgOf(H));
  const deadOf = (H) => H.ctx.dev.info.noDst[H.ctx.dev.dump.engine];
  ok(engs.every((e) => njacks(all[e]) === 30 + 36 - deadOf(all[e]).length && deadOf(all[e]).every((c) => !jk(all[e], "i:" + c)) &&
       [...Array(37).keys()].slice(1).filter((c) => !deadOf(all[e]).includes(c)).every((c) => jk(all[e], "i:" + c))),
    `patch bay generic: every engine draws exactly the inputs it uses (${engs.map((e) => `${e} ${njacks(all[e])}`).join(", ")})`);
  ok(jk(all.MARE, "i:33") && jk(all.MARE, "i:34") && !jk(all.ANALOG, "i:33") && !jk(all.ANALOG, "i:34") && !jk(all.PALOMINO, "i:34") &&
     !jk(all.ANALOG, "i:20") && jk(all.PALOMINO, "i:20") && jk(all.MARE, "i:20") && !jk(all.MARE, "i:21") && jk(all.STAMPEDE, "i:21") &&
     !jk(all.FM6, "i:23") && !jk(all.FMX, "i:23") && jk(all.FM6, "i:24") && ["MARE", "ANALOG", "SAMPLE", "STAMPEDE"].every((e) => jk(all[e], "i:23")) &&
     !jk(all.WHEEL, "i:3") && !jk(all.SAMPLE, "i:3") && !jk(all.HYBRID, "i:3") && ["MARE", "ANALOG", "PALOMINO", "FM6", "FMX", "STAMPEDE"].every((e) => jk(all[e], "i:3")) &&
     !jk(all.SAMPLE, "i:12") && !jk(all.SAMPLE, "i:14") && [7, 8, 9, 10, 11, 13].every((c) => jk(all.SAMPLE, "i:" + c)),
    "patch bay generic: FLTFM / RING only on MARE, XMOD on PALOMINO / MARE, HPF on STAMPEDE, no TRIG1 on FM6 / FMX, no SHAPE on WHEEL / SAMPLE / HYBRID, no E6 / E8 on SAMPLE");
  ok(["FMX", "WHEEL"].every((e) => jk(all[e], "i:2") && jk(all[e], "i:22") && jk(all[e], "i:16") && jk(all[e], "i:17")) &&
     jk(all.ANALOG, "i:16") && jk(all.ANALOG, "i:17"),
    "patch bay generic: kept: CUT / CUTB on FMX and WHEEL (they move FILTER 2), CUT2 / RES2 with FILTER 2 OFF (it can be turned on)");
  const old15 = show("ANALOG", { nomask: true });
  ok(njacks(old15) === 66 && jk(old15, "i:33") && jk(old15, "i:36"), `patch bay generic: a device that sends no masks (older firmware): every input (${njacks(old15)})`);
  ok(engs.every((e) => !/\bdead\b|UNUSED|未使用/.test(svgOf(all[e]))) && !/pbwarn|pbUnused|pbDead|pbWarn|pbNoShape|pbNoCut|pbOwnEnv|pbNoE\b|pbF2Off|pbOnly|\.dead\b/.test(html) &&
     !/UNUSED/.test(all.ANALOG.ctx.t("pbHelp")) && !/UNUSED|未使用/.test(show("ANALOG", { lang: "ja" }).ctx.t("pbHelp")) && /Only the inputs this engine uses/.test(all.ANALOG.ctx.t("pbHelp")),
    "patch bay generic: no dashed jack, no UNUSED, no dead-cable warning anywhere (code, styles, words); the help says only used inputs show");
  /* the modules closed up round what is left: each row filled edge to edge, modules apart, every jack inside its module */
  {
    const flow = (H) => {
      const L = H.ctx.pbLay(), bad = [];
      const byRow = new Map();
      for (const m of L.mods) (byRow.get(m.y) || byRow.set(m.y, []).get(m.y)).push(m);
      for (const [y, ms] of byRow) {
        ms.sort((p, q) => p.x - q.x);
        if (Math.abs(ms[0].x - (H.ctx.PB.IM + 3)) > 2 || Math.abs(ms.at(-1).x + ms.at(-1).w - (L.w - H.ctx.PB.IM - 3)) > 3) bad.push(`row ${y} not edge to edge`);
        for (let i = 1; i < ms.length; i++) if (ms[i].x < ms[i - 1].x + ms[i - 1].w) bad.push(`${ms[i - 1].t} / ${ms[i].t} overlap`);
      }
      for (const j of L.jacks.values()) if (!j.out && !L.mods.some((m) => j.x > m.x && j.x < m.x + m.w && j.y > m.y && j.y < m.y + m.h)) bad.push(`${j.k} outside`);
      return bad;
    };
    const bad = engs.flatMap((e) => flow(all[e]).map((x) => `${e}: ${x}`));
    const ed = (H) => { const L = H.ctx.pbLay(); return L.mods.find((m) => /^EDIT /.test(m.t)).w; };
    ok(!bad.length && ed(all.SAMPLE) < ed(all.ANALOG), `patch bay generic: the modules close up round the jacks left (SAMPLE's EDIT ${ed(all.SAMPLE)} wide, ANALOG's ${ed(all.ANALOG)})${bad.length ? ": " + bad.slice(0, 4).join("; ") : ""}`);
  }
  /* tap a jack: what it is, in the hint line */
  {
    const H = all.ANALOG, X = H.ctx, box = X.$("pbsvg");
    X.pbWire(box);
    const tgt = (k) => ({ closest: (sel) => (sel.split(",").some((s) => s.trim() === "[data-jack]") ? { dataset: { jack: k } } : null) });
    let ts = 5000;
    const tap = (k) => { for (const ty of ["pointerdown", "pointerup"]) for (const f of box.on[ty] || []) f({ type: ty, button: 0, pointerId: 1, clientX: 10, clientY: 10, target: tgt(k), timeStamp: ts += 20, preventDefault() {} }); };
    H.writes.length = 0;
    tap("i:22");
    const h2 = H.els.pbhint.textContent;
    ok(!H.writes.length && h2 === "CUTB: on ANALOG, the same as CUT", `patch bay generic: a tap on a jack says what it is in the hint line (${h2})`);
  }
  /* the MATRIX tab's DESTINATION lists only what the engine uses, in the bay's order */
  {
    const H = show("RODEO", { engines: ["ANALOG", "RODEO", "MARE"] }), X = H.ctx;
    X.pbView("mx"); X.renderMx(undefined, true);
    const sel = (/<select data-mx="1:1"[^>]*>([^]*?)<\/select>/.exec(H.els.pbmxrows.innerHTML) || [])[1] || "";
    const vals = [...sel.matchAll(/value="(\d+)"/g)].map((m) => +m[1]);
    const want = [0].concat(X.pbLay().order.i);
    ok(vals.length === 31 && js(vals) === js(want) && ![20, 21, 33, 34, 35, 36].some((c) => vals.includes(c)) && vals.includes(3) && vals.includes(2),
      `patch bay generic: MATRIX's DESTINATION on RODEO: OFF and its 30 inputs, no XMOD / HPF / FLTFM / RING / SEMI / SEM34 (${vals.length} choices)`);
  }
  /* 6 slots (older firmware): the cap and the messages say 6 */
  {
    const H = show("ANALOG", { pe0: 91 }), X = H.ctx;
    ok(H.els.pbpanel.hidden === false && X.pbSlots() === 6 && njacks(H) === 66 && edits(H)[0] === "EDIT ANALOG", "patch bay generic: shown on a 6-slot device (Clipper 0.9)");
    for (let c = 1; X.pbCables().length < 6; c++) X.pbDrop("o:" + c, "i:" + c);
    H.writes.length = 0; H.said.length = 0;
    const r7 = X.pbDrop("o:25", "i:28");
    ok(r7 === false && !H.writes.length && H.els.pbhint.textContent === "All 6 cables are in use" && H.said.includes("All 6 cables are in use"),
      `patch bay generic: a 7th cable refused, the message counts 6 (${H.els.pbhint.textContent})`);
    H.ask(true);
    X.pbClear();
    ok(H.writes.length === 18 && H.writes.every((w) => w[1] >= 60 && w[1] <= 77 && w[2] === 0) && H.asked.at(-1) === "Take out all 6 cables?",
      "patch bay generic: CLEAR ALL on 6 slots: 18 zeros, \"Take out all 6 cables?\"");
  }
  /* the strip and the EDIT card: the engine's name now, not an older device's (ENG_RENAMED) */
  {
    const ren = vm.runInNewContext("(" + /\nconst ENG_RENAMED = (\{[^}]*\});/.exec(html)[1] + ")"), old = Object.keys(ren).find((k) => ren[k] === "MARE");
    const H = show(old, { engines: ["ANALOG", old] });   /* (MARE by its old name, as an older device sends it) */
    ok(js(edits(H)) === js(["EDIT MARE"]) && /data-normal=/.test(svgOf(H)) &&
       /g\.engine \? `EDIT \$\{engKey\(info\.engines\[dev\.dump\.engine\] \|\| ""\)\}`/.test(html) &&
       !/`EDIT \$\{(dev\.)?info\.engines\[/.test(html),
      `patch bay generic: the strip and the EDIT card named by the engine's name (${edits(H).join(", ")})`);
  }
  /* no stale MARE-only words about the panel */
  {
    const me = readFileSync(fileURLToPath(import.meta.url), "utf8"), head = me.slice(0, me.indexOf("\nimport "));
    ok(!/pbShow\(\);[^\n]*MARE's patch bay/.test(html) && /pbLater\(\);[^\n]*MODE, FLTFM, RING\)/.test(html) &&
       !/MARE's patch bay/.test(head) && !/^\/\* -+ MARE's patch bay -+ \*\/$/m.test(me) && !/a MARE sound on a protocol\s+15 device/.test(me),
      "patch bay generic: no comment names the panel after one engine (the panel, the redraw triggers, this file's headers)");
  }
  /* what the device does not say: the editor's table against the firmware's engines */
  const fw = pbFirmwareEngines(), E2 = fw.out, names = Object.keys(E2), on = (f, pred) => names.filter((n) => pred(E2[n][f])).sort();
  const nz = (v) => v !== undefined && v !== "0" && v !== "";
  const reads = (f) => names.filter((n) => !new RegExp(`\\bm->${f}\\b`).test(E2[n].src)).sort();   /* (every render's vmod_t is m) */
  const want = { ownenv: on("ownenv", nz), vco2: on("vco2", nz), xmod: on("xmod", nz), hpf: on("hpf", nz), noshape: reads("shape"), nocut: reads("cutoff") };
  ok(names.length >= 27 && fw.fields.indexOf("hpf") === 18 && js(want.vco2) === js([...all.MARE.ctx.PB_ENG.vco2].sort()) &&
     js(Object.keys(all.MARE.ctx.PB_ENG)) === js(["vco2"]) && want.ownenv.includes("FM6") && want.xmod.includes("MARE") &&
     want.noshape.includes("WHEEL") && want.noshape.includes("ACID") && !want.noshape.includes("ANALOG") && js(want.nocut) === js(["FMX", "WHEEL"]),
    `patch bay generic: the editor's own engine table (vco2 only) matches eng_*.c (${names.length} engines; ${Object.entries(want).map(([k, v]) => `${k} ${v.join(" ")}`).join("; ")})`);
  /* the device's masks (MOCK_FW.dd: INFO as the firmware sends it) against eng_*.c read here on its own:
     FLTFM / RING off MARE, TRIG1 where ownenv, XMOD / HPF where none, SHAPE where never read, E slots labelled "-" */
  {
    const M = pbMock(), bad = [];
    M.eng.forEach((n, e) => {
      if (!n) return;                               /* (1.0: 3, LOFI's number, no engine) */
      const f = E2[n] || E2[Object.keys(E2).find((k) => k === n)];
      if (!f) { bad.push(`${n}: no eng_*.c`); return; }
      const labels = [...((f.edit || "").matchAll(/\{\s*"([^"]*)"/g))].map((m) => m[1]);
      const want2 = [];
      if (want.noshape.includes(n)) want2.push(3);
      labels.forEach((l, k) => { if (k < 8 && (!l || l === "-")) want2.push(7 + k); });
      if (!want.xmod.includes(n)) want2.push(20);
      if (!want.hpf.includes(n)) want2.push(21);
      if (want.ownenv.includes(n)) want2.push(23);
      if (n !== "MARE") want2.push(33, 34, 35, 36);
      want2.sort((p, q) => p - q);
      if (js(want2) !== js(M.dd[e])) bad.push(`${n}: device ${M.dd[e]}, eng_*.c ${want2}`);
    });
    ok(M.nd === 37 && M.dd.length === M.eng.length && !bad.length,
      `patch bay generic: the device's per-engine masks (INFO) match eng_*.c for all ${M.eng.filter(Boolean).length} engines${bad.length ? ": " + bad.slice(0, 3).join("; ") : ""}`);
  }
  /* what that reading takes for granted: every vmod_t pointer in eng_*.c is named m (so m->shape finds
     every read), and the shared code reads no ->shape / ->cutoff for an engine: dsp.c and voice.c none (voice.c only
     writes the vmod_t's fields, m.shape = / m.cutoff +=), engines.c only in its FILTER 2 code (f2_*: nocut's reason) */
  {
    const dir = join(HERE, "../firmware/src"), strip = (s) => s.replace(/\/\*[\s\S]*?\*\//g, "").replace(/\/\/.*$/gm, "");
    const badName = [];
    for (const f of readdirSync(dir).filter((n) => /^eng_\w+\.c$/.test(n)))
      for (const m of strip(readFileSync(join(dir, f), "utf8")).matchAll(/\bvmod_t\s*\*\s*(?:const\s+)?(\w+)/g)) if (m[1] !== "m") badName.push(`${f}: ${m[1]}`);
    const src = (f) => strip(readFileSync(join(dir, f), "utf8"));
    const arrow = (s) => [...s.matchAll(/->(shape|cutoff)\b/g)].map((m) => m.index);
    const fnAt = (s, i) => { const all = [...s.slice(0, i).matchAll(/^\w[^\n;]*?\b(\w+)\s*\([^;{]*\)\s*\{/gm)]; return all.length ? all.at(-1)[1] : ""; };
    const eng = src("engines.c"), voice = src("voice.c");
    const engRd = arrow(eng).map((i) => fnAt(eng, i)), vLines = voice.split("\n").filter((l) => /\bm\.(shape|cutoff)\b/.test(l));
    const vBad = vLines.filter((l) => !/^\s*(?:[^;=]*\)\s*)?m\.(shape|cutoff|cutb)\s*(\+|-)?=[^=]/.test(l));
    ok(!badName.length && !arrow(src("dsp.c")).length && !arrow(voice).length && engRd.length >= 1 && engRd.every((n) => /^f2_/.test(n)) && vLines.length >= 10 && !vBad.length &&
       !/->shape\b/.test(eng),
      `patch bay generic: every vmod_t in eng_*.c is m; dsp.c / voice.c read no ->shape / ->cutoff, voice.c only writes them, engines.c reads ->cutoff only in ${[...new Set(engRd)].join(", ")}${badName.length ? "; not m: " + badName.join(", ") : ""}${vBad.length ? "; voice.c reads: " + vBad.slice(0, 3).join(" | ") : ""}`);
  }
}
/* the FM-1's MATRIX page steps SOURCE and DESTINATION in the patch bay's order: ui_synth.c MX_SRC_ORD and
   MX_DST_ORD (codes by core.h's enum names) against PB_OUT and PB_IN read left to right, row after row, OFF first; the
   bay as drawn (pbLay's order) and the MATRIX tab's choices the same */
function editorPatchBayOrder() {
  const H = pbHarness("MARE", { nomask: true }), X = H.ctx;   /* (every code: a device that sends no masks) */
  const ch = readFileSync(join(HERE, "../firmware/src/core.h"), "utf8").replace(/\/\*[\s\S]*?\*\//g, "");
  const us = readFileSync(join(HERE, "../firmware/src/ui_synth.c"), "utf8").replace(/\/\*[\s\S]*?\*\//g, "");
  const enumOf = (first) => {                      /* names to values of the enum that starts with first */
    const body = (new RegExp(`enum \\{\\s*(${first}\\b[\\s\\S]*?)\\}`).exec(ch) || [])[1] || "", v = {};
    let n = 0;
    for (const it of body.split(",").map((x) => x.trim()).filter(Boolean)) {
      const [name, ex] = it.split("=").map((x) => x.trim());
      if (ex !== undefined) n = expr(ex, v);
      v[name] = n++;
    }
    return v;
  };
  const expr = (e, v) => e.split("+").map((x) => x.trim()).reduce((a, x) => a + (/^\d+$/.test(x) ? +x : v[x] ?? NaN), 0);
  const MS = enumOf("MS_OFF"), MT = enumOf("MT_OFF");
  const ord = (nm, v) => (((new RegExp(`${nm}\\[[^\\]]*\\] = \\{([\\s\\S]*?)\\};`).exec(us)) || [])[1] || "")
    .split(",").map((x) => x.trim()).filter(Boolean).map((x) => expr(x.replace(/u$/, ""), v));
  const fw = { o: ord("MX_SRC_ORD", MS), i: ord("MX_DST_ORD", MT) };
  const ins = X.PB_IN.flatMap((row) => row.flatMap(([, js]) => js.map((j) => (Array.isArray(j) ? j[0] : j))));
  const web = (k) => [0].concat(k === "o" ? X.PB_OUT : ins);
  const same = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);
  ok(MS.MS_NSRC === 31 && MT.MT_COUNT === 37 && fw.o.length === 31 && fw.i.length === 37 && fw.o.every(Number.isInteger) && fw.i.every(Number.isInteger),
    `patch bay: ui_synth.c MX_SRC_ORD / MX_DST_ORD read (${fw.o.length}, ${fw.i.length} codes)`);
  ok(same(fw.o, web("o")), `patch bay: the FM-1's SOURCE order is the panel's, left to right (${fw.o.join(" ")})`);
  ok(same(fw.i, web("i")), `patch bay: the FM-1's DESTINATION order is the panel's, left to right (${fw.i.join(" ")})`);
  X.pbShow();
  const L = X.pbLay(), rd = (out) => [...L.jacks.values()].filter((j) => j.out === out).sort((a, b) => a.y - b.y || a.x - b.x).map((j) => j.code);
  const rows = H.els.pbmxrows.innerHTML, opts = (f) => [...((new RegExp(`data-mx="1:${f}"[^>]*>(.*?)</select>`).exec(rows) || [])[1] || "").matchAll(/value="(\d+)"/g)].map((x) => +x[1]);
  ok(same([0, ...L.order.o], fw.o) && same([0, ...L.order.i], fw.i) && same([0, ...rd(true)], fw.o) && same(opts(0), fw.o) && same(opts(1), fw.i),
    `patch bay: drawn row by row in that order, and the MATRIX tab's SOURCE / DESTINATION choices too (${rd(false).join(" ")})`);
}
function editorPatchBay() {
  const H = pbHarness("MARE", { nomask: true }), X = H.ctx, svg = () => H.els.pbsvg.innerHTML;   /* (every code: no masks) */
  ok(H.MSRC.length === 31 && H.MDST.length === 37 && H.MSRC.at(-1) === "CC B" && H.MDST.at(-1) === "SEMI2", "patch bay: params.c's 31 sources, 37 destinations");
  X.pbShow();
  ok(H.els.pbpanel.hidden === false && X.pbOn(), "patch bay: shown on MARE (protocol 15, 12 slots)");
  /* every code once, as the descriptors list them */
  const count = (k) => (svg().match(new RegExp(`data-jack="${k}"`, "g")) || []).length;
  const srcOnce = H.MSRC.every((_, c) => c === 0 || count(`o:${c}`) === 1), dstOnce = H.MDST.every((_, c) => c === 0 || count(`i:${c}`) === 1);
  const jacks = (svg().match(/data-jack="/g) || []).length;
  ok(srcOnce && dstOnce && jacks === 30 + 36 && !count("o:0") && !count("i:0"), `patch bay: every source and destination code on the panel once (${jacks} jacks)`);
  const lab = (k) => { const m = new RegExp(`data-for="${k}">[^]*?<text[^>]*>([^<]*)</text>`).exec(svg()); return m && m[1]; };
  ok(lab("o:24") === "S&amp;H" && lab("i:22") === "CUTB" && lab("i:7") === "WAVE" && lab("o:29") === "CC A",
    "patch bay: names from the device's descriptors (E1..E8 as the engine's)");
  ok(/class="pbj out[^"]*" data-jack="o:1"/.test(svg()) && /class="pbj in[^"]*" data-jack="i:2"/.test(svg()) &&
     /class="pblbox"[^>]*style="stroke:var\(--t4\)"/.test(svg()) && (svg().match(/class="pblbox"/g) || []).length === 30 && H.els.pbpanel && /^Outputs: filled jack, boxed name\. /.test(H.els.pbhelp.textContent) && /pbLegend: "Outputs: filled jack, boxed name\. Inputs: open jack\."/.test(html),
     "patch bay: outputs filled and boxed, inputs open; the legend says so");
  /* modules left to right */
  const mods = [...svg().matchAll(/class="pbmt"[^>]*>([^<]*)</g)].map((m) => m[1]);
  ok(mods.join("|") === "CONTROL|LFOs|OSCILLATORS|FILTERS|ENVELOPES|SENDS|AMP|EDIT MARE",
    `patch bay: the inputs in labelled modules (${mods.join(", ")})`);
  const jx = (k) => { const m = new RegExp(`data-jack="${k}" data-x="(\\d+)" data-y="(\\d+)"`).exec(svg()); return m ? [+m[1], +m[2]] : [NaN, NaN]; };
  const oy = [...new Set([...svg().matchAll(/data-jack="o:\d+" data-x="\d+" data-y="(\d+)"/g)].map((x) => +x[1]))];
  const iy = [...new Set([...svg().matchAll(/data-jack="i:\d+" data-x="\d+" data-y="(\d+)"/g)].map((x) => +x[1]))];
  const perRow = oy.map((y) => (svg().match(new RegExp(`data-jack="o:\\d+" data-x="\\d+" data-y="${y}"`, "g")) || []).length);
  ok(oy.length === 3 && perRow.every((n) => n === 10) && iy.length === 3 && Math.min(...iy) > Math.max(...oy) + 100,
    `patch bay: the outputs in 3 rows of 10 on top, the inputs in 3 rows below (outputs at ${oy.join(", ")}: ${perRow.join(", ")}; inputs at ${iy.join(", ")})`);
  ok(jx("o:2")[0] - jx("o:24")[0] > 0 && jx("o:2")[0] - jx("o:24")[0] < 300 && Math.abs(jx("o:2")[1] - jx("o:24")[1]) < 1 && /data-link="sh" d="M[\d.]+ [\d.]+Q/.test(svg()),
    "patch bay: S&H near LFO 2 in the row, the clock link an arc over it, labelled");
  /* signal colours: the device's table */
  const want = { pitch: [6], gate: [27, 28], env: [3, 4], mod: [1, 2, 15, 24, 25, 9, 11, 12, 13, 14, 10], perf: [5, 7, 8, 21, 22, 20, 23, 16, 17, 18, 19, 29, 30], dc: [26] };
  const typeOk = Object.entries(want).every(([ty, cs]) => cs.every((c) => X.pbSrcType(c) === ty)) && Object.values(want).flat().length === 30;
  ok(typeOk && X.SIG_COL.pitch === "var(--t1)" && X.SIG_COL.gate === "var(--t3)" && X.SIG_COL.env === "var(--t2)" && X.SIG_COL.mod === "var(--t4)" &&
     X.SIG_COL.perf === "var(--pu)" && X.SIG_COL.dc === "var(--fg)" && !/var\(--red\)/.test(svg().replace(/class="pbj[^"]*bad[^]*?<\/g>/g, "")),
     "patch bay: jack colours by signal type (pitch blue, gate yellow, envelope green, LFO orange, performance purple, +V white; no red)");
  /* drag LFO1 out onto CUT in */
  X.pbDragStart("o:1");
  const cls = (k) => ((new RegExp(`class="([^"]*)" data-jack="${k}"`).exec(svg()) || [])[1] || "");
  ok(/\bok\b/.test(cls("i:2")) && /\bbad\b/.test(cls("o:2")) && H.els.pbhint.textContent === "LFO1 is an output: drop it on an input",
    "patch bay: dragging an output: inputs light green, outputs red, the hint");
  H.writes.length = 0;
  X.pbFinish("i:2");
  const m1 = X.mxId(1);
  ok(H.writes.length === 3 && js(H.writes.map((w) => [w[1], w[2]])) === js([[m1, 1], [m1 + 1, 2], [m1 + 2, 32]]) && H.writes.every((w) => w[0] === 0 && w[3] === true),
    "patch bay: LFO1 out onto CUT in writes slot 1 = (1, 2, 32)");
  ok(/data-slot="1"/.test(svg()) && X.pbState().sel === 1 && /LFO1/.test(H.els.pbseltext.textContent) && /CUT/.test(H.els.pbseltext.textContent) && /\+32/.test(H.els.pbseltext.textContent) && H.els.pbsel.hidden === false,
    `patch bay: the new cable drawn and selected, in words (${H.els.pbseltext.textContent})`);
  /* out onto out: refused */
  H.writes.length = 0;
  X.pbDragStart("o:24");
  const red = /\bbad\b/.test(cls("o:2"));
  const r = X.pbFinish("o:2");
  ok(r === false && red && !H.writes.length && H.els.pbhint.textContent === "S&H is an output: drop it on an input" && H.els.pbhint.classList.contains("err"),
    "patch bay: out onto out refused, red, with the hint");
  X.pbDragStart("i:3");
  ok(/\bbad\b/.test(cls("i:5")) && /\bok\b/.test(cls("o:4")) && H.els.pbhint.textContent === "SHAPE is an input: drop it on an output", "patch bay: dragging an input: the hint says so");
  /* either way round */
  H.writes.length = 0;
  X.pbFinish("o:4");
  ok(js(H.writes.map((w) => [w[1], w[2]])) === js([[X.mxId(2), 4], [X.mxId(2) + 1, 3], [X.mxId(2) + 2, 32]]), "patch bay: in onto out works too (slot 2 = ENV2 > SHAPE +32)");
  /* the amount */
  X.pbSelect(2);
  X.pbAmount(-20, true);
  ok(X.dev.dump.p[X.mxId(2) + 2] === -20 && /-20/.test(H.els.pbseltext.textContent) && /id="pbamt" min="-64" max="63"/.test(html),
    `patch bay: the amount slider writes the selected cable's amount (${H.els.pbseltext.textContent})`);
  /* normals and internal routes */
  ok(/data-normal="cut"/.test(svg()) && /data-normal="pit"/.test(svg()) && !/data-route=/.test(svg()), "patch bay: dotted normals CUT -> CUTB, PIT1 -> PIT2; no routes while off");
  X.pbDrop("o:1", "i:22"); X.pbDrop("o:2", "i:19");
  ok(!/data-normal="cut"/.test(svg()) && !/data-normal="pit"/.test(svg()), "patch bay: a cable into CUTB / PIT2 hides its normal");
  X.dev.dump.p[139] = 40; X.dev.dump.p[140] = 1; X.dev.dump.p[126] = 6; X.renderPb();
  ok(/data-route="ffm"/.test(svg()) && /data-route="ring"/.test(svg()) && /data-route="split"/.test(svg()), "patch bay: FILTER FM, RING, SPLT drawn as internal routes");
  X.dev.dump.p[126] = 7; X.renderPb();
  const spn = /data-route="split"/.test(svg());
  X.dev.dump.p[126] = 3; X.renderPb();
  ok(spn && !/data-route="split"/.test(svg()), "patch bay: SPL+N too; STR+N not");
  /* remove: double-click, or the end dragged off */
  H.writes.length = 0;
  X.pbRemove(4);
  ok(js(H.writes.map((w) => [w[1], w[2]])) === js([[X.mxId(4), 0], [X.mxId(4) + 1, 0], [X.mxId(4) + 2, 0]]), "patch bay: removing a cable clears its slot");
  X.pbDragStart("o:1", { slot: 3, side: "d" });
  H.writes.length = 0;
  X.pbFinish(null);
  ok(H.writes.length === 3 && H.writes.every((w) => w[2] === 0) && H.writes[0][1] === X.mxId(3), "patch bay: a cable end dragged off its jack removes the cable");
  X.pbDrop("o:1", "i:22");
  X.pbDragStart("o:1", { slot: 3, side: "d" });
  H.writes.length = 0;
  X.pbFinish("i:5");
  ok(js(H.writes.map((w) => [w[1], w[2]])) === js([[X.mxId(3) + 1, 5]]), "patch bay: a cable end moved to another input re-patches it");
  /* 12 cables: the 13th refused */
  for (let c = 6; X.pbCables().length < 12; c++) X.pbDrop("o:" + c, "i:" + c);
  H.writes.length = 0; H.said.length = 0;
  const r13 = X.pbDrop("o:25", "i:28");
  ok(X.pbCables().length === 12 && r13 === false && !H.writes.length && H.els.pbhint.textContent === "All 12 cables are in use" && H.said.includes("All 12 cables are in use"),
    "patch bay: a 13th cable refused with the message");
  /* CLEAR ALL asks once */
  H.writes.length = 0;
  H.ask(false);
  X.pbClear();
  const asked1 = H.ask(true), none = H.writes.length;
  X.pbClear();
  const ids = new Set(Array.from({ length: 12 }, (_, i) => [0, 1, 2].map((f) => X.mxId(i + 1) + f)).flat());
  ok(asked1 === 1 && none === 0 && H.ask(true) === 1 && H.writes.length === 36 && H.writes.every((w) => w[2] === 0 && ids.has(w[1])) && new Set(H.writes.map((w) => w[1])).size === 36 &&
     !X.pbCables().length && H.asked.at(-1) === "Take out all 12 cables?", "patch bay: CLEAR ALL asks once (\"Take out all 12 cables?\"), then writes 36 zeros");
  /* no matrix: hidden (the generic cases: editorPatchBayGeneric) */
  const P = pbHarness("MARE", { drum: true });
  P.ctx.pbShow();
  ok(P.els.pbpanel.hidden === true && !P.ctx.pbOn(), "patch bay: hidden on the drum track");
  /* GLO > MIDI 3 in the settings card */
  ok(js(X.SX_PAGES.find((p) => p[0] === "MIDI 3")) === js(["MIDI 3", [90, 91, 92]]) && X.PAGE_PLACE["MIDI 3"] === "sxcard",
    "patch bay: GLO > MIDI 3 (CC A, CC B, SUST) in the settings card");
  /* (Stallion) the Settings' pages named and numbered as the device's footer has them ("TOUCH 2 9/13"): GLO's pages in
     params.c's order, the FM-1's and the SMK-37's (CLIPPER_SYNTH, BOARD_SMK37) */
  {
    const src = readFileSync(join(HERE, "..", "firmware", "src", "params.c"), "utf8");
    const a = src.indexOf('{"GLOBAL", FAM_GLO'), b = src.indexOf('{"MASTER", FAM_GLO, SC_GLOBAL, GR_NONE, {G_DUST, G_DUCK');
    const blk = src.slice(a, b), names = (s) => [...s.matchAll(/\{"([^"]+)", FAM_GLO/g)].map((m) => m[1]);
    const i = blk.indexOf("#if BOARD_SMK37"), e = blk.indexOf("#else", i), z = blk.indexOf("#endif", e);
    const common = names(blk.slice(0, i)), smk = [...common, ...names(blk.slice(i, e))], fm1 = [...common, ...names(blk.slice(e, z))];
    const tags = (bd, list) => list.every((n, k) => X.devPageTag(n, bd) === `GLO ${k + 1}/${list.length}`);
    const sxF = X.sxPages("fm1"), sxS = X.sxPages("smk");
    ok(a > 0 && i > 0 && fm1.length >= 13 && smk.length >= 14 && tags("fm1", fm1) && tags("smk", smk) && X.devPageTag("EQ+COMP") === "FX 11/11" &&
       X.devPageTag("AFTERTOUCH", "smk") === `GLO 8/${smk.length}` && sxF.map((p) => p[0]).join() === "MIDI,MIDI 2,MIDI 3,TOUCH,TOUCH 2,TOUCH 3,TOUCH 4,TOUCH 5,TOUCH 6,LIGHTS,SCREEN,SEQ" &&
       sxS.map((p) => p[0]).join() === "MIDI,MIDI 2,MIDI 3,AFTERTOUCH,PAD VEL,KEY AT,KEY AT 2,KEY AT 3,PAD AT,PAD AT 2,LIGHTS,SCREEN,SEQ" &&
       sxS.find((p) => p[0] === "AFTERTOUCH")[1].join() === "61,62,63,64" && sxF.every((p) => p[1] && p[2]) && sxS.every((p) => p[1] && p[2]),
       `Stallion: the Settings' pages as the device's (GLO: FM-1 ${fm1.length}, SMK-37 ${smk.length} pages; "GLO k/n" as its footer)`);
    ok(/\.group h3, h3\.devpgh \{ color: var\(--fg\);[^}]*border-top: 3px solid/.test(html) && /class: "devpgh" \}, el\("span", \{ text: title \}\), pgTag\(tag\)/.test(html),
       "Stallion: each device page a block: a bar above, its name large and bright, its place as a badge");
  }
  ok(!/HYBRID|HY_MX|4594/.test(html.slice(html.indexOf("/*PB-BEGIN*/"), html.indexOf("/*PB-END*/")).replace(/\nconst PB_ENG = \{[^}]*\};/, "")),
    "patch bay: names not from HYBRID's list (HYBRID only as an engine in PB_ENG)");
  editorPatchBayPointer();
}

/* fix round 1: the real pointer handlers (pbWire) driven with pointerdown / move / up; the text size and colour */
function editorPatchBayPointer() {
  const H = pbHarness(), X = H.ctx, svg = () => H.els.pbsvg.innerHTML, box = X.$("pbsvg");
  X.pbShow();
  X.pbWire(box);
  const on = box.on;
  /* a stand-in for the element pressed: a jack group, a name, a plug (inside its cable) or a cable */
  const tgt = (a) => ({ closest: (sel) => {
    for (const [k, v] of Object.entries(a)) if (sel.split(",").some((s) => s.trim() === `[data-${k}]`)) return { dataset: { [k]: String(v) } };
    return null;
  } });
  const box0 = { closest: () => null };
  let ts = 1000;
  const fire = (ty, x, y, target = box0, dt = 16) => { ts += dt; for (const f of on[ty] || []) f({ type: ty, button: 0, pointerId: 1, clientX: x, clientY: y, target, timeStamp: ts, preventDefault() {} }); };
  const L = X.pbLay(), J = (k) => L.jacks.get(k);
  const drag = (target, a, b) => { fire("pointerdown", a[0], a[1], target); fire("pointermove", a[0] + 10, a[1] + 10); fire("pointermove", b[0], b[1]); fire("pointerup", b[0], b[1]); };
  const xy = (k, dx = 0, dy = 0) => [J(k).x + dx, J(k).y + dy];
  const pairs = () => js(H.writes.map((w) => [w[1], w[2]]));
  const m = (n) => X.mxId(n);
  ok(!on.dblclick && on.pointerdown && on.pointermove && on.pointerup && on.pointercancel && on.touchstart,
    "patch bay pointer: pointer handlers wired, no dblclick listener (the capture retargets it)");
  /* a drop on the centre of a patched jack (a plug drawn over it) lands on that jack */
  X.pbDrop("o:1", "i:2");                          /* slot 1: LFO1 > CUT */
  H.writes.length = 0;
  drag(tgt({ jack: "o:24" }), xy("o:24"), xy("i:2", 3, -2));
  ok(pairs() === js([[m(2), 24], [m(2) + 1, 2], [m(2) + 2, 32]]), `patch bay pointer: a new cable dropped on a patched jack's centre lands there (${pairs()})`);
  X.pbDrop("o:3", "i:3");                          /* slot 3: ENV1 > SHAPE */
  H.writes.length = 0;
  drag(tgt({ end: "1:d", slot: 1 }), xy("i:2"), xy("i:3"));
  ok(pairs() === js([[m(1) + 1, 3]]) && X.pbCables().some((c) => c.n === 1 && c.s === 1 && c.d === 3),
    `patch bay pointer: a cable end moved onto a patched jack's plug re-patches it, the cable kept (${pairs()})`);
  H.writes.length = 0;
  drag(tgt({ end: "1:d", slot: 1 }), xy("i:3"), xy("i:1", 0, -50));
  ok(!H.writes.length && X.pbCables().some((c) => c.n === 1) && H.els.pbhint.textContent === X.t("pbNear"),
    `patch bay pointer: a cable end let go near a jack but on none stays, with a hint (${H.els.pbhint.textContent})`);
  H.writes.length = 0;
  drag(tgt({ end: "1:d", slot: 1 }), xy("i:3"), [L.w + 300, 10]);
  ok(pairs() === js([[m(1), 0], [m(1) + 1, 0], [m(1) + 2, 0]]), "patch bay pointer: a cable end let go well away from every jack takes the cable out");
  H.writes.length = 0;
  drag(tgt({ jack: "o:25" }), xy("o:25"), [L.w + 300, 10]);
  ok(!H.writes.length && H.els.pbhint.textContent === "Let go over a jack: no cable was made", "patch bay pointer: a new cable let go on nothing: no write, a hint");
  H.writes.length = 0;
  drag(tgt({ for: "o:25" }), xy("o:25", 0, 50), xy("i:5", 0, 50));   /* name to name: the names are grips and targets */
  ok(pairs() === js([[m(1), 25], [m(1) + 1, 5], [m(1) + 2, 32]]), `patch bay pointer: a drag from a name onto a name works (${pairs()})`);
  /* a second un-moved click on the same cable within 400 ms takes it out */
  const click = (n, dt) => { fire("pointerdown", 0, 0, tgt({ slot: n }), dt); fire("pointerup", 0, 0, box0, 30); };
  H.writes.length = 0;
  click(3, 2000);
  const picked = X.pbState().sel === 3 && !H.writes.length;
  click(3, 600);
  const slow = X.pbState().sel === 3 && !H.writes.length;
  click(3, 250);
  ok(picked && slow && pairs() === js([[m(3), 0], [m(3) + 1, 0], [m(3) + 2, 0]]) && !X.pbCables().some((c) => c.n === 3),
    "patch bay pointer: a click picks a cable, two clicks 600 ms apart do not take it out, two within 400 ms do");
  H.writes.length = 0;
  click(2, 2000); click(1, 100);
  ok(!H.writes.length && X.pbState().sel === 1, "patch bay pointer: clicks on two different cables pick, never take out");
  /* minors: EDIT jacks in words, no second identical cable, the real count in CLEAR ALL's question */
  X.pbDrop("o:2", "i:11");
  const e5 = X.pbCables().find((c) => c.d === 11);
  X.pbDragStart("i:11");
  ok(/to E5 CUT in/.test(X.pbWords(e5)) && H.els.pbhint.textContent === "E5 CUT is an input: drop it on an output", `patch bay: EDIT inputs named apart from the filter's (${X.pbWords(e5)})`);
  X.pbFinish(null);
  H.writes.length = 0;
  X.pbDrop("o:1", "i:2");                          /* slot 4: LFO1 > CUT; slot 2 is S&H > CUT */
  const one = X.pbCables().find((c) => c.s === 1 && c.d === 2).n;
  X.pbDragStart("i:2", { slot: one, side: "s" });   /* its LFO1 end moved onto S&H: would be slot 2 again */
  H.writes.length = 0;
  const r = X.pbFinish("o:24");
  ok(r === false && !H.writes.length && X.pbState().sel === 2 && H.els.pbhint.textContent === "That cable is already there", "patch bay: a re-patch onto an identical cable makes no second one");
  H.ask(false);
  X.pbClear();
  ok(H.asked.at(-1) === `Take out all ${X.pbCables().length} cables?` && X.pbCables().length === 4, `patch bay: CLEAR ALL's question counts the cables (${H.asked.at(-1)})`);
  /* the one-screen bay: names and captions 22 px bold in the bright text colour, the SVG scaled to
     the card (the sizes: editorPatchBayFit) */
  X.dev.dump.p[139] = 40; X.renderPb();
  const rule = (sel) => { const i = html.indexOf(sel + " {"); return i < 0 ? "" : html.slice(i, html.indexOf("}", i)); };
  ok(/font-size: 22px/.test(rule(".pb text")) && /font-weight: bold/.test(rule(".pb text")) && /fill: var\(--fg\)/.test(rule(".pb text")) &&
     /font-size: 22px/.test(rule(".pb .pbnt, .pb .pbcap")) && /fill: var\(--fg\);/.test(rule(".pb .pbnt, .pb .pbcap")),
     "patch bay: names and captions 22 px bold, bright");
  ok(/class="pbhelp" id="pbhelp"/.test(html) && /color: var\(--fg\)/.test(rule(".pbhelp")) && /touch-action: pan-y/.test(rule(".pb")) && !/pan-x/.test(rule(".pb")) &&
     /touch-action: none/.test(rule(".pb .pbj, .pb .pbjl, .pb .pbc")),
    "patch bay: the help text bright, not dim; a finger scrolls the page up and down (never sideways) until it presses a jack, a name or a cable");
  /* minors: the inner nodes and the dotted lines' captions over the cables */
  const lastCable = svg().lastIndexOf('<g class="pbc'), s2 = svg();
  ok(lastCable > 0 && s2.indexOf('class="pbnode"') > lastCable && s2.indexOf('data-cap="route"') > lastCable && s2.indexOf(">CLOCK<") > lastCable,
    "patch bay: VCO / MIXER / VCF nodes and the FM and CLOCK captions drawn over the cables");
  ok(!/CONTROL \/ ARP/.test(html) && !/CONTROL \/ ARP/.test(readFileSync(join(HERE, "../tests/denylist_allow.txt"), "utf8")), "patch bay: the module is CONTROL (no allow-list entry)");
}

/* the one-screen bay: the SVG scaled to the card (no sideways scrolling at a 1280 or a 1024 px card), every
   shape inside its viewBox, the names 16 px or more at 1024; a drop found where the pointer is under that scaling */
function editorPatchBayFit() {
  const rule = (sel) => { const i = html.indexOf("\n  " + sel + " {"); return i < 0 ? "" : html.slice(i, html.indexOf("}", i)); };
  const engs = ["MARE", "ANALOG", "FM6", "WHEEL", "SAMPLE"];
  for (const lang of ["en", "ja"]) {
    const bad = [];
    let vbw = 0, vbh = 0;
    for (const e of engs) {
      const H = pbHarness(e, { lang }), X = H.ctx;
      X.pbShow(); X.pbDrop("o:1", "i:2"); X.pbDrop("o:28", "i:14"); X.dev.dump.p[139] = 40; X.dev.dump.p[140] = 40; X.renderPb();
      const svg = H.els.pbsvg.innerHTML, vb = /<svg class="pb" viewBox="0 0 ([\d.]+) ([\d.]+)"/.exec(svg);
      if (!vb || /<svg class="pb"[^>]* width="\d/.test(svg)) { bad.push(e + ": no viewBox, or a fixed width"); continue; }
      const [W, Hh] = [+vb[1], +vb[2]];
      vbw = Math.max(vbw, W); vbh = Math.max(vbh, Hh);
      for (const r of svg.matchAll(/<rect class="(\w+)"[^>]* x="([-\d.]+)" y="([-\d.]+)" width="([\d.]+)" height="([\d.]+)"/g))
        if (+r[2] < 0 || +r[2] + +r[4] > W || +r[3] < 0 || +r[3] + +r[5] > Hh) bad.push(`${e} ${r[1]} ${r[2]},${r[3]} ${r[4]}x${r[5]}`);
      for (const c of svg.matchAll(/<circle class="(\w+)" cx="([-\d.]+)" cy="([-\d.]+)" r="([\d.]+)"/g))
        if (+c[2] - +c[4] < 0 || +c[2] + +c[4] > W || +c[3] - +c[4] < 0 || +c[3] + +c[4] > Hh) bad.push(`${e} ${c[1]} ${c[2]},${c[3]}`);
      /* a name never wider than its jack's share of the row (no two names overlap) */
      for (const j of X.pbLay().jacks.values()) if ((Math.max(3, [...j.label].length) * X.PB.CH + 12) > 2 * j.hw + 2) bad.push(`${e} ${j.k} ${j.label} too wide`);
    }
    ok(!bad.length && vbw === 1248 && vbh <= 900, `patch bay fit (${lang}): every shape inside the ${vbw} x ${vbh} viewBox, names within their jacks' room${bad.length ? ": " + bad.slice(0, 6).join("; ") : ""}`);
  }
  const card = /padding: \d+px (\d+)px/.exec(rule(".card")), pad = card ? +card[1] + 1 : 99;
  const minPx = (cw) => 22 * (cw - 2 * pad) / 1248;
  ok(/width: 100%/.test(rule(".pb")) && /height: auto/.test(rule(".pb")) && !/overflow-x/.test(rule("#pbsvg")) && !html.includes('id="pbwrap"') &&
     /table-layout: fixed/.test(rule(".pbmx")) && /width: 100%/.test(rule(".pbmx")) && minPx(1024) >= 16 && minPx(1280) >= 16,
    `patch bay fit: the SVG and the MATRIX table as wide as the card, nothing scrolls sideways; 22 px names show ${minPx(1024).toFixed(1)} px at a 1024 px card, ${minPx(1280).toFixed(1)} px at 1280`);
  /* drops under scaling: the card at 1024 (scale 0.793) and at 1280, the SVG lower down the page */
  for (const cw of [1024, 1280]) {
    const sc = (cw - 2 * pad) / 1248, H = pbHarness("ANALOG", { ctm: { a: sc, d: sc, e: 260 + pad, f: 731 } }), X = H.ctx, box = X.$("pbsvg");
    X.pbShow(); X.pbWire(box);
    const tgt = (k) => ({ closest: (sel) => (sel.split(",").some((q) => q.trim() === "[data-jack]") ? { dataset: { jack: k } } : null) });
    const scr = (k, dx = 0, dy = 0) => { const j = X.pbLay().jacks.get(k); return [(j.x + dx) * sc + 260 + pad, (j.y + dy) * sc + 731]; };
    let ts = 9000;
    const fire = (ty, [x, y], target = { closest: () => null }) => { for (const f of box.on[ty] || []) f({ type: ty, button: 0, pointerId: 1, clientX: x, clientY: y, target, timeStamp: ts += 16, preventDefault() {} }); };
    const drag = (a, b, bp) => { fire("pointerdown", bp, tgt(a)); fire("pointermove", [bp[0] + 9, bp[1] + 9]); fire("pointermove", b); fire("pointerup", b); };
    drag("o:3", scr("i:2", 20, -18), scr("o:3"));        /* (off centre, inside the hit radius) */
    drag("o:4", scr("i:1", 0, 30), scr("o:4"));          /* (on its name) */
    drag("o:5", scr("i:3", 0, -50), scr("o:5"));         /* (near, on no jack) */
    const c = X.pbCables().map((x) => `${x.s}>${x.d}`).join(" ");
    ok(c === "3>2 4>1" && H.els.pbhint.textContent === "Let go over a jack: no cable was made",
      `patch bay fit: drops land on the jack under the pointer at a ${cw} px card (scale ${sc.toFixed(3)}): ${c}`);
  }
}
/* (preset format 2) a device with 24 slots (P_ALL 177): the bay and the table have 24; slots 13..24 at the protocol 15 ids
   141..176 (userSet: req.pset / req.pget scope 2 there), the 25th cable refused, CLEAR ALL 72 zeros; 12 on P_ALL 141 */
function editorPatchBay24() {
  const H = pbHarness("ANALOG", { pall24: true }), X = H.ctx, E2 = H.els, p = X.dev.dump.p;
  X.pbShow(); X.pbWire(X.$("pbsvg"));
  ok(X.pbSlots() === 24 && X.mxSlots(X.dev.info) === 24 && pbHarness("ANALOG").ctx.pbSlots() === 12 &&
     pbHarness("ANALOG", { pe0: 91 }).ctx.pbSlots() === 6, "patch bay 24: pbSlots from INFO (24 on P_ALL 177, 12 on 141, 6 on Clipper 0.9)");
  ok(js(Array.from({ length: 12 }, (_, i) => X.mxId(13 + i))) === js(Array.from({ length: 12 }, (_, i) => 141 + 3 * i)) && X.mxId(12) === 115,
    "patch bay 24: slots 13..24 at ids 141, 144 .. 174 (slot 12 at 115)");
  const v15 = { proto: 15, pcount: 127, pall: 177, pe0: 119 };
  ok(js(E.req.pset(v15, 141, 5)) === js(E.req.set(2, 141, 5)) && js(E.req.pget(v15, 176)) === js(E.req.get(2, 176)) &&
     js(E.req.pdesc(v15, 175)) === js(E.req.desc(2, 175)) && js(E.req.pset(v15, 115, 5)) === js(E.req.set(0, 115, 5)),
    "patch bay 24: slots 13..24 go by scope 2 (req.pset / pget / pdesc), slots 1..12 by scope 0");
  for (let n = 1; n <= 12; n++) { p[X.mxId(n)] = n; p[X.mxId(n) + 1] = 1; p[X.mxId(n) + 2] = 10; }
  H.writes.length = 0;
  const r13 = X.pbDrop("o:1", "i:2");
  ok(r13 && js(H.writes.map((w) => [w[0], w[1], w[2]])) === js([[0, 141, 1], [0, 142, 2], [0, 143, 32]]) && X.pbCables().length === 13 &&
     /data-slot="13"/.test(E2.pbsvg.innerHTML) && E2.pbcount.innerHTML === "Cables: <b>13</b> of 24",
    `patch bay 24: the 13th cable goes to slot 13 (ids 141..143), drawn and counted (${E2.pbcount.innerHTML})`);
  const rows = (E2.pbmxrows.innerHTML.match(/data-row="/g) || []).length;
  ok(rows === 24 && /data-row="24"/.test(E2.pbmxrows.innerHTML) && /Up to 24 cables/.test(E2.pbhelp.textContent),
    `patch bay 24: the MATRIX table lists 24 slots (${rows}), "Up to 24 cables"`);
  for (let n = 14; n <= 24; n++) { p[X.mxId(n)] = n - 12; p[X.mxId(n) + 1] = 4; p[X.mxId(n) + 2] = -5; }
  X.renderPb();
  H.writes.length = 0; H.said.length = 0;
  const r25 = X.pbDrop("o:25", "i:28");
  ok(X.pbCables().length === 24 && r25 === false && !H.writes.length && H.els.pbhint.textContent === "All 24 cables are in use",
    "patch bay 24: a 25th cable refused with the message");
  X.pbRemove(24);
  ok(js(H.writes.map((w) => [w[1], w[2]])) === js([[174, 0], [175, 0], [176, 0]]) && X.pbCables().length === 23, "patch bay 24: removing cable 24 clears ids 174..176");
  H.writes.length = 0;
  H.ask(true);
  X.pbClear();
  const ids = new Set(Array.from({ length: 24 }, (_, i) => [0, 1, 2].map((f) => X.mxId(i + 1) + f)).flat());
  ok(H.writes.length === 72 && H.writes.every((w) => w[2] === 0 && ids.has(w[1])) && new Set(H.writes.map((w) => w[1])).size === 72 &&
     ids.has(141) && ids.has(176) && !X.pbCables().length && H.asked.at(-1) === "Take out all 23 cables?",
    "patch bay 24: CLEAR ALL writes 72 zeros, slots 13..24's ids 141..176 among them");
  /* a dead input on this engine: offered nowhere, slot 13 the same as slot 1 */
  const dead = (X.dev.info.noDst[X.dev.dump.engine] || [])[0];
  if (dead !== undefined) {
    H.writes.length = 0;
    const rd = X.pbDrop("o:1", "i:" + dead);
    ok(rd === false && !H.writes.length, `patch bay 24: no cable into ANALOG's dead input ${dead}`);
  }
  /* the device's MATRIX knob pages: 24 on P_ALL 177 */
  const lay = X.LAYOUT(119, "ANALOG", 177).find((g) => g.title === "MATRIX").pages;
  ok(lay.length === 24 && js(lay[23][2]) === js([174, 175, 176]) && X.LAYOUT(119, "ANALOG", 141).find((g) => g.title === "MATRIX").pages.length === 12,
    "patch bay 24: the MATRIX knob card has 24 pages on P_ALL 177, 12 on 141");
}
/* the two tabs: PATCH BAY and MATRIX, remembered; the MATRIX table the same slots, edited the same way; the
   count and the slots from the device (pbSlots), never a number of the editor's */
function editorPatchBayTabs() {
  const store = {}, H = pbHarness("ANALOG", { store }), X = H.ctx, E2 = H.els;
  X.pbShow(); X.pbWire(X.$("pbsvg"));
  const sel = (v) => E2["pbtab" + v].getAttribute("aria-selected");
  const start = sel("pb") === "true" && sel("mx") === "false" && E2.pbviewpb.hidden === false && E2.pbviewmx.hidden === true;
  for (const f of E2.pbtabmx.on.click) f({});
  ok(start && sel("mx") === "true" && sel("pb") === "false" && E2.pbviewmx.hidden === false && E2.pbviewpb.hidden === true && store["stallion-pb-view"] === "mx",
    "patch bay tabs: PATCH BAY first; a click on MATRIX shows the table, hides the bay, and is remembered");
  const H2 = pbHarness("ANALOG", { store }); H2.ctx.pbShow();
  const H3 = pbHarness("ANALOG"); H3.ctx.pbShow(); H3.ctx.pbWire(H3.ctx.$("pbsvg"));
  for (const f of H3.els.pbtabmx.on.click || []) f({});
  ok(H2.els.pbviewmx.hidden === false && H2.els.pbtabmx.getAttribute("aria-selected") === "true" && H3.els.pbviewpb.hidden === true,
    "patch bay tabs: the next visit opens on MATRIX; a browser without storage still switches (no error)");
  for (const f of E2.pbtabmx.on.keydown) f({ key: "ArrowLeft", preventDefault() {} });
  ok(sel("pb") === "true" && store["stallion-pb-view"] === "pb", "patch bay tabs: the arrow keys move between the tabs");
  /* the table: the bay's slots */
  X.pbDrop("o:1", "i:2"); X.pbDrop("o:6", "i:4"); X.pbSelect(1); X.pbAmount(-20, true);
  const rows = () => [...E2.pbmxrows.innerHTML.matchAll(/<tr class="([^"]*)" data-row="(\d+)">(.*?)<\/tr>/g)].map((m) => ({ cls: m[1], n: +m[2], h: m[3] }));
  const pick = (h, f) => { const m = new RegExp(`data-mx="\\d+:${f}"[^>]*>.*?<option value="(\\d+)" selected>([^<]*)<`).exec(h); return m ? [+m[1], m[2]] : null; };
  const r = rows();
  ok(r.length === 12 && js(pick(r[0].h, 0)) === js([1, "LFO1"]) && js(pick(r[0].h, 1)) === js([2, "CUT"]) && /value="-20"/.test(r[0].h) && /<output>-20<\/output>/.test(r[0].h) &&
     /class="pbmxbar" style="width:40px;background:var\(--t4\)"/.test(r[0].h) && /style="color:var\(--t4\)"/.test(r[0].h) && /&gt;/.test(r[0].h) &&
     js(pick(r[1].h, 1)) === js([4, "AMP"]) && /style="color:var\(--t1\)"/.test(r[1].h) && !/dead|UNUSED/.test(E2.pbmxrows.innerHTML) &&
     r.slice(2).every((x) => /\boff\b/.test(x.cls) && />OFF<\/span>/.test(x.h) && js(pick(x.h, 0)) === js([0, "OFF"])) && /\bsel\b/.test(r[0].cls),
    "patch bay tabs: MATRIX lists the 12 slots: #, SOURCE in its colour, >, DESTINATION, AMOUNT with a bar; OFF for empty");
  ok(E2.pbcount.innerHTML === "Cables: <b>2</b> of 12", `patch bay tabs: the count over both tabs (${E2.pbcount.innerHTML})`);
  /* edits through the table: userSet as the knobs; the bay follows */
  H.writes.length = 0;
  const ch = (n, f, v) => { for (const fn of E2.pbmxrows.on.change) fn({ target: { dataset: { mx: `${n}:${f}` }, value: String(v) } }); };
  ch(1, 1, 3);
  ch(3, 0, 25); const w1 = js(H.writes.map((w) => [w[1], w[2], w[3]]));
  ch(3, 1, 28);
  ok(w1 === js([[X.mxId(1) + 1, 3, true], [X.mxId(3), 25, true]]) && js(H.writes.slice(2).map((w) => [w[1], w[2]])) === js([[X.mxId(3) + 1, 28], [X.mxId(3) + 2, 32]]) &&
     /data-slot="3"/.test(E2.pbsvg.innerHTML) && /<g class="pbc[^"]*" data-slot="1"><title>Cable 1: LFO1 out to SHAPE in/.test(E2.pbsvg.innerHTML) && E2.pbcount.innerHTML === "Cables: <b>3</b> of 12",
    "patch bay tabs: a SOURCE / DESTINATION chosen in the table writes the slot (userSet), a new slot +32; the bay and the count follow");
  H.writes.length = 0;
  for (const fn of E2.pbmxrows.on.input) fn({ target: { dataset: { mx: "3:2" }, value: "-64", parentNode: null } });
  const busy = X.pbState().mxBusy, held = E2.pbmxrows.innerHTML;
  X.renderPb();
  const kept = E2.pbmxrows.innerHTML === held;
  ch(3, 2, -64);
  ok(busy && kept && js(H.writes.map((w) => [w[1], w[2], w[3]])) === js([[X.mxId(3) + 2, -64, false], [X.mxId(3) + 2, -64, true]]) && !X.pbState().mxBusy && /<output>-64<\/output>/.test(rows()[2].h),
    "patch bay tabs: an AMOUNT slider writes as it moves (not re-drawn under the finger), the last value on release");
  X.pbRemove(1);
  ok(/\boff\b/.test(rows()[0].cls) && E2.pbcount.innerHTML === "Cables: <b>2</b> of 12", "patch bay tabs: a cable taken out of the bay is OFF in the table");
  /* the device's slot count */
  const S = pbHarness("ANALOG", { pe0: 91 }); S.ctx.pbShow();
  const n6 = (S.els.pbmxrows.innerHTML.match(/data-row="/g) || []).length;
  ok(n6 === 6 && S.els.pbcount.innerHTML === "Cables: <b>0</b> of 6" && /Up to 6 cables/.test(S.els.pbhelp.textContent) && /Up to 12 cables/.test(E2.pbhelp.textContent),
    `patch bay tabs: a 6-slot device: 6 rows, "of 6", "Up to 6 cables" (pbSlots)`);
  const blk = html.slice(html.indexOf("/*PB-BEGIN*/"), html.indexOf("/*PB-END*/")).replace(/\/\*[\s\S]*?\*\//g, "")
    .replace(/\nconst PB_OUT = [^\n]*/, "").replace(/\nconst PB_IN = \[[\s\S]*?\n\];/, "").replace(/\nconst pbSlots = [^\n]*/, "");
  const tx = Object.entries(vm.runInNewContext("(" + html.slice(html.indexOf("const TEXT = {") + 13, html.indexOf("\n};", html.indexOf("const TEXT = {")) + 2) + ")"))
    .flatMap(([, o]) => Object.entries(o).filter(([k]) => /^pb/.test(k)).map(([, v]) => v));
  ok(!/\b(n|slot|slots|N)\s*[<>]=?\s*(6|12|24)\b|pbSlots\(\)\s*[!=]==?\s*\d|length: (6|12|24)\b/.test(blk) && !tx.some((v) => /\b(6|12)\b/.test(v)) && !/pbFull6/.test(html),
    "patch bay tabs: no slot count of the editor's own (the code and the texts take pbSlots)");
  /* (Stallion) one matrix: with the patch bay there is no card of MATRIX knobs and no button to one */
  ok(/if \(g\.title === "MATRIX" && pbOn\(\)\) continue;/.test(html) && !/id="pbknobs"/.test(html) && !E2.pbknobs,
    "patch bay tabs: no second matrix: the MATRIX knob card is not built while the patch bay shows, no button to it");
  /* MARE wording: none in the card's texts or comments, only in its own features */
  const mare = (H, lang) => { const X2 = pbHarness(H, { lang }); X2.ctx.pbShow(); return X2.els.pbhelp.textContent; };
  const card = html.slice(html.indexOf('<div class="card" id="pbpanel"'), html.indexOf("<!-- Clipper 0.8: the MOD SEQ"));
  const css = html.slice(html.indexOf("/* the patch bay and the matrix (two tabs"), html.indexOf("#msbody .msrow"));
  const pbLines = html.slice(html.indexOf("/*PB-BEGIN*/"), html.indexOf("/*PB-END*/")).split("\n").filter((l) => /MARE/.test(l))
    .filter((l) => !/PB_ENG = |eng [!=]== "MARE"|pbEng\(\) === "MARE"|MODE SPLT/.test(l));
  const tests = readFileSync(fileURLToPath(import.meta.url), "utf8").split("\n").filter((l) => !/\.test\(/.test(l)).join("\n");   /* (not these checks) */
  ok(!tx.some((v) => /MARE/.test(v)) && !/MARE/.test(card + css) && !pbLines.length && !/MARE's patch bay|Patch bay \(MARE\)|MARE-only patch/i.test(html + tests) &&
     /FILTER FM, RING, SPLT/.test(mare("MARE", "en")) && !/FILTER FM|SPLT/.test(mare("ANALOG", "en")) && /SPLT/.test(mare("MARE", "ja")) && !/SPLT/.test(mare("FM6", "ja")),
    `patch bay tabs: no MARE wording in the card, its texts or comments; the dotted routes' line only on the engine with them${pbLines.length ? ": " + pbLines.join(" | ") : ""}`);
}

/* the bay's jacks never hidden or taken by a cable; the drag-off distance in screen pixels;
   no duplicate cable from the table; the table kept while a choice is open or a slider held; the count written only
   when it changes; a phone-sized card opens on MATRIX; Home / End on the tabs; the card's height budget */
function editorPatchBayFix1() {
  const svgOf = (H) => H.els.pbsvg.innerHTML;
  const css = (sel) => { const i = html.indexOf("\n  " + sel + " {"); return i < 0 ? "" : html.slice(i, html.indexOf("}", i)); };
  const px = (rule, prop) => { const m = new RegExp(`(?:^|[;{\\s])${prop}: ([^;]+);`).exec(rule); return m ? m[1].trim() : ""; };
  const box4 = (v) => { const a = (v || "0").split(/\s+/).map((x) => parseFloat(x) || 0); return [a[0], a[1] ?? a[0], a[2] ?? a[0], a[3] ?? a[1] ?? a[0]]; };
  const njk = (sv) => (sv.match(/data-jack="/g) || []).length;
  /* 1. a ring over the cables for every jack with no cable; a press there takes the jack, not the cable over it */
  {
    const H = pbHarness("ANALOG"), X = H.ctx, box = X.$("pbsvg");
    X.pbShow(); X.pbWire(box);
    X.pbDrop("o:1", "i:2");
    const s = svgOf(H), last = s.lastIndexOf('<g class="pbc'), tops = [...s.matchAll(/<g class="pbjtop[^"]*" data-top="([^"]+)"/g)];
    const firstTop = s.indexOf('<g class="pbjtop');
    ok(last > 0 && firstTop > last && tops.length === njk(svgOf(H)) - 2 && !tops.some((m) => m[1] === "o:1" || m[1] === "i:2") && /\.pb \.pbjtop \{[^}]*pointer-events: none/.test(html),
      `patch bay fix: every unpatched jack's ring drawn again over the cables (${tops.length} of ${njk(svgOf(H))}), none on a patched one`);
    const L = X.pbLay(), J = (k) => L.jacks.get(k);
    let ts = 5000;
    const fire = (ty, x, y, target = { closest: () => null }) => { for (const f of box.on[ty] || []) f({ type: ty, button: 0, pointerId: 1, clientX: x, clientY: y, target, timeStamp: ts += 16, preventDefault() {} }); };
    const onCable = { closest: (sel) => (sel.split(",").some((q) => q.trim() === "[data-slot]") ? { dataset: { slot: "1" } } : null) };
    H.writes.length = 0;
    fire("pointerdown", J("i:3").x + 3, J("i:3").y - 2, onCable); fire("pointermove", J("i:3").x + 20, J("i:3").y + 20);
    fire("pointermove", J("o:4").x, J("o:4").y); fire("pointerup", J("o:4").x, J("o:4").y);
    const made = X.pbCables().some((c) => c.s === 4 && c.d === 3);
    fire("pointerdown", J("i:3").x + 40, J("i:3").y, onCable); fire("pointerup", J("i:3").x + 40, J("i:3").y);
    ok(made && X.pbState().sel === 1, "patch bay fix: a press on a cable over an unpatched jack drags from the jack; farther off, it picks the cable");
  }
  /* 2. the drop and drag-off distances in screen pixels: a small card (half scale) */
  {
    const sc = 0.5, H = pbHarness("ANALOG", { ctm: { a: sc, d: sc, e: 0, f: 0 } }), X = H.ctx, box = X.$("pbsvg");
    X.pbShow(); X.pbWire(box);
    const L = X.pbLay(), J = (k) => L.jacks.get(k), S = (x, y) => [x * sc, y * sc];
    let ts = 7000;
    const fire = (ty, [x, y], target = { closest: () => null }) => { for (const f of box.on[ty] || []) f({ type: ty, button: 0, pointerId: 1, clientX: x, clientY: y, target, timeStamp: ts += 16, preventDefault() {} }); };
    const on = (k, v) => ({ closest: (sel) => (sel.split(",").some((q) => q.trim() === `[data-${k}]`) ? { dataset: { [k]: v } } : null) });
    const drag = (target, a, b) => { fire("pointerdown", a, target); fire("pointermove", [a[0] + 9, a[1] + 9]); fire("pointermove", b); fire("pointerup", b); };
    X.pbDrop("o:1", "i:2");
    drag(on("end", "1:d"), S(J("i:2").x, J("i:2").y), S(J("i:2").x, J("i:2").y - 70));   /* 35 px off: near, kept */
    const kept = X.pbCables().some((c) => c.n === 1) && H.els.pbhint.textContent === X.t("pbNear");
    drag(on("jack", "o:3"), S(J("o:3").x, J("o:3").y), S(J("i:4").x, J("i:4").y - 44));   /* 22 px off a jack: lands */
    const landed = X.pbCables().some((c) => c.s === 3 && c.d === 4);
    drag(on("end", "1:d"), S(J("i:2").x, J("i:2").y), S(L.w + 400, 10));                  /* far off: out */
    ok(kept && landed && !X.pbCables().some((c) => c.n === 1),
      `patch bay fix: at half scale a cable end let go 35 px off stays, a drop 22 px off a jack lands, one far off comes out (${H.els.pbhint.textContent})`);
  }
  /* 3. the table refuses a second identical cable: the slot that has it is picked and said */
  {
    const H = pbHarness("ANALOG"), X = H.ctx, E2 = H.els;
    X.pbShow(); X.pbWire(X.$("pbsvg"));
    X.pbDrop("o:1", "i:2"); X.pbDrop("o:4", "i:3");
    H.writes.length = 0;
    const ch = (n, f, v) => { for (const fn of E2.pbmxrows.on.change) fn({ target: { dataset: { mx: `${n}:${f}` }, value: String(v) } }); };
    ch(2, 0, 1);                                     /* 2: LFO1 > SHAPE: allowed */
    const ok1 = X.pbCables().some((c) => c.n === 2 && c.s === 1 && c.d === 3);
    H.writes.length = 0;
    ch(2, 1, 2);                                     /* 2: LFO1 > CUT: slot 1 has it */
    const r1 = !H.writes.length && X.pbState().sel === 1 && E2.pbhint.textContent === "That cable is already there" &&
      /data-mx="2:1"[^>]*>.*?<option value="3" selected>/.test(E2.pbmxrows.innerHTML);
    ch(3, 0, 1); H.writes.length = 0; X.pbSelect(2);
    const r2 = X.pbMxSet(3, 1, 2) === false && !H.writes.length && X.pbState().sel === 1;
    ok(ok1 && r1 && r2, "patch bay fix: the MATRIX table refuses a SOURCE / DESTINATION another slot already has; that slot is picked, \"That cable is already there\"");
  }
  /* 5, 6. the table: a held slider let go anywhere on the page frees it; an open choice is not re-drawn under it */
  {
    const H = pbHarness("ANALOG"), X = H.ctx, E2 = H.els;
    X.pbShow(); X.pbWire(X.$("pbsvg"));
    X.pbDrop("o:1", "i:2");
    for (const fn of E2.pbmxrows.on.input) fn({ target: { dataset: { mx: "1:2" }, value: "10", parentNode: null } });
    const busy = X.pbState().mxBusy;
    for (const fn of H.doc.on.pointerup || []) fn({});
    ok(busy && !X.pbState().mxBusy && (H.doc.on.pointercancel || []).length > 0, "patch bay fix: a slider let go outside the table (document pointerup / cancel) no longer holds the table");
    for (const fn of E2.pbmxrows.on.pointerup) fn({});   /* (no slider held: this is the choice alone) */
    H.doc.activeElement = { tagName: "SELECT", dataset: { mx: "1:1" } };
    const held = E2.pbmxrows.innerHTML;
    X.dev.dump.p[X.mxId(1) + 2] = 50; X.renderPb();
    const kept = E2.pbmxrows.innerHTML === held;
    H.doc.activeElement = null;
    for (const fn of E2.pbmxrows.on.focusout) fn({});
    const after = /value="50"/.test(E2.pbmxrows.innerHTML);
    ok(kept && after, "patch bay fix: a device update leaves the table alone while one of its choices has the focus, then follows");
  }
  /* 7. the count's live region written only when its text changes */
  {
    const H = pbHarness("ANALOG"), X = H.ctx, el = X.$("pbcount");
    X.pbShow();
    let n = 0, v = el.innerHTML;
    Object.defineProperty(el, "innerHTML", { get: () => v, set: (x) => { n++; v = x; } });
    X.renderPb(); X.renderPb(); X.pbSelect(null);
    const quiet = n === 0;
    X.pbDrop("o:1", "i:2");
    ok(quiet && n === 1 && v === "Cables: <b>1</b> of 12", `patch bay fix: the cable count (aria-live) written only when it changes (${n} writes)`);
  }
  /* 8. a phone-sized card opens on MATRIX (unless the tab was chosen) and says the bay wants a larger screen */
  {
    const narrow = (w, store) => { const H = pbHarness("ANALOG", store ? { store } : {}); H.ctx.$("pbpanel").clientWidth = w; H.ctx.pbShow(); return H; };
    const a = narrow(600), b = narrow(600, { "stallion-pb-view": "pb" }), c = narrow(1100);
    ok(a.els.pbviewmx.hidden === false && a.els.pbsmall.hidden === false && b.els.pbviewpb.hidden === false && b.els.pbsmall.hidden === false &&
       c.els.pbviewpb.hidden === false && c.els.pbsmall.hidden === true && /pbSmall: "[^"]*larger screen/.test(html) && html.includes('id="pbsmall"'),
      "patch bay fix: a card under 700 px opens on MATRIX (a chosen tab kept) and says the bay is best on a larger screen");
  }
  /* 9. the tabs: Home / End; the bay's panel can take the focus */
  {
    const store = {}, H = pbHarness("ANALOG", { store }), X = H.ctx;
    X.pbShow(); X.pbWire(X.$("pbsvg"));
    for (const f of H.els.pbtabpb.on.keydown) f({ key: "End", preventDefault() {} });
    const end = H.els.pbtabmx.getAttribute("aria-selected") === "true";
    for (const f of H.els.pbtabmx.on.keydown) f({ key: "Home", preventDefault() {} });
    ok(end && H.els.pbtabpb.getAttribute("aria-selected") === "true" && /id="pbviewpb" role="tabpanel"[^>]*tabindex="0"/.test(html),
      "patch bay fix: Home and End pick the first and last tab; the PATCH BAY panel is focusable (tabindex 0)");
  }
  /* 4. the height budget: tabs + bay on one screen at 1280 x 650 and 1024 x 600 (a 768 or 720 px laptop's window) */
  {
    const card = box4(px(css(".card"), "padding")), bord = parseFloat(px(css(".card"), "border")) || 0;
    const tab = css(".pbtab"), tabH = parseFloat(px(tab, "line-height")) + 2 * box4(px(tab, "padding"))[0] + 2 * (parseFloat(px(tab, "border")) || 0);
    const clr = css("#pbpanel .bar button, #pbdel, #pbclear"), clrH = parseFloat(px(clr, "line-height")) + 2 * box4(px(clr, "padding"))[0];
    const row = Math.max(tabH, clrH) + box4(px(css(".pbtabs"), "margin"))[2];
    const hl = css(".pbhint");
    const hintH = parseFloat(px(hl, "min-height")) + box4(px(hl, "margin"))[0] + box4(px(hl, "margin"))[2];
    const chrome = bord + card[0] + row + hintH;
    const cap = /max-height: max\((\d+)px, calc\(100vh - var\(--pbcapv, (\d+)px\)\)\)/.exec(css(".pb")) || [];
    const side = parseFloat((/grid-template-columns: (\d+)px/.exec(css(".app")) || [])[1]), main = box4(px(css(".main"), "padding"))[1];
    const order = (a, b) => html.indexOf(a) > 0 && html.indexOf(a) < html.indexOf(b);
    const tabsRow = html.slice(html.indexOf('<div class="pbtabs"'), html.indexOf('<p class="pbsmall"'));
    const shortTop = /@media \(max-height: 760px\) \{[^}]*#pbpanel:not\(\[hidden\]\)[^{]*\.top \{ position: static; \}/.test(html);
    const rep = [], bad = [];
    for (const [vw, vh] of [[1280, 650], [1024, 600]]) {
      const inner = vw - side - 2 * main - 2 * (card[1] + bord), sw = inner / 1248;
      for (const e of ["ANALOG", "MARE", "FM6"]) {
        const H = pbHarness(e); H.ctx.pbShow();
        const vb = /viewBox="0 0 (\d+) (\d+)"/.exec(svgOf(H)), h = +vb[2];
        const bay = Math.min(h * sw, Math.max(+cap[1], vh - +cap[2])), name = 22 * bay / h;
        rep.push(`${vw}x${vh} ${e} ${Math.round(chrome + bay)} px, names ${name.toFixed(1)} px`);
        if (chrome + bay > vh || name < 11.5) bad.push(`${vw}x${vh} ${e}`);
      }
    }
    ok(cap.length && +cap[2] >= chrome && !bad.length && shortTop && /id="pbclear"/.test(tabsRow) && /id="pbcount"/.test(tabsRow) &&
       !html.includes('class="pblegend"') && !html.includes('id="pbwarn"') && order('id="pbhint"', 'id="pbviewpb"') && order('id="pbsvg"', 'id="pbhelp"'),
      `patch bay fix: tabs + bay fit the screen (chrome ${chrome} px, the bay at most 100vh - ${cap[2]} px): ${rep.join("; ")}${bad.length ? " TOO TALL " + bad.join(", ") : ""}`);
    /* the colour legend inside the SVG's top margin, over the first output row, within the viewBox */
    const H = pbHarness("ANALOG", { lang: "ja" }), X = H.ctx; X.pbShow();
    const H2 = pbHarness("ANALOG"); H2.ctx.pbShow();
    const legs = (S) => [...svgOf(S).matchAll(/<text class="pbleg" x="([\d.]+)" y="([\d.]+)">([^<]*)</g)].map((m) => [+m[1], +m[2], m[3]]);
    /* (a line of it over the first output row's rings, or on the INPUTS line over the first module; a CJK glyph two cells) */
    const cells = (s) => [...s].reduce((n, ch) => n + (/[⺀-꓏＀-｠]/.test(ch) ? 2 : 1), 0);
    const top = X.PB.OY[0] - X.PB.JR - 6, inl = X.PB.IY - 66;
    ok([H, H2].every((S) => legs(S).length === 6 && legs(S).every(([x, y, t]) => x > 0 && x + cells(t) * X.PB.CH <= 1248 && (y < top - 10 || y === inl))) &&
       legs(H2).map((l) => l[2]).join("|") === "pitch|gate, trigger|envelope|LFO, modulation|performance|+V" && /^Outputs: filled jack, boxed name\. Inputs: open jack\. /.test(H2.els.pbhelp.textContent),
      `patch bay fix: the colour legend drawn in the bay's top margin (en and ja); its "filled / open" line opens the help under the bay`);
  }
}

/* the Sound tab's generic MATRIX card (MATRIX 1..12, the MATRIX KNOBS button): paramRow builds each slot's
   DESTINATION select with only the codes the engine uses (INFO's masks), on every engine of the demo device; the
   SOURCE and AMOUNT untouched; a device without masks offers every code; a new engine rebuilds the rows */
function editorMatrixKnobs() {
  const pc = readFileSync(join(HERE, "../firmware/src/params.c"), "utf8");
  const arr = (nm) => [...((new RegExp(`N_${nm}\\[\\] = \\{([\\s\\S]*?)\\};`).exec(pc) || [])[1] || "").replace(/\/\*[\s\S]*?\*\//g, "").matchAll(/"([^"]*)"/g)].map((m) => m[1]);
  const MSRC = arr("MSRC"), MDST = arr("MDST"), MF = pbMock();
  const lay = html.slice(html.indexOf("/*LAYOUT-BEGIN*/"), html.indexOf("/*LAYOUT-END*/"));
  const i0 = html.indexOf("function paramRow("), row = html.slice(i0, html.indexOf("/* the big value turns like a knob", i0));
  const el = (tag, at = {}, ...kids) => { const o = { tag, at, kids: [], tagName: tag.toUpperCase(), style: { setProperty() {} }, addEventListener() {},
    append(...k) { o.kids.push(...k); }, classList: { toggle() {} } }; o.kids.push(...kids.filter((k) => typeof k === "object")); return o; };
  const run = (engine, noDst) => {
    const ctx = { F: E.F, el, RICH_SELECT: false, WAVE_IC: {}, G_SKIP: new Set(), ctl: new Map(), visible: () => true, key: (s, id) => s + ":" + id,
      synDesc: (id, d) => d, fmtValue: (d, v) => [d.names[v - d.min]], showValue() {}, valueDrag() {}, userSet() {},
      dev: { info: { pe0: 119, noDst }, dump: { engine, p: new Array(141).fill(0) }, pdesc: [] } };
    vm.createContext(ctx);
    vm.runInContext(lay + "\n" + row + "\n;Object.assign(globalThis, { mxId, paramRow, isMxDst });", ctx);
    for (let n = 1; n <= 12; n++) {
      ctx.dev.pdesc[ctx.mxId(n)] = { label: "SRC", fmt: E.F.ENUM, min: 0, max: MSRC.length - 1, names: MSRC };
      ctx.dev.pdesc[ctx.mxId(n) + 1] = { label: "DST", fmt: E.F.ENUM, min: 0, max: MDST.length - 1, names: MDST };
      ctx.dev.pdesc[ctx.mxId(n) + 2] = { label: "AMT", fmt: E.F.INT, min: -64, max: 63, names: [] };
    }
    const opts = (id) => { const r = ctx.paramRow(0, id), sel = r && r.kids.find((k) => k.tag === "select"); return sel ? sel.kids.filter((k) => k.tag === "option").map((k) => +k.at.value) : null; };
    return { ctx, opts };
  };
  const bad = [], rep = [];
  MF.eng.forEach((name, e) => {
    const R = run(e, MF.dd), dead = MF.dd[e];
    for (let n = 1; n <= 12; n++) {
      const d = R.opts(R.ctx.mxId(n) + 1), sr = R.opts(R.ctx.mxId(n));
      const want = MDST.map((_, c) => c).filter((c) => !dead.includes(c));
      if (js(d) !== js(want) || !sr || sr.length !== MSRC.length) bad.push(`${name} MATRIX ${n}: ${d && d.filter((c) => dead.includes(c)).join(",")}`);
    }
    if (["RODEO", "MARE", "SAMPLE"].includes(name)) rep.push(`${name} ${MDST.length - dead.length}`);
  });
  const all = run(0, null).opts(61);
  const reb = /dev\.built !== dev\.dump\.engine[^\n]*buildGroups\(\)/.test(html) && /\{ t: "LFO", title: "MATRIX", pages: MX_PAGES\(pe0, pall\) \}/.test(html);
  ok(!bad.length && all && all.length === MDST.length && reb && MF.eng.length > 20,
    `MATRIX card: every engine's DESTINATION selects (MATRIX 1..12, MATRIX KNOBS) hold only the codes it uses (${rep.join(", ")} of ${MDST.length}), SOURCE all; no masks: every code; a new engine rebuilds them${bad.length ? ": " + bad.slice(0, 4).join("; ") : ""}`);
}

/* (Stallion) PONY's CHAR (EDIT 3, v15 id 181, params.c F_CHAR): paramRow builds the set (CLEAN, CLASSIC, FULL; CUSTOM
   shown, not chosen) and a box a quirk from the FM-1's names; a box sends the value with its bit turned, the set its
   value; the mock describes it as the firmware */
function editorChar() {
  const i0 = html.indexOf("function paramRow("), row = html.slice(i0, html.indexOf("/* the big value turns like a knob", i0));
  const el = (tag, at = {}, ...kids) => { const o = { tag, at, kids: [], tagName: tag.toUpperCase(), style: { setProperty() {} }, on: {},
    addEventListener(t, f) { o.on[t] = f; }, append(...k) { o.kids.push(...k); }, classList: { toggle() {} }, querySelectorAll: () => [] };
    o.kids.push(...kids); if (at.type === "checkbox") o.dataset = { bit: String(at["data-bit"]) }; return o; };
  const sent = [];
  const ctx = { F: E.F, el, RICH_SELECT: false, WAVE_IC: {}, G_SKIP: new Set(), ctl: new Map(), visible: () => true, key: (s, id) => s + ":" + id,
    synDesc: (id, d) => d, fmtValue: E.fmtValue, showValue() {}, valueDrag() {}, userSet: (s, id, v) => sent.push(v),
    dev: { info: { pe0: 119 }, dump: { engine: 21, p: new Array(182).fill(0) }, pdesc: [] } };
  const names = ["STEPS", "HISS", "HPF", "LEVER LFO", "GATE 1 MS", "DARK NOISE", "VCA CURVE", "LFO DELAY", "COARSE CUT", "CPU TICK"];
  ctx.dev.pdesc[181] = { label: "CHAR", fmt: E.F.CHAR, min: 0, max: 0x3FF, def: 0x3E, names };
  vm.createContext(ctx);
  vm.runInContext(row + "\n;Object.assign(globalThis, { paramRow });", ctx);
  const r = ctx.paramRow(0, 181), sel = r.kids.find((k) => k.tag === "select"), bits = r.kids.find((k) => k.at && k.at.class === "charbits");
  const opts = sel ? sel.kids.filter((k) => k.tag === "option").map((k) => +k.at.value) : [];
  const boxes = bits ? bits.kids.map((l) => l.kids.find((k) => k.tag === "input")) : [];
  ctx.ctl.get("0:181").cur = 0x3E;
  boxes[1].checked = false; boxes[1].on.change();               /* HISS off */
  boxes[9].checked = true; boxes[9].on.change();                /* CPU TICK on */
  sel.value = "1023"; sel.on.change();
  ok(js(opts) === js([0, 0x3E, 0x3FF, -1]) && boxes.length === 10 && js(sent) === js([0x3C, 0x3E | 0x200, 0x3FF]) &&
     /knob enum char/.test(r.at.class) && E.fmtValue(ctx.dev.pdesc[181], 0x3C)[0] === "CUSTOM",
    `editor: PONY's CHAR: the set (${opts.join(" ")}), ${boxes.length} boxes; HISS off sends ${sent[0]}, CPU TICK on ${sent[1]}, FULL ${sent[2]}`);
}

/* the bay's zoom: FIT, the whole bay in a laptop's card; names never under 18 px on a small screen (it starts
   zoomed, and scrolls); − / FIT / + on the top row; Ctrl or ⌘ + the wheel; a pinch; a drag on empty space pans; a cable
   dragged near the edge scrolls the bay; drops land at any zoom and scroll; the zoom remembered per browser.
   zoomRig: the harness with the box measured as a browser would (cw: its width, vh: the window's height; the SVG's
   screen matrix from the zoom and the scroll, the box at 100, 200 on the screen; the scroll clamped to the content) */
function zoomRig(engine, cw, vh, store, above = 0) {   /* above: the card laid out, the bay's box that far under its top */
  const H = pbHarness(engine, store ? { store } : {}), X = H.ctx, box = X.$("pbsvg");
  X.window = { innerHeight: vh, on: {}, addEventListener(ty, f) { (this.on[ty] ||= []).push(f); } };
  const left = 100, top = 200, st = () => X.pbState();
  let capv = 131;                                    /* (PB.CAPV: the card over the bay, as measured on a laptop) */
  if (above) { X.$("pbpanel").getBoundingClientRect = () => ({ top: 200 - above, left: 100, width: cw, height: 2000 }); capv = above + 16; }
  box.style.setProperty = (k, v) => { box.style[k] = v; };
  const fit = () => { const [w, h] = st().vb; return Math.min(cw / w, Math.max(320, vh - capv) / h); };
  const sc = () => st().zoom || fit();
  const capH = () => Math.max(320, vh - capv);
  const viewH = () => (st().zoom ? Math.min(st().vb[1] * st().zoom, capH()) : st().vb[1] * fit());
  let sl = 0, sT = 0;
  Object.defineProperty(box, "scrollLeft", { get: () => sl, set: (v) => { sl = st().zoom ? Math.max(0, Math.min(v, st().vb[0] * st().zoom - cw)) : 0; } });
  Object.defineProperty(box, "scrollTop", { get: () => sT, set: (v) => { sT = st().zoom ? Math.max(0, Math.min(v, st().vb[1] * st().zoom - viewH())) : 0; } });
  box.clientWidth = cw;
  const ctm = () => {
    const s = sc(), ox = st().zoom ? -sl : (cw - st().vb[0] * s) / 2, oy = st().zoom ? -sT : 0, e = left + ox, f = top + oy;
    return { a: s, b: 0, c: 0, d: s, e, f, inverse: () => ({ a: 1 / s, b: 0, c: 0, d: 1 / s, e: -e / s, f: -f / s }) };
  };
  const svgEl = { style: {}, getScreenCTM: ctm };
  box.querySelector = (q) => (q === "svg" ? svgEl : null);
  box.getBoundingClientRect = () => ({ left, top, right: left + cw, bottom: top + viewH(), width: cw, height: viewH() });
  X.pbShow(); X.pbWire(box);
  const scr = (k) => { const j = X.pbLay().jacks.get(k), m = ctm(); return [m.a * j.x + m.e, m.d * j.y + m.f]; };
  const unit = (x, y) => { const v = ctm().inverse(); return [v.a * x + v.e, v.d * y + v.f]; };
  let ts = 90000;
  const fire = (ty, x, y, target = { closest: () => null }, more = {}) => {
    let prevented = false;
    for (const f of box.on[ty] || []) f({ type: ty, button: 0, pointerId: 1, pointerType: "mouse", clientX: x, clientY: y, target, timeStamp: ts += 16, preventDefault() { prevented = true; }, ...more });
    return prevented;
  };
  const onJack = (k) => ({ closest: (sel) => (sel.split(",").some((q) => q.trim() === "[data-jack]") ? { dataset: { jack: k } } : null) });
  const click = (id) => { for (const f of X.$(id).on.click || []) f({}); };
  /* the window resized to a card cw2 wide, vh2 high (its resize listeners) */
  const resize = (cw2, vh2) => { cw = cw2; vh = vh2; box.clientWidth = cw2; X.window.innerHeight = vh2; for (const f of X.window.on.resize || []) f({}); };
  return { H, X, box, svgEl, st, fit, sc, scr, unit, fire, onJack, click, resize, names: () => 22 * sc() };
}
function editorPatchBayZoom() {
  const engs = ["ANALOG", "MARE", "FM6"];
  /* FIT unchanged on a 14" laptop's card (1280 .. 1512 px wide, a window 860 or 900 px high): no zoom, the SVG sized by
     the CSS as before, FIT pressed, names 18 px or more */
  {
    const rep = [], bad = [];
    for (const cw of [1280, 1400, 1512]) for (const vh of [860, 900]) for (const e of engs) {
      const R = zoomRig(e, cw, vh);
      if (R.st().zoom !== 0 || R.svgEl.style.width !== "" || R.box.classList.contains("zoomed") || R.X.$("pbzfit").getAttribute("aria-pressed") !== "true" || R.names() < 18) bad.push(`${cw}x${vh} ${e}`);
      if (vh === 860 && cw !== 1400) rep.push(`${cw} ${e} ${R.names().toFixed(1)} px`);
    }
    const rule = (sel) => { const i = html.indexOf(sel + " {"); return i < 0 ? "" : html.slice(i, html.indexOf("}", i)); };
    ok(!bad.length && /width: 100%/.test(rule(".pb")) && /max-height: max\(320px, calc\(100vh - var\(--pbcapv, 131px\)\)\)/.test(rule(".pb")),
      `patch bay zoom: FIT as before on a 14" laptop's card (${rep.join(", ")})${bad.length ? "; zoomed: " + bad.join(", ") : ""}`);
  }
  /* MARE (the tallest bay: its inner nodes) at FIT on a 14" laptop's card, 800 to 900 px high (a bookmarks bar, a
     smaller window): names 18.4 px or more, never in scroll mode; every engine's FIT within 2.5 px of what it was (the
     bay's old geometry: MARE's node band 60, the window less 156 px) */
  {
    const rep = [], bad = [];
    for (const cw of [1280, 1400, 1512]) for (const vh of [800, 820, 860, 900]) for (const e of engs) {
      const R = zoomRig(e, cw, vh), h0 = R.st().vb[1] + (e === "MARE" ? 2 * (60 - R.X.PB.NB) : 0);
      const was = 22 * Math.min(cw / 1248, Math.max(320, vh - 156) / h0);
      if (R.st().zoom !== 0 || R.box.classList.contains("zoomed") || (e === "MARE" ? R.names() < 18.4 : R.names() < 18) || Math.abs(R.names() - was) > 2.5)
        bad.push(`${cw}x${vh} ${e} ${R.names().toFixed(1)} (was ${was.toFixed(1)})`);
      if (e === "MARE" && cw !== 1400) rep.push(`${cw}x${vh} ${R.names().toFixed(1)}`);
    }
    ok(!bad.length, `patch bay zoom: MARE at FIT on a 14" laptop's card, 800 to 900 px high: ${rep.join(", ")} px; every engine within 2.5 px of before${bad.length ? " BAD " + bad.join(", ") : ""}`);
  }
  /* CAPV measured: the bay's box under the card's top as laid out (the top row on two lines: a wrapped row, a long
     language): the bay's cap and FIT follow it, the CSS's cap the same (--pbcapv) */
  {
    const one = zoomRig("ANALOG", 1400, 860, undefined, 115), two = zoomRig("ANALOG", 1400, 860, undefined, 171);
    const fit1 = one.fit(), fit2 = two.fit(), h = two.st().vb[1];
    ok(one.box.style["--pbcapv"] === "131px" && two.box.style["--pbcapv"] === "187px" && Math.abs(fit2 - (860 - 187) / h) < 1e-9 && fit2 < fit1 &&
       Math.abs(two.names() - 22 * fit2) < 1e-9 && /height \(NB 34:/.test(html) && /function pbCapV\(\)/.test(html),
      `patch bay zoom: CAPV measured from the card (one line: 131 px, two: 187 px), FIT and the CSS cap follow (${(22 * fit1).toFixed(1)}, ${(22 * fit2).toFixed(1)} px names)`);
  }
  /* the 18 px floor: a 1024 x 600 window (card 694 px), 768 px wide (card 670 px; 1024 or 600 high): zoomed to 18 px names */
  {
    const rep = [], bad = [];
    for (const [cw, vh] of [[694, 600], [670, 1024], [670, 600]]) for (const e of engs) {
      const R = zoomRig(e, cw, vh), z = R.st().zoom;
      rep.push(`${cw}x${vh} ${e} ${R.names().toFixed(1)} px (FIT ${(22 * R.fit()).toFixed(1)})`);
      if (Math.abs(R.names() - 18) > 0.01 || !R.box.classList.contains("zoomed") || R.svgEl.style.width !== Math.round(1248 * z) + "px" ||
          R.svgEl.style.height !== Math.round(R.st().vb[1] * z) + "px" || R.X.$("pbzfit").getAttribute("aria-pressed") !== "true" ||
          R.X.$("pbzout").disabled !== true) bad.push(`${cw}x${vh} ${e}`);
    }
    const zr = /#pbsvg\.zoomed \{([^}]*)\}/.exec(html), zs = /#pbsvg\.zoomed \.pb \{([^}]*)\}/.exec(html);
    ok(!bad.length && zr && /overflow: auto/.test(zr[1]) && /max-height: max\(320px, calc\(100vh - var\(--pbcapv, 131px\)\)\)/.test(zr[1]) && /touch-action: none/.test(zr[1]) &&
       zs && /max-width: none/.test(zs[1]),
      `patch bay zoom: names never under 18 px: on a small screen FIT is the 18 px floor (FIT pressed, − off), the bay scrolls in a box as high as the bay at FIT (${rep.slice(0, 3).join("; ")})${bad.length ? " BAD " + bad.join(", ") : ""}`);
    /* the page still scrolls with the pointer over the bay (no scroll chaining cut, but sideways: no swipe back) */
    ok(zr && !/overscroll-behavior:/.test(zr[1]) && /overscroll-behavior-x: contain/.test(zr[1]),
      "patch bay zoom: zoomed, the page scrolls on past the bay's box (overscroll contained sideways only)");
  }
  /* the floor on every path: −, the wheel, a pinch, Safari's gesture, FIT: never names under 18 px; − off at the floor */
  {
    const R = zoomRig("MARE", 694, 600), fl = 18 / 22, rep = [];
    const at = (what) => { rep.push(`${what} ${R.names().toFixed(1)}`); return Math.abs(R.st().zoom - fl) < 1e-9; };
    const t = (ty, id, x, y) => { for (const fn of R.box.on[ty] || []) fn({ type: ty, button: 0, pointerId: id, pointerType: "touch", clientX: x, clientY: y, target: { closest: () => null }, timeStamp: 1, preventDefault() {} }); };
    const g = (ty, scale) => { for (const fn of R.box.on[ty] || []) fn({ type: ty, scale, clientX: 400, clientY: 400, preventDefault() {} }); };
    const r = [];
    R.click("pbzout"); r.push(at("−"));
    R.click("pbzin"); const up = R.st().zoom > fl * 1.2 && R.X.$("pbzout").disabled === false && R.X.$("pbzfit").getAttribute("aria-pressed") === "false";
    R.click("pbzout"); R.click("pbzout"); R.click("pbzout"); r.push(at("+ − − −"));
    R.click("pbzin"); R.fire("wheel", 400, 400, undefined, { deltaY: 3000, ctrlKey: true }); r.push(at("wheel"));
    R.click("pbzin"); t("pointerdown", 11, 500, 400); t("pointerdown", 12, 650, 400); t("pointermove", 11, 560, 400); t("pointermove", 12, 590, 400); r.push(at("pinch"));
    t("pointerup", 12, 590, 400); t("pointerup", 11, 560, 400);
    R.click("pbzin"); g("gesturestart", 1); g("gesturechange", 0.2); r.push(at("gesture")); g("gestureend", 0.2);
    R.click("pbzin"); R.click("pbzfit"); r.push(at("FIT"));
    R.X.pbZoomTo(0.1); r.push(at("0.1"));
    const off = R.X.$("pbzout").disabled === true && R.X.$("pbzfit").getAttribute("aria-pressed") === "true";
    /* a laptop: FIT is the CSS's fit, − off, the wheel out leaves it so */
    const L = zoomRig("MARE", 1400, 900);
    L.fire("wheel", 400, 400, undefined, { deltaY: 3000, ctrlKey: true });
    ok(r.every(Boolean) && up && off && L.st().zoom === 0 && L.X.$("pbzout").disabled === true && !L.box.classList.contains("zoomed"),
      `patch bay zoom: the 18 px floor on every path (${rep.join(", ")} px); − off at it; on a laptop FIT stays the CSS's fit${r.every(Boolean) ? "" : " BAD " + js(r)}`);
  }
  /* a stored zoom at or under FIT is FIT (the laptop's FIT look), above it kept; a window made larger: FIT again */
  {
    const f = zoomRig("MARE", 1400, 900).fit(), res = [];
    for (const v of ["0.5", String(+f.toFixed(4)), String(+(f * 0.999).toFixed(4))]) {
      const R = zoomRig("MARE", 1400, 900, { "stallion-pb-zoom": v });
      res.push(R.st().zoom === 0 && !R.box.classList.contains("zoomed") && R.svgEl.style.width === "" && R.X.$("pbzfit").getAttribute("aria-pressed") === "true");
    }
    const K = zoomRig("MARE", 1400, 900, { "stallion-pb-zoom": String(+(f * 1.3).toFixed(4)) }), kept = Math.abs(K.st().zoom - f * 1.3) < 1e-3;
    const S = zoomRig("MARE", 694, 600, { "stallion-pb-zoom": "0.5" }), small = Math.abs(S.names() - 18) < 1e-6;
    const W = zoomRig("MARE", 694, 600);
    W.X.pbZoomTo(f * 0.97);
    const z0 = W.st().zoom;
    W.resize(1400, 900);
    const big = W.st().zoom === 0 && !W.box.classList.contains("zoomed") && W.X.$("pbzfit").getAttribute("aria-pressed") === "true";
    W.resize(694, 600);
    ok(res.every(Boolean) && kept && small && z0 > 18 / 22 && big && Math.abs(W.names() - 18) < 1e-6,
      `patch bay zoom: a stored zoom at or under FIT opens at FIT on a laptop (${js(res)}), one above kept; on a small screen the floor; a window resized past it: FIT, back: the floor`);
  }
  /* Safari: a pinch sends gesture events and Ctrl + wheel events: only the gesture zooms (no double zoom) */
  {
    const R = zoomRig("ANALOG", 1400, 900), f = R.fit();
    const g = (ty, scale) => { for (const fn of R.box.on[ty] || []) fn({ type: ty, scale, clientX: 600, clientY: 500, preventDefault() {} }); };
    g("gesturestart", 1); g("gesturechange", 1.5);
    const z1 = R.st().zoom, p = R.fire("wheel", 600, 500, undefined, { deltaY: -200, ctrlKey: true }), z2 = R.st().zoom;
    g("gestureend", 1.5);
    R.fire("wheel", 600, 500, undefined, { deltaY: -200, ctrlKey: true });
    ok(Math.abs(z1 - f * 1.5) < 1e-6 && p && z2 === z1 && R.st().zoom > z1,
      `patch bay zoom: during Safari's gesture the Ctrl + wheel events zoom nothing (x${(z2 / f).toFixed(2)}), after it they do`);
  }
  /* − FIT + on the top row, as large as the tabs; each a step of 1.25; back at or under FIT is FIT; + stops at 2 px a unit */
  {
    const store = {}, R = zoomRig("MARE", 1400, 900, store), f = R.fit(), got = [];
    R.click("pbzin"); got.push(R.st().zoom); R.click("pbzin"); got.push(R.st().zoom);
    const kept = store["stallion-pb-zoom"];
    R.click("pbzout"); got.push(R.st().zoom); R.click("pbzout"); got.push(R.st().zoom);
    const fitStored = store["stallion-pb-zoom"];
    R.click("pbzin"); R.click("pbzfit"); got.push(R.st().zoom);
    for (let i = 0; i < 12; i++) R.click("pbzin");
    const top = R.st().zoom, inOff = R.X.$("pbzin").disabled;
    const row = html.slice(html.indexOf('<div class="pbtabs"'), html.indexOf('<p class="pbsmall"'));
    const bz = /\.pbzm button \{([^}]*)\}/.exec(html);
    ok(Math.abs(got[0] - f * 1.25) < 1e-6 && Math.abs(got[1] - f * 1.5625) < 1e-6 && Math.abs(got[2] - f * 1.25) < 1e-6 && got[3] === 0 && got[4] === 0 &&
       +kept === +(f * 1.5625).toFixed(4) && fitStored === "fit" && top === 2 && inOff &&
       /id="pbzout"[^>]*>&minus;<\/button><button id="pbzfit" data-t="pbFit"[^>]*><\/button><button id="pbzin"[^>]*>\+<\/button>/.test(row) &&
       bz && /font-size: 24px/.test(bz[1]) && /line-height: 32px/.test(bz[1]) && /min-width: 56px/.test(bz[1]),
      `patch bay zoom: − FIT + on the top row (24 px): + ${got.slice(0, 2).map((z) => z.toFixed(3)).join(", ")}, − ${got[2].toFixed(3)}, FIT; + stops at 2`);
    R.X.pbView("mx");
    ok(R.X.$("pbzm").hidden === true, "patch bay zoom: the zoom buttons only with the bay (hidden on MATRIX)");
  }
  /* Ctrl / ⌘ + the wheel: zooms, the point under the pointer stays; the wheel alone scrolls as ever */
  {
    const R = zoomRig("ANALOG", 1400, 900), [w, h] = R.st().vb;
    const mid = [...R.X.pbLay().jacks.values()].sort((a, b) => Math.hypot(a.x - w / 2, a.y - h / 2) - Math.hypot(b.x - w / 2, b.y - h / 2))[0];
    const [x, y] = R.scr(mid.k), u0 = R.unit(x, y);   /* (a jack mid-bay: the scroll has room either way) */
    const p1 = R.fire("wheel", x, y, undefined, { deltaY: -120, ctrlKey: true });
    const z1 = R.st().zoom, u1 = R.unit(x, y);
    const p2 = R.fire("wheel", x, y, undefined, { deltaY: -120, metaKey: true }), z2 = R.st().zoom;
    const p3 = R.fire("wheel", x, y, undefined, { deltaY: -120 }), z3 = R.st().zoom;
    ok(p1 && z1 > R.fit() * 1.2 && Math.hypot(u1[0] - u0[0], u1[1] - u0[1]) < 1 && p2 && z2 > z1 && !p3 && z3 === z2,
      `patch bay zoom: Ctrl / ⌘ + wheel zooms (${(22 * z1).toFixed(1)}, ${(22 * z2).toFixed(1)} px names), the jack under the pointer stays under it; the plain wheel is the page's`);
  }
  /* a drag on empty space pans (zoomed); at FIT it does nothing */
  {
    const R = zoomRig("ANALOG", 694, 600);
    R.box.scrollLeft = 100; R.box.scrollTop = 60;
    const p = R.fire("pointerdown", 400, 400);
    R.fire("pointermove", 350, 370);
    const a = [R.box.scrollLeft, R.box.scrollTop];
    R.fire("pointerup", 350, 370);
    const F = zoomRig("ANALOG", 1400, 900), q = F.fire("pointerdown", 400, 400);
    F.fire("pointermove", 350, 370); F.fire("pointerup", 350, 370);
    ok(p && a[0] === 150 && a[1] === 90 && !R.box.classList.contains("panning") && !q && F.box.scrollLeft === 0 && !F.X.pbCables().length,
      `patch bay zoom: a drag on empty space pans the bay (scroll ${a.join(", ")}); at FIT it leaves the page alone`);
  }
  /* a cable dragged near the box's edge scrolls the bay (every jack reachable), then lands on a jack that was out of sight */
  {
    const R = zoomRig("ANALOG", 694, 600), X = R.X;
    const far = [...X.pbLay().jacks.values()].filter((j) => !j.out).sort((a, b) => b.x - a.x)[0];   /* (the right-most input) */
    const [fx0] = R.scr("i:" + far.code);
    const [ox, oy] = R.scr("o:1");
    R.fire("pointerdown", ox, oy, R.onJack("o:1"));
    R.fire("pointermove", ox + 12, oy + 12);
    R.fire("pointermove", 100 + 694 - 8, oy + 120);                   /* (8 px from the right edge) */
    const x0 = X.pbState().drag && X.pbState().drag.x;
    for (let i = 0; i < 60; i++) X.pbState().autoTick();
    const moved = R.box.scrollLeft, x1 = X.pbState().drag && X.pbState().drag.x;
    const [fx, fy] = R.scr("i:" + far.code);
    R.fire("pointermove", fx - 60, fy);                               /* (out of the band: it stops) */
    const s1 = R.box.scrollLeft;
    X.pbState().autoTick();
    R.fire("pointermove", fx, fy); R.fire("pointerup", fx, fy);
    const made = X.pbCables().some((c) => c.s === 1 && c.d === far.code);
    ok(fx0 > 100 + 694 && moved > 300 && x1 > x0 + 300 && R.box.scrollLeft === s1 && made,
      `patch bay zoom: near the edge the bay scrolls while a cable is dragged (${moved} px), its end follows, the cable lands on ${far.label} (out of sight before)`);
  }
  /* drops land on the jack under the pointer at any zoom and scroll (the SVG's screen matrix), 20 px off too */
  {
    const res = [];
    for (const k of [1, 2, 4]) {
      const R = zoomRig("ANALOG", 900, 860), X = R.X;
      for (let i = 0; i < k; i++) R.click("pbzin");
      R.box.scrollLeft = 140 * k; R.box.scrollTop = 50 * k;
      const drag = (from, to, off) => { const a = R.scr(from), b = R.scr(to); R.fire("pointerdown", a[0], a[1], R.onJack(from)); R.fire("pointermove", a[0] + 10, a[1] + 10); R.fire("pointermove", b[0] + off, b[1]); R.fire("pointerup", b[0] + off, b[1]); };
      drag("o:4", "i:2", 0); drag("o:6", "i:1", 20);
      res.push([R.st().zoom.toFixed(2), X.pbCables().map((c) => `${c.s}>${c.d}`).join(" ")]);
    }
    ok(res.every(([, c]) => c === "4>2 6>1"), `patch bay zoom: drops land at every zoom and scroll, 20 px off included (${res.map(([z, c]) => `x${z}: ${c}`).join("; ")})`);
  }
  /* remembered per browser: a zoom chosen comes back; a FIT chosen on a small screen stays FIT; no storage, no error */
  {
    const store = {}, R = zoomRig("MARE", 1400, 900, store);
    R.click("pbzin"); R.click("pbzin");
    const z = R.st().zoom, again = zoomRig("MARE", 1400, 900, store).st().zoom;
    const s2 = {}, small = zoomRig("MARE", 694, 600, s2);
    small.click("pbzfit");
    const B = zoomRig("MARE", 694, 600, s2), back = B.st().zoom;
    const none = zoomRig("MARE", 1400, 900);
    none.click("pbzin");
    ok(Math.abs(again - z) < 1e-3 && s2["stallion-pb-zoom"] === "fit" && Math.abs(back - 18 / 22) < 1e-9 && Math.abs(B.names() - 18) < 1e-6 &&
       B.X.$("pbzfit").getAttribute("aria-pressed") === "true" && none.st().zoom > 0,
      `patch bay zoom: the zoom remembered per browser (${again.toFixed(3)}), FIT chosen on a small screen comes back as FIT there (the 18 px floor), a browser without storage still zooms`);
  }
  /* a pinch: two fingers apart zoom in, the zoom kept when they let go */
  {
    const store = {}, R = zoomRig("ANALOG", 1400, 900, store), f = R.fit();
    const t = (ty, id, x, y) => { for (const fn of R.box.on[ty] || []) fn({ type: ty, button: 0, pointerId: id, pointerType: "touch", clientX: x, clientY: y, target: { closest: () => null }, timeStamp: 1, preventDefault() {} }); };
    t("pointerdown", 11, 600, 500); t("pointerdown", 12, 700, 500);
    t("pointermove", 11, 575, 500); t("pointermove", 12, 725, 500);   /* (100 px apart, then 150) */
    const z = R.st().zoom, before = store["stallion-pb-zoom"];
    t("pointerup", 12, 725, 500); t("pointerup", 11, 575, 500);
    ok(Math.abs(z - f * 1.5) < 1e-6 && before === undefined && +store["stallion-pb-zoom"] === +z.toFixed(4) && !R.X.pbCables().length,
      `patch bay zoom: a pinch zooms (x1.5 of FIT: ${(22 * z).toFixed(1)} px names), kept when the fingers lift, no cable made`);
  }
}

/* ------------------------------------------- user samples: JS == sampleio.py --- */
function wav(sr, ch, bits, float, frames, f) {
  const bps = bits / 8, data = Buffer.alloc(frames * ch * bps);
  for (let i = 0; i < frames; i++) for (let c = 0; c < ch; c++) {
    const v = f(i, c), o = (i * ch + c) * bps;
    if (float) data.writeFloatLE(v, o);
    else if (bits === 8) data[o] = Math.max(0, Math.min(255, Math.round(v * 127 + 128)));
    else if (bits === 16) data.writeInt16LE(Math.round(v * 32000), o);
    else if (bits === 24) data.writeIntLE(Math.round(v * 8000000), o, 3);
  }
  const fmt = Buffer.alloc(16);
  fmt.writeUInt16LE(float ? 3 : 1, 0); fmt.writeUInt16LE(ch, 2); fmt.writeUInt32LE(sr, 4);
  fmt.writeUInt32LE(sr * ch * bps, 8); fmt.writeUInt16LE(ch * bps, 12); fmt.writeUInt16LE(bits, 14);
  const chunk = (id, b) => Buffer.concat([Buffer.from(id), Buffer.from(Uint32Array.of(b.length).buffer), b]);
  const body = Buffer.concat([Buffer.from("WAVE"), chunk("fmt ", fmt), chunk("data", data)]);
  return Buffer.concat([Buffer.from("RIFF"), Buffer.from(Uint32Array.of(body.length).buffer), body]);
}

function samplesMatch() {
  const dir = mkdtempSync(join(tmpdir(), "felucca-web-"));
  const files = [
    ["tone_A4.wav", wav(44100, 1, 16, false, 9000, (i) => Math.sin(i * 0.0627) * Math.exp(-i / 4000))],
    ["pad C3.wav", wav(48000, 2, 24, false, 7000, (i, c) => Math.sin(i * (c ? 0.031 : 0.0313)) * 0.7)],
    ["Bb2 float.wav", wav(22050, 1, 32, true, 5000, (i) => ((i % 97) / 48 - 1) * 0.5)],
    ["BD1 lofi.wav", wav(96000, 1, 8, false, 12000, (i) => Math.sin(i * 0.01) * Math.exp(-i / 3000))],
  ].map(([n, b]) => { const p = join(dir, n); writeFileSync(p, b); return p; });
  const zones = files.map((p) => {
    const w = E.parseWav(readFileSync(p));
    const s = E.normalize(E.resample(w.x, w.sr, E.SMP.RATE));
    return { s, root: E.rootFromName(p.split("/").pop().replace(/\.[^.]*$/, "")) };
  });
  const js = E.buildSlot("Mix ä 12345", zones);
  execFileSync(PYTHON, [join(HERE, "../tools/fm1_sample_upload.py"), "build", "Mix ä 12345", join(dir, "slot"), ...files]);
  const pyHdr = readFileSync(join(dir, "slot.hdr")), pyData = readFileSync(join(dir, "slot.bin"));
  ok(eq(js.hdr, pyHdr) && eq(js.data, pyData), `samples: editor == sampleio.py (${files.length} WAV formats, ${js.data.length} B)`);
}

/* ------------------------------------------------------- CHOP (pure helpers) --- */
function chopTests() {
  const R = E.SMP.RATE, N = R * 4;
  /* a break: 8 hits (kick-like and snare-like, loud and ghost) on a quiet noise floor */
  let seed = 7;
  const rnd = () => ((seed = (seed * 1103515245 + 12345) >>> 0) / 2 ** 32) * 2 - 1;
  const x = new Float64Array(N);
  for (let i = 0; i < N; i++) x[i] = rnd() * 0.002;
  const HITS = [0.10, 0.52, 0.93, 1.31, 1.80, 2.26, 2.70, 3.33].map((t) => Math.round(t * R));
  const AMP = [1, 0.8, 0.25, 0.9, 1, 0.3, 0.85, 0.7];
  HITS.forEach((h, k) => {
    for (let i = 0; i < R * 0.3 && h + i < N; i++) {
      const env = Math.exp(-i / (k % 2 ? 1500 : 3000)) * AMP[k];
      x[h + i] += env * (k % 2 ? rnd() * 0.8 : Math.sin(2 * Math.PI * 60 * i / R) * 0.9 + rnd() * 0.1);
    }
  });
  const nov = E.chopNovelty(x), hits = E.chopHits(x, nov, 5);
  const near = (a, b, ms) => Math.abs(a - b) <= ms * R / 1000;
  ok(hits.length === HITS.length && hits.every((h, i) => near(h, HITS[i] - E.CHOP.PRE, 3)),
    `chop: hits found (${hits.length} of ${HITS.length}, each within 3 ms of its attack)`);
  ok(E.chopHits(x, nov, 1).length < HITS.length && E.chopHits(x, nov, 1).length >= 4, "chop: low sensitivity keeps the hard hits only");
  const late = E.chopSnap(x, nov, HITS[3] + 0.035 * R), early = E.chopSnap(x, nov, HITS[4] - 0.03 * R), none = E.chopSnap(x, nov, 1.1 * R);
  ok(near(late, HITS[3] - E.CHOP.PRE, 3) && near(early, HITS[4] - E.CHOP.PRE, 3) && none === Math.round(1.1 * R),
    "chop: a TAP 35 ms late / 30 ms early lands on its hit; away from hits it stays");
  const grid = E.chopGrid(0, Math.round(R * 60 / 90 * 16), 90, 1);
  ok(grid.length === 16 && grid[1] === Math.round(R * 60 / 90) && E.chopEqual(100, 900, 4).join() === "100,300,500,700",
    "chop: grid (16 beats at 90 BPM) and equal parts");
  const list = E.chopList([10, 50, 400], 1000, 100);
  ok(list.map((c) => `${c.start}-${c.end}`).join() === "10-50,50-150,400-500", "chop: chops end at the next marker or the max length");
  const chops = E.chopList(hits, N), zones = E.chopZones(x, chops, 60, 0);
  const peak = (s) => s.reduce((a, v) => Math.max(a, Math.abs(v)), 0);
  ok(zones.length === 8 && zones.every((z, i) => z.root === 60 + i && z.lo === z.root && z.hi === z.root && z.s[0] === 0)
    && Math.abs(peak(zones[2].s) / peak(zones[0].s) - 0.25) < 0.05 && zones[0].fname === "CHOP01_C4.wav" && zones[1].fname === "CHOP02_C#4.wav",
    "chop: one key each from C4, levels kept (a ghost stays quiet), faded in");
  const one = E.chopZones(x, chops, 60, 1, 3);
  ok(one.length === 1 && one[0].root === 60 && one[0].lo === 0 && one[0].hi === 127 && one[0].fname === "CHOP04_C4.wav",
    "chop: one chop over the whole keyboard");
  const slot = E.buildSlot("BREAK", zones), v = new DataView(slot.hdr.buffer);
  ok(slot.hdr[6] === 8 && slot.hdr[32 + 25] === 60 && slot.hdr[32 + 26] === 60 && slot.hdr[32 + 7 * 28 + 25] === 67 && v.getInt16(32 + 20, true) === 60 * 16
    && slot.data.length <= E.SMP.MAX_DATA, "chop: slot header (8 zones, one key each)");
  const w = E.parseWav(E.wavFile(zones[1].s));
  ok(w.sr === R && w.x.length === zones[1].s.length && Math.abs(w.x[100] * 32768 - zones[1].s[100]) < 2, "chop: WAV writer round trip");
  const dir = mkdtempSync(join(tmpdir(), "sloop-chop-")), zp = join(dir, "c.zip");
  writeFileSync(zp, E.zipStore(zones.slice(0, 3).map((z) => ({ name: "BREAK/" + z.fname, data: E.wavFile(z.s) }))));
  const r = py(`import sys, zipfile
z = zipfile.ZipFile(sys.argv[1]); assert z.testzip() is None
print(",".join(i.filename + ":" + str(i.file_size) for i in z.infolist()))`, zp).toString().trim();
  ok(r === zones.slice(0, 3).map((z) => `BREAK/${z.fname}:${44 + z.s.length * 2}`).join(), "chop: ZIP of the WAVs (Python reads it)");

  /* keep / leave out, own lengths, fit: a recording longer than a slot */
  const opt = [null, { off: true }, { len: 30 }];
  const kl = E.chopList([10, 50, 400], 1000, 100, opt);
  ok(kl.map((c) => `${c.start}-${c.end}${c.off ? "x" : ""}/${c.full}`).join() === "10-50/50,50-150x/400,400-430/1000"
    && E.chopList([10, 400], 1000, 0, [{ len: 9999 }])[0].end === 400,
    "chop: options (left out, own length over the max, never past the next marker)");
  const kz = E.chopZones(x, E.chopList(hits, N, 0, [{}, { off: true }, {}, { off: true }]), 60, 0);
  ok(kz.length === 6 && kz.map((z) => z.root).join() === "60,61,62,63,64,65" && kz[1].fname === "CHOP03_C#4.wav" && kz[2].fname === "CHOP05_D4.wav",
    "chop: left-out chops: the kept ones on consecutive keys, files keep their numbers");
  const L = R * 20, long = new Float64Array(L);
  for (let i = 0; i < L; i++) long[i] = Math.sin(i * 0.05) * 0.5;
  const marks = E.chopEqual(0, L, 40), room = E.SMP.MAX_DATA * 2;
  const all = E.chopList(marks, L), pick = E.chopPick(all);
  ok(pick.length === 16 && pick[15].i === 15 && E.chopPick(all, 1, 7)[0].i === 7, "chop: 40 chops: the first 16 kept go to the slot; mode 1 the selected");
  const lo = E.chopList(marks, L, 0, marks.map((_, i) => ({ off: i % 4 !== 0 })));   /* keep 10 of 40 (each 0.5 s) */
  ok(E.chopPick(lo).length === 10 && E.chopFit(E.chopPick(lo), room) === Infinity, "chop: 20 s recording, 10 chops kept: they fit");
  const many = E.chopPick(E.chopList(marks, L, 0, marks.map((_, i) => ({ off: i >= 16 })))), Lf = E.chopFit(many, room - many.length);
  const fitted = many.map((c) => ({ ...c, end: Math.min(c.end, c.start + Lf) }));
  let built = null;
  try { built = E.buildSlot("LONG", E.chopZones(long, fitted, 60, 0)); } catch (e) { built = null; }
  ok(Lf < R * 0.5 && Lf > R * 0.4 && built && built.data.length <= E.SMP.MAX_DATA && built.hdr[6] === 16,
    `chop: Fit to slot: 16 x 0.5 s cut to ${(Lf / R).toFixed(3)} s each, the slot builds`);
  ok(E.chopFit([{ start: 0, end: 100 }, { start: 0, end: 300 }], 250) === 150, "chop: fit keeps short chops whole, cuts the long ones");
}

/* ------------------------------------------------------- packages: JS == Python --- */
async function packages() {
  /* SMK-37 Pro: the stock v016 package has 36 interleave blocks (outside the repo, read only; skipped without it) */
  const v016 = join(process.env.SMK_STOCK_DIR || join(HERE, "no-stock-packages"), "SMK-37_Pro_016.fwsc");
  if (existsSync(v016)) {
    const raw = readFileSync(v016);
    const logical = py(`import sys; raw = open(sys.argv[1], "rb").read()
sys.stdout.buffer.write(b"".join(raw[i * 48:i * 48 + 47] for i in range(36)) + raw[36 * 48:])`, v016);
    ok(blockCount(raw) === 36 && eq(logicalImage(raw), logical) && productOf(raw) === "SMK-37 Pro_016",
      "fm1pkg.js: stock SMK-37_Pro_016.fwsc: 36 blocks, identity SMK-37 Pro_016");
    const st = await validateStockPackage(new Uint8Array(raw));
    ok(st.product === "SMK-37 Pro_016" && eq(st.image, logical), "fm1pkg.js: the official SMK-37 Pro V016 is accepted for the return");
  } else console.log("packages: SMK-37 Pro stock package skipped (SMK_STOCK_DIR not set)");
  const pkg = join(HERE, "../build/felucca.fwsc");
  if (!existsSync(pkg)) {
    console.log("packages: skipped (run ./build.sh first)");
    return;
  }
  const raw = readFileSync(pkg);
  const logical = py(`import sys; raw = open(sys.argv[1], "rb").read()
sys.stdout.buffer.write(b"".join(raw[i * 48:i * 48 + 47] for i in range(20)) + raw[960:])`, pkg);
  ok(eq(logicalImage(raw), logical), "fm1pkg.js logicalImage");
  ok(/^FM-1_1042\d\d$/.test(productOf(raw)), "fm1pkg.js productOf (Stallion's own number, FM-1_1042XY)");
}

/* ------------- sloop-pro #3: Chrome's MIDI let go while the keyboard restarts (smkota.js, MidiSession) --- */
/* "The installer freezes my Chrome browser when installing via usb" (ChanceTheMaker: "MIDI is not released when the
   device reboots into flashing mode"). Chrome's MIDI as Chromium's sources do it (media/midi/midi_service.cc,
   midi_manager_win.cc, Blink's MIDIPort): the browser opens every device while any document holds a MIDIAccess,
   whatever the page's port.open() / close() say, and lets them go when the last such document ends (an iframe
   removed; the page's own document only with the tab). On Windows a keyboard that leaves the USB while the browser
   holds it wedges Chrome's MIDI thread (which WinMM call blocks: to be confirmed on hardware): no message moves any
   more, no new MIDIAccess is given, and ending a session then blocks the browser (here: the event loop, 1.5 s). */
async function midiRestarts() {
  const lib = await import("./smkota.js");
  const U = lib.Updater, MidiSession = lib.MidiSession;
  class Chrome {
    constructor() { this.docs = new Set(); this.wedged = false; this.froze = 0; this.leaves = 0; this.held = 0; }
    request(doc, access) { if (this.wedged) return new Promise(() => {}); this.docs.add(doc); return Promise.resolve(access); }
    end(doc) {
      if (!this.docs.delete(doc) || !this.wedged) return;
      const t = Date.now() + 1500;
      while (Date.now() < t);
      this.froze++;
    }
    leave() { this.leaves++; if (this.docs.size) { this.held++; this.wedged = true; } }
  }
  /* an SMK-37 Pro: step 1, its loader, the write, the new firmware (each restart 300 ms after the last answer) */
  class Kbd {
    constructor(chrome, image) {
      this.chrome = chrome; this.image = image; this.access = { inputs: new Map(), outputs: new Map(), onstatechange: null };
      this.late = 0; this.closes = 0; this.served = 0;
      this.boot("SMK-37 Pro_016");
    }
    boot(identity) {
      if (this.access.inputs.size) this.chrome.leave();
      for (const m of [this.access.inputs, this.access.outputs]) { for (const p of m.values()) p.state = "disconnected"; m.clear(); }
      this.identity = identity; this.waiting = null; this.queue = [];
      const id = Math.random().toString(36).slice(2), k = this;
      const port = (kind) => ({ id: kind + id, name: "SMK-37 Pro", state: "connected", onstatechange: null, open: async () => {},
                                close: async () => { k.closes++; } });
      this.input = { ...port("i"), onmidimessage: null };
      this.output = { ...port("o"), send(d) {
        if (this.state !== "connected") { k.late++; return; }   /* Chrome: no exception, the message waits for the port */
        if (!k.chrome.wedged) setTimeout(() => k.rx(Array.from(d)), 1);
      } };
      this.access.inputs.set(this.input.id, this.input);
      this.access.outputs.set(this.output.id, this.output);
    }
    tx(b) { const i = this.input; setTimeout(() => { if (i.state === "connected" && i.onmidimessage && !this.chrome.wedged) i.onmidimessage({ data: Uint8Array.from(b) }); }, 1); }
    rx(d) {
      if (eq(d, HS)) {
        const t = [...new TextEncoder().encode(this.identity)];
        this.tx([0xF0, ...pack7([0, 0x59, 0x11, 0, 0, 0, ...t, ...new Array(28 - t.length).fill(0)]), 0xF7]);
      } else if (eq(d, UPGRADE)) {
        this.queue = this.identity.startsWith("ota-") ? [[0, 512], [512, 512], [0xF0000000, 8]] : [[0, 64], [0x40, 160], [0xE0000000, 8]];
        this.next();
      } else if (this.waiting) {
        const [addr] = this.waiting;
        this.waiting = null; this.served++;
        if (addr === 0xE0000000) setTimeout(() => this.boot("ota-SMK-37 Pro_104200"), 300);
        else if (addr === 0xF0000000) setTimeout(() => this.boot("SMK-37 Pro_104200"), 300);
        else this.next();
      }
    }
    next() {
      const r = this.queue.shift();
      if (!r) return;
      this.waiting = r;
      const [addr, len] = r;
      const u = [0, 0x59, 0x30, 0, 0, 0, 0, addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF, len & 0xFF, len >> 8, 0];
      let s = 0;
      for (let i = 6; i < 14; i++) s += u[i];
      u.push(~s & 0xFF);
      this.tx([0xF0, ...pack7(u), 0xF7]);
    }
  }
  /* a document whose iframes each get their own MIDIAccess from Chrome */
  const page = (chrome, kbd) => {
    const frames = new Set();
    return { frames, createElement: () => ({ hidden: false, attrs: {}, contentWindow: null, setAttribute(k, v) { this.attrs[k] = v; },
               remove() { if (frames.delete(this)) { this.contentWindow = null; chrome.end(this); } } }),
             body: { appendChild(f) { frames.add(f); f.contentWindow = { navigator: { requestMIDIAccess: () => chrome.request(f, kbd.access) } }; return f; } } };
  };
  const image = Uint8Array.from({ length: 0x2000 }, (_, i) => (i * 7) & 0xFF);
  const timing = { loader: 4000, jumped: 4000, back: 4000, settle: 200, reboot: 300, away: 1000, step1Idle: 3000 };
  const watch = () => {                                 /* the watchdog: the longest the event loop was blocked */
    const w = { lag: 0, last: Date.now() };
    w.id = setInterval(() => { const n = Date.now(); w.lag = Math.max(w.lag, n - w.last - 50); w.last = n; }, 50);
    return w;
  };
  {   /* the way the page did it: one MIDIAccess of the page's own document, never let go: the keyboard leaves the USB
         while Chrome holds it (the model's hang; its loader is never seen) */
    const chrome = new Chrome(), kbd = new Kbd(chrome, image);
    const access = await chrome.request("page", kbd.access);
    const e = await new U(access, null, { timing }).install(image, "SMK-37 Pro_104200").then(() => null, (x) => x);
    ok(e && e.code === "noloader" && chrome.wedged && chrome.held === 1,
      `smkota.js (#3): the page's own MIDIAccess holds the keyboard through its restart (reproduced: ${e && e.code})`);
  }
  if (typeof MidiSession !== "function") { ok(false, "smkota.js (#3): MidiSession: Chrome's MIDI in a disposable document"); return; }
  {   /* with a MidiSession: let go 40 ms after the last answer, before the keyboard leaves; a new one once it is back */
    const chrome = new Chrome(), kbd = new Kbd(chrome, image), doc = page(chrome, kbd);
    const session = new MidiSession(doc, null), log = [], w = watch();
    const access = await session.open();
    let tm;
    const r = await Promise.race([new U(access, (t) => log.push(t), { session, timing }).install(image, "SMK-37 Pro_104200").catch((x) => x),
                                  new Promise((res) => { tm = setTimeout(() => res(null), 30000); })]);
    clearTimeout(tm);
    await session.release();
    clearInterval(w.id);
    ok(r === "SMK-37 Pro_104200" && session.isolated, `smkota.js (#3): install with a MidiSession: through both restarts (${r && r.message || r})`);
    ok(chrome.leaves === 2 && chrome.held === 0 && !chrome.wedged && chrome.froze === 0 && w.lag < 1000 && session.ends === 3,
      `smkota.js (#3): Chrome's MIDI let go at both restarts, nothing wedged or blocked (${chrome.held} held, ${w.lag} ms)`);
    ok(chrome.docs.size === 0 && doc.frames.size === 0 && kbd.late === 0 && log.filter((t) => /MIDI let go/.test(t)).length === 2,
      "smkota.js (#3): nothing sent to a port that is gone; no document holds MIDI at the end");
  }
  {   /* release(): no handler left (messages, state changes), the iframe removed; a second release is harmless */
    const chrome = new Chrome(), kbd = new Kbd(chrome, image), doc = page(chrome, kbd);
    const session = new MidiSession(doc, null);
    const a = await session.open();
    a.onstatechange = () => {}; kbd.input.onmidimessage = () => {}; kbd.input.onstatechange = () => {};
    await session.release(); await session.release();
    ok(a.onstatechange === null && kbd.input.onmidimessage === null && kbd.input.onstatechange === null && doc.frames.size === 0
       && chrome.docs.size === 0 && session.access === null && session.ends === 1,
      "MidiSession.release: handlers cleared (onmidimessage, onstatechange), its document ended");
  }
  {   /* no document (or an iframe without Web MIDI): the page's own navigator, one access kept; release() closes the
         ports with a deadline (a close that never answers does not stop it); Updater then watches the ports as before */
    const chrome = new Chrome(), kbd = new Kbd(chrome, image);
    let asked = 0;
    const nav = { requestMIDIAccess: () => { asked++; return chrome.request("page", kbd.access); } };
    const s1 = new MidiSession(null, nav);
    const a1 = await s1.open();
    kbd.output.close = () => new Promise(() => {});
    const t0 = Date.now();
    await s1.release();
    const a2 = await s1.open();
    const bare = new MidiSession({ createElement: () => ({ remove() {}, contentWindow: {} }), body: { appendChild() {} } }, nav);
    await bare.open();
    ok(a1 === kbd.access && a2 === a1 && asked === 2 && !s1.isolated && !bare.isolated && kbd.closes === 1 && Date.now() - t0 < 1500,
      "MidiSession without an iframe: the page's own MIDIAccess, ports closed with a deadline");
  }
}

/* ------------------------------------------------- update protocol (fm1ota.js) --- */
const HS = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];
const UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];

/* an FM-1 on WebMIDI: identity, then "device asks, host answers" reads of the image */
class FakeFM1 {
  constructor(image, { unplugAfter = Infinity, finalIdentity = "FM-1_104200", step1 = null } = {}) {
    this.image = image; this.unplugAfter = unplugAfter; this.served = 0; this.bad = 0;
    this.finalIdentity = finalIdentity; this.step1 = step1;   /* step1: the running firmware's reads, [addr, len] */
    this.access = { inputs: new Map(), outputs: new Map() };
    this.boot("FM-1_015", "FM-1");
  }
  boot(identity, name) {
    this.identity = identity; this.waiting = null; this.queue = [];
    for (const m of [this.access.inputs, this.access.outputs]) { for (const p of m.values()) p.state = "disconnected"; m.clear(); }
    const id = Math.random().toString(36).slice(2);
    this.input = { id: "i" + id, name, state: "connected", onmidimessage: null, open: async () => {} };
    this.output = { id: "o" + id, name, state: "connected", open: async () => {}, send: (d) => {
      if (this.output.state !== "connected") throw new Error("InvalidStateError");
      setTimeout(() => this.rx(Array.from(d)), 1);
    } };
    this.access.inputs.set(this.input.id, this.input);
    this.access.outputs.set(this.output.id, this.output);
  }
  tx(bytes) { const i = this.input; setTimeout(() => { if (i.state === "connected" && i.onmidimessage) i.onmidimessage({ data: Uint8Array.from(bytes) }); }, 1); }
  rx(d) {
    if (eq(d, HS)) {
      const t = [...new TextEncoder().encode(this.identity)];
      const body = [0, 0x59, 0x11, 0, 0, 0, ...t, ...new Array(28 - t.length).fill(0)];
      this.tx([0xF0, ...pack7(body), 0xF7]);
    } else if (eq(d, UPGRADE)) {
      this.queue = this.identity.startsWith("ota-")
        ? [...Array.from({ length: 6 }, (_, k) => [k * 512, 512]), [0xF0000000, 8]]
        : this.step1 ? this.step1.map((r) => [...r]) : [[0, 64], [0x40, 160], [0x1000, 512], [0xE0000000, 8]];
      this.next();
    } else if (this.waiting) {
      const u = unpack7(d.slice(1, -1));
      const [addr, len] = this.waiting;
      const got = u.slice(14, 14 + (addr >= 0xE0000000 ? 8 : len));
      const want = addr >= 0xE0000000 ? [...new TextEncoder().encode("success"), 0] : Array.from(this.image.subarray(addr, addr + len));
      if (!eq(got, want)) this.bad++;
      this.waiting = null;
      this.served++;
      if (this.served >= this.unplugAfter) { this.input.state = this.output.state = "disconnected"; return; }
      if (addr === 0xE0000000) setTimeout(() => this.boot("ota-FM-1_104200", "Felucca Update"), 300);
      else if (addr === 0xF0000000) setTimeout(() => this.boot(this.finalIdentity, "Felucca"), 300);
      else this.next();
    }
  }
  next() {
    const r = this.queue.shift();
    if (!r) return;
    this.waiting = r;
    const [addr, len] = r;
    const u = [0, 0x59, 0x30, 0, 0, 0, 0, addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF, len & 0xFF, len >> 8, 0];
    let s = 0;
    for (let i = 6; i < 14; i++) s += u[i];
    u.push(~s & 0xFF);
    this.tx([0xF0, ...pack7(u), 0xF7]);
  }
}

/* (0.7, after SLOOP 2.7) the editor finds the FM-1 by the installer's names (a port name kept from another firmware) */
async function editorPorts() {
  const { FM1_PORT } = await import("./fm1ota.js");
  ok(E.FM1_PORT_ED.source === FM1_PORT.source && E.FM1_PORT_ED.flags === FM1_PORT.flags, "editor: its FM1_PORT is the installer's");
  const port = (name, type, state = "connected") => ({ id: `${type}-${name}`, name, type, state });
  const acc = (names, gone = []) => ({
    inputs: new Map([...names, ...gone].map((n) => [n, port(n, "input", gone.includes(n) ? "disconnected" : "connected")])),
    outputs: new Map([...names, ...gone].map((n) => [n, port(n, "output", gone.includes(n) ? "disconnected" : "connected")])),
  });
  const pn = (a) => E.fm1Ports(a).map((x) => x.input.name + "/" + x.output.name).join(",");
  ok(pn(acc(["X0X FM-1"])) === "X0X FM-1/X0X FM-1", "editor: a port named after another firmware (X0X FM-1) is found");
  ok(pn(acc(["Arturia KeyStep Pro", "USB-MIDI", "X0X FM-1", "Clipper"], ["SLOOP"])) ===
     "Clipper/Clipper,X0X FM-1/X0X FM-1,USB-MIDI/USB-MIDI",
     "editor: Clipper first, then FM-1, then generic names; other gear and unplugged ports left out");
  ok(pn(acc(["Arturia KeyStep Pro", "loopMIDI Port"])) === "", "editor: no FM-1 name: nothing to try");
}

/* (0.7, after SLOOP 2.7) a file's loop: LOOP (the whole file while held), LOOP+TAIL (its points, on zero crossings) */
function sampleLoops() {
  const n = 8000, s = Int16Array.from({ length: n }, (_, i) => Math.round(12000 * Math.sin(i * 2 * Math.PI / 100)));
  const one = E.buildSlot("one", [{ s, root: 60 }]);
  ok(one.hdr[32 + 27] === 0, "loop: ONE SHOT, looped 0 in the header");
  const lp = E.buildSlot("lp", [{ s, root: 60, loop: 1 }]);
  const v = new DataView(lp.hdr.buffer);
  ok(lp.hdr[32 + 27] === 2 && v.getUint32(32 + 8, true) === 0 && v.getUint32(32 + 12, true) === n - 1, "loop: LOOP, the whole file, held (looped 2)");
  const [a, b] = E.LOOP.snapRange(s, 1234, 5678);
  ok(s[a - 1] < 0 && s[a] >= 0 && s[b] < 0 && s[b + 1] >= 0, `loop: LOOP+TAIL's points on rising zero crossings (${a}, ${b})`);
  const lt = E.buildSlot("lt", [{ s, root: 60, loop: 2, ls: a, le: b }]);
  const w = new DataView(lt.hdr.buffer);
  ok(lt.hdr[32 + 27] === 2 && w.getUint32(32 + 8, true) === a && w.getUint32(32 + 12, true) === b, "loop: LOOP+TAIL, its points in the header");
  const pv = E.LOOP.preview({ s, loop: 2, ls: a, le: b });
  ok(pv.length === n + 2 * (b - a + 1), "loop: the preview plays the loop twice more, then the tail");
  ok(E.LOOP.range({ s: s.slice(0, 100), loop: 2, ls: 10, le: 20 })[0] === 0, "loop: a file too short for LOOP+TAIL: the whole of it");
}

/* the official path refused by the FM-1 (ota.c -5: its head is from another official version): it stops after the
   head reads; the installer says so in plain words, in every language. Synthetic package: a UFW list (flash.bin at
   0x400, ota.bin at 0x8400 with the official loader's header CRCs and length), no official bytes */
async function headOther(pk) {
  const { headRefused } = await import("./fm1ota.js");
  const jl = (b) => { let k = 0xFFFF; for (let i = 0; i < b.length; i++) { b[i] ^= k & 0xFF; k = ((k << 1) ^ (k & 0x8000 ? 0x1021 : 0)) & 0xFFFF; } };
  const synth = (official) => {
    const img = Uint8Array.from({ length: 0x8400 + 0x4E01 }, (_, i) => (i * 13) & 0xFF), v = new DataView(img.buffer);
    img.fill(0, 0, 0x40 + 2 * 0x50);
    v.setUint16(8, 2, true);
    v.setUint16(0x40, 0, true); v.setUint32(0x48, 0x400, true); v.setUint32(0x4C, 0x8000, true);
    v.setUint16(0x90, 100, true); v.setUint32(0x98, 0x8400, true); v.setUint32(0x9C, 0x4E01, true);
    v.setUint16(0x8400, official ? 0xEBAA : 0x1234, true); v.setUint16(0x8402, 0x5881, true); v.setUint32(0x8408, 0x4DE1, true);
    jl(img.subarray(0, 0x40)); jl(img.subarray(0x40, 0x90)); jl(img.subarray(0x90, 0xE0));
    return img;
  };
  const img = synth(true), mine = synth(false);
  ok(js(pk.flashHead(img)) === js({ flOff: 0x400, official: true }) && js(pk.flashHead(mine)) === js({ flOff: 0x400, official: false }),
     "fm1pkg.js: flashHead: flash.bin's offset, the official loader by its header CRCs and length");
  ok(headRefused(img, 0x400) && headRefused(img, 0x400 + 0x3E00) && !headRefused(img, 0x8400) && !headRefused(img, 0x200) &&
     !headRefused(mine, 0x400 + 0x3E00) && !headRefused(img, -1), "fm1ota.js: a stop on a head read of the official path, only that");
  const reads = [[0, 512], [512, 512], [0x8400, 512], ...Array.from({ length: 32 }, (_, k) => [0x400 + k * 512, 512])];
  const dev = new FakeFM1(img, { step1: reads });
  const up = new Updater(dev.access);
  up.checkIdleMs = 1500;
  const e = await up.install(img, "FM-1_104200").then(() => null, (x) => x);
  ok(e && e.code === "headother" && dev.served === reads.length && dev.bad === 0,
     `fm1ota.js: the FM-1 stops after the head reads of the official path -> 'headother' (${e && e.code})`);
  const dev2 = new FakeFM1(img, { step1: reads.slice(0, 3) });
  const up2 = new Updater(dev2.access);
  up2.checkIdleMs = 1500;
  const e2 = await up2.install(img, "FM-1_104200").then(() => null, (x) => x);
  ok(e2 && e2.code === "stopped", "fm1ota.js: a stop before the head reads stays 'stopped'");
  /* the page: the message in en, fr and ja, shown by failed() for the code */
  const html = readFileSync(join(HERE, "index_pkg.html"), "utf8");
  const tb = html.slice(html.indexOf("const TEXT = {"), html.indexOf("\n};", html.indexOf("const TEXT = {")) + 2);
  const T = vm.runInNewContext(tb.replace("const TEXT =", "(") + ")");
  const all = ["en", "fr", "ja"].map((l) => T[l].headother || "");
  ok(/FM-1 is unchanged/.test(all[0]) && /another official version/.test(all[0]) && /official file the FM-1 last had/.test(all[0]) &&
     /official updater/.test(all[0]) && /n'a pas été modifiée/.test(all[1]) && /autre version officielle/.test(all[1]) &&
     /outil de mise à jour officiel/.test(all[1]) && /変更されていません/.test(all[2]) && /別の公式バージョン/.test(all[2]) &&
     /公式のアップデーター/.test(all[2]) && all.every((x) => /V16/.test(x) && /V15/.test(x) && !/-5/.test(x)),
     "installer: 'headother' in en, fr and ja: the FM-1 unchanged, another official version, what to do (no '-5')");
  const failedFn = html.slice(html.indexOf("const failed = (e) => {"), html.indexOf("\n};", html.indexOf("const failed = (e) => {")) + 3);
  const said = [];
  vm.runInNewContext(failedFn + "\nfailed(err);", { SMK_PRO: /^(ota-)?SMK-37 Pro$/i, TEXT: T, lang: "fr",
    sayK: (k) => said.push(k), log: () => {}, String, err: e });
  ok(said.join() === "headother", "installer: failed() shows the 'headother' message for the code");
}

async function updater() {
  const image = Uint8Array.from({ length: 0x2000 }, (_, i) => (i * 7) & 0xFF);
  const dev = new FakeFM1(image);
  const steps = [];
  const got = await new Updater(dev.access).install(image, "FM-1_104200", (k) => steps.push(k));
  ok(got === "FM-1_104200" && dev.bad === 0 && steps.includes("write") && steps.at(-1) === "done",
    `fm1ota.js: install: running firmware -> loader -> Felucca (${dev.served} reads)`);

  const rescue = new FakeFM1(image);
  rescue.boot("FM-1_000", "Felucca");
  const recovered = await new Updater(rescue.access).install(image, "FM-1_104200");
  ok(recovered === "FM-1_104200" && rescue.bad === 0, "fm1ota.js: recovery mode -> loader -> normal firmware");
  const failedBoot = new FakeFM1(image, { finalIdentity: "FM-1_000" });
  const rescueSteps = [];
  const bootError = await new Updater(failedBoot.access).install(image, "FM-1_104200", (k) => rescueSteps.push(k)).then(() => null, (e) => e);
  ok(bootError?.code === "mismatch" && !rescueSteps.includes("done"), "fm1ota.js: boot into recovery is not reported as successful installation");

  const dev2 = new FakeFM1(image, { unplugAfter: 3 });
  dev2.boot("ota-FM-1_104200", "Felucca Update");
  const t0 = Date.now();
  const done = await new Updater(dev2.access).resume(image);
  ok(done === false && Date.now() - t0 < 6000, "fm1ota.js: unplugged during the write -> stops at once");

  /* Stallion's own number (FM-1_1042XY): a unit that runs Stallion from before it (FM-1_900, port "Stallion") takes
     the new identity, and an update cut off on the way resumes: the new package's loader (ota-FM-1_104200) and the
     900 one an earlier update left behind are both Stallion's own (fm1ota.js OUR_LOADER) */
  const { OUR_LOADER: OL, STALLION_NUM: SN } = await import("./fm1ota.js");
  ok(SN === "1042" && OL({ text: "ota-FM-1_104200" }) && OL({ text: "ota-FM-1_104210" }) && OL({ text: "ota-FM-1_900" })
     && !OL({ text: "ota-FM-1_015" }) && !OL({ text: "ota-FM-1_1042" }) && !OL({ text: "ota-FM-1_10001400" }),
     "fm1ota.js: own loaders: Stallion's (ota-FM-1_1042XY) and the 900 one; not the stock or another number");
  const d900 = new FakeFM1(image);
  d900.boot("FM-1_900", "Stallion");
  const g900 = await new Updater(d900.access).install(image, "FM-1_104200");
  ok(g900 === "FM-1_104200" && d900.bad === 0, "fm1ota.js: from Stallion FM-1_900 (port 'Stallion') -> FM-1_104200");
  for (const ldr of ["ota-FM-1_104200", "ota-FM-1_900"]) {
    const r = new FakeFM1(image);
    r.boot(ldr, "Felucca Update");
    const fin = await new Updater(r.access).resume(image);
    ok(fin === true && r.bad === 0 && r.served >= 6, `fm1ota.js: from 900, cut off: ${ldr} resumed, the write finished`);
  }

  const dev3 = new FakeFM1(image, { unplugAfter: 2 });
  const e = await new Updater(dev3.access).install(image, "FM-1_104200").then(() => null, (x) => x);
  ok(e && e.code === "lost", "fm1ota.js: unplugged in step 1 -> error code 'lost'");
  const e2 = await new Updater({ inputs: new Map(), outputs: new Map() }).install(image, "FM-1_104200").then(() => null, (x) => x);
  ok(e2 && e2.code === "notfound", "fm1ota.js: no device -> error code 'notfound'");
  const stock = new FakeFM1(image);
  stock.boot("ota-FM-1_015", "FM-1 Update");
  const e3 = await new Updater(stock.access).resume(image).then(() => null, (x) => x);
  ok(e3 && e3.code === "foreign" && e3.detail === "ota-FM-1_015" && stock.served === 0, "fm1ota.js: another firmware's loader is never resumed ('foreign')");
  const back = new FakeFM1(image, { finalIdentity: "FM-1_015" });   /* the return to the official V15, interrupted */
  back.boot("ota-FM-1_015", "FM-1 Update");
  const st = [];
  const r4 = await new Updater(back.access).resume(image, (k) => st.push(k), { product: "FM-1_015" });
  ok(r4 === true && back.bad === 0 && st.at(-1) === "done", "fm1ota.js: return to official: its own loader resumed, V15 checked when back");
  const wrong = new FakeFM1(image, { finalIdentity: "FM-1_104200" });
  wrong.boot("ota-FM-1_015", "FM-1 Update");
  const e5 = await new Updater(wrong.access).resume(image, null, { product: "FM-1_015" }).then(() => null, (x) => x);
  ok(e5 && e5.code === "mismatch", "fm1ota.js: return to official: another firmware coming back is not 'done'");
  const pk = await import(join(HERE, "fm1pkg.js"));
  await headOther(pk);
  const notStock = new Uint8Array(pk.STOCK_V15_SIZE);
  const e6 = await pk.validateStockPackage(notStock).then(() => null, (x) => x);
  ok(e6 && /official FM-1 file/.test(e6.message), "fm1pkg.js: only the exact official V15 / V16 is accepted (SHA-256)");
  /* (after 1.2, sloop-fm1 #8) V16, the file M-VAVE now offers, is accepted too: its size, digest and identity pinned;
   * a file of its size with other bytes is not. FM1_V16=path/FM-1.fwsc (M-VAVE's V16, not shipped here) checks the
   * real file */
  ok(pk.STOCK_V16_SIZE === 718196 && pk.STOCK_V16_PRODUCT === "FM-1_016" && pk.STOCK_FM1.length === 2 &&
     pk.STOCK_FM1.some((k) => k.product === "FM-1_015"), "fm1pkg.js: the official FM-1 V16 and V15 pinned");
  const e7 = await pk.validateStockPackage(new Uint8Array(pk.STOCK_V16_SIZE)).then(() => null, (x) => x);
  ok(e7 && /official FM-1 file/.test(e7.message), "fm1pkg.js: a file of V16's size, other bytes: refused");
  if (process.env.FM1_V16) {
    const v = await pk.validateStockPackage(new Uint8Array(readFileSync(process.env.FM1_V16)));
    ok(v.product === "FM-1_016" && v.version === "V16" && v.image.length === pk.STOCK_V16_SIZE - 20,
       "fm1pkg.js: M-VAVE's FM-1 V16 accepted (identity FM-1_016)");
  }
}

await editorMock();
await editorMockNames();
await editorDemoPage();
await stallionUx();
await editorLibrarian();
await editorLive();
await editorTracks();
await editorMixer();
await editorTrackParam();
await editorV5();
hybridPatch();
await editorPorts();
sampleLoops();
await fmxPatch();
/* (Stallion) the envelope's playhead: ENVT against the firmware's own envelope (voice.c env_tick / env2_tick run tick
   by tick in integers, on the tables of build/gen/felucca_tables.h, else the same tables from tools/gen_tables.py's
   formulas), and ENVG.head on the picture */
function fwTables() {
  const p = join(HERE, "..", "build", "gen", "felucca_tables.h");
  const tab = (name) => {
    if (!existsSync(p)) return null;
    const m = new RegExp(`${name}\\[128\\][^{]*\\{([^}]*)\\}`).exec(readFileSync(p, "utf8"));
    return m ? m[1].split(",").map((x) => parseInt(x, 10)).filter((x) => !Number.isNaN(x)) : null;
  };
  const ms = Array.from({ length: 128 }, (_, v) => 10000 ** (v / 127));
  return {
    src: existsSync(p) ? "felucca_tables.h" : "gen_tables.py formulas",
    LIN: tab("ENV_LIN") || ms.map((m) => Math.max(1, Math.floor(((1 << 24) * 32) / ((m * 44100) / 1000)))),
    EXP: tab("ENV_EXP") || ms.map((m) => Math.max(1, Math.min(65535, Math.floor(65536 * (1 - Math.exp(-32 / ((m / 1000 / 4.6) * 44100))))))),
    HZ100: tab("LFO_HZ_X100") || ms.map((_, v) => Math.round(0.05 * 800 ** (v / 127) * 100)),
  };
}
function envPlayhead() {
  const TB = fwTables(), TICK = 32 / 44100;
  const mulq16 = (a, k) => Math.floor(a / 65536) * k + Math.floor(((a & 0xFFFF) * k) / 65536);
  /* the firmware's ENV (env2: ENV 2, loop) from silence, the key let go after hold s: the level each tick (0..1) */
  const fw = (a, d, s, r, hold, env2, loop) => {
    let env = 0, st = 1;
    const sus = s << 17, out = [];
    for (let n = 0; n < 200000 && st; n++) {
      if (st < 3 && n * TICK >= hold) st = 3;
      if (st === 1) { env += TB.LIN[env2 ? a : Math.max(a, 15)]; if (env >= 1 << 24) { env = 1 << 24; st = 2; } }
      else if (st === 2) {
        env += mulq16(sus - env, TB.EXP[d]);
        if (loop && env - sus < 1 << 18) st = 1;
      } else { env -= mulq16(env, TB.EXP[env2 ? r : Math.max(r, 50)]); if (env < 1 << 12) { env = 0; st = 0; } }
      out.push(env / (1 << 24));
    }
    return out;
  };
  let worst = 0, worstEnd = 0, cases = 0;
  const rnd = (() => { let x = 12345; return () => ((x = (x * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff); })();
  for (let c = 0; c < 60; c++) {
    const env2 = c % 3 === 2, loop = env2 && c % 6 === 5;
    const [a, d, s, r] = [Math.floor(rnd() * 100), Math.floor(rnd() * 100), Math.floor(rnd() * 128), Math.floor(rnd() * 100)];
    const T = E.ENVT.times(a, d, s, r, env2), hold = Math.min(3, T.tA + T.tD * (0.2 + rnd()));
    const lv = fw(a, d, s, r, hold, env2, loop), end = lv.findIndex((x, i) => i > 0 && x === 0 && i * TICK > hold);
    for (let i = 0; i < lv.length; i += 3) {             /* (within the firmware's levels 3 ticks either side: its 0.7 ms
                                                             steps, not a model's error) */
      const h = E.ENVT.at(T, (i + 1) * TICK, hold, 0, loop), w = lv.slice(Math.max(0, i - 3), i + 4);
      worst = Math.max(worst, Math.max(0, Math.min(...w) - h.lv, h.lv - Math.max(...w)));
    }
    if (end > 0) {
      const tEnd = (end + 1) * TICK, h = E.ENVT.at(T, tEnd * 1.03 + 3 * TICK, hold, 0, loop), g = E.ENVT.at(T, tEnd * 0.97 - 3 * TICK, hold, 0, loop);
      worstEnd = Math.max(worstEnd, h.st === "end" && g.st !== "end" ? 0 : 1);
    }
    cases++;
  }
  let tabs = 0;
  for (let v = 0; v < 128; v++) tabs += Math.abs(E.ENVT.lin(v) - TB.LIN[v]) > 1 || Math.abs(E.ENVT.k(v) - TB.EXP[v]) > 1;
  ok(cases === 60 && worst < 0.02 && worstEnd === 0 && !tabs,
     `Stallion: the playhead's envelope runs as the firmware's (${TB.src}; worst level ${worst.toFixed(3)} beyond a tick's, ends within 3 %)`);
  /* the known seconds: TIME 0 1 ms, 127 10 s; ENV's floors (3 ms attack, 37 ms release); ENV 2 none */
  const T0 = E.ENVT.times(0, 0, 64, 0), T2 = E.ENVT.times(0, 127, 64, 127, true);
  ok(Math.abs(T0.tA - 0.00297) < 0.0001 && Math.abs(T0.tR - 0.0376) < 0.0005 && Math.abs(T2.tA - 0.001) < 1e-6 && Math.abs(T2.tD - 10) < 0.5 &&
     E.ENVT.at(T0, 1, null).st === "D" && Math.abs(E.ENVT.at(T0, 1, null).lv - 0.5) < 1e-3 && E.ENVT.at(T0, 1.2, 1).st === "end" &&
     E.ENVT.at(T0, T0.tA / 4, null, 0.5).lv > 0.7 && E.ENVT.held(T0, 0, 0.5).lv === 0.5,
     "Stallion: TIME 0 .. 127 is 1 ms .. 10 s, ENV's shortest attack and release, a restart from the level it had");
  /* on the picture: the attack's middle half way up its line, the decay's end at its corner, then along the held
     stretch (never its end), the release's 99 % point at the end */
  const v = [64, 64, 80, 64], P = E.ENVG.points(...v), T = E.ENVT.times(...v);
  const hA = E.ENVG.head(v, undefined, T, { st: "A", lv: 0.5, ts: 0 }), hD = E.ENVG.head(v, undefined, T, { st: "D", lv: 0.63, ts: T.tD });
  const hH = E.ENVG.head(v, undefined, T, { st: "D", lv: 0.63, ts: T.tD + 100 }), hR = E.ENVG.head(v, undefined, T, { st: "R", lv: 0.01, ts: T.tR });
  /* let go before the decay reached SUS: the ring waits at the release's start, on the line, until the
     sound comes down to SUS; half way down from SUS: on the release's curve (the quadratic the picture draws) */
  const hE = E.ENVG.head(v, undefined, T, { st: "R", lv: 0.95, ts: 0.01 }), sl = 1 - (P.ys - E.ENVG.TOP) / (E.ENVG.BOT - E.ENVG.TOP);
  const hM = E.ENVG.head(v, undefined, T, { st: "R", lv: sl / 2, ts: 0.1 }), u = 1 - Math.sqrt(0.5);
  const cx = (1 - u) * (1 - u) * P.x3 + 2 * u * (1 - u) * (P.x3 + 6) + u * u * P.x4, cy = (1 - u) * (1 - u) * P.ys + 2 * u * (1 - u) * E.ENVG.BOT + u * u * E.ENVG.BOT;
  ok(Math.abs(hA[0] - P.x1 / 2) < 1e-9 && Math.abs(hA[1] - (E.ENVG.BOT + E.ENVG.TOP) / 2) < 1e-9 && Math.abs(hD[0] - P.x2) < 1e-6 &&
     hH[0] > P.x2 && hH[0] < P.x3 && hR[0] > P.x3 + 0.75 * (P.x4 - P.x3) && Math.abs(hE[0] - P.x3) < 1e-9 && Math.abs(hE[1] - P.ys) < 1e-9 &&
     Math.abs(hM[0] - cx) < 1e-6 && Math.abs(hM[1] - cy) < 1e-6,
     "Stallion: the envelope's playhead on the picture's line (attack, decay, held stretch, release; let go early: at the release's start)");
  /* the UI: the device's notes feed it, one animation frame at a time only while a note sounds, ENV 2 too */
  ok(/onmidimessage = \(e\) => \{ ntMidi\(e\); l\.receive\(e\.data\); \}/.test(html) && /if \(cur\) NT\.raf = requestAnimationFrame\(ntFrame\)/.test(html) &&
     /function ntKick\(\) \{ if \(!NT\.raf/.test(html) && /phPlace\(eb\.phl\[i\], eb\.phd\[i\]/.test(html) && /\.envph \{/.test(html),
     "Stallion: the playheads follow the device's own notes, animated only while one sounds");
}
/* (Stallion) the LFO's picture: RATE in Hz as the firmware's table, the grab points map back onto RATE PHASE FADE, the
   time shown holds 2.5 cycles, the waves as lfo_wave_n, FADE as track_lfo_tick's, the playhead */
function lfoGraph() {
  const TB = fwTables(), L = E.LFOG;
  let hzBad = 0, back = true;
  for (let v = 0; v < 128; v++) { if (Math.abs(L.hz(v) * 100 - TB.HZ100[v]) > 1) hzBad++; if (L.rate(L.hz(v)) !== v) back = false; }
  ok(!hzBad && back && Math.abs(L.hz(0) - 0.05) < 1e-12 && Math.abs(L.hz(127) - 40) < 1e-9, `Stallion: LFO RATE as the firmware's Hz (${TB.src}), and back`);
  let worst = 0, inside = true, cycles = true, n = 0;
  const rnd = (() => { let x = 777; return () => ((x = (x * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff); })();
  for (let c = 0; c < 400; c++) {
    const v = [Math.floor(rnd() * 128), Math.floor(rnd() * 6), Math.floor(rnd() * 128), c % 5 ? Math.floor(rnd() * 128) : 0];
    const span = L.span(v), p = L.points(v, span), pre = (v[2] / 128) * p.T;
    cycles = cycles && (span - pre >= 2.5 * p.T - 1e-9 || span === L.ZOOMS[L.ZOOMS.length - 1]);
    inside = inside && p.R[0] <= L.W && p.P[0] >= 0 && p.P[0] <= L.W && p.F[0] <= L.W && [p.P, p.R].every(([, y]) => y >= L.TOP - 1e-9 && y <= L.BOT + 1e-9);
    if (p.R[0] < L.W) worst = Math.max(worst, Math.abs(L.drag("R", p.R[0], v, span)[0] - v[0]));
    worst = Math.max(worst, Math.abs(L.drag("P", p.P[0], v, span)[2] - v[2]));
    if (p.F[0] < L.W && p.F[0] - p.x0 >= 3) { const f = L.drag("F", p.F[0], v, span)[3]; worst = Math.max(worst, Math.abs(L.fadeS(f) - L.fadeS(v[3])) < 1e-9 ? 0 : Math.abs(f - v[3])); n++; }   /* (FADEs of one time: the same) */
    if (!v[3]) worst = Math.max(worst, L.drag("F", p.F[0], v, span)[3]);
  }
  ok(worst <= 1 && inside && cycles && n > 100 && L.drag("R", 99999, [64, 0, 0, 0], 1)[0] === L.rate(1 / (99999 / 2 / 400)) &&
     L.drag("P", -50, [64, 0, 0, 0], 4)[2] === 0 && L.drag("P", 9999, [64, 0, 0, 0], 4)[2] === 127 && L.drag("F", 1, [64, 0, 0, 90], 4)[3] === 0,
     `Stallion: the LFO's grab points map back onto RATE PHASE FADE (worst ${worst}), inside the picture, 2.5 cycles shown`);
  /* the waves at known phases (voice.c lfo_wave_n: TRI from the bottom, SAW rising, SQR high first), all within -1 .. 1 */
  let within = true;
  for (let w = 0; w < 6; w++) for (let ph = 0; ph < 8; ph += 0.013) { const y = L.wave(w, ph); within = within && y >= -1 - 1e-9 && y <= 1 + 1e-9; }
  ok(within && Math.abs(L.wave(0, 0.25) - 1) < 1e-9 && Math.abs(L.wave(0, 0)) < 1e-9 && L.wave(1, 0) === -1 && L.wave(1, 0.5) === 1 &&
     L.wave(2, 0) === -1 && L.wave(3, 0.25) === 1 && L.wave(3, 0.75) === -1 && L.wave(4, 0.1) === L.wave(4, 0.9) && L.wave(4, 0.9) !== L.wave(4, 1.1),
     "Stallion: the LFO waves as the firmware's (SIN TRI SAW SQR, S&H held a cycle, CHAO), within -1 .. 1");
  /* FADE: track_lfo_tick adds ENV_LIN[FADE] >> 9 a tick to a Q15 depth (about FADE's TIME, longer at the top) */
  let fadeBad = 0;
  for (let f = 1; f < 128; f += 3) {
    const step = Math.max(1, TB.LIN[f] >> 9), ticks = Math.ceil(32767 / step), t = (ticks * 32) / 44100;
    if (Math.abs(t - L.fadeS(f)) > 1e-9) fadeBad++;
  }
  ok(!fadeBad && L.fadeS(0) === 0, "Stallion: the LFO's FADE takes as long as the firmware's");
  /* the playhead: at the start x0 (PHASE), a cycle later a cycle on, faded in by FADE, back into the picture past its edge */
  const v = [70, 0, 32, 60], span = L.span(v), p = L.points(v, span), h0 = L.head(v, span, 0), h1 = L.head(v, span, p.T);
  const far = L.head(v, span, 1000), full = L.head(v, span, p.fs + 3 * p.T);
  ok(Math.abs(h0[0] - p.x0) < 1e-9 && Math.abs(h0[1] - L.mid()) < 1e-9 && Math.abs(h1[0] - (p.x0 + p.Tpx)) < 1e-6 && far[0] >= 0 && far[0] <= L.W &&
     Math.abs(full[1] - L.y(v, span, full[0])) < 1e-6,
     "Stallion: the LFO's playhead from PHASE, a cycle a period, faded in, kept in the picture");
  ok(/const LFO_SETS = \[\["LFO", \[9, 10, 11, 12\]/.test(html) && /\["LFO 2", \[53, 54, 55\]/.test(html) && /\["LFO 3", \[92, 93, 94\]/.test(html) &&
     /function lfoGrab\(box, h\)/.test(html) && /box\.frozen = box\.span/.test(html) && /lfoIds\(\)\.includes\(id\)\) lfoDraw\(\)/.test(html),
     "Stallion: the LFO card's picture, its switch (LFO, LFO 2, LFO 3) and grab points");
}

await wavetables();
await editorV7();
await editorV8();
await editorV9();
await editorBackup();
await backupTouch();
await editor25();
await editorDsyn();
await editorCart();
await patternTools();
editorTabs();
editorClipperPages();
editorIcons();
try { editorPatchBay(); } catch (e) { ok(false, "patch bay: " + (e && e.message)); }
try { editorPatchBayGeneric(); } catch (e) { ok(false, "patch bay generic: " + (e && e.message)); }
try { await editorDevbar(); } catch (e) { ok(false, "device banner: " + (e && e.message)); }
try { editorPatchBayOrder(); } catch (e) { ok(false, "patch bay order: " + (e && e.message)); }
try { editorPatchBayFit(); } catch (e) { ok(false, "patch bay fit: " + (e && e.message)); }
try { editorPatchBayTabs(); } catch (e) { ok(false, "patch bay tabs: " + (e && e.message)); }
try { editorPatchBay24(); } catch (e) { ok(false, "patch bay 24 slots: " + (e && e.message)); }
try { editorPatchBayFix1(); } catch (e) { ok(false, "patch bay fix: " + (e && e.message)); }
try { editorPatchBayZoom(); } catch (e) { ok(false, "patch bay zoom: " + (e && e.message)); }
try { editorMatrixKnobs(); } catch (e) { ok(false, "MATRIX card: " + (e && e.message)); }
try { editorChar(); } catch (e) { ok(false, "PONY CHAR: " + (e && e.stack)); }
samplesMatch();
chopTests();
await packages();
await updater();
await midiRestarts();
console.log(failed ? `WEB TESTS FAILED (${failed})` : "web tests passed");
process.exit(failed ? 1 : 0);
