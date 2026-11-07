// The ground's shape (map.c: map_floor_d, map_plaza_d, map_feat_at, map_ground_h, map_walkable), for gen-maps.mjs
// and check-maps.mjs. Points in tile units over the grid (tile (x, y) spans [x, x + 1] x [y, y + 1]).
//   fields({ w, h, tile(x, y) -> '.', 'g', 'o', ..., routes: [[[x, y, w], ...], ...], core: [x, y], slots: [{ x, y }],
//            feats: [{ kind, x, y, r, h }] })
export const GROUND_H = 0.5, CURB = 0.25, INSET = 0.15, PAD_R = 0.5;
export const WATER_LINE = (GROUND_H + 0.12) / (GROUND_H + 0.6);

// a route's curve as segments (the generator's Catmull-Rom at a fifth of a tile; the first runs on 10 tiles out of
// its gate the way it comes in)
export function routeSegments(wp, route = 0) {
  const segs = [], n = wp.length;
  let prev = null;
  for (let i = 1; i < n; i++) {
    const p0 = wp[Math.max(i - 2, 0)], p1 = wp[i - 1], p2 = wp[i], p3 = wp[Math.min(i + 1, n - 1)];
    const len = Math.hypot(p2[0] - p1[0], p2[1] - p1[1]), k1 = Math.max(1, Math.ceil(len / 0.2));
    for (let k = 0; k < k1; k++) {
      const t = k / k1, t2 = t * t, t3 = t2 * t;
      const c = (a) => 0.5 * (2 * p1[a] + (-p0[a] + p2[a]) * t + (2 * p0[a] - 5 * p1[a] + 4 * p2[a] - p3[a]) * t2 + (-p0[a] + 3 * p1[a] - 3 * p2[a] + p3[a]) * t3);
      const q = [c(0), c(1), p1[2] + (p2[2] - p1[2]) * t];
      if (!prev) {
        const dx = p2[0] - p1[0], dy = p2[1] - p1[1], l = Math.hypot(dx, dy);
        if (l > 1e-4) segs.push([q[0] - (dx / l) * 10, q[1] - (dy / l) * 10, q[0], q[1], q[2], q[2], route]);
      } else segs.push([prev[0], prev[1], q[0], q[1], prev[2], q[2], route]);
      prev = q;
    }
  }
  const e = wp[n - 1];
  if (prev) segs.push([prev[0], prev[1], e[0], e[1], prev[2], e[2], route]);
  return segs;
}

const smin = (a, b, k) => { const h = Math.max(k - Math.abs(a - b), 0) / k; return Math.min(a, b) - h * h * k * 0.25; };
const smax = (a, b, k) => -smin(-a, -b, k);
const bump = (t) => (t >= 1 ? 0 : 0.5 + 0.5 * Math.cos(Math.PI * t));

