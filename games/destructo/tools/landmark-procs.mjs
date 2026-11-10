/* destructo's hand-shaped landmarks, for tools/voxelize.mjs (a landmark with no openly licensed model good enough):
 * each a shape in the landmark's real metres, `cell(x, y, z)` giving what fills a point ("material:PAINT") or null,
 * with `ext`, the box it fills in game metres (x, y, z; centred on x and z, its foot on y = 0). The configuration
 * (tools/landmark-models.json) names it with "proc" and sets "size" to the same box. */

const sq = (x) => x * x;
const clamp01 = (x) => Math.max(0, Math.min(1, x));

/* ── the Great Sphinx of Giza (73 m long, 20 m high): a lion lying on the rock, its back dipping behind the raised
 * shoulders and rising again to the haunches, the forelegs stretched out in front ending in paws, the tall head in
 * its striped headdress (the nemes) wider than the neck, the face with brow, nose and mouth in relief. Carved from
 * one rock: welded (a hollow skin of bricks). Facing +x; real metres ── */
const SPH_S = 0.24;
const SPH_X0 = -37, SPH_X1 = 38, SPH_Z = 9.5;
function sphinx(x, y, z) {
  const X = x / SPH_S, Y = y / SPH_S, Z = z / SPH_S, aZ = Math.abs(Z);
  /* the forelegs and paws: two, stretched out from the chest, a hollow between them */
  if (X > 9 && X < 38 && aZ > 1.4 && aZ < 7.2) {
    let h = X < 28 ? 4.2 : 4.2 * Math.sqrt(clamp01(1 - sq((X - 28) / 10)));
    if (aZ > 6.4) h *= 0.75;
    if (Y < h) return 'stone:SAND_STONE';
  }
  if (X > 9 && X < 12 && aZ <= 1.4 && Y < 3.2) return 'stone:SAND_STONE'; /* between the legs at the chest */
  /* the body: the back dips behind the shoulders and rises to the haunches, rounded at the rump */
  let top = 0;
  if (X > -36 && X <= -22) top = 13.0 * Math.pow(clamp01(1 - sq((X + 22) / 14)), 0.35);
  else if (X > -22 && X <= 9) { const t = (X + 22) / 31; top = 13.0 - 2.6 * Math.sin(Math.PI * t) + 1.0 * t; }
  if (X > -36 && X < 12 && Y < top) {
    let hw = 7.5;
    if (X < -22) hw = 7.5 + 1.0 * clamp01((X + 36) / 10) * Math.sqrt(clamp01(1 - sq((X + 30) / 7)));  /* the haunches bulge */
    if (X > -22 && X <= 9) hw = 7.5 - 1.0 * Math.sin(Math.PI * (X + 22) / 31);                           /* the waist draws in */
    hw *= Math.sqrt(clamp01(1 - sq(Math.max(0, Y - top + 4) / 4.6)));                                    /* the back rounded across */
    if (aZ < hw) return Y < 5 ? 'stone:LIMESTONE' : 'stone:SAND_STONE';
  }
  /* the chest: square under the head, the shoulders up to the neck */
  if (X >= 9 && X < 14.5 && Y < 14.0 && aZ < 6.2 - 1.6 * clamp01((Y - 8) / 6)) return 'stone:SAND_STONE';
  /* the neck, narrower than the headdress above it */
  if (X > 8 && X < 15.5 && Y >= 14.0 && Y < 15.5 && aZ < 3.2) return 'stone:SAND_STONE';
  /* the headdress (nemes): broad lappets down to the shoulders, the hood over the head, rounded at the crown and
   * at the back, striped */
  const stripe = Math.floor((Y - 13) / 1.6) & 1 ? 'stone:SAND_STONE' : 'stone:TRIM_CREAM';
  if (Y >= 13.0 && Y < 21.2 && X > 6.0 && X < 16.0) {
    let hw = Y < 17.5 ? 5.4 - 0.5 * clamp01((Y - 13) / 4.5) : 4.9 * Math.sqrt(clamp01(1 - sq((Y - 17.5) / 3.9)));
    const back = 6.0 + 2.6 * clamp01((Y - 16) / 5.2); /* the back of the hood curves forward at the crown */
    if (X > back && aZ < hw && (Y >= 14.0 || aZ > 3.2)) return stripe;
  }
  /* the face: a flat plane with the brow standing out, the eyes set in, the nose, the lips and the chin */
  if (X >= 16.0 && X < 17.6 && Y >= 13.6 && Y < 20.0 && aZ < 2.9 * Math.sqrt(clamp01(1 - sq((Y - 16.8) / 3.6)))) {
    if (X > 16.8 && Y > 17.3 && Y < 18.3 && aZ > 0.8 && aZ < 2.2) return null;           /* the eyes */
    return 'stone:SAND_STONE';
  }
  if (X >= 17.6 && X < 18.2 && Y >= 18.3 && Y < 19.0 && aZ < 2.4) return 'stone:SAND_STONE'; /* the brow */
  if (X >= 17.6 && X < 18.8 && Y >= 15.6 && Y < 17.6 && aZ < 0.8) return 'stone:SAND_STONE';  /* the nose */
  if (X >= 17.6 && X < 18.1 && Y >= 14.6 && Y < 15.1 && aZ < 1.4) return 'stone:SAND_STONE';  /* the lips */
  if (X >= 17.6 && X < 18.0 && Y >= 13.6 && Y < 14.2 && aZ < 1.2) return 'stone:SAND_STONE';  /* the chin */
  return null;
}

/* ── the Eiffel Tower (324 m): four lattice legs leaning in under the arches to the first platform, the second
 * platform where they meet, the single lattice shaft up to the top platform, the spire. Open ironwork: corner posts,
 * horizontal bars every bay and a diagonal in each bay, every member one brick thick and the bays open. Welded; real
 * metres, cells of 3 m ── */
