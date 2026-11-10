#!/usr/bin/env node
/* destructo's landmark voxeliser: an openly licensed 3D model (OBJ) to the grid of cells the masonry builder
 * (src/landmarks.c) lays as bricks.
 *
 *   node games/destructo/tools/voxelize.mjs --models <dir> [--only name,name] [--out games/destructo/src/lm_data.c]
 *
 * The models are not in the repository (games/destructo/CREDITS names each one, its author, licence and source);
 * --models is the directory that holds them, each as <dir>/<obj path in the config>. The settings for each landmark
 * are in tools/landmark-models.json:
 *
 *   obj        the model, relative to --models: Y up, metres, base on y = 0 (any scale: `size` rescales it)
 *   proc       instead of a model: a shape drawn in tools/landmark-procs.mjs; "only" / "except" (a regex on the
 *              paint) keep one part of it (a crag in big blocks, the building on it in small)
 *   stripe     { "every": n, "mat", "paint" }: every n-th course takes this (a tower's floors); coreMat: the core's
 *              material when it is not the first one the model names
 *   yaw        degrees about y applied first (the game's camera looks from +z toward -z)
 *   shear      x moved by this times the height (a lean the camera sees)
 *   yscale     the height stretched by this after `size` (a model flatter than the landmark)
 *   member     [courses, cells]: a welded member at most this tall and this wide (default [12, 6]; a slender tower
 *              needs tall members, every weld in a stack gives a little)
 *   weld       true: the cells are members welded to each other (statues, steel lattice, roofs on eaves) and need no
 *              support below; only cells joined to the ground stay ("connect": false keeps parts that rest on
 *              another part of the landmark, a walkway between two towers)
 *   size       { "axis": "x"|"y"|"z", "len": metres in the game }: the model's extent along that axis after yaw
 *   cell       the brick's width across (x and z), metres; course: its height
 *   fill       "flood": inside is whatever the outside cannot reach (closed models); "columns": each column filled
 *              from the ground up to its highest surface cell (statues, scans open underneath); "surface": the
 *              surface cells only
 *   wall       cells: keep only this many cells in from the outside (the rest is the core, or empty); 0 keeps all
 *   core       k: the inside further than `wall` from the outside is laid as a core of bricks k cells wide and k
 *              courses tall (fewer, bigger blocks the shell stands on); 0: no core (hollow)
 *   materials  [{ "match": regex on the OBJ material name, "mat": "stone"|"wood"|"glass"|"steel", "paint": P_ name
 *              without P_, or "auto" (the paint nearest the material's colour among "paints") }], first match wins;
 *              "default" for the rest
 *   bands      [{ "y0", "y1" (game metres), "mat", "paint" }]: cells in a height band take this instead
 *   ymin, ymax only the cells between these heights (game metres): a model made in two parts
 *   carve      [{ "box": [x0, y0, z0, x1, y1, z1] (game metres), "mat"?, "paint"? }]: cells in the box take this, or
 *              are emptied when neither is given (doors, a sunken court); with "top": [c, ax, az, wobble] only the
 *              cells above the plane y = c + ax x + az z (+ wobble sin(...)) in the box (a ruin's broken edge)
 *
 * Output: src/lm_data.c, one lm_vox per landmark: the grid's size and placement, a palette of (material, paint) and
 * the cells as runs (index order y, x, z) of (palette index + 1, 0 empty, 255 core in k-blocks, 254 core in
 * k/2-blocks; count up to 255). Grids sit on whole cells from the middle, so parts made apart line up. */
import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const sq = (x) => x * x;
const HERE = dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
const opt = (k, d) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : d; };
const MODELS = opt('--models');
const OUT = resolve(opt('--out', join(HERE, '../src/lm_data.c')));
const ONLY = opt('--only') ? opt('--only').split(',') : null;
const PROCS = (await import('./landmark-procs.mjs')).default;
const CONF = JSON.parse(readFileSync(join(HERE, 'landmark-models.json'), 'utf8'));
if (!MODELS) { console.error('--models <dir>: the directory holding the models (see CREDITS)'); process.exit(2); }

