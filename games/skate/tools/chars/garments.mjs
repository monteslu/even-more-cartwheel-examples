// The riders' clothes as painted texture sets, one per rider and garment: the shirt (torso, sleeves and a plain patch for
// the hood), the trousers (the legs and the seat) and the shoes (the upper and the cupsole). Each garment is mapped once
// round and once along (make-skaters.mjs lays the UVs), so a fold, a seam or the print on the back sits at one place on
// the body instead of repeating like a tiled weave. Colours are baked (the materials use a white tint); the height plane
// becomes the normal map and also darkens the valleys (occlusion), the roughness is cloth matte, the soles rubber.
// Every pixel is made here; the prints are plain geometric shapes, no lettering and no marks of any maker.
import { Tex, writeSet, fbm, hash2, smooth, clamp, lerp } from './texlib.mjs';

const PI = Math.PI;
const srgb = (h) => [((h >> 16) & 255) / 255, ((h >> 8) & 255) / 255, (h & 255) / 255];
const gauss = (x, w) => Math.exp(-(x * x) / (w * w));
/** u distance with the wrap, -0.5..0.5 */
const du = (u, c) => ((((u - c) % 1) + 1.5) % 1) - 0.5;
const mix = (o, c, k) => { o[0] = lerp(o[0], c[0], k); o[1] = lerp(o[1], c[1], k); o[2] = lerp(o[2], c[2], k); };
/** a ridge line profile: 1 on the line, falling off over w */
const ridge = (d, w) => gauss(d, w);
/** soft folds: noisy ridges across a band, tileable along u (f must be a whole number) */
const foldNoise = (u, v, f, seed) => { const n = fbm(u, v, f, 3, seed); return Math.pow(1 - Math.abs(2 * n - 1), 3); };
/** folds that run round a garment (bunching, stacking): broken, wavy, about `period` metres apart at height y (m) */
const ringFolds = (u, y, period, seed, f = 3) => foldNoise(u, y / (period * f), f, seed);
/** a dashed stitch line along a seam: d the distance from the seam (m), s the distance along it (m) */
const stitch = (d, s, w = 0.0012) => ridge(d, w) * smooth(0.25, 0.45, 0.5 + 0.5 * Math.sin(s * 2 * PI / 0.01));

function paint(w, h, px) {
  const t = new Tex(w, h), o = { c: [0, 0, 0], h: 0, ro: 0.92, ao: 1 };
  t.each((u, v, i) => {
    o.c[0] = o.c[1] = o.c[2] = 0; o.h = 0; o.ro = 0.92; o.ao = 1;
    px(u, v, o);
    t.a[0][i] = clamp(o.c[0]); t.a[1][i] = clamp(o.c[1]); t.a[2][i] = clamp(o.c[2]);
    t.height[i] = o.h; t.rough[i] = clamp(o.ro); t.metal[i] = 0; t.ao[i] = clamp(o.ao);
  });
  return t;
}

// ── the back prints: coverage of each colour at (x, y) metres from the print's centre (x to the viewer's right seen from behind)
const PRINTS = {
  // a low sun cut by three bars, in a thin outer ring
  sun: (x, y) => { const r = Math.hypot(x, y), e = 0.0025; const disc = smooth(0.112 + e, 0.112 - e, r) * (1 - [-0.012, -0.042, -0.072].reduce((q, b) => Math.max(q, smooth(0.009 + e, 0.009 - e, Math.abs(y - b)) * smooth(-0.002, 0.004, -y)), 0));
    return [disc, smooth(0.006, 0.0035, Math.abs(r - 0.13))]; },
  // three nested arcs over a dot
  arcs: (x, y) => { const r = Math.hypot(x, y + 0.04), up = smooth(-0.002, 0.003, y + 0.04), band = (r0, r1) => smooth(r0 - 0.002, r0 + 0.002, r) * smooth(r1 + 0.002, r1 - 0.002, r) * up;
    return [band(0.105, 0.128) + smooth(0.026, 0.022, r), band(0.072, 0.094), band(0.04, 0.061)]; },
  // stacked chevrons pointing down
  chevrons: (x, y) => { const v = (y0) => { const d = Math.abs((y - y0) - Math.abs(x) * 0.7); return smooth(0.017, 0.013, d) * smooth(0.122, 0.118, Math.abs(x)); };
    return [Math.max(v(0.06), v(-0.04)), v(0.01), v(-0.09)]; },
  // three stacked waves
  waves: (x, y) => { const w = (y0) => smooth(0.014, 0.01, Math.abs(y - y0 - 0.016 * Math.sin(x / 0.03))) * smooth(0.126, 0.118, Math.abs(x));
    return [Math.max(w(0.05), w(-0.05)), w(0)]; },
};