const EI_S = 0.05, EI_C = 3; /* game metres a metre; a cell in real metres */
const eiW = (Y) => Math.max(62 * Math.exp(-Y / 95), 6);          /* the outer half-width at height Y */
const eiIn = (Y) => Y < 105 ? 37 * Math.pow(1 - Y / 105, 0.8) : 0; /* a leg's inner edge */
function eiffel(x, y, z) {
  const X = x / EI_S, Y = y / EI_S, Z = z / EI_S, aX = Math.abs(X), aZ = Math.abs(Z);
  const w = eiW(Y), inn = eiIn(Y), c = EI_C;
  const row = Math.floor(Y / c), bay = 4, inBay = row % bay, bar = inBay === 0;
  const IRON = 'steel:IRON';
  /* the platforms: a slab a course thick, out past the ironwork */
  if (Y >= 57 && Y < 57 + c && aX < eiW(57) + 4 && aZ < eiW(57) + 4) return IRON;
  if (Y >= 115 && Y < 115 + c && aX < eiW(115) + 3 && aZ < eiW(115) + 3) return IRON;
  /* the top: the cabin and the spire */
  if (Y >= 276 && Y < 290) return aX < 7 && aZ < 7 && (aX > 7 - c || aZ > 7 - c || Y < 276 + c || Y >= 290 - c) ? IRON : null;
  if (Y >= 290 && Y < 324) return Math.hypot(X, Z) < (Y < 300 ? 2.2 : 1.6) ? IRON : null;
  if (Y >= 276) return null;
  if (aX > w || aZ > w) return null;
  if (Y < 110) {
    /* four legs: each a square tube from inn to w on both axes */
    if (aX < inn || aZ < inn) {
      /* the arches between the legs under the first platform, on the outer faces */
      const onFace = (aX > w - c && aZ < inn) || (aZ > w - c && aX < inn);
      const u = aX > w - c ? aZ : aX;
      const r = Math.hypot(u, Y - 18);
      if (onFace && Y > 18 && Y < 56 && Math.abs(r - 37) < c * 0.7) return IRON;
      return null;
    }
    const xEdge = aX > w - c || aX < inn + c, zEdge = aZ > w - c || aZ < inn + c;
    if (xEdge && zEdge) return IRON;                       /* the corner posts */
    if (xEdge || zEdge) {
      if (bar) return IRON;                                /* a horizontal bar each bay */
      /* one diagonal across the bay, its direction alternating bay to bay */
      const along = xEdge ? aZ : aX, span = w - inn - 2 * c, t = (along - inn - c) / span;
      const dir = (Math.floor(row / bay) & 1) ? 1 - t : t;
      if (span > 0 && Math.abs(dir * bay - inBay - 0.5) < 0.5) return IRON;
    }
    return null;
  }
  /* the shaft above the second platform: one lattice tower */
  const xEdge = aX > w - c, zEdge = aZ > w - c;
  if (xEdge && zEdge) return IRON;
  if (xEdge || zEdge) {
    if (bar) return IRON;
    const along = xEdge ? Z : X, t = (along + w) / (2 * w);
    const dir = (Math.floor(row / bay) & 1) ? 1 - t : t;
    if (Math.abs(dir * bay - inBay - 0.5) < 0.5) return IRON;
  }
  return null;
}

/* ── the Space Needle (184 m): three pairs of white legs splayed at the foot, pinched to a waist a third of the way
 * up and flaring again to hold the saucer; the dark lattice of the lift shaft in the middle; the saucer's ribbed
 * underside, its ring of glass, the halo and the orange cap; the spire. Welded steel (the legs alone cannot stand as
 * masonry) ── */
const SN_S = 0.09;
function needle(x, y, z) {
  const X = x / SN_S, Y = y / SN_S, Z = z / SN_S, r = Math.hypot(X, Z);
  if (Y < 2) return r < 22 && Math.abs(X) < 20 && Math.abs(Z) < 20 ? 'stone:CONCRETE' : null; /* the plaza */
  /* the saucer: hollow (a ring of rooms round the shaft), its ribbed underside, the glass, the halo, the cap */
  if (Y >= 145 && Y < 163) {
    if (r < 3) return 'steel:WHITE_PAINT'; /* the shaft goes on up through it */
    if (Y < 152) { const ro = 5 + (Y - 145) * 2.3; return r < ro && r < 21 && r > ro - 2.2 && (Math.floor(Math.atan2(Z, X) / 0.2) % 2 === 0 || Y > 150.5) ? 'steel:WHITE_PAINT' : null; }
    if (Y < 153) return r < 20.5 ? 'steel:WHITE_PAINT' : null;
    if (Y < 156) return r < 20.5 && r > 18.5 ? 'glass:DARK_GLASS' : null;
    if (Y < 157.5) return r < 22.5 && r > 15.5 ? 'steel:WHITE_PAINT' : null; /* the halo */
    if (Y < 159) return r < 18 && r > 16 ? 'glass:DARK_GLASS' : null;
    const rc = 17 - (Y - 159) * 3.2;
    return r < rc && r > rc - 2.2 ? 'steel:ROOF_RED' : null; /* the cap */
  }
  if (Y >= 163) return Y < 184 && r < (Y < 168 ? 2.6 : 1.0) ? 'steel:WHITE_PAINT' : null; /* the spire */
  /* the lift shaft: dark, solid (it carries the saucer, and a stack of short welded members would sag) */
  if (Math.abs(X) < 3.2 && Math.abs(Z) < 3.2) return 'steel:BRONZE';
  /* the legs: radius of the pair's middle from the axis, and the spread between its two columns */
  const rl = Y < 60 ? 4 + 15 * Math.pow(1 - Y / 60, 1.5) : 4 + 9.5 * sq((Y - 60) / 87);
  const spread = Y < 60 ? 1.8 + 4.5 * Math.pow(1 - Y / 60, 1.3) : 1.8 + 2.5 * ((Y - 60) / 87);
  for (let p = 0; p < 3; p++) {
    const a = Math.PI / 2 + p * 2.0943951, ca = Math.cos(a), sa = Math.sin(a);
    const along = X * ca + Z * sa, across = -X * sa + Z * ca;
    if (Math.abs(along - rl) < 1.3 && Math.abs(Math.abs(across) - spread) < 1.1) return 'steel:WHITE_PAINT';
    /* the ties between leg and shaft every so often (the landings) */
    if (Y > 8 && Math.abs(Y - 100) < 0.9 && along > 3 && along < rl && Math.abs(across) < 0.8) return 'steel:WHITE_PAINT';
  }
  return null;
}

/* ── Neuschwanstein Castle, as from the bridge across the gorge: the tall white palace (the palas) under its steep
 * slate roof and stepped gable, rows of arched windows, the slender stair tower rising behind it to a spire, the
 * turret on its front, the lower wing, the square tower with its battlements and turret, the red gatehouse, all on a
 * crag. Game metres (solid: the voxeliser keeps a skin of small bricks over a core of big ones) ── */