/* the game's paints (src/view.c PAINT), for "auto" */
const PAINTS = {
  ROOF_RED: [0.42, 0.1, 0.045], ROOF_SLATE: [0.075, 0.09, 0.125], ROOF_COPPER: [0.1, 0.36, 0.3], TRIM_CREAM: [0.72, 0.6, 0.4],
  TIMBER: [0.12, 0.065, 0.035], PLASTER: [0.72, 0.66, 0.54], GOLD: [1.0, 0.68, 0.12], TURQUOISE: [0.04, 0.42, 0.46], BRICK: [0.45, 0.15, 0.08],
  DARK_STONE: [0.2, 0.2, 0.23], SAND_STONE: [0.68, 0.5, 0.3], ROSE_STONE: [0.62, 0.38, 0.32], WHITE_STONE: [0.66, 0.63, 0.56],
  MARBLE: [0.8, 0.78, 0.74], LIMESTONE: [0.6, 0.55, 0.45], BRONZE: [0.13, 0.095, 0.07], DARK_GLASS: [0.05, 0.055, 0.065],
  TILE_WHITE: [0.84, 0.82, 0.77], RED_BRICK: [0.5, 0.21, 0.12], WHITE_PAINT: [0.76, 0.76, 0.74], BLUE_GLASS: [0.16, 0.26, 0.36],
  ROCK: [0.27, 0.25, 0.23], SILVER: [0.5, 0.53, 0.57], SARSEN: [0.46, 0.47, 0.42], VERDIGRIS: [0.36, 0.58, 0.5], CONCRETE: [0.58, 0.57, 0.54], SHELL_WHITE: [0.95, 0.94, 0.9], PODIUM: [0.4, 0.28, 0.2], IRON: [0.3, 0.19, 0.12],
};
const MATS = { wood: 'M_WOOD', stone: 'M_STONE', glass: 'M_GLASS', steel: 'M_STEEL' };

function loadObj(path) {
  const text = readFileSync(path, 'utf8');
  const V = [], tris = [], mtlColour = {};
  let mtl = '';
  const dir = dirname(path);
  for (const line of text.split('\n')) {
    const t = line.trim().split(/\s+/);
    if (t[0] === 'v') V.push([+t[1], +t[2], +t[3]]);
    else if (t[0] === 'usemtl') mtl = t.slice(1).join(' ');
    else if (t[0] === 'mtllib') {
      try {
        let cur = '';
        for (const l of readFileSync(join(dir, t.slice(1).join(' ')), 'utf8').split('\n')) {
          const u = l.trim().split(/\s+/);
          if (u[0] === 'newmtl') cur = u.slice(1).join(' ');
          else if (u[0] === 'Kd') mtlColour[cur] = [+u[1], +u[2], +u[3]];
        }
      } catch { /* no colours: names only */ }
    } else if (t[0] === 'f') {
      const ix = t.slice(1).map((s) => { const i = parseInt(s, 10); return i < 0 ? V.length + i : i - 1; });
      for (let k = 1; k + 1 < ix.length; k++) tris.push([ix[0], ix[k], ix[k + 1], mtl]);
    }
  }
  return { V, tris, mtlColour };
}

function nearestPaint(c, allowed) {
  let best = null, bd = 1e9;
  for (const n of allowed) {
    const p = PAINTS[n], d = (p[0] - c[0]) ** 2 + (p[1] - c[1]) ** 2 + (p[2] - c[2]) ** 2;
    if (d < bd) bd = d, best = n;
  }
  return best;
}

