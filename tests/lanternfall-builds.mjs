// Lanternfall's people: the hooded lamplighter (the hero), the knight, the Brass Sentinel and the lamplighter statues
// have athletic proportions: tapered limbs, a waist narrower than the chest, defined shoulders, feet and hands in
// proportion; a big role is built big on purpose and keeps them all the same. Each figure is measured from its painted
// parts as the game paints them (src/creatures.c, src/scenery.c, read by a small interpreter for the painter's calls:
// capsules, ellipses, polygons, strokes), put together in its standing pose with the joint offsets its draw code uses.
//
// Every figure is drawn facing along x, so the trunk is measured in side view (its depth, front to back):
//
//   H                 stature: the soles to the top of the head (a hood or a helm counted as the head)
//   heads             H over the head's height: the hood's (or helm's) top to the chin
//   chest / waist     the deepest trunk row at 0.68 to 0.80 H over the shallowest at 0.55 to 0.66 H (side depth)
//   waist / H         that shallowest depth over the stature
//   legs / H          the hip joint's height over the stature
//   upper arm, thigh  the limb's greatest thickness across the middle half of its length, over that length
//   foot / H          the boot's length heel to toe;  hand / H  the hand's greatest extent
//
// The limits, from an athletic adult, in tests/char-builds.mjs's LIMITS where it has them: the upper arm at most 0.42
// of its length thick (0.5 for a big role), the thigh at most 0.5 (0.58 big), the foot at most 0.17 H, the hand at most
// 0.12 H. Side view: the chest at least 1.1 times as deep as the waist (an athletic adult's is about 1.15 to 1.25), the
// waist at most 0.15 H deep clothed (about 0.11 H bare; a coat adds some). The legs at least 0.45 H (0.5 H bare;
// a hood adds to the stature). Heads: the 2D style keeps the head a little large so a face in a hood reads at 60
// pixels a tile; with the hood or helm counted as the head (it adds about a sixth to the head's height) the bounds
// are 5.5 to 7.5 (about 6.4 to 8.7 bare heads).
// The Brass Sentinel is a clockwork giant whose furnace barrel is its design, a big role: it is held to the big
// limits and to a chest deeper than its waist, and its furnace's depth over H is reported, not judged.
//
// CONTROL: --control <rev> reads the figures from that commit (1751b7a: as they shipped before the build pass); every
// one of them must fail.
//
// Run: node tests/lanternfall-builds.mjs [--json] [--control <rev>]
import { readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';
import { LIMITS } from './char-builds.mjs';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const SRC = 'games/lanternfall/src';
export const SIDE = { chestWaist: { min: 1.1 }, waistH: { max: 0.15 }, legsH: { min: 0.45 }, heads: { min: 5.5, max: 7.5 } };

function source(rev, file) {
  return rev ? execFileSync('git', ['-C', ROOT, 'show', `${rev}:${SRC}/${file}`], { encoding: 'utf8', maxBuffer: 1 << 26 }) : readFileSync(join(ROOT, SRC, file), 'utf8');
}
function between(src, a, b) {
  const i = src.indexOf(a);
  if (i < 0) throw new Error(`not found: ${a}`);
  const j = src.indexOf(b, i + a.length);
  if (j < 0) throw new Error(`not found after ${a}: ${b}`);
  return src.slice(i + a.length, j);
}
/** a number read from the draw code: the first capture of re */
function num(src, re, k = 1) {
  const m = re.exec(src);
  if (!m) throw new Error(`draw code: no match for ${re}`);
  return parseFloat(m[k]);
}

// ── the painter's calls, run in JS: each part is a list of shapes in tiles on its canvas, with its pivot ──
/**
 * @param {string} code the painting code (C) of a run of parts
 * @param {object} names map of the C names a part is stored as (`H[i].thigh`, `E.kthigh`, `PP.statue`) -> part name
 */
function paint(code, alias = {}) {
  let c = code.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/.*$/gm, '');
  c = c.replace(/(\d)f\b/g, '$1').replace(/\(float\)\s*/g, '').replace(/\b(cosf|sinf|fabsf|fminf|fmaxf|sqrtf)\(/g, (m, f) => `Math.${f.slice(0, -1).replace('fabs', 'abs').replace('fmin', 'min').replace('fmax', 'max')}(`);
  c = c.replace(/= \{[^}]*\}/g, (m) => m.replace(/\s*\n\s*/g, ' ')); /* (an initializer over several lines) */
  const out = [];
  for (let line of c.split('\n')) {
    let s = line.trim();
    if (!s) continue;
    s = s.replace(/^([\w.[\]]+)\s*=\s*(done|pfin)\(/, (m, name, fn) => `${fn}(${JSON.stringify(alias[name] || name)}, `);
    if (/^(pstyle|static|pc_ellipse|cw_)/.test(s) || /^\w+\.\w+\s*=[^=]/.test(s) || /^if \(i\)/.test(s)) continue; /* styles, tables */
    s = s.replace(/(\w+)\[\d*\] = \{([^}]*)\}/g, '$1 = [$2]');
    s = s.replace(/^float /, 'let ').replace(/for \(int /g, 'for (let ').replace(/&(\w+)/g, "'$1'").replace(/&\(pstyle\)\{[^}]*\}/g, "'none'");
    out.push(s);
  }
  const parts = {};
  let cur = [];
  const api = {
    cv: (w, h) => { cur = []; }, cvs: (w, h) => { cur = []; },
    cap: (ax, ay, bx, by, ra, rb, st) => cur.push({ k: 'cap', a: [ax, ay], b: [bx, by], ra, rb, st }),
    el: (cx, cy, rx, ry, rot, st) => cur.push({ k: 'el', c: [cx, cy], rx, ry, rot, st }),
    E: (cx, cy, rx, ry, rot, st) => cur.push({ k: 'el', c: [cx, cy], rx, ry, rot, st }),
    poly: (xy, n, round, st) => cur.push({ k: 'poly', p: pts(xy, n), st }),
    P: (xy, n, round, st) => cur.push({ k: 'poly', p: pts(xy, n), st }),
    stk: (xy, n, r0, r1, st) => cur.push({ k: 'stk', p: pts(xy, n), r0, r1, st }),
    K: (xy, n, r0, r1, st) => cur.push({ k: 'stk', p: pts(xy, n), r0, r1, st }),
    lf: (ax, ay, bx, by, w, bend, st) => cur.push({ k: 'leaf', a: [ax, ay], b: [bx, by], w, st }),
    done: (name, px, py) => { parts[name] = { shapes: cur, px, py }; cur = []; },
    pfin: (name, px, py) => { parts[name] = { shapes: cur, px, py }; cur = []; },
  };
  const fn = new Function(...Object.keys(api), 'C', 'PPT', 'SPT', out.join('\n'));
  fn(...Object.values(api), null, 128, 128);
  return parts;
}
function pts(xy, n) { const p = []; for (let i = 0; i < n; i++) p.push([xy[i * 2], xy[i * 2 + 1]]); return p; }