const NS_ROCK = 1.4;
const inCyl = (x, z, cx, cz, r) => sq(x - cx) + sq(z - cz) < r * r;
function nsTower(x, y, z, cx, cz, r, top, cone, paintWall) {
  if (!inCyl(x, z, cx, cz, r + 0.14)) return undefined;
  if (y < top) {
    if (!inCyl(x, z, cx, cz, r) && !(y > top - 0.42 && y < top)) return undefined; /* a corbelled gallery at the top */
    if (y > top - 1.2 && y < top - 0.6 && ((Math.floor(Math.atan2(z - cz, x - cx) * 3) & 1) === 0)) return 'stone:DARK_GLASS';
    return 'stone:' + paintWall;
  }
  const t = (y - top) / cone;
  return t < 1 && inCyl(x, z, cx, cz, (r + 0.14) * (1 - t)) ? 'stone:ROOF_SLATE' : undefined;
}
function nsWindows(u, y, base, storey) { /* arched windows in rows: u along the face */
  const fy = (y - base) % storey, col = ((u % 0.6) + 0.6) % 0.6;
  if (y < base + 0.3) return false;
  if (col < 0.36 && col > 0.12 && fy > 0.25 && fy < 0.75) return !(fy > 0.66 && (col < 0.16 || col > 0.32)); /* the round head */
  return false;
}
function neuschwanstein(x, y, z) {
  const az = Math.abs(z);
  /* the crag */
  const e = sq(x / 9.6) + sq(z / 3.6);
  if (e < 1) {
    let top = NS_ROCK;
    if (!(x > -7.0 && x < 9.0 && az < 2.0)) top = NS_ROCK * (1.1 - 0.9 * e) + 0.22 * Math.sin(x * 2.3 + z * 3.1);
    if (y < top) return 'stone:ROCK';
  }
  let c;
  if ((c = nsTower(x, y, z, -4.4, -2.0, 0.55, 9.4, 2.6, 'MARBLE'))) return c;   /* the north stair tower */
  if ((c = nsTower(x, y, z, -2.0, 1.85, 0.36, 7.4, 1.8, 'MARBLE'))) return c;   /* the turret on the front */
  if ((c = nsTower(x, y, z, -6.75, 1.75, 0.24, 6.6, 0.9, 'MARBLE'))) return c;  /* corner turrets */
  if ((c = nsTower(x, y, z, -6.75, -1.75, 0.24, 6.6, 0.9, 'MARBLE'))) return c;
  if ((c = nsTower(x, y, z, 5.6, 0, 0.32, 8.7, 0.7, 'MARBLE'))) return c;       /* the square tower's turret */
  if ((c = nsTower(x, y, z, 7.4, 1.0, 0.28, 4.6, 0.7, 'RED_BRICK'))) return c; /* the gatehouse's turrets */
  if (y < NS_ROCK) return null;
  /* the palace: walls to the eaves, the steep roof along x, the stepped gable at the front end */
  if (x > -6.6 && x < 0.6 && az < 1.75) {
    if (y < 6.3) return nsWindows(az > 1.6 ? x : z, y, NS_ROCK + 0.2, 0.95) && (az > 1.5 || x < -6.4 || x > 0.4) ? 'stone:DARK_GLASS' : 'stone:MARBLE';
    const t = (y - 6.3) / 2.5;
    if (x < -6.3) return t < 1 && az < 1.75 * (1 - Math.floor(t * 5) / 5) ? 'stone:MARBLE' : null; /* the gable, stepped */
    if (t < 1 && az < 1.8 * (1 - t)) {
      if (t < 0.45 && Math.abs(az - 1.0) < 0.18 && (Math.floor((x + 6.6) / 1.2) & 1) && y < 7.0) return 'stone:MARBLE'; /* dormers */
      return 'wood:ROOF_SLATE';
    }
    return null;
  }
  /* the lower wing to the square tower */
  if (x >= 0.6 && x < 5.0 && az < 1.35) {
    if (y < NS_ROCK + 0.7) return 'stone:LIMESTONE';
    if (y < 5.0) return nsWindows(az > 1.2 ? x : z, y, NS_ROCK + 0.7, 0.9) && az > 1.1 ? 'stone:DARK_GLASS' : 'stone:MARBLE';
    const t = (y - 5.0) / 1.6;
    return t < 1 && az < 1.4 * (1 - t) ? 'wood:ROOF_SLATE' : null;
  }
  /* the square tower, battlements on top */
  if (x >= 5.0 && x < 6.2 && az < 0.6) {
    if (y < 7.6) return nsWindows(az > 0.45 ? x : z, y, 4.2, 1.1) && az > 0.45 ? 'stone:DARK_GLASS' : 'stone:MARBLE';
    if (y < 7.9) return (az > 0.45 || x < 5.15 || x > 6.05) && (Math.floor((x + z) / 0.24) & 1) ? 'stone:MARBLE' : null;
    return null;
  }
  /* the gatehouse, red */
  if (x >= 6.2 && x < 8.4 && az < 1.2) {
    if (y < 4.2) return z > 1.0 && Math.abs(x - 7.3) < 0.35 && y < NS_ROCK + 0.9 ? null : 'stone:RED_BRICK';
    if (y < 4.45) return (az > 1.0 || x > 8.2) && (Math.floor(x / 0.24) & 1) ? 'stone:RED_BRICK' : null;
  }
  return null;
}

/* ── the Sydney Opera House (183 m long): on its granite podium two rows of white shells, each row nested sails
 * rising toward the harbour (-x), the open mouth of each glazed and set back, a shell at the back of each row facing
 * the other way, the restaurant's small pair apart; the shells welded (a thin vault cannot stand course on course),
 * the podium masonry. Real metres ── */