function voxelize(name, C) {
  const P = C.proc ? PROCS[C.proc] : null; /* a landmark shaped by hand (tools/landmark-procs.mjs): no model */
  if (C.proc && !P) throw new Error(`${name}: no proc ${C.proc}`);
  const { V, tris, mtlColour } = P ? { V: [[-P.ext[0] / 2, 0, -P.ext[2] / 2], [P.ext[0] / 2, P.ext[1], P.ext[2] / 2]], tris: [], mtlColour: {} } : loadObj(join(MODELS, C.obj));
  /* yaw, then scale to size, base on the ground, centred */
  const a = ((C.yaw || 0) * Math.PI) / 180, ca = Math.cos(a), sa = Math.sin(a);
  for (const v of V) { const x = v[0] * ca + v[2] * sa, z = -v[0] * sa + v[2] * ca; v[0] = x, v[2] = z; }
  const lo = [1e9, 1e9, 1e9], hi = [-1e9, -1e9, -1e9];
  for (const v of V) for (let k = 0; k < 3; k++) lo[k] = Math.min(lo[k], v[k]), hi[k] = Math.max(hi[k], v[k]);
  const ax = { x: 0, y: 1, z: 2 }[C.size.axis];
  const s = C.size.len / (hi[ax] - lo[ax]);
  const ys = s * (C.yscale || 1); /* a model flatter or taller than the landmark: its height stretched */
  const cx = (lo[0] + hi[0]) / 2, cz = (lo[2] + hi[2]) / 2;
  for (const v of V) v[0] = (v[0] - cx) * s, v[1] = (v[1] - lo[1]) * ys, v[2] = (v[2] - cz) * s;
  if (C.shear) for (const v of V) v[0] += v[1] * C.shear; /* leaning further (the camera sees the lean across, x) */
  const ext = [(hi[0] - lo[0]) * s, (hi[1] - lo[1]) * ys, (hi[2] - lo[2]) * s];
  const cell = C.cell, course = C.course, K = Math.max(1, C.core || 0);
  /* the grid: whole cores across (a multiple of k cells), centred */
  const roundK = (n) => Math.ceil(n / K) * K;
  const nx = roundK(Math.ceil(ext[0] / cell) + 2), nz = roundK(Math.ceil(ext[2] / cell) + 2), ny = roundK(Math.ceil(ext[1] / course));
  const x0 = -Math.floor(nx / 2) * cell, z0 = -Math.floor(nz / 2) * cell; /* on whole cells from the middle: parts made apart line up */
  const N = nx * ny * nz, at = (i, j, k) => (j * nx + i) * nz + k;
  const surf = new Int32Array(N).fill(-1); /* the material (index into names) of a surface cell */
  const names = [];
  const mi = new Map();
  /* the surface: points over each triangle closer than a third of a cell */
  const step = Math.min(cell, course) / 3;
  for (const [p, q, r, m] of tris) {
    if (!mi.has(m)) mi.set(m, names.length), names.push(m);
    const id = mi.get(m);
    const A = V[p], B = V[q], Cc = V[r];
    const len = Math.max(Math.hypot(B[0] - A[0], B[1] - A[1], B[2] - A[2]), Math.hypot(Cc[0] - A[0], Cc[1] - A[1], Cc[2] - A[2]), Math.hypot(Cc[0] - B[0], Cc[1] - B[1], Cc[2] - B[2]));
    const n = Math.max(1, Math.ceil(len / step));
    for (let u = 0; u <= n; u++)
      for (let w = 0; w <= n - u; w++) {
        const bu = u / n, bw = w / n, bv = 1 - bu - bw;
        const x = A[0] * bv + B[0] * bu + Cc[0] * bw, y = A[1] * bv + B[1] * bu + Cc[1] * bw, z = A[2] * bv + B[2] * bu + Cc[2] * bw;
        const i = Math.floor((x - x0) / cell), j = Math.floor(y / course), k = Math.floor((z - z0) / cell);
        if (i >= 0 && i < nx && j >= 0 && j < ny && k >= 0 && k < nz) surf[at(i, j, k)] = id;
      }
  }
  if (P) { /* the hand shape: every cell its own material, solid as it says */
    for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) for (let k = 0; k < nz; k++) {
      const m = P.cell(x0 + (i + 0.5) * cell, (j + 0.5) * course, z0 + (k + 0.5) * cell);
      if (!m || (C.only && !new RegExp(':(' + C.only + ')$').test(m)) || (C.except && new RegExp(':(' + C.except + ')$').test(m))) continue; /* one part of a hand shape (a crag in big blocks, the building on it in small) */
      if (!mi.has(m)) mi.set(m, names.length), names.push(m);
      surf[at(i, j, k)] = mi.get(m);
    }
  }
  /* inside */
  const solid = new Int32Array(N).fill(-1);
  for (let c = 0; c < N; c++) solid[c] = surf[c];
  const fill = P ? 'surface' : C.fill || 'flood';
  if (fill === 'flood') {
    const out = new Uint8Array(N), st = [];
    const push = (i, j, k) => { if (i < 0 || j < 0 || k < 0 || i >= nx || j >= ny || k >= nz) return; const c = at(i, j, k); if (out[c] || surf[c] >= 0) return; out[c] = 1, st.push(c); };
    for (let i = 0; i < nx; i++) for (let k = 0; k < nz; k++) push(i, ny - 1, k);
    for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) push(i, j, 0), push(i, j, nz - 1);
    for (let j = 0; j < ny; j++) for (let k = 0; k < nz; k++) push(0, j, k), push(nx - 1, j, k);
    while (st.length) {
      const c = st.pop(), k = c % nz, i = Math.floor(c / nz) % nx, j = Math.floor(c / (nz * nx));
      push(i + 1, j, k), push(i - 1, j, k), push(i, j + 1, k), push(i, j - 1, k), push(i, j, k + 1), push(i, j, k - 1);
    }
    /* inside cells take the material of the surface nearest above them, else below */
    for (let i = 0; i < nx; i++) for (let k = 0; k < nz; k++) {
      let m = -1;
      for (let j = ny - 1; j >= 0; j--) { const c = at(i, j, k); if (surf[c] >= 0) m = surf[c]; else if (!out[c] && m >= 0) solid[c] = m; }
    }
  } else if (fill === 'columns') {
    for (let i = 0; i < nx; i++) for (let k = 0; k < nz; k++) {
      let m = -1;
      for (let j = ny - 1; j >= 0; j--) { const c = at(i, j, k); if (surf[c] >= 0) m = surf[c]; else if (m >= 0) solid[c] = m; }
    }
  }
  /* depth: cells from the outside (6-connected), to keep a wall and make the core */
  const depth = new Int32Array(N).fill(1 << 20);
  let front = [];
  for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) for (let k = 0; k < nz; k++) {
    const c = at(i, j, k);
    if (solid[c] < 0) continue;
    let edge = false;
    for (const [di, dj, dk] of [[1, 0, 0], [-1, 0, 0], [0, 1, 0], [0, -1, 0], [0, 0, 1], [0, 0, -1]]) {
      const ii = i + di, jj = j + dj, kk = k + dk;
      if (jj < 0) continue; /* the ground is not outside */
      if (ii < 0 || kk < 0 || ii >= nx || jj >= ny || kk >= nz || solid[at(ii, jj, kk)] < 0) { edge = true; break; }
    }
    if (edge) depth[c] = 1, front.push(c);
  }
  for (let d = 1; front.length; d++) {
    const next = [];
    for (const c of front) {
      const k = c % nz, i = Math.floor(c / nz) % nx, j = Math.floor(c / (nz * nx));
      for (const [di, dj, dk] of [[1, 0, 0], [-1, 0, 0], [0, 1, 0], [0, -1, 0], [0, 0, 1], [0, 0, -1]]) {
        const ii = i + di, jj = j + dj, kk = k + dk;
        if (ii < 0 || jj < 0 || kk < 0 || ii >= nx || jj >= ny || kk >= nz) continue;
        const e = at(ii, jj, kk);
        if (solid[e] >= 0 && depth[e] > d + 1) depth[e] = d + 1, next.push(e);
      }
    }
    front = next;
  }
  /* the palette */
  const pal = [], pk = new Map();
  const code = (mat, paint) => { const key = mat + ':' + paint; if (!pk.has(key)) pk.set(key, pal.length), pal.push([mat, paint]); return pk.get(key) + 1; };
  if (C.coreMat) code(C.coreMat.mat, C.coreMat.paint); /* the core's material (palette entry 0) when not the first the model names */
  const rule = (mname) => {
    for (const r of C.materials || []) if (new RegExp(r.match, 'i').test(mname)) return r;
    return C.default || { mat: 'stone', paint: 'NONE' };
  };
  const paintOf = (r, mname) => (r.paint === 'auto' ? nearestPaint(mtlColour[mname] || [0.6, 0.6, 0.6], r.paints || Object.keys(PAINTS)) : r.paint || 'NONE');
  const mcode = names.map((m) => { if (P) { const [mt, pt] = m.split(':'); return code(mt, pt); } const r = rule(m); return code(r.mat, paintOf(r, m)); });
  const cells = new Uint8Array(N), shellCode = new Uint8Array(N), done = new Uint8Array(N);
  const wall = C.wall || 0;
  for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) for (let k = 0; k < nz; k++) {
    const c = at(i, j, k);
    if (solid[c] < 0) continue;
    const y = (j + 0.5) * course, x = x0 + (i + 0.5) * cell, z = z0 + (k + 0.5) * cell;
    let v = mcode[solid[c]];
    for (const b of C.bands || []) if (y >= b.y0 && y < b.y1) v = code(b.mat, b.paint || 'NONE');
    if (C.stripe && j % C.stripe.every === 0) v = code(C.stripe.mat, C.stripe.paint); /* a band every so many courses (a tower's floors) */
    let carved = false;
    for (const b of C.carve || []) {
      const [ax0, ay0, az0, ax1, ay1, az1] = b.box;
      const above = b.top ? y > b.top[0] + b.top[1] * x + b.top[2] * z + (b.top[3] || 0) * Math.sin(x * 2.7 + z * 1.9) : true; /* a ruin's broken top */
      const inEll = b.ellipse ? sq((x - b.ellipse[0]) / b.ellipse[2]) + sq((z - b.ellipse[1]) / b.ellipse[3]) < 1 : true; /* an oval court */
      if (x >= ax0 && x < ax1 && y >= ay0 && y < ay1 && z >= az0 && z < az1 && above && inEll) {
        if (!b.mat && !b.paint) carved = true; else v = code(b.mat || pal[v - 1][0], b.paint || pal[v - 1][1]);
      }
    }
    if (carved) continue;
    if ((C.ymin != null && y < C.ymin) || (C.ymax != null && y >= C.ymax)) continue; /* one height of a model (its masonry below, its welded dome above) */
    shellCode[c] = v;
    if (C.weld && C.coreMat && wall > 0 && depth[c] > wall) v = code(C.coreMat.mat, C.coreMat.paint); /* a welded tower's inside: one material, so its members run the whole height (stiff), its skin banded */
    else if (wall > 0 && depth[c] > wall) v = 255; /* inside: the core, or nothing */
    cells[c] = v;
  }
  /* the core: whole k-blocks of inside cells (255), then whole k/2-blocks of what is left (254); the rest of the
   * inside is shell (a core) or empty (no core: hollow) */
  let ncore = 0;
  if (wall > 0) {
    for (const [KK, mark] of K >= 4 ? [[K, 255], [K / 2, 254]] : [[K, 255]])
      for (let j = 0; j < ny; j += KK) for (let i = 0; i < nx; i += KK) for (let k = 0; k < nz; k += KK) {
        let all = C.core > 0;
        for (let a = 0; a < KK && all; a++) for (let b = 0; b < KK && all; b++) for (let d = 0; d < KK && all; d++) all = cells[at(i + b, j + a, k + d)] === 255 && !done[at(i + b, j + a, k + d)];
        if (!all) continue;
        for (let a = 0; a < KK; a++) for (let b = 0; b < KK; b++) for (let d = 0; d < KK; d++) { const c = at(i + b, j + a, k + d); done[c] = 1, cells[c] = mark; }
        ncore++;
      }
    for (let c = 0; c < N; c++) if (cells[c] === 255 && !done[c]) cells[c] = C.core > 0 ? shellCode[c] : 0;
  }
  /* welded lattice: only the cells joined to the ground through others (26 neighbours) stay */
  if (C.weld && C.connect !== false) {
    const seen = new Uint8Array(N), st = [];
    let j0 = 0; /* the lowest layer: the ground, or what it stands on (a podium laid apart) */
    while (j0 < ny - 1) { let any = false; for (let i = 0; i < nx && !any; i++) for (let k = 0; k < nz && !any; k++) any = cells[at(i, j0, k)] > 0; if (any) break; j0++; }
    for (let i = 0; i < nx; i++) for (let k = 0; k < nz; k++) { const c = at(i, j0, k); if (cells[c]) seen[c] = 1, st.push(c); }
    while (st.length) {
      const c = st.pop(), k = c % nz, i = Math.floor(c / nz) % nx, j = Math.floor(c / (nz * nx));
      for (let dj = -1; dj <= 1; dj++) for (let di = -1; di <= 1; di++) for (let dk = -1; dk <= 1; dk++) {
        const ii = i + di, jj = j + dj, kk = k + dk;
        if (ii < 0 || jj < 0 || kk < 0 || ii >= nx || jj >= ny || kk >= nz) continue;
        const e = at(ii, jj, kk);
        if (cells[e] && !seen[e]) seen[e] = 1, st.push(e);
      }
    }
    let cut = 0;
    for (let c = 0; c < N; c++) if (cells[c] && !seen[c]) cells[c] = 0, cut++;
    if (cut) console.error(`${name}: ${cut} cells not joined to the ground left out`);
  }
  /* runs */
  const runs = [];
  for (let c = 0; c < N;) {
    const v = cells[c];
    let n = 1;
    while (c + n < N && cells[c + n] === v && n < 255) n++;
    runs.push(v, n);
    c += n;
  }
  let nshell = 0;
  for (let c = 0; c < N; c++) nshell += cells[c] > 0 && cells[c] < 254;
  /* welded: the members the game will merge the cells into (src/landmarks.c build_vox: the longest box of one paint up the
   * column, then as wide as that holds, up to 12 courses by 6 by 6 cells), so the body count is known here */
  const [MH, MW] = C.member || [12, 6]; /* a welded member at most this many courses tall and cells across (taller for a slender tower: every weld in a stack gives a little, and a tower of short members sags) */
  let members = 0;
  if (C.weld) {
    const used = new Uint8Array(N);
    for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) for (let k = 0; k < nz; k++) {
      const c0 = cells[at(i, j, k)];
      if (!c0 || c0 >= 254 || used[at(i, j, k)]) continue;
      let h = 1, w = 1, d = 1;
      while (j + h < ny && h < MH && cells[at(i, j + h, k)] === c0 && !used[at(i, j + h, k)]) h++;
      for (let grow = true; grow && i + w < nx && w < MW;) { for (let t = 0; t < h && grow; t++) grow = cells[at(i + w, j + t, k)] === c0 && !used[at(i + w, j + t, k)]; if (grow) w++; }
      for (let grow = true; grow && k + d < nz && d < MW;) { for (let t = 0; t < h && grow; t++) for (let s2 = 0; s2 < w && grow; s2++) grow = cells[at(i + s2, j + t, k + d)] === c0 && !used[at(i + s2, j + t, k + d)]; if (grow) d++; }
      for (let t = 0; t < h; t++) for (let s2 = 0; s2 < w; s2++) for (let u = 0; u < d; u++) used[at(i + s2, j + t, k + u)] = 1;
      members++;
    }
  }
  console.error(`${name}: ${nx} x ${ny} x ${nz} cells of ${cell} x ${course} m, ${ext.map((e) => e.toFixed(1)).join(' x ')} m; ${nshell} shell cells (${C.weld ? members + ' welded members' : 'about ' + Math.round(nshell / 2.4) + ' bricks'}), ${ncore} core blocks; ${tris.length} triangles; ${runs.length / 2} runs; materials: ${names.join(', ')}`);
  return { name, nx, ny, nz, cell, course, x0, z0, K: C.core || 0, weld: C.weld ? 1 : 0, mh: MH, mw: MW, pal, runs };
}