/** is canvas point (x, y) inside the shape? */
function inside(s, x, y) {
  if (s.k === 'el') {
    const dx = x - s.c[0], dy = y - s.c[1], c = Math.cos(s.rot), sn = Math.sin(s.rot);
    const u = dx * c + dy * sn, v = -dx * sn + dy * c;
    return (u / s.rx) ** 2 + (v / s.ry) ** 2 <= 1;
  }
  if (s.k === 'cap') return segIn(s.a, s.b, s.ra, s.rb, x, y);
  if (s.k === 'stk') {
    let tot = 0; const L = [];
    for (let i = 1; i < s.p.length; i++) L.push(tot += Math.hypot(s.p[i][0] - s.p[i - 1][0], s.p[i][1] - s.p[i - 1][1]));
    let at = 0;
    for (let i = 1; i < s.p.length; i++) {
      const r = (d) => s.r0 + (s.r1 - s.r0) * (d / tot);
      if (segIn(s.p[i - 1], s.p[i], r(at), r(L[i - 1]), x, y)) return true;
      at = L[i - 1];
    }
    return false;
  }
  if (s.k === 'poly') {
    let c = false;
    for (let i = 0, j = s.p.length - 1; i < s.p.length; j = i++) {
      const [xi, yi] = s.p[i], [xj, yj] = s.p[j];
      if ((yi > y) !== (yj > y) && x < ((xj - xi) * (y - yi)) / (yj - yi) + xi) c = !c;
    }
    return c;
  }
  if (s.k === 'leaf') { /* a lens from a to b, w its half width at the middle */
    const ax = s.b[0] - s.a[0], ay = s.b[1] - s.a[1], L = Math.hypot(ax, ay), t = ((x - s.a[0]) * ax + (y - s.a[1]) * ay) / (L * L);
    if (t < 0 || t > 1) return false;
    const d = Math.abs((x - s.a[0]) * ay - (y - s.a[1]) * ax) / L;
    return d <= s.w * Math.sin(Math.PI * t);
  }
  return false;
}
function segIn(a, b, ra, rb, x, y) {
  const dx = b[0] - a[0], dy = b[1] - a[1], L2 = dx * dx + dy * dy;
  const t = L2 ? Math.max(0, Math.min(1, ((x - a[0]) * dx + (y - a[1]) * dy) / L2)) : 0;
  const r = ra + (rb - ra) * t;
  return Math.hypot(x - a[0] - t * dx, y - a[1] - t * dy) <= r;
}