const OP_S = 0.1, OP_POD = 12;
/* [tip x, tip height over the podium, back foot x, half width at the foot, z of the row] */
const OP_SHELLS = [
  [-58, 62, -20, 19, 20], [-30, 52, 2, 17, 20], [-6, 40, 22, 15, 20], [40, 26, 16, 13, 20],
  [-52, 52, -16, 15, -24], [-26, 44, 4, 14, -24], [-4, 34, 22, 12, -24], [34, 24, 10, 11, -24],
];
function operaShell(X, Y, Z, s) {
  const [xt, ht, xb, w, zr] = s, dir = Math.sign(xb - xt); /* the tip toward -dir */
  const len = Math.abs(xb - xt), FR = 0.45; /* the front edge drops from the tip and curves out to the podium FR len ahead of it */
  const u = (X - xt) * dir;                                   /* behind the tip: 0..len; ahead: -FR len..0 */
  if (u < -FR * len || u > len) return null;
  const yr = u >= 0 ? ht * Math.pow(1 - u / len, 0.45) : ht * (1 - Math.sqrt(clamp01(-u / (FR * len)))); /* a sail: a sharp tip, the front edge falling near vertical from it and curving out to the podium, the ridge behind bulging down to the back foot */
  if (Y > yr || Y < 0) return null;
  const wb = 0.5 * w * (0.3 + 0.7 * Math.sqrt(Math.min(1, Math.max(0, u + FR * len) / ((1 + FR) * len) * 1.4))), dz = Math.abs(Z - zr); /* narrow at the tip, wider at the foot; thin vaults */
  const hw = wb * Math.sqrt(Math.max(0, 1 - Math.pow(Y / yr, 3))); /* the vault's sides near vertical, curving in only at the ridge */
  if (dz > hw + 0.1) return null;
  const skin = dz > hw - 2.0 || Y > yr - 2.2;
  /* the vault's foot: the shell springs from piers, the walls under it glazed and set back (a dark band along the
   * podium that parts each shell from the next) */
  if (Y < 5 && skin) return dz < hw - 1.2 ? 'glass:DARK_GLASS' : null;
  if (skin) return 'stone:SHELL_WHITE'; /* the vault, white tiles */
  if (u < 2.5) return 'glass:DARK_GLASS'; /* the glazed mouth under the tip */
  return null;
}
function opera(x, y, z, part) {
  const X = x / OP_S, Y = y / OP_S, Z = z / OP_S;
  if (Y < OP_POD) { /* the podium, rounded at the harbour end, the broad steps up from the city at the back */
    if (Math.abs(Z) > 48) return null;
    if (X < -70 && sq((X + 70) / 22) + sq(Z / 48) > 1) return null;
    if (X > 66) return Y < OP_POD * (1 - (X - 66) / 16) ? 'stone:PODIUM' : null;
    return 'stone:PODIUM';
  }
  for (const s of OP_SHELLS) { const c = operaShell(X, Y - OP_POD, Z, s); if (c) return c; }
  return null;
}

/* ── Tower Bridge (244 m): two Gothic towers on their piers, each with four corner turrets under pointed slate roofs
 * round a tall pyramid roof; the two high walkways between them; the bascule road low between the towers; the side
 * spans hung from chains that curve down from the towers to the shore towers. The steelwork (painted blue and white)
 * is welded, the towers are masonry. Real metres ── */
const TB_S = 0.1;
function tbSteel(X, Y, Z) {
  const aX = Math.abs(X), aZ = Math.abs(Z);
  /* the high walkways: two lattice girders between the towers, ends set into the towers */
  if (aX < 44 && Y >= 40 && Y < 46 && Math.abs(aZ - 5.5) < 2.2) {
    const face = Math.abs(aZ - 5.5) > 1.2, bar = Y < 41 || Y > 45;
    return bar || !face || Math.abs(((X + 100) % 6) - 3) < 0.8 + 0.5 * Math.abs(Y - 43) ? 'steel:TURQUOISE' : null;
  }
  /* the road: the bascules between the towers, the side spans out to the shore */
  if (Y >= 8 && Y < 10.5 && aZ < 9 && aX < 122) return Y > 9.7 ? 'steel:DARK_STONE' : 'steel:TURQUOISE';
  /* the side spans' chains: from each tower's top corner down to the road and up again to the shore tower */
  if (aX > 50 && aX < 112 && Math.abs(aZ - 8) < 1.1) {
    const t = (aX - 50) / 62, yc = 10.5 + 30 * sq(1 - t) * (1 - t) + 6 * t * t; /* sagging from 40 at the tower to 16 at the shore */
    if (Math.abs(Y - yc) < 1.3) return 'steel:WHITE_PAINT';
    if (Y > 10.5 && Y < yc && Math.abs(((aX - 50) % 6) - 3) < 0.6) return 'steel:TURQUOISE'; /* the hangers */
  }
  return null;
}
function tbTower(X, Y, Z, cx) {
  const dx = X - cx, adx = Math.abs(dx), aZ = Math.abs(Z);
  if (Y < 8) return adx < 13 && aZ < 18 ? 'stone:LIMESTONE' : null;                   /* the pier */
  if (Y < 50 && adx < 10 && aZ < 13) {                                                 /* the tower */
    const face = adx > 9 || aZ > 12, u = adx > 9 ? Z : dx;
    if (face && Y > 14 && Y < 48 && ((u + 40) % 5.5) < 1.6 && ((Y - 14) % 11) < 6) return 'stone:DARK_GLASS'; /* the tall windows */
    return 'stone:LIMESTONE';
  }
  /* corner turrets and their pointed roofs; the middle pyramid roof */
  for (const [sx, sz] of [[1, 1], [1, -1], [-1, 1], [-1, -1]]) {
    const r = Math.hypot(dx - sx * 7.4, Z - sz * 10.4);
    if (Y >= 50 && Y < 56 && r < 2.6) return 'stone:LIMESTONE';
    if (Y >= 56 && Y < 65 && r < 2.8 * (1 - (Y - 56) / 9)) return 'wood:ROOF_SLATE';
  }
  if (Y >= 50 && Y < 62) { const t = (Y - 50) / 12; return adx < 8 * (1 - t) && aZ < 10 * (1 - t) ? 'wood:ROOF_SLATE' : null; }
  return null;
}
function towerBridge(x, y, z) {
  const X = x / TB_S, Y = y / TB_S, Z = z / TB_S;
  const s = tbSteel(X, Y, Z);
  if (s) return s;
  for (const cx of [-40, 40]) { const t = tbTower(X, Y, Z, cx); if (t) return t; }
  /* the shore towers the chains come down to */
  if (Math.abs(Math.abs(X) - 116) < 5 && Math.abs(Z) < 11 && Y < 22) return Y < 10.5 && Math.abs(Z) < 9 ? 'stone:LIMESTONE' : 'stone:LIMESTONE';
  return null;
}

/* ── Saint Basil's Cathedral (Moscow): on a raised gallery of red brick, the tall central church under its tent roof
 * and little gold onion; round it four big octagonal chapels and four small ones, each under an onion dome painted
 * in its own spiral or stripes; the bell tower with its tent roof apart. Real metres ── */