export function fields(m) {
  const PAD = 6, gw = m.w + 2 * PAD, gh = m.h + 2 * PAD;
  const segs = m.routes.flatMap((wp, r) => routeSegments(wp, r));
  const bins = Array.from({ length: gw * gh }, () => []);
  for (const s of segs) {
    const r = Math.max(s[4], s[5]) / 2 + 3;
    const x0 = Math.floor(Math.min(s[0], s[2]) - r) + PAD, x1 = Math.floor(Math.max(s[0], s[2]) + r) + PAD;
    const y0 = Math.floor(Math.min(s[1], s[3]) - r) + PAD, y1 = Math.floor(Math.max(s[1], s[3]) + r) + PAD;
    for (let y = Math.max(y0, 0); y <= Math.min(y1, gh - 1); y++) for (let x = Math.max(x0, 0); x <= Math.min(x1, gw - 1); x++) bins[y * gw + x].push(s);
  }
  const floorD = (x, y) => {
    let d = 3, wd = 2;
    if (!m.routes.length) {
      d = m.tile(Math.floor(x), Math.floor(y)) === '.' ? -0.5 : 0.6;
    } else {
      const best = m.routes.map(() => 3), bw = m.routes.map(() => 2);
      const cx = Math.floor(x) + PAD, cy = Math.floor(y) + PAD;
      if (cx >= 0 && cy >= 0 && cx < gw && cy < gh)
        for (const s of bins[cy * gw + cx]) {
          const ex = s[2] - s[0], ey = s[3] - s[1], l2 = ex * ex + ey * ey;
          const t = l2 > 1e-8 ? Math.min(1, Math.max(0, ((x - s[0]) * ex + (y - s[1]) * ey) / l2)) : 0;
          const w = s[4] + (s[5] - s[4]) * t, sd = Math.hypot(s[0] + ex * t - x, s[1] + ey * t - y) - (w / 2 - INSET);
          if (sd < best[s[6]]) best[s[6]] = sd, bw[s[6]] = w;
        }
      let bmin = 9;
      for (let r = 0; r < m.routes.length; r++) {
        if (best[r] < bmin) bmin = best[r], wd = bw[r];
        d = r ? smin(d, best[r], 0.6) : best[r];
      }
      d = smin(d, Math.hypot(x - (m.core[0] + 1), y - (m.core[1] + 1)) - 1.2, 0.6);
    }
    if (d < 1.2)
      for (const s of m.slots) {
        const sx = x - (s.x + 0.5), sy = y - (s.y + 0.5), r2 = sx * sx + sy * sy;
        if (r2 > 6.25) continue;
        d = smax(d, PAD_R + CURB - Math.sqrt(r2), 0.25);
      }
    return { d, w: wd };
  };
  const featAt = (x, y) => {
    let rock = 0, water = 0;
    for (const q of m.feats) { const b = bump(Math.hypot(x - q.x, y - q.y) / q.r); if (q.kind === 'rock') rock = Math.max(rock, q.h * Math.pow(b, 0.55)); else water = Math.max(water, b); }
    return { rock, water };
  };
  // the ground's height without the outcrops' weathering (map.c adds a little noise over every outcrop: props and
  // paths never stand on one)
  const groundH = (x, y) => {
    const { d } = floorD(x, y), t = Math.min(1, Math.max(0, d / CURB));
    let h = GROUND_H * t * t * (3 - 2 * t);
    const tl = m.tile(Math.floor(x), Math.floor(y));
    if (tl === '#') h = Math.max(h, 1.6);
    if (tl === '~') h = Math.min(h, -0.4);
    const { rock, water } = featAt(x, y);
    if (rock > 0) h += rock * (h / GROUND_H);
    return water > 0 ? Math.min(h, GROUND_H - (GROUND_H + 0.6) * water) : h;
  };
  const walkable = (x, y) => m.tile(Math.floor(x), Math.floor(y)) === '.' && floorD(x, y).d <= 0;
  const plazaD = (x, y) => { const { d, w } = floorD(x, y); return Math.max(d + CURB + 0.1, (4.4 - w) * 0.5); };
  return { floorD, featAt, groundH, walkable, plazaD, segs };
}

// the gates whose creeps cannot walk the floor to the core with room to pass (a sixth of a tile's grid, each cell
// walkable at its middle and CLEAR tiles round it): [] when every gate gets there
export function floorBlocked(m, F, CLEAR = 0.2) {
  const S = 6, W = m.w * S, H = m.h * S;
  const ok = new Uint8Array(W * H);
  const ring = Array.from({ length: 8 }, (_, k) => [Math.cos((k * Math.PI) / 4) * CLEAR, Math.sin((k * Math.PI) / 4) * CLEAR]);
  for (let j = 0; j < H; j++)
    for (let i = 0; i < W; i++) {
      const x = (i + 0.5) / S, y = (j + 0.5) / S;
      if (!F.walkable(x, y)) continue;
      // the gates' mouths run off the grid: the ring may leave it there
      ok[j * W + i] = ring.every(([a, b]) => { const px = x + a, py = y + b; return px < 0 || py < 0 || px >= m.w || py >= m.h || F.walkable(px, py); }) ? 1 : 0;
    }
  const [cx, cy] = [m.core[0] + 1, m.core[1] + 1];
  const seen = new Uint8Array(W * H), q = [];
  for (let j = 0; j < H; j++) for (let i = 0; i < W; i++) if (ok[j * W + i] && Math.hypot((i + 0.5) / S - cx, (j + 0.5) / S - cy) < 0.7) seen[j * W + i] = 1, q.push(j * W + i);
  while (q.length) {
    const c = q.pop(), i = c % W, j = (c - i) / W;
    for (let b = -1; b <= 1; b++) for (let a = -1; a <= 1; a++) {
      const ni = i + a, nj = j + b, n = nj * W + ni;
      if (ni < 0 || nj < 0 || ni >= W || nj >= H || seen[n] || !ok[n]) continue;
      seen[n] = 1, q.push(n);
    }
  }
  const bad = [];
  for (const [gx, gy] of m.spawn) {
    const side = gx === 0 || gx === m.w - 1;
    const x = side ? gx + 0.5 : gx + 1, y = side ? gy + 1 : gy + 0.5;
    let reached = false;
    for (let j = Math.floor((y - 0.5) * S); j <= Math.floor((y + 0.5) * S) && !reached; j++)
      for (let i = Math.floor((x - 0.5) * S); i <= Math.floor((x + 0.5) * S) && !reached; i++) if (i >= 0 && j >= 0 && i < W && j < H && seen[j * W + i]) reached = true;
    if (!reached) bad.push([gx, gy]);
  }
  return bad;
}