const RES = 0.004;
/** a part's own rows on its canvas: v -> [min u, max u] (shapes filtered by keep) */
function canvasRows(part, keep = () => true) {
  const sh = part.shapes.filter(keep), R = new Map();
  const box = bounds(sh);
  for (let v = Math.floor(box[1] / RES); v <= Math.ceil(box[3] / RES); v++)
    for (let u = Math.floor(box[0] / RES); u <= Math.ceil(box[2] / RES); u++) {
      const x = u * RES, y = v * RES;
      if (!sh.some((s) => inside(s, x, y))) continue;
      const r = R.get(v); if (r) r[0] = Math.min(r[0], x), r[1] = Math.max(r[1], x); else R.set(v, [x, x]);
    }
  return R;
}
function bounds(sh) {
  let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
  const add = (x, y, r = 0) => { x0 = Math.min(x0, x - r); y0 = Math.min(y0, y - r); x1 = Math.max(x1, x + r); y1 = Math.max(y1, y + r); };
  for (const s of sh) {
    if (s.k === 'el') add(s.c[0], s.c[1], Math.max(s.rx, s.ry));
    else if (s.k === 'cap') add(...s.a, s.ra), add(...s.b, s.rb);
    else if (s.k === 'stk') for (const p of s.p) add(...p, Math.max(s.r0, s.r1));
    else if (s.k === 'poly') for (const p of s.p) add(...p);
    else if (s.k === 'leaf') add(...s.a, s.w), add(...s.b, s.w);
  }
  return [x0, y0, x1, y1];
}
const rowW = (R, y) => { const r = R.get(Math.round(y / RES)); return r ? r[1] - r[0] : 0; };
const extent = (R) => { const ks = [...R.keys()]; return ks.length ? [Math.min(...ks) * RES, Math.max(...ks) * RES] : [0, 0]; };