const SB_S = 0.24;
const octR = (X, Z) => { const a = Math.abs(X), b = Math.abs(Z); return Math.max(a, b, (a + b) * 0.7071); }; /* an octagon's "radius" */
function onion(X, Y, Z, y0, R, H, scheme) {
  const t = (Y - y0) / H;
  if (t < 0 || t >= 1) return null;
  const r = t < 0.6 ? R * (0.78 + 0.42 * Math.sin(Math.PI * t / 0.72)) : R * 1.12 * Math.pow(1 - (t - 0.6) / 0.4, 1.2);
  const d = Math.hypot(X, Z);
  if (d >= r) return null;
  const ang = Math.atan2(Z, X);
  const [a, b, kind] = scheme;
  const band = kind === 'spiral' ? Math.floor(ang * 4 / Math.PI + t * 6) : kind === 'zig' ? Math.floor(t * 9 + Math.abs(((ang * 8 / Math.PI) % 2) - 1) * 1.5) : Math.floor(ang * 6 / Math.PI);
  return 'stone:' + ((band & 1) ? a : b);
}
function sbChapel(X, Y, Z, cx, cz, R, body, R2, scheme, oct) {
  const dx = X - cx, dz = Z - cz;
  const rr = oct ? octR(dx, dz) : Math.hypot(dx, dz);
  if (Y < body) {
    if (rr >= R) return null;
    const trim = Math.abs(((Y - 8) % 6) - 0.4) < 0.5 || rr > R - 0.9 && Math.abs(Math.abs(dx) - Math.abs(dz)) < 0.7;
    return trim ? 'stone:MARBLE' : 'stone:RED_BRICK';
  }
  if (Y < body + 3) return rr < R * 0.62 ? (Y < body + 0.8 ? 'stone:MARBLE' : 'stone:RED_BRICK') : null; /* the drum */
  const o = onion(dx, Y, dz, body + 3, R2, R2 * 2.3, scheme);
  if (o) return o;
  return Y < body + 3 + R2 * 2.3 + 1.2 && Math.hypot(dx, dz) < 0.9 ? 'stone:GOLD' : null; /* the gilded finial (a thin post would hum and never sleep) */
}
function stBasils(x, y, z) {
  const X = x / SB_S, Y = y / SB_S, Z = z / SB_S;
  if (Math.abs(X - 23) < 3.4 && Math.abs(Z + 19) < 3.4 && Y < 6) return 'stone:BRICK'; /* the bell tower's foot */
  if (Y < 6) { /* the gallery they all stand on */
    const r = octR(X, Z);
    if (r < 25) return 'stone:BRICK';
    return null;
  }
  let c;
  /* the central church: octagon, tent roof, gold onion */
  const ro = octR(X, Z);
  if (Y < 30) { if (ro < 7) return Math.abs(Y - 22) < 0.8 || Math.abs(Y - 15) < 0.6 ? 'stone:MARBLE' : 'stone:RED_BRICK'; }
  else if (Y < 45) { const t = (Y - 30) / 15; if (ro < 7.4 * (1 - t) + 1.4 * t) return (Math.floor((Y - 30) / 1.6) & 1) && ro > 6.6 * (1 - t) + 0.6 ? 'stone:GOLD' : 'stone:WHITE_PAINT'; }
  else if ((c = onion(X, Y, Z, 45, 1.9, 4.4, ['GOLD', 'GOLD', 'stripe']))) return c;
  else if (Y < 50.5 && Math.hypot(X, Z) < 0.9) return 'stone:GOLD';
  /* the four big chapels and their domes */
  const SCH = [['ROOF_RED', 'TURQUOISE', 'spiral'], ['GOLD', 'ROOF_COPPER', 'spiral'], ['WHITE_PAINT', 'ROOF_RED', 'zig'], ['TURQUOISE', 'GOLD', 'stripe'],
               ['ROOF_COPPER', 'GOLD', 'zig'], ['ROOF_RED', 'WHITE_PAINT', 'stripe'], ['GOLD', 'ROOF_RED', 'spiral'], ['TURQUOISE', 'WHITE_PAINT', 'spiral']];
  for (let k = 0; k < 4; k++) {
    const a = k * Math.PI / 2;
    if ((c = sbChapel(X, Y, Z, Math.cos(a) * 15, Math.sin(a) * 15, 5, 24, 4.4, SCH[k], true))) return c;
  }
  for (let k = 0; k < 4; k++) {
    const a = Math.PI / 4 + k * Math.PI / 2;
    if ((c = sbChapel(X, Y, Z, Math.cos(a) * 14, Math.sin(a) * 14, 3.2, 18, 3.0, SCH[4 + k], false))) return c;
  }
  /* the bell tower: square, then octagonal, then its tent roof */
  const bx = X - 23, bz = Z + 19;
  if (Math.abs(bx) < 3.4 && Math.abs(bz) < 3.4 && Y < 18) return 'stone:RED_BRICK';
  if (Y >= 18 && Y < 27 && octR(bx, bz) < 3.2) return Y > 22 && Y < 25.5 && octR(bx, bz) > 2.4 && (Math.floor(Math.atan2(bz, bx) * 4 / Math.PI) & 1) ? null : 'stone:MARBLE';
  if (Y >= 27 && Y < 37 && octR(bx, bz) < 3.2 * (1 - (Y - 27) / 10)) return 'stone:ROOF_COPPER';
  return null;
}

/* ── the Petronas Towers (452 m): twin towers whose plan is an eight-pointed star with round lobes between its
 * points, stepping back in tiers toward the top, rounding into drums and a pinnacle; silver bands of steel between
 * the bands of glass; the skybridge between them on its A-frame. Real metres ── */