/**
 * the shirt: torso (v 0..0.6, 0 at the neck, u round the body, 0.25 the front, 0.75 the back), sleeves (v 0.62..0.9,
 * 0 at the shoulder, u round the arm, 0.5 the inside), a plain patch (v 0.92..1) for the hood and the collar.
 * d: { C, H: torso round and length (m), SC, SH: the sleeve's, elbow: 0..1 down the sleeve, long, hoodie }
 */
export function shirtTex(spec, d) {
  const base = srgb(spec.colors.shirt), inks = spec.inks.map(srgb), seed = spec.name.length * 31 + 7, print = PRINTS[spec.print];
  const shade = (o, k) => { o.c[0] *= k; o.c[1] *= k; o.c[2] *= k; };
  return paint(512, 512, (u, v, o) => {
    const grain = fbm(u, v, 24, 3, seed) - 0.5, fz = fbm(u, v, 64, 1, seed + 1) - 0.5;
    o.c[0] = base[0]; o.c[1] = base[1]; o.c[2] = base[2];
    shade(o, 0.93 + 0.08 * grain + 0.025 * fz);
    let hgt = 0, ro = 0.93, valley = 0;
    if (v < 0.6) { // ── the torso
      const vt = v / 0.6, y = vt * d.H, x = du(u, 0.75) * d.C;
      // drape: long folds hanging from the chest and back to the hem, deeper toward the hem
      const drape = foldNoise(u, vt * 0.35, 11, seed + 3) * smooth(0.25, 0.95, vt);
      // the pull from the armpits: diagonal folds running down and in from each side
      let pit = 0;
      for (const s of [0, 0.5]) { const xs = du(u, s) * d.C, yy = y - 0.12; for (const k of [0, 1, 2]) pit += ridge(Math.abs(xs) * 0.8 - (yy - 0.035 * k) * 0.55, 0.009) * smooth(0.0, 0.03, yy - 0.035 * k) * smooth(0.2, 0.08, Math.abs(xs)) * smooth(0.32, 0.16, yy); }
      // the bunching over the belt: soft rings broken up along the way round
      const bunch = ringFolds(u, y, 0.04, seed + 5, 4) * smooth(0.62, 0.8, vt) * smooth(1.0, 0.9, vt) * smooth(0.3, 0.7, fbm(u, vt, 3, 2, seed + 6));
      hgt += 0.7 * drape + 0.6 * pit + 0.4 * bunch; valley += 0.35 * (1 - drape) * smooth(0.4, 1, vt) + 0.15 * bunch;
      // side seams with a stitch, the collar rib, the hem band (a rib on a hoodie)
      for (const s of [0, 0.5]) { const xs = du(u, s) * d.C; hgt -= 0.25 * ridge(xs, 0.002); valley += 0.3 * ridge(xs, 0.0025); hgt += 0.12 * stitch(Math.abs(xs) - 0.004, y); }
      if (y < 0.025) { const rib = 0.5 + 0.5 * Math.sin(u * d.C / 0.004 * 2 * PI); hgt += 0.15 * rib; valley += 0.15 * (1 - rib); shade(o, 0.95); }
      const hemW = spec.hoodie ? 0.06 : 0.022, fromHem = d.H - y;
      if (fromHem < hemW) { hgt += 0.12; if (spec.hoodie) { const rib = 0.5 + 0.5 * Math.sin(u * d.C / 0.005 * 2 * PI); hgt += 0.2 * rib; valley += 0.2 * (1 - rib); shade(o, 0.94); } }
      hgt += 0.15 * stitch(fromHem - hemW, u * d.C); valley += 0.3 * ridge(fromHem - hemW, 0.002);
      // a hoodie's front pocket: the outline seam and a little fullness inside it
      if (spec.hoodie) {
        const px = Math.abs(du(u, 0.25) * d.C), top = d.H - 0.26, bot = d.H - hemW, slant = top + Math.max(0, px - 0.1) * 0.9;
        const inside = smooth(0.165, 0.16, px) * smooth(slant - 0.003, slant + 0.003, y) * smooth(bot + 0.003, bot - 0.003, y);
        const edge = Math.max(ridge(px - 0.163, 0.003) * smooth(slant - 0.004, slant + 0.01, y) * smooth(bot + 0.002, bot - 0.01, y), ridge(y - slant, 0.003) * smooth(0.168, 0.16, px));
        hgt += 0.15 * inside - 0.4 * edge; valley += 0.5 * edge;
      }
      // the print on the back (plastisol: a little raised, smoother, cracked here and there)
      if (print) {
        const yc = spec.printY ?? 0.42, py = -(y - yc * d.H), cov = print(-x, py);
        let any = 0;
        cov.forEach((k, i) => { if (k > 0) { const crack = hash2(Math.floor(u * 512 / 3), Math.floor(v * 1024 / 3), seed + 9) > 0.93 ? 0.6 : 1; mix(o.c, inks[i % inks.length], k * crack); any = Math.max(any, k); } });
        hgt += 0.08 * any; ro = lerp(ro, 0.78, any);
        // a small mark on the left chest, the print's first colour
        const fx = du(u, 0.21) * d.C, fy = y - 0.1, dot = smooth(0.018, 0.015, Math.hypot(fx, fy));
        mix(o.c, inks[0], dot); ro = lerp(ro, 0.8, dot);
      }
    } else if (v >= 0.62 && v < 0.9) { // ── the sleeves
      const vs = (v - 0.62) / 0.28, y = vs * d.SH, ins = Math.abs(du(u, 0.5)), cuffW = d.long ? 0.05 : 0.02, fromEnd = d.SH - y;
      if (d.long) { // bunched at the inside of the elbow, stacked over the cuff
        const el = d.elbow * d.SH, ringy = ringFolds(u, y, 0.028, seed + 11);
        const atElbow = gauss(y - el, 0.05) * (0.45 + 0.55 * gauss(ins, 0.22)), stack = smooth(0.18, 0.06, fromEnd - cuffW) * smooth(0, 0.01, fromEnd - cuffW);
        hgt += 0.9 * ringy * (atElbow + stack); valley += 0.5 * (1 - ringy) * (atElbow + stack);
      } else { // a short sleeve: folds from the armpit and a hem band
        const f = foldNoise(u, vs * 0.4, 6, seed + 13);
        hgt += 0.5 * f * smooth(0.3, 0.0, ins) ; valley += 0.25 * (1 - f) * smooth(0.3, 0, ins);
      }
      hgt += 0.3 * foldNoise(u, vs * 0.5, 5, seed + 17) * 0.5;
      if (fromEnd < cuffW) { hgt += 0.12; if (d.long) { const rib = 0.5 + 0.5 * Math.sin(u * d.SC / 0.004 * 2 * PI); hgt += 0.2 * rib; valley += 0.2 * (1 - rib); shade(o, 0.94); } }
      hgt += 0.15 * stitch(fromEnd - cuffW, u * d.SC); valley += 0.25 * ridge(fromEnd - cuffW, 0.002);
      // the seam under the arm
      valley += 0.25 * ridge(ins * d.SC, 0.002);
    } else if (v >= 0.92) { // ── the plain patch (the hood): soft creases
      const f = foldNoise(u, (v - 0.92) / 0.08 * 0.3, 7, seed + 19);
      hgt += 0.5 * f; valley += 0.25 * (1 - f);
    }
    o.h = hgt; o.ro = ro; o.ao = 1 - 0.32 * clamp(valley);
    shade(o, 1 - 0.18 * clamp(valley));
  });
}

