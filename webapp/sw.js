// SPDX-License-Identifier: GPL-3.0-only
// Changes: Copyright (C) 2026 Eberon Limited (John Gorman)
// Clipper 1.1 (after SLOOP 2.6): the site without internet (webapp/sw.js, made by web/make_site.py). Opened once online, the
// installer, the editor and the firmware they install are kept here; offline they come from this copy, still on
// https, so Web MIDI asks as usual (a downloaded file on Android opens as content://, where Chrome refuses MIDI).
// Online the network always wins (the page as published, never an old one); the copy is only the fallback.
// A new site (any file changed) is a new sw.js: the new copy replaces the old one.
const STAMP = "1.1-72d5687aa5fc";                          // version + a hash of the files
const CACHE = "stallion-fm1-site-" + STAMP;      // (Stallion: its own name; github.io serves every EberonLtd repo from one origin)
const FILES = ["../firmware/stallion-1.1.fwsc", "../firmware/stallion-smk37-1.1.fwsc", "editor/", "editor/FUKIAI-LICENSE.txt", "editor/fukiai.ttf", "icon-192.png", "icon-512.png", "icon-maskable-512.png", "installer-smk37/", "installer/", "screens/shot-env.png", "screens/shot-env@2x.png", "screens/shot-env@3x.png", "screens/shot-filter.png", "screens/shot-filter2.png", "screens/shot-filter2@2x.png", "screens/shot-filter2@3x.png", "screens/shot-filter@2x.png", "screens/shot-filter@3x.png", "screens/shot-home-mare.png", "screens/shot-home-mare@2x.png", "screens/shot-home-mare@3x.png", "screens/shot-home-palomino.png", "screens/shot-home-palomino@2x.png", "screens/shot-home-palomino@3x.png", "screens/shot-home-rodeo.png", "screens/shot-home-rodeo@2x.png", "screens/shot-home-rodeo@3x.png", "screens/shot-home-stampede.png", "screens/shot-home-stampede@2x.png", "screens/shot-home-stampede@3x.png", "screens/shot-home.png", "screens/shot-home@2x.png", "screens/shot-home@3x.png", "screens/shot-insert.png", "screens/shot-insert@2x.png", "screens/shot-insert@3x.png", "screens/shot-mare-edit.png", "screens/shot-mare-edit2.png", "screens/shot-mare-edit2@2x.png", "screens/shot-mare-edit2@3x.png", "screens/shot-mare-edit3.png", "screens/shot-mare-edit3@2x.png", "screens/shot-mare-edit3@3x.png", "screens/shot-mare-edit@2x.png", "screens/shot-mare-edit@3x.png", "screens/shot-matrix.png", "screens/shot-matrix@2x.png", "screens/shot-matrix@3x.png", "screens/shot-palomino-edit.png", "screens/shot-palomino-edit2.png", "screens/shot-palomino-edit2@2x.png", "screens/shot-palomino-edit2@3x.png", "screens/shot-palomino-edit@2x.png", "screens/shot-palomino-edit@3x.png", "screens/shot-reverb.png", "screens/shot-reverb2.png", "screens/shot-reverb2@2x.png", "screens/shot-reverb2@3x.png", "screens/shot-reverb@2x.png", "screens/shot-reverb@3x.png", "screens/shot-rodeo-edit.png", "screens/shot-rodeo-edit2.png", "screens/shot-rodeo-edit2@2x.png", "screens/shot-rodeo-edit2@3x.png", "screens/shot-rodeo-edit@2x.png", "screens/shot-rodeo-edit@3x.png", "screens/shot-smk-keyat.png", "screens/shot-smk-keyat@2x.png", "screens/shot-smk-keyat@3x.png", "screens/shot-smk-padat.png", "screens/shot-smk-padat@2x.png", "screens/shot-smk-padat@3x.png", "screens/shot-stampede-edit.png", "screens/shot-stampede-edit2.png", "screens/shot-stampede-edit2@2x.png", "screens/shot-stampede-edit2@3x.png", "screens/shot-stampede-edit@2x.png", "screens/shot-stampede-edit@3x.png", "screens/shot-touch.png", "screens/shot-touch3.png", "screens/shot-touch3@2x.png", "screens/shot-touch3@3x.png", "screens/shot-touch@2x.png", "screens/shot-touch@3x.png", "screens/shot-touchcal.png", "screens/shot-touchcal@2x.png", "screens/shot-touchcal@3x.png", "stallion-pixel.ttf", "stallion.webmanifest"];                          // relative to this file
const KEEP = new Set(FILES.map((f) => new URL(f, self.location).href));

const key = (url) => {                              // the copy's name of a request: no query, index.html = the folder
  const u = new URL(url);
  u.search = "";
  u.hash = "";
  if (u.pathname.endsWith("/index.html")) u.pathname = u.pathname.slice(0, -10);
  return u.href;
};

self.addEventListener("install", (e) => {
  e.waitUntil((async () => {
    const c = await caches.open(CACHE);
    await c.addAll(FILES.map((f) => new Request(f, { cache: "reload" })));   // all or nothing: never half a site
    await self.skipWaiting();
  })());
});

self.addEventListener("activate", (e) => {
  e.waitUntil((async () => {
    for (const k of await caches.keys())
      if (k.startsWith("stallion-fm1-site-") && k !== CACHE) await caches.delete(k);
    await self.clients.claim();
  })());
});

self.addEventListener("fetch", (e) => {
  const req = e.request;
  if (req.method !== "GET") return;
  const k = key(req.url);
  if (!KEEP.has(k)) return;                         // not ours: the browser as usual
  e.respondWith((async () => {
    try {
      const r = await fetch(req.url, { cache: "no-cache" });   // online: what is published now (by its URL: a
                                                    // page's own request, mode navigate, takes no options)
      if (r.ok) {
        const copy = r.clone();
        e.waitUntil(caches.open(CACHE).then((c) => c.put(k, copy)).catch(() => {}));
      }
      return r;
    } catch (err) {                                 // offline: the copy
      const hit = await caches.match(k, { cacheName: CACHE });
      if (hit) return hit;
      throw err;
    }
  })());
});