/** the figure in world rows: parts placed (pivot at (x, y), turned a, scaled sx, sy), each part's shapes filtered */
function worldRows(placed) {
  const R = new Map();
  for (const { part, x, y, a = 0, sx = 1, sy = 1, keep = () => true } of placed) {
    const sh = part.shapes.filter(keep), b = bounds(sh), c = Math.cos(a), s = Math.sin(a);
    const corners = [[b[0], b[1]], [b[2], b[1]], [b[0], b[3]], [b[2], b[3]]].map(([u, v]) => { const dx = (u - part.px) * sx, dy = (v - part.py) * sy; return [x + dx * c - dy * s, y + dx * s + dy * c]; });
    const X0 = Math.min(...corners.map((p) => p[0])), X1 = Math.max(...corners.map((p) => p[0])), Y0 = Math.min(...corners.map((p) => p[1])), Y1 = Math.max(...corners.map((p) => p[1]));
    for (let j = Math.floor(Y0 / RES); j <= Math.ceil(Y1 / RES); j++)
      for (let i = Math.floor(X0 / RES); i <= Math.ceil(X1 / RES); i++) {
        const wx = i * RES - x, wy = j * RES - y, u = (wx * c + wy * s) / sx + part.px, v = (-wx * s + wy * c) / sy + part.py;
        if (!sh.some((q) => inside(q, u, v))) continue;
        const r = R.get(j), X = i * RES; if (r) r[0] = Math.min(r[0], X), r[1] = Math.max(r[1], X); else R.set(j, [X, X]);
      }
  }
  return R;
}
/** a limb part hanging from its pivot (turn 0): its greatest width across the middle half of its length L */
function limbRatio(part, L, keep) {
  const R = canvasRows(part, keep);
  let w = 0;
  for (let v = part.py + 0.25 * L; v <= part.py + 0.75 * L; v += RES) w = Math.max(w, rowW(R, v));
  return { len: L, thick: w, ratio: w / L };
}
/** the trunk's side depth: chest (deepest at 0.68..0.80 H) and waist (shallowest at 0.55..0.66 H), soles at y = 0 */
function trunkDepth(R, H) {
  let chest = 0, waist = Infinity;
  for (let h = 0.68 * H; h <= 0.8 * H; h += RES) chest = Math.max(chest, rowW(R, -h));
  for (let h = 0.55 * H; h <= 0.66 * H; h += RES) { const w = rowW(R, -h); if (w > 0) waist = Math.min(waist, w); }
  return { chest, waist };
}

// ── the figures ──
const styleIs = (...n) => (s) => n.includes(s.st);
const styleNot = (...n) => (s) => !n.includes(s.st);