/**
 * the trousers: legs (v 0..0.78, 0 at the top of the leg, u round the leg, 0 the outside, 0.25 the front, 0.5 the
 * inside), the seat (v 0.8..1, 0 at the waistband, u round the body, 0.25 the front, 0.75 the back).
 * d: { LC, LH: leg round and length, knee: 0..1 down the leg, PC, PH: the seat's, long: full length }
 */
export function pantsTex(spec, d) {
  const base = srgb(spec.colors.pants), seed = spec.name.length * 17 + 3, denim = spec.pantsFab === 'denim', cargo = spec.bottom === 'cargo';
  const thread = denim ? [0.85, 0.6, 0.3] : base.map((c) => c * 0.7);
  const shade = (o, k) => { o.c[0] *= k; o.c[1] *= k; o.c[2] *= k; };
  return paint(512, 512, (u, v, o) => {
    const grain = fbm(u, v, 24, 3, seed) - 0.5, fz = fbm(u, v, 64, 1, seed + 1) - 0.5;
    o.c[0] = base[0]; o.c[1] = base[1]; o.c[2] = base[2];
    let hgt = 0, ro = 0.94, valley = 0, light = 0;
    if (denim) shade(o, 0.95 + 0.14 * grain + 0.05 * (fbm(u + v, v, 40, 1, seed + 2) - 0.5));
    else shade(o, 0.94 + 0.08 * grain + 0.025 * fz);
    if (v < 0.78) { // ── a leg
      const vl = v / 0.78, y = vl * d.LH, x = du(u, 0) * d.LC, fromEnd = d.LH - y, kn = d.knee * d.LH;
      const back = gauss(du(u, 0.75), 0.15), front = gauss(du(u, 0.25), 0.18);
      // behind the knee: tight curved creases; in front of it: a few soft ones
      const creaseB = ringFolds(u, y, 0.03, seed + 4) * gauss(y - kn, 0.07) * back;
      const creaseF = ringFolds(u, y, 0.045, seed + 5) * gauss(y - kn + 0.02, 0.06) * front;
      // long drape folds down the shin, and the stack where a long leg piles onto the shoe
      const drape = foldNoise(u, vl * 0.3, 9, seed + 7);
      const stack = d.long ? ringFolds(u, y, 0.035, seed + 8) * smooth(0.2, 0.05, fromEnd) : 0;
      hgt += 0.9 * creaseB + 0.6 * creaseF + 0.4 * drape * smooth(0.2, 0.6, vl) + 0.9 * stack;
      valley += 0.6 * creaseB + 0.3 * creaseF + 0.25 * (1 - drape) * smooth(0.3, 0.7, vl) + 0.4 * stack;
      // the seams: out and in, a double stitch on denim
      for (const s of [0, 0.5]) { const xs = du(u, s) * d.LC; valley += 0.35 * ridge(xs, 0.0025); const st = stitch(Math.abs(xs) - 0.005, y); hgt += 0.1 * st; mix(o.c, thread, 0.5 * st); }
      if (denim) { // worn: paler on the front of the thigh and the knee, whiskers at the top of the thigh
        light += 0.18 * front * gauss(y - 0.12, 0.12) + 0.12 * front * gauss(y - kn, 0.05);
        light += 0.22 * front * smooth(0.2, 0.03, y) * Math.max(0, Math.sin((y / 0.02) * 2 * PI + 8 * du(u, 0.25))) ** 4;
        light += 0.12 * creaseB;
      }
      if (cargo) { // a bellows pocket on the outside of the thigh, with its flap
        const px = Math.abs(x), top = 0.2, bot = 0.4, ins = smooth(0.072, 0.068, px) * smooth(top - 0.003, top + 0.003, y) * smooth(bot + 0.003, bot - 0.003, y);
        const edge = Math.max(ridge(px - 0.07, 0.003) * smooth(top - 0.005, top, y) * smooth(bot + 0.005, bot, y), ridge(y - bot, 0.003) * smooth(0.075, 0.068, px));
        const flap = smooth(0.078, 0.074, px) * smooth(top - 0.02, top - 0.016, y) * smooth(top + 0.03, top + 0.026, y), flapEdge = ridge(y - (top + 0.028), 0.003) * smooth(0.078, 0.07, px);
        hgt += 0.3 * ins + 0.35 * flap - 0.3 * edge - 0.3 * flapEdge; valley += 0.5 * edge + 0.6 * flapEdge; shade(o, 1 - 0.04 * ins);
      }
      const hemW = 0.025;
      if (fromEnd < hemW) hgt += 0.12;
      const st = stitch(fromEnd - hemW, u * d.LC); hgt += 0.1 * st; mix(o.c, thread, 0.5 * st); valley += 0.25 * ridge(fromEnd - hemW, 0.002);
    } else if (v >= 0.8) { // ── the seat
      const vp = (v - 0.8) / 0.2, y = vp * d.PH, xb = du(u, 0.75) * d.PC;
      // the waistband and its belt loops
      const wb = 0.04;
      if (y < wb) { hgt += 0.1; shade(o, 0.97); }
      valley += 0.35 * ridge(y - wb, 0.002); { const st = stitch(y - wb - 0.004, u * d.PC); hgt += 0.1 * st; mix(o.c, thread, 0.5 * st); }
      for (const lu of [0.1, 0.4, 0.62, 0.75, 0.88]) { const lx = du(u, lu) * d.PC, loop = smooth(0.007, 0.005, Math.abs(lx)) * smooth(wb + 0.008, wb + 0.004, y); hgt += 0.4 * loop; valley += 0.3 * ridge(Math.abs(lx) - 0.007, 0.0015) * smooth(wb + 0.01, wb, y); }
      // back pockets: stitched outlines (a pointed bottom), a little proud
      for (const sx of [-1, 1]) {
        const px = xb - sx * 0.075, top = wb + 0.035, bot = top + 0.13, w0 = 0.065, wy = top + (bot - top) * 0.75;
        const tipY = (x) => wy + (bot - wy) * (1 - Math.abs(x) / w0);
        const ins = smooth(w0 + 0.002, w0 - 0.002, Math.abs(px)) * smooth(top - 0.002, top + 0.002, y) * smooth(tipY(px) + 0.003, tipY(px) - 0.003, y);
        const edge = Math.max(ridge(Math.abs(px) - w0, 0.0025) * smooth(top - 0.002, top + 0.004, y) * smooth(wy + 0.004, wy - 0.002, y), ridge(y - tipY(px), 0.0025) * smooth(w0 + 0.004, w0 - 0.002, Math.abs(px)), ridge(y - top, 0.0025) * smooth(w0 + 0.003, w0 - 0.003, Math.abs(px)));
        hgt += 0.2 * ins - 0.25 * edge; valley += 0.45 * edge; mix(o.c, thread, 0.45 * edge);
      }
      // the crotch: folds fanning from it, the seam down the middle at front and back
      const fan = foldNoise(u, vp * 0.3, 8, seed + 21) * smooth(0.5, 1, vp);
      hgt += 0.4 * fan; valley += 0.2 * (1 - fan) * smooth(0.5, 1, vp);
      for (const s of [0.25, 0.75]) valley += 0.3 * ridge(du(u, s) * d.PC, 0.0025) * smooth(0.25, 0.4, vp);
      if (denim) light += 0.1 * gauss(du(u, 0.75), 0.08) * smooth(0.4, 0.9, vp);
    }
    o.c[0] += (1 - o.c[0]) * light * 0.6; o.c[1] += (1 - o.c[1]) * light * 0.7; o.c[2] += (1 - o.c[2]) * light * 0.8;
    o.h = hgt; o.ro = ro; o.ao = 1 - 0.32 * clamp(valley);
    shade(o, 1 - 0.18 * clamp(valley));
  });
}

