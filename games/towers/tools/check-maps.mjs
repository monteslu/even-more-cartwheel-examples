#!/usr/bin/env node
// The map check (CI): every map in assets/maps must
//   parse      be a valid .cwtd (the records map.c reads, every position on its grid)
//   path       let every gate reach the core along the path (its 2 x 2 block), with the gates on the map's edge
//   slots      have every 'o' tile a slot and every slot an 'o', beside the path, reaching at least 10 path cells
//              within 7 m, and no two slots touching
//   shares     deal the slots fairly for two, three and four players: every seat at least four slots, and each
//              seat's reach within 30% of the mean
//   grounded   stand every prop on flat ground: the lowest point of every decor and lamp frame at the floor (1 cm),
//              and the ground under its footprint (the model's reach, the heightfield as mapbuild.c builds it) flat at
//              the ground's height, so nothing floats over a bank or sinks into one
//   terrain    keep its rock outcrops and pools off the path and the pads (the heightfield flat over every one)
//   bake       have its bake: lightmaps/<n>.cwlm present and lightmaps/<n>.hash equal to the map's bake hash (a map
//              or a kit changed after its bake fails)
//   clear      be held by the bots in the soak build (--soak <towers_soak>): two bots from the start hold the core
//              through every wave with the game's rules
// The controls (tools/maps-broken/*.cwtd, each broken on purpose in one way, named <check>-<what>.cwtd) must each
// fail the check its name says; a check that cannot fail is not a check, so the run fails when a control passes.
//
//   node games/towers/tools/check-maps.mjs [--soak <path>] [--only 1,2] [--jobs n] [--no-controls]
import { existsSync, readFileSync, readdirSync } from 'node:fs';
import { dirname, join, basename } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawn } from 'node:child_process';
import { cpus } from 'node:os';

const GAME = join(dirname(fileURLToPath(import.meta.url)), '..');
const A = join(GAME, 'assets');
const BROKEN = join(GAME, 'tools/maps-broken');
const argv = process.argv.slice(2);
const opt = (k) => { const i = argv.indexOf(k); return i >= 0 ? argv[i + 1] : null; };
const SOAK = opt('--soak');
const ONLY = opt('--only') ? new Set(opt('--only').split(',').map(Number)) : null;
const JOBS = Number(opt('--jobs') || Math.max(1, cpus().length - 2));
const CONTROLS = !argv.includes('--no-controls');
const THEMES = ['meadow', 'autumn', 'desert', 'snow', 'volcano', 'crystal'];
const KINDS = ['skitter', 'ghoul', 'hound', 'imp', 'sporeling', 'bloater', 'shade', 'ogre'];
const TILE = 2, GROUND_H = 0.5, RES = 4, MARGIN = 3;