const PT_S = 0.048;
function ptPlan(X, Z, s) { /* the star: two squares, one turned an eighth, and round lobes between their points */
  const a = Math.abs(X), b = Math.abs(Z), c = Math.abs(X + Z) * 0.7071, d = Math.abs(X - Z) * 0.7071;
  return (a < s && b < s) || (c < s && d < s) || Math.hypot(X, Z) < s * 1.08;
}
function ptTower(X, Y, Z) {
  const r = Math.hypot(X, Z);
  const tiers = [[175, 23], [255, 21], [300, 19], [340, 16.5], [370, 13]];
  let s = 0;
  for (const [y1, w] of tiers) if (Y < y1) { s = w; break; }
  const band = Y % 12.5 > 4.2 ? 'glass:BLUE_GLASS' : 'steel:SILVER'; /* (a band of steel every third floor: a welded frame of members a floor each is too soft this tall) */
  if (s) return ptPlan(X, Z, s) ? band : null;
  if (Y < 385) return r < 10 ? band : null;
  if (Y < 398) return r < 7 ? 'steel:SILVER' : null;
  if (Y < 452) { const t = (Y - 398) / 54; return r < 3.2 * (1 - t) + 0.5 || (Math.floor(Y / 6) % 3 === 0 && r < 2.6 * (1 - t) + 1.2) ? 'steel:SILVER' : null; } /* the pinnacle and its rings */
  return null;
}
function petronas(x, y, z) {
  const X = x / PT_S, Y = y / PT_S, Z = z / PT_S;
  if (Y < 12 && Math.abs(X) < 76 && Math.abs(Z) < 25) return 'stone:CONCRETE'; /* the podium they rise from */
  /* the skybridge (two decks) and its legs down to the towers */
  if (Math.abs(Z) < 3 && Y >= 170 && Y < 179 && Math.abs(X) < 33) return Y > 177 || Y < 171 ? 'steel:WHITE_PAINT' : 'glass:DARK_GLASS';
  for (const sx of [-1, 1]) {
    const t = (Y - 120) / 50, xl = sx * (31 - 29 * t);
    if (t >= 0 && t < 1 && Math.abs(X - xl) < 1.6 && Math.abs(Math.abs(Z) - 1.8) < 1.2) return 'steel:WHITE_PAINT';
  }
  for (const cx of [-52, 52]) { const c = ptTower(X - cx, Y, Z); if (c) return c; }
  return null;
}

/* ── the CN Tower (553 m): the Y of three tapering concrete wings round the hexagonal shaft, the main pod with its
 * ring of windows and the deck, the shaft going on to the small top pod and the antenna. Welded (one piece of
 * concrete). Real metres ── */
const CN_S = 0.043;
function hexR(X, Z) { const a = Math.abs(X), b = Math.abs(Z); return Math.max(b * 1.1547, (a * 0.866 + b * 0.5) * 1.1547); }
function cnTower(x, y, z) {
  const X = x / CN_S, Y = y / CN_S, Z = z / CN_S, r = Math.hypot(X, Z);
  /* the pod: the underside sloping out, the glass ring, the deck and its cap */
  if (Y >= 330 && Y < 368) {
    if (Y < 342) return r < 9 + (Y - 330) * 1.4 ? 'stone:CONCRETE' : null;
    if (Y < 352) return r < 24 ? (r > 21.5 ? 'glass:DARK_GLASS' : 'stone:CONCRETE') : null;
    if (Y < 356) return r < 25 ? 'stone:WHITE_PAINT' : null;
    if (Y < 362) return r < 20 ? (r > 18 ? 'glass:DARK_GLASS' : 'stone:CONCRETE') : null;
    return r < 18 - (Y - 362) * 1.6 ? 'stone:CONCRETE' : null;
  }
  if (Y >= 440 && Y < 452) return r < 8.5 ? (Y > 443 && Y < 449 && r > 7 ? 'glass:DARK_GLASS' : 'stone:CONCRETE') : null; /* the top pod */
  if (Y >= 452) { const t = (Y - 452) / 101; return t < 1 && r < 3.6 * (1 - t) + 0.9 ? (Math.floor(Y / 14) & 1 ? 'steel:WHITE_PAINT' : 'steel:SILVER') : null; } /* the antenna */
  /* the shaft: hexagonal, a little narrower going up */
  const core = 7.5 - 1.5 * Math.min(1, Y / 440);
  if (hexR(X, Z) < core) return 'stone:CONCRETE';
  /* the three wings: wide at the foot, merging into the shaft at the pod */
  if (Y < 330) {
    const reach = 7 + 26 * Math.pow(1 - Y / 330, 1.6), half = 3.2 + 1.5 * (1 - Y / 330);
    for (let w = 0; w < 3; w++) {
      const a = Math.PI / 2 + w * 2.0943951, along = X * Math.cos(a) + Z * Math.sin(a), across = Math.abs(-X * Math.sin(a) + Z * Math.cos(a));
      if (along > 0 && along < reach && across < half * (1 - 0.45 * along / reach)) return 'stone:CONCRETE';
    }
  }
  return null;
}

/* ── Angkor Wat, from the west: the temple-mountain of three rising terraces, each ringed by a gallery under its
 * roof, steep stairs up the middle of each side, the four corner towers and the tall central tower, each a lotus bud
 * of stacked tiers. Grey sandstone. Real metres ── */
const AW_S = 0.105;
function lotus(X, Y, Z, y0, R, H) { /* a tower shaped like a lotus bud, in tiers */
  const t = (Y - y0) / H;
  if (t < 0 || t >= 1) return null;
  let r = R * (t < 0.25 ? 1 : Math.pow(1 - (t - 0.25) / 0.75, 0.75) * 1.02);
  if (Math.floor(t * 11) & 1) r *= 0.9;                                        /* the tiers */
  return Math.max(Math.abs(X), Math.abs(Z)) * 0.85 + Math.hypot(X, Z) * 0.2 < r ? (Math.floor(t * 11) & 1 ? 'stone:DARK_STONE' : 'stone:LIMESTONE') : null;
}
function awGallery(aX, aZ, Y, hx, hz, y0, h) { /* a ring of gallery on a terrace: a wall with pillared openings under a roof */
  const onRing = (aX < hx && aZ < hz) && (aX > hx - 4.5 || aZ > hz - 4.5);
  if (!onRing || Y < y0 || Y >= y0 + h + 2.5) return null;
  if (Y >= y0 + h) return (aX < hx - (Y - y0 - h) * 1.2 && aZ < hz - (Y - y0 - h) * 1.2) || (aX > hx - 4.5 + (Y - y0 - h) && aZ > hz - 4.5 + (Y - y0 - h)) ? 'stone:DARK_STONE' : 'stone:DARK_STONE';
  const outer = aX > hx - 1.2 || aZ > hz - 1.2, u = aX > hx - 1.2 ? aZ : aX;
  if (outer && Y > y0 + 1 && Y < y0 + h - 1 && (u % 4) < 2.2) return null; /* the openings between the pillars */
  return 'stone:LIMESTONE';
}
function angkorWat(x, y, z) {
  const X = x / AW_S, Y = y / AW_S, Z = z / AW_S, aX = Math.abs(X), aZ = Math.abs(Z);
  let c;
  /* the terraces, stepped */
  if (Y < 4) return aX < 62 && aZ < 54 ? 'stone:LIMESTONE' : null;
  if ((c = awGallery(aX, aZ, Y, 62, 54, 4, 6))) return c;
  if (Y < 11) { if (aX < 44 && aZ < 38) return 'stone:LIMESTONE'; }
  if ((c = awGallery(aX, aZ, Y, 44, 38, 11, 6))) return c;
  if (Y < 30) { /* the steep top terrace */
    const w = 30 - (Y - 11) * 0.2;
    if (aX < w && aZ < w * 0.94) return 'stone:LIMESTONE';
    if (aZ < 4 && aX < 44 - (Y - 11) * 0.75 && aX > w - 1) return 'stone:DARK_STONE'; /* the stairs up the middle */
    if (aX < 4 && aZ < 38 - (Y - 11) * 0.45 && aZ > w * 0.94 - 1) return 'stone:DARK_STONE';
  }
  if ((c = awGallery(aX, aZ, Y, 26, 24, 30, 6))) return c;
  /* the towers: four at the corners of the top terrace, the tall one in the middle */
  for (const [cx, cz] of [[21, 19], [21, -19], [-21, 19], [-21, -19]]) if ((c = lotus(X - cx, Y, Z - cz, 30, 8.5, 40))) return c;
  if ((c = lotus(X, Y, Z, 30, 11.5, 55))) return c;
  if (Y >= 30 && Y < 34 && aX < 26 && aZ < 24) return 'stone:LIMESTONE'; /* the top floor between the towers */
  return null;
}