/**
 * skate shoes: the upper (v 0..0.78 heel to toe, u round the shoe, 0.25 on top, 0 and 0.5 the sides) and the cupsole's
 * wall (v 0.8..1 heel to toe, u round it). A suede upper, a mudguard band round its foot, a heel panel, a plain straight
 * side stripe, laces over the eyestay; the sole a vulcanised wall with a line round it.
 */
export function shoeTex(spec) {
  const C = spec.shoe ?? {}, up = srgb(spec.colors.shoes), guard = srgb(C.guard ?? spec.colors.shoes), heel = srgb(C.heel ?? spec.colors.accent), stripe = srgb(C.stripe ?? spec.colors.accent);
  const lace = srgb(C.lace ?? 0xf4f2ec), sole = srgb(C.sole ?? 0xf0ece0), line = srgb(C.line ?? spec.colors.accent), seed = spec.name.length * 11 + 5;
  return paint(256, 256, (u, v, o) => {
    const grain = fbm(u, v, 16, 3, seed) - 0.5, fz = fbm(u, v, 48, 1, seed + 1) - 0.5;
    const a = u * 2 * PI, s = Math.sin(a), side = Math.abs(Math.cos(a));
    let hgt = 0.03 * fz, ro = 0.93, valley = 0;
    if (v < 0.78) {
      const t = v / 0.78; // heel 0 .. toe 1
      o.c[0] = up[0]; o.c[1] = up[1]; o.c[2] = up[2];
      let k = 0.94 + 0.1 * grain + 0.04 * fz; // suede: a soft nap
      // the mudguard band round the foot of the upper and the toe cap
      const g = smooth(0.0, -0.06, s), toe = smooth(0.8, 0.83, t);
      mix(o.c, guard, Math.max(g, toe));
      valley += 0.4 * ridge(s + 0.03, 0.02) * (1 - toe) + 0.4 * ridge(t - 0.815, 0.006);
      // the heel panel
      const hp = smooth(0.2, 0.17, t) * smooth(-0.1, 0.0, s);
      mix(o.c, heel, hp); valley += 0.35 * ridge(t - 0.185, 0.006) * smooth(-0.1, 0, s);
      // the side stripe: straight, rounded at both ends, on both sides
      const sy = (s - 0.17) / 0.07, sx = (t - 0.45) / 0.22, st = smooth(1.05, 0.95, Math.hypot(Math.max(0, Math.abs(sx) - 0.7) / 0.3, sy)) * smooth(0.3, 0.5, side);
      mix(o.c, stripe, st); hgt += 0.1 * st; valley += 0.3 * ridge(Math.hypot(Math.max(0, Math.abs(sx) - 0.7) / 0.3, sy) - 1, 0.08) * smooth(0.3, 0.5, side);
      // the eyestay and laces on top
      const ux = du(u, 0.25), onTop = smooth(0.1, 0.07, Math.abs(ux)) * smooth(0.3, 0.33, t) * smooth(0.74, 0.71, t);
      if (onTop > 0) {
        k *= 1 - 0.1 * onTop;
        const ph = ((t - 0.33) / 0.065) % 1, bar = smooth(0.42, 0.3, Math.abs(ph - 0.5)) * smooth(0.075, 0.06, Math.abs(ux));
        mix(o.c, lace, bar * onTop); hgt += 0.7 * bar * onTop; valley += 0.4 * (1 - bar) * onTop * smooth(0.06, 0.03, Math.abs(ux));
        const eye = smooth(0.012, 0.008, Math.hypot((Math.abs(ux) - 0.07) * 1.3, (ph - 0.5) * 0.065)) * onTop;
        mix(o.c, [0.1, 0.1, 0.1], eye); valley += 0.3 * eye;
      }
      // stitches along the guard and the toe cap
      const dash = smooth(0.25, 0.45, 0.5 + 0.5 * Math.sin(t * 2 * PI * 45));
      hgt += 0.15 * dash * (ridge(s + 0.075, 0.012) * (1 - toe) + ridge(t - 0.83, 0.004));
      k *= 1 - 0.18 * clamp(valley);
      o.c[0] *= k; o.c[1] *= k; o.c[2] *= k;
    } else if (v >= 0.8) { // the cupsole's wall: rubber, a little shinier, ribbed, a line round it
      o.c[0] = sole[0]; o.c[1] = sole[1]; o.c[2] = sole[2];
      const rib = Math.max(0, Math.sin(s * 30)) ** 2 * smooth(0.6, 0.4, Math.abs(s));
      const ln = smooth(0.07, 0.04, Math.abs(s - 0.35));
      mix(o.c, line, ln);
      hgt += 0.3 * rib; valley += 0.2 * rib;
      if (s < -0.75) { o.c[0] *= 0.35; o.c[1] *= 0.35; o.c[2] *= 0.35; } // the tread underneath
      const k = (0.95 + 0.06 * grain) * (1 - 0.12 * clamp(valley));
      o.c[0] *= k; o.c[1] *= k; o.c[2] *= k;
      ro = 0.6 + 0.08 * fz;
    } else { o.c[0] = sole[0]; o.c[1] = sole[1]; o.c[2] = sole[2]; ro = 0.6; }
    o.h = hgt; o.ro = ro; o.ao = 1 - 0.3 * clamp(valley);
  });
}