function hero(rev) {
  const src = source(rev, 'creatures.c');
  const paintCode = between(src, 'static void paint_hero(int i) {', 'if (i) {');
  const P = paint(paintCode, Object.fromEntries(['thigh', 'shin', 'coat', 'cape', 'hood', 'tip', 'arm', 'fore', 'hand'].map((n) => [`H[i].${n}`, n])));
  const draw = between(src, 'void creature_hero(const player *p, int i, int region, float t) {', '\nvoid creatures_init');
  const hipy = num(draw, /float hipy = (-?[\d.]+)f - bob;/);
  const knee = num(draw, /cw_vec2 kn = rot2\(0, ([\d.]+)f, a1\)/);
  const coatY = num(draw, /put\(&h->coat, 0, hipy \+ ([\d.]+)f/);
  const hd = [num(draw, /cw_vec2 hd = rot2\((-?[\d.]+)f, (-?[\d.]+)f, lean\)/, 1), num(draw, /cw_vec2 hd = rot2\((-?[\d.]+)f, (-?[\d.]+)f, lean\)/, 2)];
  const shY = num(draw, /cw_vec2 sh = rot2\((-?[\d.]+)f, (-?[\d.]+)f, lean\)/, 2);
  const elbow = num(draw, /cw_vec2 el = rot2\(0, ([\d.]+)f, ua\)/);
  const hoodRows = canvasRows(P.hood, styleIs('hood')), face = canvasRows(P.hood, styleIs('dark'));
  const hoodTop = hipy + hd[1] + (extent(hoodRows)[0] - P.hood.py), chin = hipy + hd[1] + (extent(face)[1] - P.hood.py);
  // the legs straight under the hip: the soles
  const shinRows = canvasRows(P.shin), sole = hipy + knee + (extent(shinRows)[1] - P.shin.py);
  const H = sole - hoodTop;
  const trunk = worldRows([{ part: P.coat, x: 0, y: hipy + coatY - sole, keep: styleIs('cloth') }]);
  const { chest, waist } = trunkDepth(trunk, H);
  const boot = canvasRows(P.shin, styleIs('leather')), [b0, b1] = extent(boot);
  let foot = 0; for (let v = b1 - 0.03; v <= b1; v += RES) foot = Math.max(foot, rowW(boot, v));
  const hb = bounds(P.hand.shapes);
  return {
    name: 'lamplighter (hero)', role: 'hero', H, heads: H / (chin - hoodTop), chest, waist, legs: (sole - hipy) / H,
    upperArm: limbRatio(P.arm, elbow), thigh: limbRatio(P.thigh, knee), foot: foot / H, hand: Math.max(hb[2] - hb[0], hb[3] - hb[1]) / H, shoulderY: (sole - (hipy + shY)) / H,
  };
}

function knight(rev) {
  const src = source(rev, 'creatures.c');
  const code = between(src, '/* ── knight ── */', 'E.plume = done').replace(/^\s*\{/, '');
  const P = paint(code + 'E.plume = done(0,0,0);', Object.fromEntries(['kthigh', 'kshin', 'tabard', 'torso', 'helm', 'plume'].map((n) => [`E.${n}`, n])));
  const armCode = between(src, 'E.shield = done', 'E.karm = done');
  const A = paint(armCode.slice(armCode.indexOf(';') + 1) + 'E.karm = done(0.13f, 0.06f, 3);', { 'E.karm': 'karm' });
  const armPiv = num(src, /E\.karm = done\(([\d.]+)f, ([\d.]+)f/, 2);
  A.karm.py = armPiv;
  const draw = between(src, 'static void draw_knight(', 'static void draw_puff(');
  const hipy = num(draw, /float hipy = (-?[\d.]+)f - bob/);
  const thighY = num(draw, /hy = hipy \+ ([\d.]+)f;/);
  const knee = num(draw, /cw_vec2 kp = rot2\(0, ([\d.]+)f, sw\)/);
  const torsoY = num(draw, /put\(&E\.torso, hipx \+ lean \* [\d.]+f, hipy \+ ([\d.]+)f/);
  const nk = num(draw, /cw_vec2 nk = rot2\((-?[\d.]+)f, (-?[\d.]+)f, lean\)/, 2);
  const armY = num(draw, /putk\(&E\.karm, hipx \+ [\d.]+f \+ lean \* [\d.]+f, hipy - ([\d.]+)f/);
  const helm = canvasRows(P.helm, styleIs('hs')), [h0, h1] = extent(helm);
  const helmTop = hipy + nk + (h0 - P.helm.py), chin = hipy + nk + (h1 - P.helm.py);
  const shin = canvasRows(P.kshin), sole = hipy + thighY + knee + (extent(shin)[1] - P.kshin.py);
  const H = sole - helmTop;
  const trunk = worldRows([{ part: P.torso, x: 0, y: hipy + torsoY - sole, keep: styleIs('ct') }, { part: P.tabard, x: 0, y: hipy - 0.06 - sole, keep: styleIs('cloth') }]);
  const { chest, waist } = trunkDepth(trunk, H);
  const sab = canvasRows(P.kshin), [, s1] = extent(sab);
  let foot = 0; for (let v = s1 - 0.03; v <= s1; v += RES) foot = Math.max(foot, rowW(sab, v));
  /* the arm is one piece, shoulder to gauntlet: its upper half is the upper arm */
  const armShapes = A.karm.shapes.filter((s) => s.k === 'cap');
  const armLen = Math.max(...armShapes.map((s) => s.b[1])) - A.karm.py;
  const gaunt = A.karm.shapes.filter((s) => s.k === 'el').map((s) => 2 * Math.max(s.rx, s.ry));
  return {
    name: 'knight', role: 'soldier', H, heads: H / (chin - helmTop), chest, waist, legs: (sole - (hipy + thighY)) / H,
    upperArm: limbRatio(A.karm, armLen * 0.55), thigh: limbRatio(P.kthigh, knee, styleNot('verd')), foot: foot / H, hand: Math.max(0, ...gaunt) / H, shoulderY: (sole - (hipy - armY)) / H,
  };
}

function brass(rev) {
  const src = source(rev, 'creatures.c');
  const code = between(src, '/* ── 2 the Brass Sentinel ── */', '/* ── 3 the Tide Serpent ── */').replace(/\{ \/\* a gear[\s\S]*?B\.bgear = done[^;]*;/, '');
  const P = paint(code, Object.fromEntries(['bleg1', 'bleg2', 'bpelvis', 'btorso', 'bcore', 'bhead', 'barm1', 'barm2', 'bstack'].map((n) => [`B.${n}`, n])));
  const draw = between(src, 'static void boss_brass(', 'static void boss_serpent(');
  const hipy = num(draw, /float hipy = (-?[\d.]+)f \+ crouch/);
  const legY = num(draw, /putk\(&B\.bleg1, o \* [\d.]+f, hipy \+ ([\d.]+)f/);
  const knee = num(draw, /cw_vec2 kn = rot2\(0, ([\d.]+)f, sw\)/);
  const torsoY = num(draw, /put\(&B\.btorso, 0, hipy - ([\d.]+)f/);
  const hp = num(draw, /cw_vec2 hp = rot2\((-?[\d.]+)f, (-?[\d.]+)f, lean\)/, 2);
  const shd = num(draw, /cw_vec2 shd = rot2\(o \* [\d.]+f \+ [\d.]+f, (-?[\d.]+)f, lean\)/);
  const elbow = num(draw, /cw_vec2 el = rot2\(0, ([\d.]+)f, aa\)/);
  const head = canvasRows(P.bhead, styleIs('hb')), [h0, h1] = extent(head);
  const top = hipy + hp + (h0 - P.bhead.py), chin = hipy + hp + (h1 - P.bhead.py);
  const sole = 0; /* (the shin is stretched to the floor at rest: hipy + legY + knee + its length) */
  const H = sole - top;
  const trunk = worldRows([{ part: P.btorso, x: 0, y: hipy - torsoY, keep: styleIs('chest') }, { part: P.bpelvis, x: 0, y: hipy, keep: styleIs('iron') }]);
  const { chest, waist } = trunkDepth(trunk, H);
  const foot = canvasRows(P.bleg2, styleIs('brass')), [, f1] = extent(foot);
  let fl = 0; for (let v = f1 - 0.06; v <= f1; v += RES) fl = Math.max(fl, rowW(foot, v));
  return {
    name: 'Brass Sentinel', role: 'big', H, heads: H / (chin - top), chest, waist, legs: -(hipy + legY) / H,
    upperArm: limbRatio(P.barm1, elbow, styleIs('brass')), thigh: limbRatio(P.bleg1, knee, styleIs('brass')), foot: fl / H, hand: null, shoulderY: -(hipy + shd) / H,
  };
}

function statue(rev) {
  const src = source(rev, 'scenery.c');
  const code = between(src, '/* the statue:', 'PP.statue = pfin');
  const P = paint(code.slice(code.indexOf('{') + 1) + 'PP.statue = pfin(1.1f, 3.58f, 2.5f);', { 'PP.statue': 'statue' });
  const S = P.statue.shapes;
  const plinth = S[0]; /* the first polygon: the plinth; the figure stands on its top */
  const base = Math.min(...plinth.p.map((p) => p[1]));
  const hood = S.find((s) => s.k === 'poly' && s.st === 'stone' && s !== plinth && s.p.length <= 8 && Math.max(...s.p.map((p) => p[1])) < 1.2);
  const face = S.find((s) => s.k === 'el' && s.st === 'dark');
  const top = Math.min(...hood.p.map((p) => p[1])), chin = face.c[1] + face.ry, H = base - top;
  const fig = S.filter((s) => s !== plinth && s.st === 'stone' && s.k === 'poly' && s !== hood);
  const R = new Map(); /* the robe or coat in rows, relative to the soles */
  for (let v = top; v <= base; v += RES)
    for (let u = 0; u <= 2.2; u += RES)
      if (fig.some((s) => inside(s, u, v))) { const k = Math.round((v - base) / RES), r = R.get(k); if (r) r[0] = Math.min(r[0], u), r[1] = Math.max(r[1], u); else R.set(k, [u, u]); }
  const { chest, waist } = trunkDepth(R, H);
  /* the arm: the stone stroke(s) from the shoulder; the upper arm is the first segment */
  const arms = S.filter((s) => s.k === 'stk' && s.st === 'stone');
  const a = arms[0], L = Math.hypot(a.p[1][0] - a.p[0][0], a.p[1][1] - a.p[0][1]), tot = a.p.slice(1).reduce((t, p, i) => t + Math.hypot(p[0] - a.p[i][0], p[1] - a.p[i][1]), 0);
  const rMid = a.r0 + (a.r1 - a.r0) * (0.5 * L / tot);
  const boots = S.filter((s) => s.st === 'boot');
  const foot = boots.length ? Math.max(...boots.map((s) => { const b = bounds([s]); return b[2] - b[0]; })) : null;
  return {
    name: 'lamplighter statue', role: 'statue', H, heads: H / (chin - top), chest, waist, legs: null,
    upperArm: { len: L, thick: 2 * rMid, ratio: 2 * rMid / L }, thigh: null, foot: foot === null ? null : foot / H, hand: null, shoulderY: null,
  };
}

export function judge(m) {
  const big = m.role === 'big', fails = [];
  const lim = (k) => (big ? LIMITS[k].big : LIMITS[k].max);
  const r = { heads: m.heads, chestWaist: m.chest / m.waist, waistH: m.waist / m.H, legsH: m.legs, upperArm: m.upperArm?.ratio, thigh: m.thigh?.ratio, footH: m.foot, handH: m.hand };
  if (r.heads < SIDE.heads.min || r.heads > SIDE.heads.max) fails.push('heads');
  if (r.chestWaist < SIDE.chestWaist.min) fails.push('chest/waist');
  if (!big && r.waistH > SIDE.waistH.max) fails.push('waist');
  if (r.legsH != null && r.legsH < SIDE.legsH.min) fails.push('legs');
  if (r.upperArm != null && r.upperArm > lim('upperArm')) fails.push('upper arm');
  if (r.thigh != null && r.thigh > lim('thigh')) fails.push('thigh');
  if (r.footH != null && r.footH > LIMITS.footH.max) fails.push('foot');
  if (r.handH != null && r.handH > LIMITS.handH.max) fails.push('hand');
  return { r, fails };
}

export function measureAll(rev = null) { return [hero(rev), knight(rev), brass(rev), statue(rev)]; }

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const args = process.argv.slice(2), json = args.includes('--json');
  const ci = args.indexOf('--control'), rev = ci >= 0 ? args[ci + 1] : null;
  const rows = measureAll(rev).map((m) => ({ ...m, ...judge(m) }));
  const f = (x, d = 2) => (x == null ? '  -  ' : x.toFixed(d));
  if (json) console.log(JSON.stringify(rows, null, 1));
  else {
    console.log('figure               role     H     heads chest/waist waist/H legs/H uArm  thigh foot/H hand/H  verdict');
    for (const x of rows) console.log([x.name.padEnd(20), x.role.padEnd(7), f(x.H), f(x.r.heads).padStart(5), f(x.r.chestWaist).padStart(11), f(x.r.waistH, 3).padStart(7), f(x.r.legsH, 3).padStart(6),
      f(x.r.upperArm).padStart(5), f(x.r.thigh).padStart(5), f(x.r.footH, 3).padStart(6), f(x.r.handH, 3).padStart(6), ' ' + (x.fails.length ? 'FAIL ' + x.fails.join(', ') : 'ok')].join(' '));
  }
  const failed = rows.filter((x) => x.fails.length);
  if (rev) {
    if (failed.length < rows.length) { console.error(`CONTROL: ${rows.length - failed.length} old figure(s) pass; every old figure must fail`); process.exitCode = 1; }
    else console.log(`control: all ${rows.length} old figures fail, as they must`);
  } else if (failed.length) process.exitCode = 1;
}