/* ── Willis Tower: nine black square tubes bundled three by three, ending at four heights; bands of dark glass
 * between the black frame's bands (the two antennas are masts, src/landmarks.c). Game metres ── */
const WT_T = 0.78, WT_FLOOR = 15 / 108;
const WT_H = [[66, 108, 90], [90, 108, 90], [50, 66, 50]];
function willis(x, y, z) {
  const i = Math.floor(x / WT_T + 1.5), j = Math.floor(z / WT_T + 1.5);
  if (i < 0 || i > 2 || j < 0 || j > 2 || y > WT_H[j][i] * WT_FLOOR) return null;
  return y % 0.56 > 0.14 ? 'glass:DARK_GLASS' : 'stone:DARK_STONE';
}

/* ── the Empire State Building: the podium, the setbacks, the long shaft of limestone piers between columns of
 * windows, the stepped crown, the mast's foot. Game metres ── */
const ES_SEC = [[1.0, 2.5, 1.15], [3.2, 1.7, 1.0], [3.9, 1.45, 0.9], [11.3, 1.15, 0.8], [12.6, 0.9, 0.65], [13.3, 0.7, 0.5], [13.65, 0.55, 0.42]];
function empire(x, y, z) {
  const ax = Math.abs(x), az = Math.abs(z);
  let s = 0;
  while (s < 7 && y >= ES_SEC[s][0]) s++;
  if (s >= 7) { const r = 0.36 - 0.1 * (y - 13.65) / 1.6; return y < 15.25 && ax < r && az < r ? 'stone:SILVER' : null; }
  const hx = ES_SEC[s][1], hz = ES_SEC[s][2];
  if (ax > hx || az > hz) return null;
  const fx = ax > hx - 0.13, fz = az > hz - 0.13;
  if (s >= 1 && (fx || fz) && !(fx && fz)) {
    const col = Math.floor((fx ? z : x) / 0.13 + 100);
    if ((col & 1) && y % 0.28 > 0.08) return 'glass:DARK_GLASS';
  }
  return 'stone:LIMESTONE';
}

/* ── the Pyramids of Giza: the Great Pyramid, Khafre's with its cap of casing, Menkaure's and three queens', on a
 * diagonal. Game metres ── */
const GZ = [[-7.3, -5.2, 4.75, 6.0, 2], [1.9, 0.8, 4.3, 5.5, 0.8], [8.7, 6.8, 2.1, 2.7, 2], [11.9, 6.0, 0.6, 0.75, 2], [11.9, 7.6, 0.6, 0.75, 2], [11.9, 9.1, 0.55, 0.7, 2]];
function giza(x, y, z) {
  for (const [cx, cz, half, h, cap] of GZ) {
    const t = y / h;
    if (t >= 1) continue;
    const hw = half * (1 - t);
    if (hw < 0.3) continue; /* a flat top a crystal can stand on (the apex itself is long gone on most) */
    if (Math.abs(x - cx) < hw && Math.abs(z - cz) < hw) return t > cap ? 'stone:LIMESTONE' : (Math.floor(y / 0.3) & 1 ? 'stone:SAND_STONE' : 'stone:TRIM_CREAM');
  }
  return null;
}

/* ── the Colosseum (189 by 156 m, 48 m): the outer wall of three storeys of eighty arches between half columns and
 * the attic of small windows above, broken away on its south side where the lower inner wall stands instead; inside,
 * the inner ring, the seating stepping down to the arena and the walls of the hypogeum under its floor. Travertine.
 * Real metres ── */