/** socks: white ribbed knit with two stripes near the top (v 0 the top edge, 1 the ankle) */
export function sockTex(spec) {
  const base = srgb(spec.sock?.color ?? 0xf2f0ea), band = srgb(spec.sock?.stripe ?? spec.colors.accent), seed = spec.name.length * 13 + 1;
  return paint(128, 128, (u, v, o) => {
    const rib = 0.5 + 0.5 * Math.sin(u * 40 * 2 * PI), g = fbm(u, v, 8, 2, seed) - 0.5;
    o.c[0] = base[0]; o.c[1] = base[1]; o.c[2] = base[2];
    const st = spec.sock?.stripes === false ? 0 : Math.max(smooth(0.03, 0.02, Math.abs(v - 0.16)), smooth(0.03, 0.02, Math.abs(v - 0.26)));
    mix(o.c, band, st);
    const k = 0.9 + 0.08 * rib + 0.06 * g; o.c[0] *= k; o.c[1] *= k; o.c[2] *= k;
    o.h = 0.3 * rib + (v < 0.06 ? 0.2 : 0); o.ro = 0.95; o.ao = 0.9 + 0.1 * rib;
  });
}

/** write a rider's garment sets into dir: shirt_<name>, pants_<name>, shoe_<name>, sock_<name> */
export function writeGarments(dir, spec, dims) {
  writeSet(dir, 'shirt_' + spec.name, shirtTex(spec, dims.shirt), 5);
  writeSet(dir, 'pants_' + spec.name, pantsTex(spec, dims.pants), 5);
  writeSet(dir, 'shoe_' + spec.name, shoeTex(spec), 3);
  writeSet(dir, 'sock_' + spec.name, sockTex(spec), 2);
}