const out = [];
for (const [name, C] of Object.entries(CONF)) {
  if (name.startsWith('//')) continue;
  if (ONLY && !ONLY.includes(name)) continue;
  out.push(voxelize(name, C));
}
/* keep the landmarks not re-made this run */
let keep = '';
if (ONLY) {
  try {
    const prev = readFileSync(OUT, 'utf8');
    for (const m of prev.matchAll(/\/\* begin (\w+) \*\/[\s\S]*?\/\* end \1 \*\/\n/g)) if (!ONLY.includes(m[1]) && CONF[m[1]]) keep += m[0];
  } catch { /* first run */ }
}
let c = `/* GENERATED by tools/voxelize.mjs from tools/landmark-models.json: do not edit. The models each landmark is made\n * from, their authors and licences are in CREDITS. */\n#include "lm_data.h"\n\n` + keep;
for (const v of out) {
  c += `/* begin ${v.name} */\nstatic const uint16_t ${v.name}_pal[] = { ${v.pal.map(([m, p]) => `LMV_CELL(${MATS[m]}, P_${p})`).join(', ')} };\n`;
  c += `static const uint8_t ${v.name}_runs[] = {`;
  for (let i = 0; i < v.runs.length; i++) c += (i % 24 ? ' ' : '\n  ') + v.runs[i] + ',';
  c += `\n};\nconst lm_vox LMV_${v.name} = { ${v.nx}, ${v.ny}, ${v.nz}, ${v.cell}f, ${v.course}f, ${v.x0.toFixed(4)}f, ${v.z0.toFixed(4)}f, ${v.K}, ${v.weld}, ${v.mh}, ${v.mw}, ${v.pal.length}, ${v.name}_pal, ${v.runs.length / 2}, ${v.name}_runs };\n/* end ${v.name} */\n`;
}
writeFileSync(OUT, c);
const names = Object.keys(CONF).filter((n) => !n.startsWith('//'));
writeFileSync(OUT.replace(/\.c$/, '.h'), `/* GENERATED by tools/voxelize.mjs: do not edit */\n#ifndef DESTRUCTO_LM_DATA_H\n#define DESTRUCTO_LM_DATA_H\n#include <stdint.h>\n#include "game.h"\n/* a landmark as cells (tools/voxelize.mjs): runs over the grid in index order y, x, z of (0 empty, 255 core in k-blocks,\n * 254 core in k/2-blocks, else the palette entry + 1, then the count) */\ntypedef struct lm_vox {\n  int nx, ny, nz;\n  float cell, course, x0, z0;\n  int core;\n  int weld;\n  int mh, mw;  /* a welded member at most this many courses tall and cells across */\n  int npal;\n  const uint16_t *pal;\n  int nruns;\n  const uint8_t *runs;\n} lm_vox;\n#define LMV_CELL(m, p) ((uint16_t)(((p) << 8) | ((m) + 1)))\n` + names.map((n) => `extern const lm_vox LMV_${n};\n`).join('') + `#endif\n`);
console.error('wrote', OUT);