// ── the format (map.c) ──────────────────────────────────────────────
function parse(text) {
  const lines = text.split('\n').map((l) => l.replace(/[\s\r]+$/, ''));
  const m = { grid: [], spawn: [], slot: [], prop: [], light: [], kinds: [], feat: [] };
  let i = 0;
  const next = () => lines[i++];
  while (i < lines.length && (!lines[i] || lines[i][0] === '#')) i++;
  if (next() !== 'towers-map 1') throw new Error('no towers-map header');
  while (i < lines.length) {
    const ln = next();
    if (!ln || ln[0] === '#') continue;
    const w = ln.trim().split(/\s+/), k = w[0], n = w.slice(1).map(Number);
    if (k === 'map') m.num = n[0];
    else if (k === 'name') m.name = ln.slice(5);
    else if (k === 'theme') { m.theme = w[1]; if (!THEMES.includes(m.theme)) throw new Error(`unknown theme ${w[1]}`); }
    else if (k === 'size') { [m.w, m.h] = n; if (!(m.w >= 8 && m.h >= 6 && m.w <= 40 && m.h <= 24)) throw new Error('bad size'); }
    else if (k === 'seed') m.seed = n[0];
    else if (k === 'waves') { m.waves = n[0]; if (!(m.waves >= 1 && m.waves <= 60)) throw new Error('bad waves'); }
    else if (k === 'diff') { m.diff = n[0]; if (!(m.diff > 0.05 && m.diff < 20)) throw new Error('bad diff'); }
    else if (k === 'kinds') { m.kinds = w.slice(1); for (const q of m.kinds) if (!KINDS.includes(q)) throw new Error(`unknown creep ${q}`); }
    else if (k === 'grid') {
      if (!m.w) throw new Error('grid before size');
      for (let y = 0; y < m.h; y++) {
        const row = next();
        if (row === undefined || row.length !== m.w) throw new Error(`grid row ${y} is not ${m.w} wide`);
        if (/[^.go#~]/.test(row)) throw new Error(`grid row ${y} has an unknown tile`);
        m.grid.push(row);
      }
      if (next() !== 'end') throw new Error('the grid must close with end');
    } else if (k === 'spawn') m.spawn.push(n);
    else if (k === 'core') m.core = n;
    else if (k === 'slot') { if (n.length !== 5 || n[2] > 1 || n[3] > 2 || n[4] > 3) throw new Error(`bad slot ${ln}`); m.slot.push({ x: n[0], y: n[1], own: [0, n[2], n[3], n[4]] }); }
    else if (k === 'prop') { if (!['decor', 'lamp'].includes(w[3])) throw new Error(`unknown prop ${w[3]}`); m.prop.push({ x: +w[1], y: +w[2], kind: w[3], frame: +w[4], turn: +w[5] & 3 }); }
    else if (k === 'feature') { if (!['rock', 'water'].includes(w[1]) || w.length !== 6 || !(+w[4] > 0.2 && +w[4] < 12)) throw new Error(`bad feature ${ln}`); m.feat.push({ kind: w[1], x: +w[2], y: +w[3], r: +w[4], h: +w[5] }); }
    else if (k === 'light') m.light.push(n);
    else throw new Error(`unknown record ${k}`);
  }
  if (!m.theme || !m.grid.length || !m.spawn.length || !m.core || !m.slot.length || !m.kinds.length) throw new Error('missing theme, grid, spawn, core, slots or kinds');
  const on = (x, y) => x >= 0 && y >= 0 && x < m.w && y < m.h;
  for (const [x, y] of m.spawn) {
    const side = x === 0 || x === m.w - 1, top = y === 0 || y === m.h - 1;
    if (!side && !top) throw new Error(`a spawn at ${x} ${y} is not on the map's edge`);
    const [x2, y2] = side ? [x, y + 1] : [x + 1, y];
    if (!on(x, y) || !on(x2, y2) || m.grid[y][x] !== '.' || m.grid[y2][x2] !== '.') throw new Error(`a spawn at ${x} ${y} does not open on two path tiles`);
  }
  const [cx, cy] = m.core;
  for (let k = 0; k < 4; k++) if (!on(cx + (k & 1), cy + (k >> 1)) || m.grid[cy + (k >> 1)][cx + (k & 1)] !== '.') throw new Error('the core does not stand on a 2 x 2 block of path');
  for (const s of m.slot) if (!on(s.x, s.y) || m.grid[s.y][s.x] !== 'o') throw new Error(`a slot at ${s.x} ${s.y} is not an 'o' tile`);
  for (const p of m.prop) if (!on(p.x, p.y)) throw new Error(`a prop at ${p.x} ${p.y} is off the grid`);
  for (const l of m.light) if (!on(l[0], l[1])) throw new Error(`a light at ${l[0]} ${l[1]} is off the grid`);
  return m;
}

// ── the bake hash (map.c map_bake_hash, mapbuild.c mapbuild_bake_hash) ──
const FNV = 0xcbf29ce484222325n, PRIME = 0x100000001b3n, MASK = (1n << 64n) - 1n;
const fold = (h, bytes) => { for (const c of bytes) h = ((h ^ BigInt(c)) * PRIME) & MASK; return h; };
function bakeHash(text, theme, models) {
  let h = FNV, grid = false, rows = 0, hgt = 0;
  for (const raw of text.split('\n')) {
    const ln = raw.replace(/[ \r\t]+$/, ''), s = ln.replace(/^[ \t]+/, '');
    let take = false;
    if (grid) { if (s === 'end') grid = false; else take = rows++ < hgt; if (!take) continue; }
    else if (s === 'grid') { grid = true; continue; }
    else if (s.startsWith('size ')) { hgt = +s.split(/\s+/)[2]; take = true; }
    else take = s.startsWith('theme ') || s.startsWith('prop ') || s.startsWith('light ') || s.startsWith('feature ');
    if (!take) continue;
    h = fold(h, Buffer.from(s, 'latin1'));
    h = fold(h, [10]);
  }
  for (const f of [`decor_${theme}.cwchar`, 'lamp.cwchar']) {
    const p = join(models, f), b = existsSync(p) ? readFileSync(p) : Buffer.alloc(0);
    h = fold(h, b);
    h = ((h ^ BigInt(b.length)) * PRIME) & MASK;
  }
  return h.toString(16).padStart(16, '0');
}

// ── .cwchar bounds per frame (group 0 lowest point; the footprint over every group) ──
const chrCache = new Map();
function chrFrames(path) {
  if (chrCache.has(path)) return chrCache.get(path);
  let out = null;
  if (existsSync(path)) {
    const b = readFileSync(path);
    if (b.toString('latin1', 0, 4) === 'CWCH') {
      const u32 = (o) => b.readUInt32LE(o), nclip = u32(8), ngroup = u32(12);
      let p = 16, total = 0;
      for (let k = 0; k < nclip; k++, p += 4) total += u32(p);
      const fr = Array.from({ length: total }, () => ({ minY: Infinity, reach: 0 }));
      for (let g = 0; g < ngroup; g++) {
        const nv = u32(p), nt = u32(p + 4);
        p += 8 + 20 * nv + ((nt * 6 + 3) & ~3);
        for (let f = 0; f < total; f++) {
          for (let v = 0; v < nv; v++) {
            const x = b.readFloatLE(p + v * 12), y = b.readFloatLE(p + v * 12 + 4), z = b.readFloatLE(p + v * 12 + 8);
            if (g === 0 && y < fr[f].minY) fr[f].minY = y;
            if (y < 0.15) fr[f].reach = Math.max(fr[f].reach, Math.hypot(x, z)); /* what touches the ground */
          }
          p += 24 * nv;
        }
      }
      out = fr;
    }
  }
  chrCache.set(path, out);
  return out;
}

// ── the heightfield (mapbuild.c vert_h without the rock's roughness: props never stand on rock) ──
function heightfield(m) {
  const tileAt = (x, y) => {
    const cx = Math.min(Math.max(x, 0), m.w - 1), cy = Math.min(Math.max(y, 0), m.h - 1);
    let t = m.grid[cy][cx];
    const out = cx !== x || cy !== y;
    if (out && t === 'o') t = 'g';
    if (out && t === '.') {
      const xo = x < 0 || x >= m.w, yo = y < 0 || y >= m.h;
      const across = xo ? y === cy : x === cx;
      if (!across || xo === yo) t = 'g';
    }
    return t;
  };
  const kh = (t) => (t === '.' ? 0 : t === '~' ? -0.4 : t === '#' ? 1.6 : GROUND_H);
  const vh = (i, j) => {
    let tx0 = Math.floor((i - 1) / RES) - MARGIN, tx1 = Math.floor(i / RES) - MARGIN;
    let ty0 = Math.floor((j - 1) / RES) - MARGIN, ty1 = Math.floor(j / RES) - MARGIN;
    if (i % RES) tx0 = tx1;
    if (j % RES) ty0 = ty1;
    let h = Infinity;
    for (let y = ty0; y <= ty1; y++) for (let x = tx0; x <= tx1; x++) h = Math.min(h, kh(tileAt(x, y)));
    if (h > 0.01) { // a notch in a curve of path cut on the diagonal (mapbuild.c)
      const fx = i / RES - MARGIN, fy = j / RES - MARGIN;
      for (let y = ty0; y <= ty1; y++) for (let x = tx0; x <= tx1; x++) {
        if (tileAt(x, y) !== 'g') continue;
        for (const [dx, dy] of [[1, -1], [1, 1], [-1, 1], [-1, -1]]) {
          if (tileAt(x + dx, y) !== '.' || tileAt(x, y + dy) !== '.' || tileAt(x + dx, y + dy) !== '.') continue;
          const sd = Math.abs(fx - x - (dx > 0 ? 1 : 0)) + Math.abs(fy - y - (dy > 0 ? 1 : 0));
          h = Math.min(h, GROUND_H * Math.min(1, Math.max(0, (sd - 0.5) / Math.SQRT2 / 0.25)));
        }
      }
    }
    // the features (mapbuild.c feat_at): the highest outcrop dome added on ground, the deepest pool cut down
    const px = i / RES - MARGIN, py = j / RES - MARGIN, bump = (t) => (t >= 1 ? 0 : 0.5 + 0.5 * Math.cos(Math.PI * t));
    let rock = 0, water = 0;
    for (const q of m.feat) { const b = bump(Math.hypot(px - q.x, py - q.y) / q.r); if (q.kind === 'rock') rock = Math.max(rock, q.h * b); else water = Math.max(water, b); }
    if (h >= GROUND_H - 0.01) h += rock;
    return water > 0 ? Math.min(h, GROUND_H - (GROUND_H + 0.6) * water) : h;
  };
  return (x, z) => {
    const fi = (x / TILE + m.w / 2 + MARGIN) * RES, fj = (z / TILE + m.h / 2 + MARGIN) * RES;
    const i = Math.floor(fi), j = Math.floor(fj), u = fi - i, v = fj - j;
    return (vh(i, j) * (1 - u) + vh(i + 1, j) * u) * (1 - v) + (vh(i, j + 1) * (1 - u) + vh(i + 1, j + 1) * u) * v;
  };
}
// where a prop stands (mapbuild.c prop_place)
function propPlace(m, p) {
  let x = (p.x - m.w / 2 + 0.5) * TILE, z = (p.y - m.h / 2 + 0.5) * TILE;
  if (p.kind === 'lamp') {
    const DX = [0, 1, 0, -1], DY = [-1, 0, 1, 0];
    for (let d = 0; d < 4; d++) {
      const q = (d + p.turn) & 3, nx = p.x + DX[q], ny = p.y + DY[q];
      if (nx >= 0 && ny >= 0 && nx < m.w && ny < m.h && m.grid[ny][nx] === '.') { x += DX[q] * 0.25; z += DY[q] * 0.25; break; }
    }
  }
  return [x, z];
}

// ── the checks ─────────────────────────────────────────────────────
function checkMap(text, { bakeDir = join(A, 'lightmaps'), models = join(A, 'models') } = {}) {
  const fails = [];
  let m;
  try { m = parse(text); } catch (e) { return { fails: [{ check: 'parse', msg: e.message }] }; }
  const at = (x, y) => (x >= 0 && y >= 0 && x < m.w && y < m.h ? m.grid[y][x] : '#');
  // path
  const seen = new Set([`${m.core[0]},${m.core[1]}`]), q = [m.core];
  while (q.length) {
    const [x, y] = q.shift();
    for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
      const k = `${x + dx},${y + dy}`;
      if (at(x + dx, y + dy) === '.' && !seen.has(k)) seen.add(k), q.push([x + dx, y + dy]);
    }
  }
  for (const [x, y] of m.spawn) if (!seen.has(`${x},${y}`)) fails.push({ check: 'path', msg: `the gate at ${x} ${y} does not reach the core` });
  // slots
  const reach = (sx, sy) => {
    let c = 0;
    for (let ty = 0; ty < m.h; ty++) for (let tx = 0; tx < m.w; tx++) {
      if (m.grid[ty][tx] !== '.') continue;
      for (let a = 0; a < 2; a++) for (let b = 0; b < 2; b++) {
        const dx = tx * 2 + a + 0.5 - (sx * 2 + 1), dy = ty * 2 + b + 0.5 - (sy * 2 + 1);
        c += dx * dx + dy * dy < 49;
      }
    }
    return c;
  };
  const slotAt = new Set(m.slot.map((s) => `${s.x},${s.y}`));
  for (let y = 0; y < m.h; y++) for (let x = 0; x < m.w; x++) if (m.grid[y][x] === 'o' && !slotAt.has(`${x},${y}`)) fails.push({ check: 'slots', msg: `the 'o' at ${x} ${y} has no slot record` });
  for (const s of m.slot) {
    s.r = reach(s.x, s.y);
    let beside = false;
    for (let b = -1; b <= 1; b++) for (let a = -1; a <= 1; a++) beside ||= at(s.x + a, s.y + b) === '.';
    if (!beside) fails.push({ check: 'slots', msg: `the slot at ${s.x} ${s.y} is not beside the path` });
    if (s.r < 10) fails.push({ check: 'slots', msg: `the slot at ${s.x} ${s.y} reaches only ${s.r} path cells` });
    for (const o of m.slot) if (o !== s && Math.max(Math.abs(o.x - s.x), Math.abs(o.y - s.y)) < 2 && (o.x < s.x || (o.x === s.x && o.y < s.y))) fails.push({ check: 'slots', msg: `the slots at ${s.x} ${s.y} and ${o.x} ${o.y} touch` });
  }
  // shares
  for (const np of [2, 3, 4]) {
    const sum = Array(np).fill(0), cnt = Array(np).fill(0);
    for (const s of m.slot) sum[s.own[np - 1]] += s.r, cnt[s.own[np - 1]]++;
    const mean = sum.reduce((a, b) => a + b) / np;
    for (let p = 0; p < np; p++) {
      if (cnt[p] < 4) fails.push({ check: 'shares', msg: `with ${np} players seat ${p + 1} has ${cnt[p]} slots` });
      if (Math.abs(sum[p] - mean) > mean * 0.3) fails.push({ check: 'shares', msg: `with ${np} players seat ${p + 1} reaches ${sum[p]} (the mean ${mean.toFixed(0)})` });
    }
  }
  // grounded
  const hf = heightfield(m);
  for (const p of m.prop) {
    if (at(p.x, p.y) !== 'g') { fails.push({ check: 'grounded', msg: `a ${p.kind} at ${p.x} ${p.y} stands on '${at(p.x, p.y)}', not ground` }); continue; }
    const fr = chrFrames(join(models, p.kind === 'lamp' ? 'lamp.cwchar' : `decor_${m.theme}.cwchar`));
    if (!fr) { fails.push({ check: 'grounded', msg: `no model for a ${p.kind}` }); continue; }
    const f = fr[p.kind === 'lamp' ? 0 : p.frame % fr.length];
    if (Math.abs(f.minY) > 0.01) fails.push({ check: 'grounded', msg: `the ${p.kind} model's frame ${p.frame} has its lowest point at ${f.minY.toFixed(3)}` });
    const [x, z] = propPlace(m, p), r = f.reach * 1.1;
    for (let k = 0; k <= 16; k++) {
      const a = (k / 16) * Math.PI * 2, px = k === 16 ? x : x + Math.cos(a) * r, pz = k === 16 ? z : z + Math.sin(a) * r;
      const h = hf(px, pz);
      if (Math.abs(h - GROUND_H) > 0.02) { fails.push({ check: 'grounded', msg: `a ${p.kind} at ${p.x} ${p.y} reaches over a bank (ground ${h.toFixed(2)} m under its edge)` }); break; }
    }
  }
  // terrain: the outcrops and pools leave every path tile and pad flat (sampled over each tile)
  for (let y = 0; y < m.h; y++) for (let x = 0; x < m.w; x++) {
    const t = m.grid[y][x];
    if (t !== '.' && t !== 'o') continue;
    const want = t === '.' ? 0 : GROUND_H;
    let bad = false;
    for (const [a, b] of [[0.5, 0.5], [0.3, 0.3], [0.7, 0.3], [0.3, 0.7], [0.7, 0.7]]) {
      const h = hf((x + a - m.w / 2) * TILE, (y + b - m.h / 2) * TILE);
      if (Math.abs(h - want) > 0.02) bad = true;
    }
    if (bad) { fails.push({ check: 'terrain', msg: `a feature reaches the ${t === '.' ? 'path' : 'pad'} at ${x} ${y}` }); break; }
  }
  // bake
  const id = String(m.num).padStart(3, '0');
  const lm = join(bakeDir, `${id}.cwlm`), hp = join(bakeDir, `${id}.hash`);
  if (!existsSync(lm) || !existsSync(hp)) fails.push({ check: 'bake', msg: `no bake (${basename(lm)})` });
  else {
    const want = bakeHash(text, m.theme, models), have = readFileSync(hp, 'utf8').trim();
    if (want !== have) fails.push({ check: 'bake', msg: `the bake is stale (${have}, the map now ${want}): bake it again with towers_mapview --bake` });
  }
  return { m, fails };
}

function clear(file) {
  return new Promise((res) => {
    const p = spawn(SOAK, ['--clear', file, '2', '45'], { stdio: ['ignore', 'pipe', 'pipe'] });
    let out = '';
    p.stdout.on('data', (d) => (out += d));
    p.stderr.on('data', (d) => (out += d));
    p.on('close', (code) => res({ code, line: out.trim().split('\n').filter((l) => l.startsWith('clear:')).slice(-1)[0] || out.trim().split('\n').slice(-1)[0] }));
  });
}
async function pool(items, fn) {
  const out = new Array(items.length);
  let next = 0;
  await Promise.all(Array.from({ length: Math.min(JOBS, items.length) }, async () => { while (next < items.length) { const k = next++; out[k] = await fn(items[k]); } }));
  return out;
}

let bad = 0;
const files = readdirSync(join(A, 'maps')).filter((f) => /^\d{3}\.cwtd$/.test(f)).sort().filter((f) => !ONLY || ONLY.has(+f.slice(0, 3)));
const count = +readFileSync(join(A, 'maps/count.txt'), 'utf8');
if (!ONLY && files.length !== count) { console.log(`FAIL count.txt says ${count} maps, ${files.length} on disk`); bad++; }
const results = files.map((f) => ({ f, ...checkMap(readFileSync(join(A, 'maps', f), 'utf8')) }));
if (SOAK) {
  const cl = await pool(results.filter((r) => !r.fails.length), (r) => clear(join(A, 'maps', r.f)).then((c) => ({ r, c })));
  for (const { r, c } of cl) { r.clear = c.line; if (c.code !== 0) r.fails.push({ check: 'clear', msg: c.line }); }
}
for (const r of results) {
  if (r.fails.length) { bad++; console.log(`FAIL ${r.f}: ${r.fails.map((x) => `[${x.check}] ${x.msg}`).join('; ')}`); }
  else console.log(`ok   ${r.f}${r.clear ? `: ${r.clear.replace(/^clear: /, '')}` : ''}`);
}
if (CONTROLS) { // each broken map must fail its own check
  for (const f of readdirSync(BROKEN).filter((x) => x.endsWith('.cwtd')).sort()) {
    const want = f.split('-')[0];
    if (want === 'clear' && !SOAK) continue;
    const text = readFileSync(join(BROKEN, f), 'utf8');
    const r = checkMap(text, { bakeDir: join(BROKEN, 'lightmaps') });
    if (want === 'clear') { const c = await clear(join(BROKEN, f)); if (c.code !== 0) r.fails.push({ check: 'clear', msg: c.line }); }
    const hit = r.fails.filter((x) => x.check === want);
    if (!hit.length) { bad++; console.log(`CONTROL PASSED (a failure): ${f} should fail [${want}] (${r.fails.map((x) => x.check).join(', ') || 'nothing failed'})`); }
    else console.log(`ok   control ${f} fails [${want}]: ${hit[0].msg}`);
  }
}
console.log(bad ? `check-maps: ${bad} failing` : `check-maps: ${results.length} maps ok`);
process.exit(bad ? 1 : 0);