const CO_S = 0.095, CO_A = 94.5, CO_B = 78;
function colosseum(x, y, z) {
  const X = x / CO_S, Y = y / CO_S, Z = z / CO_S;
  const e = Math.sqrt(sq(X / CO_A) + sq(Z / CO_B)); /* 1 on the outer face */
  const ang = Math.atan2(Z / CO_B, X / CO_A), bay = ((ang / (2 * Math.PI)) * 80 + 80) % 1; /* where in its bay round the ring */
  const depth = (1 - e) * CO_B; /* metres in from the outer face (about) */
  if (e > 1) return null;
  /* the broken south side (toward the launcher and right): the outer ring gone above the first storeys */
  const south = Z > -20 && X > 15 ? Math.min(1, (X - 15) / 22) : 0;
  const ruinTop = 48 - south * (36 + 3.5 * (Math.floor(ang * 37 + 100) % 3)); /* its broken edge, stepped course by course */
  /* the outer wall: arches in three storeys, the attic with windows */
  if (depth < 2.6) {
    if (Y > ruinTop) return null;
    const st = Math.floor(Y / 10.5), fy = Y - st * 10.5;
    if (st < 3) {
      const open = bay > 0.18 && bay < 0.82 && fy > 1.4 && fy < 7.4 + 1.6 * Math.sqrt(Math.max(0, 1 - sq((bay - 0.5) / 0.32)));
      if (open) return null;
      return fy > 9.4 ? 'stone:LIMESTONE' : 'stone:TRIM_CREAM';
    }
    if (Y < 48) return (bay > 0.4 && bay < 0.6 && fy > 3 && fy < 6) ? null : 'stone:TRIM_CREAM'; /* the attic, a window a bay every other storey */
    return null;
  }
  /* the corridor behind the outer wall, then the inner ring wall (two storeys of arches, lower) */
  if (depth < 8) return Y < 1 ? 'stone:LIMESTONE' : null;
  if (depth < 10.5) {
    if (Y > 32) return null;
    const st = Math.floor(Y / 10.5), fy = Y - st * 10.5;
    if (st < 3 && bay > 0.22 && bay < 0.78 && fy > 1.4 && fy < 6.8 + 1.5 * Math.sqrt(Math.max(0, 1 - sq((bay - 0.5) / 0.28)))) return null;
    return 'stone:LIMESTONE';
  }
  /* the seating (the cavea), stepping down to the arena wall */
  const eA = Math.sqrt(sq(X / 43.5) + sq(Z / 27.5));
  if (eA > 1) {
    const top = 4 + 26 * Math.min(1, (eA - 1) / ((CO_A / 43.5) - 1 - 0.12)), step = Math.floor(top / 3.3) * 3.3;
    return Y < step ? (Math.floor(Y / 3.3) & 1 ? 'stone:SAND_STONE' : 'stone:PLASTER') : null;
  }
  /* the arena: its floor gone, the walls of the hypogeum below it */
  if (Y < 4 && (Math.abs(((Z + 100) % 5) - 2.5) < 0.8 || Math.abs(X) > 40)) return 'stone:LIMESTONE';
  return null;
}

/* ── the Parthenon as it stands (69.5 by 30.9 m): the three steps of its base, the peristyle of 8 by 17 Doric columns
 * (fluted, swelling a little), the architrave and frieze still on them, the west pediment standing and the east one
 * broken, no roof, a stub of the cella's walls. Pentelic marble, honey with age. Real metres; long side along x ── */
const PA_S = 0.216;
function parthenon(x, y, z) {
  const X = x / PA_S, Y = y / PA_S, Z = z / PA_S, aX = Math.abs(X), aZ = Math.abs(Z);
  /* the steps */
  for (let s = 0; s < 3; s++) if (Y < 0.55 * (s + 1) && aX < 36 - s * 0.75 && aZ < 16.6 - s * 0.75) return 'stone:LIMESTONE';
  const base = 1.65;
  if (Y < base) return null;
  const COLS = 10.4, ENT = 3.3;
  /* the columns: centres every 4.3 m round the edge, 1.9 m thick, a little narrower at the top */
  if (Y < base + COLS) {
    const r = 0.95 * (1 - 0.18 * (Y - base) / COLS) + (Y > base + COLS - 0.7 ? 0.45 : 0); /* the capital */
    const cx = Math.round((X + 32.3) / 4.31) * 4.31 - 32.3, cz = Math.round((Z + 13.0) / 4.33) * 4.33 - 13.0;
    const onLong = Math.abs(aZ - 13.0) < 1.5 && aX < 33, onEnd = Math.abs(aX - 32.3) < 1.5 && aZ < 14;
    if ((onLong && Math.hypot(X - cx, aZ - 13.0) < r) || (onEnd && Math.hypot(aX - 32.3, Z - cz) < r)) return 'stone:MARBLE';
    /* a stub of the cella's walls */
    if (Y < base + 2.5 && aZ < 10.5 && aX < 24 && (aZ > 9.3 || aX > 22.8)) return 'stone:TRIM_CREAM';
    return null;
  }
  /* the architrave and the frieze, a band round the colonnade */
  if (Y < base + COLS + ENT) {
    const ring = (aX < 33.6 && aZ < 14.3) && (aX > 30.8 || aZ > 11.5);
    if (!ring) return null;
    const frieze = Y > base + COLS + 1.4, tri = Math.floor(((aX > 30.8 ? Z : X) + 100) / 1.08) % 2; /* triglyphs and metopes */
    return frieze && tri ? 'stone:TRIM_CREAM' : 'stone:MARBLE';
  }
  /* the pediments at the ends: the west whole, the east fallen to its lower corners */
  const t = (Y - base - COLS - ENT) / 4.4;
  if (t < 1 && aX > 30.8 && aX < 33.6 && aZ < 14.3 * (1 - t)) return X < 0 ? 'stone:MARBLE' : (aZ > 7 ? 'stone:MARBLE' : null);
  return null;
}

export default {
  parthenon: { ext: [72 * PA_S, 20 * PA_S, 34 * PA_S], cell: parthenon },
  colosseum: { ext: [2 * CO_A * CO_S, 48 * CO_S, 2 * CO_B * CO_S], cell: colosseum },
  willis: { ext: [3 * WT_T, 15.01, 3 * WT_T], cell: willis },
  empire: { ext: [5.0, 15.25, 2.5], cell: empire },
  giza: { ext: [26, 6.0, 20], cell: (x, y, z) => giza(x - 1.5, y, z + 1.5) },
  petronas: { ext: [154 * PT_S, 453 * PT_S, 52 * PT_S], cell: petronas },
  cn_tower: { ext: [72 * CN_S, 554 * CN_S, 72 * CN_S], cell: cnTower },
  angkor_wat: { ext: [126 * AW_S, 86 * AW_S, 110 * AW_S], cell: angkorWat },
  st_basils: { ext: [60 * SB_S, 53 * SB_S, 60 * SB_S], cell: stBasils },
  tower_bridge: { ext: [244 * TB_S, 66 * TB_S, 38 * TB_S], cell: towerBridge },
  opera: { ext: [184 * OP_S, 70 * OP_S, 100 * OP_S], cell: opera },
  neuschwanstein: { ext: [19.2, 12.2, 7.2], cell: neuschwanstein },
  needle: { ext: [44 * SN_S, 184 * SN_S, 44 * SN_S], cell: needle },
  sphinx: { ext: [(SPH_X1 - SPH_X0) * SPH_S, 21.4 * SPH_S, 2 * SPH_Z * SPH_S], cell: sphinx },
  eiffel: { ext: [126 * EI_S, 324 * EI_S, 126 * EI_S], cell: eiffel },
};
