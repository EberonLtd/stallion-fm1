// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Eberon Limited (John Gorman)
// The editor's demo device (editor.html makeMockDevice, ?mock=1) as the real firmware describes itself: every descriptor
// (scopes 0..3), the engines' EDIT values and their factory presets, the labels that follow a value, the start values,
// read from the firmware on the host (tests/editor_bridge.c) with the editor's own protocol code, written into
// editor.html between /*MOCKFW-BEGIN*/ and /*MOCKFW-END*/ (MOCK_FW):
//   node web/gen_mock.mjs [build/host/editor_bridge]           rewrite the block
//   node web/gen_mock.mjs --check [build/host/editor_bridge]   exit 1 when the block is not what the firmware says
// (web/test_editor_p15.mjs runs --check and compares the mock with the firmware id by id.)

import { spawn } from "node:child_process";
import { readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

const HERE = fileURLToPath(new URL(".", import.meta.url));
const args = process.argv.slice(2), check = args.includes("--check");
const BRIDGE = args.filter((a) => a !== "--check")[0] || join(HERE, "../build/host/editor_bridge");
const FILE = join(HERE, "editor.html");
const html = readFileSync(FILE, "utf8");
const cut = (a, b) => { const i = html.indexOf(a), j = html.indexOf(b); return i >= 0 && j > i ? html.slice(i, j + b.length) : ""; };
const E = vm.runInNewContext(cut("/*PROTO-BEGIN*/", "/*PROTO-END*/") + ";({ parse, req, Link, CMD, F })",
  { setTimeout, clearTimeout, setInterval, clearInterval, console, TextEncoder, TextDecoder });
const C = E.CMD;

/* ---- the bridge: a line of hex a frame, "." after each line */
const br = spawn(BRIDGE, [], { stdio: ["pipe", "pipe", "inherit"] });
const syncs = [];
let buf = "", cover = null, link;
br.stdout.setEncoding("utf8");
br.stdout.on("data", (d) => {
  buf += d;
  let k;
  while ((k = buf.indexOf("\n")) >= 0) {
    const l = buf.slice(0, k).trim();
    buf = buf.slice(k + 1);
    if (l === ".") { const r = syncs.shift(); if (r) r(); }
    else if (l.startsWith("{")) cover = JSON.parse(l);
    else if (/^F0/.test(l)) link.receive(Uint8Array.from(l.split(/\s+/).map((x) => parseInt(x, 16))));
  }
});
link = new E.Link((f) => { syncs.push(() => {}); br.stdin.write(Array.from(f, (b) => b.toString(16).padStart(2, "0")).join(" ") + "\n"); },
  { timeout: 3000 });
const put = (line) => new Promise((res) => { syncs.push(res); br.stdin.write(line + "\n"); });
const rq = (r) => link.request(r);

/* a descriptor as an array: [label, fmt, min, max, def, unit, names (0: none), flags]; deduplicated in D */
const D = [], dIdx = new Map();
const dArr = (d) => [d.label, d.fmt, d.min, d.max, d.def, d.unit || "", d.names.length ? d.names : 0, d.flags | 0];
const di = (d) => {
  const a = dArr(d), k = JSON.stringify(a);
  if (!dIdx.has(k)) { dIdx.set(k, D.length); D.push(a); }
  return dIdx.get(k);
};
const same = (a, b) => JSON.stringify(dArr(a)) === JSON.stringify(dArr(b));
/* scope 0 / 1 as the firmware derives them from the full descriptor: label and unit 8 letters, names of an enum only */
const as0 = (d) => ({ ...d, label: d.label.slice(0, 8), unit: (d.unit || "").slice(0, 8), names: d.fmt === E.F.ENUM ? d.names.map((n) => n.slice(0, 8)) : [], flags: 0 });

try {
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const { pcount: PC, pe0: PE0, pall: PALL, gcount: GC } = info;
  const desc = async (sc, id) => E.parse[C.DESC](await rq(sc === 2 ? E.req.pdesc(info, id) : E.req.desc(sc, id)));
  const pget = async (id) => E.parse[C.GET](await rq(E.req.pget(info, id))).value;
  const pset = (id, v) => rq(E.req.pset(info, id, v));
  const isE = (id) => id >= PE0 && id < PE0 + 8;
  await put("#cover");

  /* the start: the four tracks, the globals, the settings (a fresh FM-1) */
  const tr = E.parse[C.TRACK](await rq(E.req.track()));
  const tracks = [];
  for (let i = 0; i < tr.ntrk; i++) {
    const d = E.parse[C.TRACK_DUMP](await rq(E.req.trackDump(i, 1)), info);
    tracks.push([d.engine, d.preset, d.p]);
  }
  await rq(E.req.track(0));
  const dump = E.parse[C.DUMP](await rq(E.req.dump(1)), info);

  /* the sound's descriptors (track 1: its engine's); a matrix DST with E1..E8 (scope 0's names) */
  const tp = [], tp0 = {}, mdst = [];
  const s0 = [], s2 = [];
  for (let id = 0; id < PALL; id++) {
    s2[id] = await desc(2, id);
    s0[id] = id < PC ? await desc(0, id) : null;
  }
  let e1 = -1;
  for (let id = 0; id < PC; id++) {
    if (isE(id)) continue;
    const a = s2[id], b = s0[id];
    if (a.fmt === E.F.ENUM && b.names.includes("E1") && b.names.join() !== a.names.join()) {   /* a matrix DST */
      mdst.push(id);
      e1 = b.names.indexOf("E1");
      s2[id] = { ...a, names: a.names.map((n, k) => (k >= e1 && k < e1 + 8 ? b.names[k] : n)) };
    }
  }
  /* preset format 2's slots 13..24 (ids P_COUNT..): no scope 0 descriptor, but the same DST as slot 1's (the engine's
     E1..E8 names, params.c): a scope 2 descriptor equal to slot 1's (as the engine names it) is a DST too */
  if (mdst.length) {
    const m0 = await desc(2, mdst[0]), k0 = JSON.stringify(dArr(m0));
    for (let id = PC; id < PALL; id++) {
      if (JSON.stringify(dArr(s2[id])) !== k0) continue;
      mdst.push(id);
      s2[id] = { ...s2[id], names: s2[mdst[0]].names.slice() };
    }
  }
  for (let id = 0; id < PALL; id++) {
    if (isE(id)) { tp.push(-1); continue; }
    tp.push(di(s2[id]));
    if (id < PC && !same(as0(s2[id]), { ...s0[id], flags: 0 })) tp0[id] = di({ ...s0[id], flags: 0 });
  }
  const gp = [];
  for (let id = 0; id < GC; id++) gp.push(di({ ...(await desc(1, id)), flags: 0 }));
  const xs = [];
  for (const id of cover.settings) {
    const d = await desc(3, id), v = E.parse[C.GET](await rq(E.req.get(3, id))).value;
    xs.push([id, di(d), v]);
  }

  /* the ids a preset leaves as they are (ui.c param_kept): every value at its top, then its bottom, a preset loaded */
  /* the arp's MODE and HOLD, by their labels (the first MODE in id order is the arp's: the LFOs' come after; a load
     keeps the arp's settings while it runs, so these two are left out and the arp is set OFF first) */
  const aMode = s2.findIndex((d) => d.label === "MODE"), aHold = s2.findIndex((d) => d.label === "HOLD");
  if (aMode < 0 || aHold < 0 || s2[aMode].names[0] !== "OFF") throw new Error("the arp's MODE, HOLD: not found by label");
  const turnable = [];
  for (let id = 0; id < PALL; id++) if (!isE(id) && s2[id].max > s2[id].min && id !== aMode && id !== aHold) turnable.push(id);
  const keptBy = async (top) => {
    await rq(E.req.set(0, aMode, 0));
    for (const id of turnable) await pset(id, top ? s2[id].max : s2[id].min);
    await rq(E.req.preset(0, 1));
    const d = E.parse[C.DUMP](await rq(E.req.dump(1)), info);
    return new Set(turnable.filter((id) => d.p[id] === (top ? s2[id].max : s2[id].min)));
  };
  const k1 = await keptBy(true), k2 = await keptBy(false);
  const kept = turnable.filter((id) => k1.has(id) && k2.has(id));

  /* every engine: its pages' titles, EDIT values (as shown at their defaults; scope 0's where it says otherwise), its
     factory presets (E1..E8, then the ids a preset sets away from the default, as id, value pairs) */
  const eng = [];
  for (let e = 0; e < info.nengines; e++) {
    await rq(E.req.set(1, 20, e));
    const nm = E.parse[C.NAMES](await rq(E.req.names(e)));
    const ed = [], ed0 = {};
    for (let j = 0; j < 8; j++) {
      const a = await desc(2, PE0 + j), b = await desc(0, PE0 + j);
      ed.push(di(a));
      if (!same(as0(a), { ...b, flags: 0 })) ed0[j] = di({ ...b, flags: 0 });
    }
    const pr = [];
    for (let k = 0; k < nm.names.length; k++) {
      await rq(E.req.set(0, 17, 0));
      await rq(E.req.preset(e, k));
      const d = E.parse[C.DUMP](await rq(E.req.dump(1)), info);
      const diff = [];
      for (let id = 0; id < PALL; id++) if (!isE(id) && !kept.includes(id) && d.p[id] !== s2[id].def) diff.push(id, d.p[id]);
      pr.push([nm.names[k], d.p.slice(PE0, PE0 + 8), diff]);
    }
    eng.push({ t: nm.titles, ed, ...(Object.keys(ed0).length ? { ed0 } : {}), pr });
  }

  /* the labels that follow a value (params.c track_desc, the engines' desc hooks): for every value of each id that drives
     others, the descriptors of the ids that follow, where they differ from the plain ones: [engine (-1: any), drive id,
     id, from, to, descriptor] */
  const rules = [];
  for (let e = 0; e < info.nengines; e++) {
    await rq(E.req.set(1, 20, e));
    const base = [], drv = [], fol = [];             /* (relative to the plain ones: tp, the engine's ed) */
    for (let id = 0; id < PALL; id++) {
      const d = await desc(2, id);
      if (d.flags & 2) drv.push(id);
      if (d.flags & 1) {
        fol.push(id);
        const [label, fmt, min, max, def, unit, names, flags] = D[isE(id) ? eng[e].ed[id - PE0] : tp[id]];
        base[id] = { label, fmt, min, max, def, unit, names: names || [], flags };
      }
    }
    const dds = new Map();                           /* (every driver at its default first: each one's rule from
                                                       the same plain sound, the others at theirs) */
    for (const dv of drv) { const dd = await desc(2, dv); dds.set(dv, dd); await pset(dv, dd.def); }
    const driven = new Set();                        /* (the ids a driver's value changes) */
    for (const dv of drv) {
      const dd = dds.get(dv);
      const runs = new Map();                        /* id -> [from, to, descriptor index] */
      for (let v = dd.min; v <= dd.max; v++) {
        await pset(dv, v);
        for (const id of fol) {
          const d = await desc(2, id);
          const x = same(d, base[id]) ? -1 : di(d), r = runs.get(id);
          if (r && r[r.length - 1][2] === x && r[r.length - 1][1] === v - 1) r[r.length - 1][1] = v;
          else if (!r) runs.set(id, [[v, v, x]]); else r.push([v, v, x]);
        }
      }
      await pset(dv, dd.def);
      for (const [id, r] of runs) {                  /* (the same over dv's whole range: another driver's doing) */
        if (r.length > 1) driven.add(id);
        if (r.length > 1) for (const [a, b, x] of r) if (x >= 0) rules.push([e, dv, id, a, b, x]);
      }
    }
    for (const id of fol) {                          /* (Stallion) one the engine alone sets (PONY's MODL list): a rule
                                                       on itself over its whole range */
      if (isE(id) || driven.has(id)) continue;
      const d = await desc(2, id);
      if (!same(d, base[id])) rules.push([e, id, id, d.min, d.max, di(d)]);
    }
  }
  /* the same rule on every engine: one rule, engine -1 */
  const byKey = new Map();
  for (const r of rules) { const k = JSON.stringify(r.slice(1)); byKey.set(k, (byKey.get(k) || []).concat(r[0])); }
  const rl = [];
  for (const [k, es] of byKey) {
    const r = JSON.parse(k);
    if (es.length === info.nengines) rl.push([-1, ...r]); else for (const e of es) rl.push([e, ...r]);
  }
  /* the base descriptors of the following ids are the plain ones of every engine but the E ones (theirs: ed) */
  for (const id of new Set(rl.map((r) => r[2]))) if (!isE(id) && tp[id] < 0) throw new Error(`rule on ${id}`);

  const FW = { v: info.version, eng: info.engines, pc: PC, pe0: PE0, pall: PALL, gc: GC, ntrk: info.ntrk, nstep: info.nstep,
    proto: info.proto, nd: info.noDst ? info.ndst : 0, dd: info.noDst || [], D, tp, tp0, gp, xs, mdst, e1, kept, rules: rl, g: dump.g, tracks, e: eng };
  const lines = ["const MOCK_FW = {"];
  for (const [k, v] of Object.entries(FW)) {
    if (k === "D" || k === "e" || k === "tracks") {
      lines.push(`  ${k}: [`);
      for (const x of v) lines.push("    " + JSON.stringify(x) + ",");
      lines.push("  ],");
    } else lines.push(`  ${k}: ${JSON.stringify(v)},`);
  }
  lines.push("};");
  const block = "/*MOCKFW-BEGIN*/\n/* the editor's demo device as the firmware describes itself: written by web/gen_mock.mjs from tests/editor_bridge.c\n" +
    "   (do not edit; run node web/gen_mock.mjs after a change of the firmware's values, presets or pages) */\n" +
    lines.join("\n") + "\n/*MOCKFW-END*/";
  const i = html.indexOf("/*MOCKFW-BEGIN*/"), j = html.indexOf("/*MOCKFW-END*/");
  if (i < 0 || j < i) throw new Error("editor.html: no /*MOCKFW-BEGIN*/ .. /*MOCKFW-END*/ block");
  const old = html.slice(i, j + "/*MOCKFW-END*/".length);
  if (check) {
    console.log(old === block ? "MOCK_FW: as the firmware says" : "MOCK_FW: differs from the firmware (run node web/gen_mock.mjs)");
    process.exitCode = old === block ? 0 : 1;
  } else {
    writeFileSync(FILE, html.slice(0, i) + block + html.slice(j + "/*MOCKFW-END*/".length));
    console.log(`MOCK_FW: ${block.length} bytes, ${D.length} descriptors, ${eng.reduce((n, x) => n + x.pr.length, 0)} presets, ${rl.length} rules`);
  }
} finally {
  link.close();
  br.stdin.end();
  br.kill();
}
