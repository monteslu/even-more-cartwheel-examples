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
  if (Math.abs(X) < 3.2 && Math.abs(Z) < 3.2) return Math.abs(X) > 2.0 || Math.abs(Z) > 2.0 || Math.floor(Y) % 22 === 0 ? 'steel:BRONZE' : null; /* a hollow shaft with a landing every so often */
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
  if (hexR(X, Z) < core) return hexR(X, Z) > core - 3.2 || Math.floor(Y) % 38 < 3 ? 'stone:CONCRETE' : null; /* the shaft: a wall and a landing every so often, hollow */
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


/* ══ the second wave of real landmarks (hand shapes, game metres; foot on y = 0, facing +z) ══ */
const cl = (v, a, b) => Math.max(a, Math.min(b, v));
const hsh = (a, b) => { const s = Math.sin(a * 127.1 + b * 311.7) * 43758.5453; return s - Math.floor(s); };
const ell = (x, y, z, cx, cy, cz, rx, ry, rz) => sq((x - cx) / rx) + sq((y - cy) / ry) + sq((z - cz) / rz) < 1;
/* a point on a straight segment's thick line: distance from (x, y) to the segment a..b in the plane */
const segD = (x, y, ax, ay, bx, by) => { const dx = bx - ax, dy = by - ay, t = cl(((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy || 1), 0, 1); return Math.hypot(x - ax - t * dx, y - ay - t * dy); };

/* ── Mount Rushmore: a granite mountain with a steep face, four heads carved on it, each a mass of its own stone on a
 * bulge of the face (it can break away from the cliff); the crystals lie on the summit behind them ── */
const MR_H = [[-6.9, 0.1, 1.55], [-2.3, -0.5, 1.55], [2.3, 0.0, 1.55], [6.9, 0.1, 1.6]]; /* x, z offset, size: Washington, Jefferson, Roosevelt, Lincoln */
const mrFace = (y, x) => (y < 1.6 ? 2.5 + (1.6 - y) * 0.8 : y < 6.6 ? 2.5 - (y - 1.6) * 0.14 : 1.8 - (y - 6.6) * 0.32) + 0.22 * Math.sin(y * 3.1 + x * 0.6) * (y > 1.6 ? 1 : 0); /* stepped strata along the face */
function rushmore(x, y, z) {
  const ax = Math.abs(x);
  const n = Math.sin(x * 0.9) * 0.5 + Math.sin(x * 2.3 + 1) * 0.35 + Math.sin(x * 5.1) * 0.15;
  const cy = 7.2, hd = 1.45 * 1.55;
  /* the heads and their busts, in the pale granite */
  for (const [hx, hz, sc] of MR_H) {
    const cz = mrFace(cy, hx) + 0.55 + hz, X = x - hx, Y = y - cy, Z = z - cz;
    const jw = 1 - 0.32 * Math.max(0, -Y) / (1.45 * sc), jaw = hx > 5.5;
    if (ell(X / jw, Y, Z, 0, 0, 0, 1.15 * sc, 1.45 * sc, 1.0 * sc)) {
      const fr = 1.0 * sc * Math.sqrt(Math.max(0, 1 - sq(X / (1.15 * sc * jw)) - sq(Y / (1.45 * sc))));
      if (Z > fr - 0.7 && Math.abs(X) > 0.3 * sc && Math.abs(X) < 0.9 * sc && Y > -0.05 && Y < 0.5) return null; /* the eye sockets, deep */
      if (Z > fr - 0.3 && Y < -0.8 && Y > -1.05 && Math.abs(X) < 0.5 * sc) return null; /* the mouth */
      return 'stone:MARBLE';
    }
    if (Math.abs(X) < 0.42 && Y > -1.0 && Y < 0.4 && Z > 0 && Z < 2.0 * sc) return 'stone:MARBLE'; /* the nose, long */
    if (Math.abs(X) < 1.1 * sc && Y > 0.45 && Y < 0.95 && Z > 0.1 && Z < 1.3 * sc) return 'stone:MARBLE'; /* the brow, heavy */
    if (ell(X, Y, Z, 0, -1.6 * sc, 0.1, jaw ? 0.65 : 0.52, jaw ? 0.8 : 0.42, 0.55)) return 'stone:MARBLE'; /* the chin (a beard on the last) */
    if (Math.abs(X) < 2.1 && Y < -1.55 && Y > -3.4 && Z > -1.2 && Z < 0.9 - 0.3 * (-Y - 1.55) && ell(X, 0, 0, 0, 0, 0, 2.1, 1, 1) && ell(X, Y, Z, 0, -2.5, -0.2, 2.1, 1.1, 1.5)) return 'stone:MARBLE'; /* the shoulders */
  }
  /* the mountain: strata of rock, a steep face stepping back, a ridge that falls away behind, rough along the top */
  const top = 9.6 + n * 1.1 + (ax > 6.5 ? -1.0 * (ax - 6.5) * 0.5 : 0) - Math.max(0, -z - 1.0) * 1.1;
  const zb = -1.6, xs = 9 - 0.22 * y + 0.35 * Math.sin(y * 1.9 + z);
  if (ax > xs || z < zb || y > top) return null;
  let fc = mrFace(y, x);
  for (const [hx] of MR_H) { const d = Math.abs(x - hx); if (d < 2.4 && y > 4.0 && y < 9.2) fc -= 0.4 * Math.min(1, (2.4 - d) * 2) * Math.min(1, (y - 4.0) * 2, (9.2 - y) * 2); } /* the wall the heads are cut from stands back from them */
  if (z > fc) return null;
  const k = Math.floor(y * 1.5 + Math.sin(x * 0.7) * 0.7 + z * 0.3);
  return ((k % 2) + 2) % 2 === 0 ? 'stone:ROCK' : 'stone:DARK_STONE';
}

/* ── the Kremlin: the red brick wall with its swallowtail merlons, the corner towers under green tents, the clock gate
 * tower with its stepped crown and gold star, the white bell tower, the cathedral's five gold domes ── */
/* the Spasskaya clock tower: a square shaft in red brick, a stage with the clock faces, an open arcade, the tall green tent and the gold star */
function spasskaya(x, y, z, cx, cz) {
  const X = Math.abs(x - cx), Z = Math.abs(z - cz);
  if (y < 4.6) return X < 0.95 && Z < 0.95 ? (y > 3.6 && y < 4.0 && Z > 0.6 && X < 0.55 ? 'stone:TRIM_CREAM' : 'stone:RED_BRICK') : null;
  if (y < 5.4) return X < 0.78 && Z < 0.78 ? 'stone:RED_BRICK' : (X < 1.0 && Z < 1.0 && y < 4.8 ? 'stone:RED_BRICK' : null);
  if (y < 6.9) { /* the clock stage: the dial on every face, a white disc with a dark rim */
    if (X >= 0.82 || Z >= 0.82) return null;
    const dial = Z > 0.55 ? Math.hypot(x - cx, y - 6.1) : X > 0.55 ? Math.hypot(z - cz, y - 6.1) : 9;
    return dial < 0.5 ? (dial > 0.4 ? 'stone:DARK_STONE' : 'stone:WHITE_PAINT') : 'stone:RED_BRICK';
  }
  if (y < 7.6) { const col = (X > 0.5 && X < 0.8 && Z > 0.5 && Z < 0.8) || (X < 0.85 && Z < 0.85 && y > 7.4); return col ? 'stone:TRIM_CREAM' : null; } /* the arcade: four posts and a ring */
  if (y < 10.6) { const t = (y - 7.6) / 3.0, w = 0.85 * (1 - 0.92 * t); return X < w && Z < w ? 'stone:ROOF_COPPER' : null; } /* the green tent */
  if (y < 10.9) return X < 0.12 && Z < 0.12 ? 'stone:GOLD' : null;
  /* the star: a gold cross of two bars and its diagonals, in the plane of the wall */
  const dx = x - cx, dy = y - 11.5;
  if (Math.abs(z - cz) < 0.08 && y >= 10.9 && y < 12.1 && (Math.abs(dx) < 0.1 || Math.abs(dy) < 0.1 || Math.abs(Math.abs(dx) - Math.abs(dy)) < 0.09) && Math.hypot(dx, dy) < 0.6) return 'stone:GOLD';
  return null;
}
function kremlin(x, y, z) {
  const ax = Math.abs(x), az = Math.abs(z);
  /* the wall: one brick thick, the front (z > 0) toward the square, swallowtail merlons */
  const onWall = (Math.abs(ax - 8.85) < 0.15 && az < 5.0) || (Math.abs(az - 4.85) < 0.15 && ax < 9.0);
  if (onWall && y < 1.9) return 'stone:RED_BRICK';
  if (onWall && y < 2.3) return Math.floor((ax > 8 ? z : x) / 0.3 + 100) % 2 ? 'stone:RED_BRICK' : null;
  /* corner towers: round, under green tents */
  for (const [cx, cz] of [[-8.85, -4.85], [8.85, -4.85], [-8.85, 4.85], [8.85, 4.85]]) {
    const r = Math.hypot(x - cx, z - cz);
    if (y < 2.9 && r < 0.75) return 'stone:RED_BRICK';
    if (y >= 2.9 && y < 3.3 && r < 0.9) return 'stone:RED_BRICK';
    if (y >= 3.3 && y < 5.4 && r < 0.9 * (1 - (y - 3.3) / 2.1)) return 'stone:ROOF_COPPER';
  }
  const sp = spasskaya(x, y, z, 3.0, 4.85); if (sp) return sp;
  const ni = spasskaya(x, y * 1.5, z, -4.6, 4.85); if (ni && y < 8.6) return ni; /* the Nikolskaya: the same tower, lower */
  /* the Ivan the Great bell tower: white tiers, the gold dome */
  const bx = Math.abs(x - 4.8), bz = Math.abs(z + 1.8);
  if (y < 4.2 && bx < 0.85 && bz < 0.85) return 'stone:PLASTER';
  if (y >= 4.2 && y < 6.2 && bx < 0.65 && bz < 0.65) return (bx > 0.3 && bz > 0.3) ? 'glass:DARK_GLASS' : 'stone:PLASTER';
  if (y >= 6.2 && y < 7.4 && bx < 0.55 && bz < 0.55) return 'stone:PLASTER';
  if (ell(x, y, z, 4.8, 7.4, -1.8, 0.62, 1.1, 0.62) && y >= 7.4) return 'stone:GOLD';
  if (y >= 8.4 && y < 9.2 && bx < 0.06 && bz < 0.06) return 'stone:GOLD';
  /* the Assumption cathedral: white, five gold onion domes on drums */
  if (Math.abs(x + 1.2) < 2.3 && Math.abs(z + 0.6) < 2.1 && y < 3.2) return y > 1.2 && y < 2.4 && (Math.abs(Math.abs(x + 1.2) - 2.3) < 0.1 || Math.abs(Math.abs(z + 0.6) - 2.1) < 0.1) ? 'stone:TRIM_CREAM' : 'stone:PLASTER';
  for (const [dx, dz, r] of [[-1.2, -0.6, 0.8], [-2.8, -1.8, 0.55], [0.4, -1.8, 0.55], [-2.8, 0.6, 0.55], [0.4, 0.6, 0.55]]) {
    const rr = Math.hypot(x - dx, z - dz);
    if (y >= 3.2 && y < 3.9 && rr < r * 0.72) return 'stone:PLASTER';
    if (y >= 3.9 && ell(x, y, z, dx, 3.9 + r * 0.85, dz, r, r * 1.5, r)) return 'stone:GOLD';
    if (y >= 3.9 + r * 2.2 && y < 3.9 + r * 2.9 && rr < 0.05) return 'stone:GOLD';
  }
  /* the palace along the back */
  if (z < -3.2 && z > -4.6 && ax < 7.5 && y < 2.4) return y > 0.9 && y < 1.6 && Math.floor(x / 0.5) % 2 ? 'glass:DARK_GLASS' : 'stone:TRIM_CREAM';
  return null;
}

/* ── Taipei 101: a podium, eight bowl-shaped segments, each flaring outward, joined by gold bands, then the crown and the spire ── */
const TP_HB = (k) => 0.98 - 0.045 * k;
function taipei(x, y, z) {
  const ax = Math.abs(x), az = Math.abs(z);
  if (y < 0.5) return ax < 1.75 && az < 1.75 ? 'stone:CONCRETE' : null;
  if (y < 1.1) { const w = 1.5 - 0.3 * (y - 0.5); return ax < w && az < w ? (y % 0.26 > 0.07 ? 'glass:BLUE_GLASS' : 'stone:CONCRETE') : null; }
  const y0 = 1.1, SEG = 1.55;
  if (y < y0 + 8 * SEG) {
    const k = Math.floor((y - y0) / SEG), t = (y - y0 - k * SEG) / SEG;
    if (t < 0.06) { const h = TP_HB(k) + 0.08; return ax < h && az < h ? 'stone:GOLD' : null; }
    const h = TP_HB(k) + 0.3 * t;
    const c = Math.max(ax, az) < h && ax + az < h * 1.5;
    if (!c) return null;
    const edge = Math.max(ax, az) > h - 0.13 || ax + az > h * 1.5 - 0.2;
    return !edge || y % 0.24 > 0.07 ? (y % 0.24 > 0.07 && !edge ? 'stone:TURQUOISE' : 'glass:BLUE_GLASS') : 'stone:TURQUOISE';
  }
  const yc = y0 + 8 * SEG; /* 13.5 */
  if (y < yc + 1.2) { const t = (y - yc) / 1.2, h = 0.8 - 0.28 * t; return Math.max(ax, az) < h ? 'stone:TURQUOISE' : null; }
  if (y < yc + 1.7) return ax < 0.3 && az < 0.3 ? 'stone:WHITE_PAINT' : null;
  const r = 0.15 * (1 - (y - yc - 1.7) / 4.0);
  return y < 17.0 && ax < Math.max(0.045, r) && az < Math.max(0.045, r) ? 'steel:WHITE_PAINT' : null;
}

/* ── the Golden Gate Bridge: two tall orange towers braced between their legs, the main cables over them down to the
 * deck, the hangers, the deck on abutments. Welded steel ── */
const GG_T = 9, GG_TOP = 7.7, GG_DECK = 1.25;
function goldenGate(x, y, z) {
  const ax = Math.abs(x), az = Math.abs(z), O = 'steel:RED_BRICK';
  /* the towers' legs, tapering, braced by portals */
  const dt = Math.abs(ax - GG_T);
  if (y < GG_TOP && dt < 0.33 - 0.05 * (y / GG_TOP) && Math.abs(az - 0.72) < 0.24) return O;
  if (y < GG_TOP && dt < 0.3 && az < 0.72) for (const py of [1.9, 3.0, 4.1, 5.2, 6.3, 7.4]) if (y >= py && y < py + 0.28 + (py > 7 ? 0.2 : 0)) return O;
  if (y < GG_TOP && dt < 0.3 && az < 0.72 && Math.abs(y - 0.5) < 0.5 && false) return O;
  /* the deck, its truss and the abutments */
  if (ax < 17.5 && az < 0.78) {
    if (y >= GG_DECK - 0.3 && y < GG_DECK) return 'steel:SILVER';
    if (y >= GG_DECK - 0.75 && y < GG_DECK - 0.3 && (az > 0.62 || Math.floor((x + 100) / 0.45) % 2 === 0 && az < 0.2)) return O;
    if (y < GG_DECK && ax > 16.6 && az < 0.78) return 'stone:CONCRETE';
  }
  /* the cables: over the towers, down to the deck at mid span, to the anchorages */
  if (Math.abs(az - 0.84) < 0.16) {
    let cy;
    if (ax <= GG_T) cy = 1.8 + (GG_TOP - 1.8) * sq(ax / GG_T);
    else cy = GG_TOP - (GG_TOP - 1.5) * Math.pow((ax - GG_T) / 8.2, 1.15);
    if (ax < 17.1 && Math.abs(y - cy) < 0.2) return O;
    /* the hangers */
    if (ax <= GG_T - 0.4 && y > GG_DECK && y < cy && Math.floor((ax + 100) / 0.9 + 0.5) !== Math.floor((ax + 100.3) / 0.9 + 0.5) && Math.abs(az - 0.84) < 0.08) return O;
    if (ax <= GG_T - 0.4 && y > GG_DECK && y < cy && Math.abs(((ax + 100) % 0.9) - 0.45) < 0.08) return O;
  }
  return null;
}

/* ── the Brooklyn Bridge: two stone towers with pointed arches, the cables, and the diagonal stays that fan from the
 * tower tops to the deck ── */
const BB_T = 8, BB_TOP = 7.0, BB_DECK = 1.0;
function brooklyn(x, y, z) {
  const ax = Math.abs(x), az = Math.abs(z), dt = ax - BB_T;
  if (Math.abs(dt) < 0.85 && az < 1.45 && y < BB_TOP) {
    /* the two pointed arches the roadway runs through, and the pair above */
    const arch = (zc, hw, y0, y1) => { const dz = Math.abs(z) - zc; const w = Math.abs(dz); if (y < y0 || y >= y1) return false; const t = (y - y0) / (y1 - y0); return w < hw * (1 - Math.pow(Math.max(0, (t - 0.55) / 0.45), 1.6)); };
    if (az < 1.2 && Math.abs(dt) < 0.85) {
      if (arch(0, 0.0, 0, 1)) return null;
      for (const s of [-1, 1]) {
        if (arch(0.66, 0.5, 0, 3.4) && Math.abs(z - s * 0.66) < 0.5) return Math.abs(dt) < 0.8 ? null : null;
      }
    }
    const inArch = [0.66, -0.66].some((zc) => Math.abs(z - zc) < 0.5 * (y < 2.2 ? 1 : Math.max(0, 1 - Math.pow((y - 2.2) / 1.2, 1.6))) && y < 3.4) ;
    const inUpper = [0.55, -0.55].some((zc) => Math.abs(z - zc) < 0.32 * (y < 5.4 ? 1 : Math.max(0, 1 - Math.pow((y - 5.4) / 0.9, 1.6))) && y > 4.1 && y < 6.3);
    if (inArch || inUpper) return null;
    if (y > BB_TOP - 0.35) return 'stone:SAND_STONE';
    return 'stone:LIMESTONE';
  }
  if (ax < 17.6 && az < 0.78 && y > BB_DECK - 0.28 && y < BB_DECK && !(Math.abs(dt) < 0.85)) return 'steel:SILVER';
  if (ax < 17.6 && az < 0.78 && y > BB_DECK - 0.65 && y < BB_DECK - 0.28 && (az > 0.62 || Math.floor((x + 100) / 0.5) % 2 === 0 && az < 0.18) && !(Math.abs(dt) < 0.85)) return 'steel:IRON';
  if (ax > 16.7 && ax < 17.6 && az < 0.78 && y < BB_DECK) return 'stone:CONCRETE';
  if (Math.abs(az - 0.88) < 0.09 || Math.abs(az - 0.88) < 0.0) {
    let cy = BB_DECK;
    if (ax <= BB_T) cy = 1.7 + (BB_TOP - 0.2 - 1.7) * sq(ax / BB_T);
    else if (ax < 17.1) cy = BB_TOP - 0.2 - (BB_TOP - 1.5) * Math.pow((ax - BB_T) / 9.1, 1.1);
    if (ax < 17.1 && Math.abs(y - cy) < 0.17 && Math.abs(dt) > 0.8) return 'steel:IRON';
    if (ax < BB_T - 0.85 && y > BB_DECK && y < cy && Math.abs(((ax + 100) % 1.1) - 0.55) < 0.1) return 'steel:IRON';
    /* the stays: from a tower's top to the deck, a fan each side */
    for (const sx of [-1, 1]) for (let k = 1; k <= 8; k++) {
      for (const dir of [-1, 1]) {
        const tx = sx * BB_T, bx = sx * (BB_T + dir * k * 1.05);
        if (Math.abs(bx) > 17.0) continue;
        if (segD(x, y, tx, BB_TOP - 0.5, bx, BB_DECK) < 0.07 && Math.abs(x - tx) > 0.85) return 'steel:IRON';
      }
    }
  }
  return null;
}

/* ── the Chrysler Building: the white-brick shaft in setbacks, the crown of steel arches in tiers, the spire ── */
function chrysler(x, y, z) {
  const ax = Math.abs(x), az = Math.abs(z);
  const H = y < 2.4 ? 1.55 : y < 4.6 ? 1.3 : y < 8.4 ? 1.12 : y < 10.8 ? 0.92 : 0;
  if (H) {
    if (ax >= H || az >= H) return null;
    const fx = ax > H - 0.14, fz = az > H - 0.14;
    if (y > 0.5 && (fx || fz) && !(fx && fz) && Math.floor(((fx ? z : x) + 100) / 0.14) % 2 && y % 0.3 > 0.09) return 'glass:DARK_GLASS';
    return y > 10.4 ? 'stone:SILVER' : 'stone:TILE_WHITE';
  }
  if (y < 15.0) {
    const t = (y - 10.8) / 4.2, k = Math.floor(t * 7), f = t * 7 - k;
    const R = 0.92 * Math.cos(t * 1.45 * 0.5 * Math.PI * 0.95) * (1 - 0.14 * f) + 0.04;
    const r = Math.max(R * (1 - 0.7 * Math.pow(t, 2.5)), 0.1);
    if (Math.max(ax, az) < r && ax + az < r * 1.5) { if (f > 0.35 && f < 0.6 && ax < r * 0.55 && az > r - 0.1) return 'glass:DARK_GLASS'; return 'stone:SILVER'; }
    return null;
  }
  return y < 16.8 && ax < 0.08 && az < 0.08 ? 'steel:SILVER' : null;
}

/* ── the Gateway Arch: a weighted catenary of stainless steel, thicker at the legs, a hollow shell ── */
const GA_PTS = [];
for (let i = 0; i <= 1200; i++) { const x = -9.4 + (18.8 * i) / 1200; GA_PTS.push([x, 0.0987 * (211.49 - 20.96 * Math.cosh(0.032916 * (x / 0.0987)))]); }
function gateway(x, y, z) {
  let best = 1e9, by = 0;
  for (let i = 0; i < GA_PTS.length; i += 3) { const d = Math.hypot(x - GA_PTS[i][0], y - GA_PTS[i][1]); if (d < best) best = d, by = GA_PTS[i][1]; }
  if (y < 0) return null;
  const tk = 0.34 + 0.55 * Math.pow(1 - Math.min(by, 18.8) / 18.8, 1.2);
  const zk = 0.28 + 0.42 * Math.pow(1 - Math.min(by, 18.8) / 18.8, 1.2);
  return best < tk && Math.abs(z) < zk ? 'steel:SILVER' : null;
}

/* ── the Arc de Triomphe: the great arch, the small arch across it, the frieze, the cornice and the attic ── */
function arc(x, y, z) {
  const ax = Math.abs(x), az = Math.abs(z);
  if (ax >= 5.5 || az >= 2.4 || y >= 10.0) return null;
  /* the great arch (through z) and the small one (through x) */
  const big = ax < 1.6 && (y < 4.9 || ell(x, y, 0, 0, 4.9, 0, 1.6, 1.6, 9));
  const small = az < 0.95 && (y < 2.9 || ell(0, y, z, 0, 2.9, 0, 9, 1.2, 0.95)) && y < 4.1;
  if ((big && az < 2.5) || (small && ax < 6)) return null;
  if (y >= 6.5 && y < 7.0 && ax > 5.2 || (y >= 6.5 && y < 7.0 && az > 2.1)) { /* the cornice stands out */ }
  if (y > 8.0 && y < 9.0 && (ax > 1.2 || az > 1.2)) { /* the attic's panels */
    const edge = ax > 5.1 || az > 2.0;
    return !edge && Math.floor((x + 100) / 0.9) % 2 ? 'stone:TRIM_CREAM' : 'stone:LIMESTONE';
  }
  if (y > 6.2 && y < 6.8 && (ax > 5.0 || az > 2.2 || (ax > 1.2 && az < 1.2))) return 'stone:TRIM_CREAM';
  if (az > 2.0 && y > 2.2 && y < 5.5 && ax > 2.2 && ax < 4.6 && ((ax - 3.4) / 1.1) ** 2 + ((y - 4.0) / 1.7) ** 2 < 1 && az > 2.2) return 'stone:SAND_STONE'; /* the reliefs */
  return 'stone:LIMESTONE';
}

/* ── the Brandenburg Gate: a broad flat-topped gate: six Doric columns in each of two rows, five passages, the architrave and frieze, the high attic, and on it the chariot with four horses abreast and the goddess ── */
function brandenburg(x, y, z) {
  const ax = Math.abs(x), az = Math.abs(z), S = 'stone:SAND_STONE', G = 'stone:VERDIGRIS';
  if (ax < 6.4 && az < 1.6 && y < 0.5) return S; /* the steps */
  if (y < 4.7) {
    if (ax > 6.2 || az > 1.5) return null;
    const cx = Math.round((x + 5) / 2) * 2 - 5; /* the six columns: x = -5, -3, -1, 1, 3, 5 */
    if (Math.abs(cx) <= 5.01) for (const zc of [1.05, -1.05]) if (Math.hypot(x - cx, z - zc) < (y > 4.35 ? 0.5 : 0.42 - 0.04 * (y / 4.7)) && y >= 0.5) return S;
    /* the walls between the rows, at the columns, part the five passages */
    if (az < 1.05 && Math.abs(x - cx) < 0.16 && Math.abs(cx) <= 5.01 && y >= 0.5) return S;
    return null;
  }
  if (y < 5.45) return ax < 6.25 && az < 1.5 ? (y > 5.05 && Math.floor((x + 100) / 0.5) % 2 ? 'stone:TRIM_CREAM' : S) : null; /* the architrave and the triglyph frieze */
  if (y < 5.75) return ax < 6.4 && az < 1.6 ? S : null; /* the cornice */
  if (y < 6.8) return ax < 5.6 && az < 1.3 ? (y > 6.0 && y < 6.6 && ax < 5.0 && Math.floor((x + 100) / 1.7) % 2 && az > 0.9 ? 'stone:TRIM_CREAM' : S) : null; /* the attic */
  if (y < 7.05) return ax < 2.6 && az < 0.9 ? S : null; /* the chariot's platform */
  /* the quadriga: four horses abreast, the car behind, the goddess standing in it */
  for (const hx of [-1.5, -0.5, 0.5, 1.5]) {
    if (Math.abs(x - hx) < 0.26 && z > -0.1 && z < 0.9 && y < 8.0) return G; /* a horse's body */
    if (Math.abs(x - hx) < 0.26 && z > 0.35 && z < 0.9 && y >= 8.0 && y < 8.6) return G; /* its neck and head, raised */
  }
  if (ax < 1.4 && z < -0.15 && z > -1.0 && y < 7.9) return G; /* the car */
  if (ax < 0.18 && z < -0.3 && z > -0.8 && y < 9.4) return G; /* the goddess */
  if (ax < 0.9 && y > 9.0 && y < 9.3 && Math.hypot(ax, 0) < 0.9 && z < -0.3 && z > -0.8) return G; /* her wreath and staff */
  return null;
}

/* ── the Pantheon: the great dome on its drum, the oculus, the porch of eight granite columns under a pediment ── */
function pantheon(x, y, z) {
  const R = 3.7, r = Math.hypot(x, z - 0), PZ = R + 1.25;
  const base = 0.35;
  if (y < base && r < R + 0.3) return 'stone:CONCRETE';
  /* the rotunda: the drum, then the dome (a shell) */
  if (y < 3.7 && r < R + 0.3 && r > R - 0.62 && !(z > 0 && Math.abs(x) < 2.9 && z > R - 0.5)) return y > 1.2 && y < 1.55 || y > 2.6 && y < 2.8 ? 'stone:TRIM_CREAM' : 'stone:ROSE_STONE';
  if (y < 3.7 && !(z > 0 && Math.abs(x) < 2.9 && z > R - 0.5)) return null;
  const dy = y - 3.7, dr = Math.sqrt(Math.max(0, R * R - dy * dy));
  if (dy < R) { const out = Math.hypot(r, dy); if (out < R + 0.05 && out > R - 0.55 && !(dy > R - 0.9 && r < 0.62)) return dy < 0.9 ? 'stone:ROSE_STONE' : 'stone:CONCRETE'; }
  /* the portico */
  if (z > 0 && Math.abs(x) < 2.9 && z < PZ + 1.15 && z > R - 0.5) {
    const zz = z - (R + 0.1);
    if (y < 0.35) return 'stone:MARBLE';
    if (y < 2.55) {
      for (let row = 0; row < 3; row++) for (let c = 0; c < 8; c++) {
        const xc = -2.55 + c * 0.73, zc = R + 0.35 + row * 0.62;
        if (row === 1 && c > 0 && c < 7) continue;
        if (Math.hypot(x - xc, z - zc) < 0.17 + (y > 2.3 ? 0.05 : 0) - (y > 1.0 ? 0.015 : 0)) return 'stone:SARSEN';
      }
      /* the porch's back wall with the doorway */
      if (z < R + 0.3 && !(Math.abs(x) < 0.5 && y < 1.7)) return 'stone:MARBLE';
      return null;
    }
    if (y < 3.2) return Math.abs(x) < 2.95 && z < PZ + 1.0 ? (y > 2.85 ? 'stone:TRIM_CREAM' : 'stone:MARBLE') : null; /* the entablature */
    const t = (y - 3.2) / 0.95;
    if (t < 1 && Math.abs(x) < 3.0 * (1 - t) + 0.05 && z > PZ - 0.4 - 0.25 * 0 && z < PZ + 1.0) return t > 0.1 && t < 0.6 && Math.abs(x) < 2.0 * (1 - t) && z > PZ + 0.7 ? 'stone:TRIM_CREAM' : 'stone:MARBLE';
  }
  return null;
}

/* ── Notre-Dame de Paris: the west front with its three portals, the gallery of kings, the rose and the two square towers; the long nave roof and the spire where the transept crosses it ── */
function notreDame(x, y, z) {
  const ax = Math.abs(x), L = 'stone:LIMESTONE';
  /* the west front: z from 0 to 1.5 */
  if (z > -0.4 && z < 1.5 && ax < 4.2) {
    const front = z > 0.9;
    const tower = ax > 2.2;
    const H = tower ? 13.4 : 9.4;
    if (y >= H) return null;
    if (front) {
      /* three portals, pointed, the middle the widest */
      if (y < 3.0) { const pc = [[0, 0.85], [-2.2 * 0 - 1.65, 0.55], [1.65, 0.55]].some(([cx, hw]) => Math.abs(x - cx) < hw * (1 - Math.pow(Math.max(0, (y - 1.4) / 1.6), 2)) && y < 3.0); if (pc) return null; }
      if (!tower && y > 5.4 && y < 8.0 && Math.hypot(x, y - 6.7) < 1.1 && Math.hypot(x, y - 6.7) > 0.1) return Math.hypot(x, y - 6.7) > 0.8 ? 'stone:TRIM_CREAM' : 'glass:BLUE_GLASS'; /* the rose */
      if (!tower && y > 3.6 && y < 4.2) return 'stone:TRIM_CREAM'; /* the gallery of kings */
      if (tower && y > 5.0 && y < 12.6 && ax > 2.6 && ax < 3.4 && y > 6.0 && Math.abs(y - 8.5) < 1.2 + (y < 8.5 ? 0 : 0) && ax < 3.2) return 'glass:DARK_GLASS'; /* the louvred bays */
      if (!tower && y > 8.3 && y < 8.8) return 'stone:TRIM_CREAM';
    }
    if (tower && y > 12.7 && y < 13.4 && Math.abs(ax - 3.2) > 0.78) return null; /* the open top: a parapet only */
    return L;
  }
  /* the nave: walls and the steep roof, from the front back to the crossing and on */
  if (z <= -0.4 && z > -10 && ax < 2.6) {
    const wallH = 4.2 + (z > -6.8 ? 0 : 0);
    const roofH = 4.2 + (2.6 - ax) * 1.5;
    if (y < wallH) return ax > 2.1 || ax < 1.0 ? (Math.abs(((ax < 1.0 ? 0 : 1) * 0) + 0) < 1 && y > 1.2 && y < 3.4 && ax > 2.1 && ax < 2.5 && Math.floor((z + 100) / 0.8) % 2 ? 'glass:BLUE_GLASS' : L) : null;
    if (y < roofH && y >= roofH - 0.3) return 'stone:ROOF_SLATE';
    return null;
  }
  /* the transept, wider, and the spire over the crossing */
  if (z <= -5.2 && z > -7.4 && ax < 4.0) { const rh = 4.6 + (2.2 - Math.abs(z + 6.3)) * 1.1 * 0.8; if (y < 4.2) return L; if (y < rh && y >= rh - 0.3) return 'stone:ROOF_SLATE'; return null; }
  if (Math.abs(z + 6.3) < 0.5 && ax < 0.5 && y > 4.5) { const t = (y - 4.5) / 6.5, w = 0.42 * (1 - t); return y < 11 && ax < w && Math.abs(z + 6.3) < w ? 'stone:ROOF_SLATE' : null; }
  return null;
}

/* ── Tokyo Tower: four lattice legs braced in X panels, narrowing to the main deck and the top deck, then the antenna; orange and white ── */
function tokyoTower(x, y, z) {
  const hw = y < 13.8 ? 2.1 * Math.pow(1 - y / 14.5, 1.3) + 0.22 : 0.2;
  const ax = Math.abs(x), az = Math.abs(z);
  const band = Math.floor(y / 3.9) % 2 ? 'steel:WHITE_PAINT' : 'steel:RED_BRICK';
  if (y > 14.3) { /* the antenna: a lattice mast then a thin rod */
    if (y < 17.0) return ax < 0.18 && az < 0.18 ? band : null;
    return y < 18.2 && ax < 0.07 && az < 0.07 ? 'steel:RED_BRICK' : null;
  }
  /* the decks */
  if (y >= 7.4 && y < 8.1) return ax < hw + 0.55 && az < hw + 0.55 ? 'steel:RED_BRICK' : null;
  if (y >= 8.1 && y < 8.5) return ax < hw + 0.65 && az < hw + 0.65 ? 'glass:DARK_GLASS' : null;
  if (y >= 8.5 && y < 8.8) return ax < hw + 0.7 && az < hw + 0.7 ? 'steel:WHITE_PAINT' : null;
  if (y >= 11.6 && y < 12.1) return ax < hw + 0.4 && az < hw + 0.4 ? 'steel:RED_BRICK' : null;
  if (y >= 12.1 && y < 12.45) return ax < hw + 0.45 && az < hw + 0.45 ? 'glass:DARK_GLASS' : null;
  /* the legs: corner posts */
  if (ax > hw - 0.2 && az > hw - 0.2 && ax < hw + 0.2 && az < hw + 0.2) return band;
  if (y < 0.3 && ax < hw + 0.3 && az < hw + 0.3 && ax > hw - 0.3 && az > hw - 0.3) return 'stone:CONCRETE';
  /* the X panels on each face */
  for (const face of [0, 1]) {
    const u = face ? z : x, w = face ? az : ax, o = face ? ax : az; /* u along the face, o across it */
    if (Math.abs(o - hw) < 0.12 && Math.abs(u) < hw) {
      const pitch = 1.9, yy = (y % pitch) / pitch, uu = (u / hw + 1) / 2;
      const nn = 1;
      if (Math.abs(((uu * 2 * nn) % 1) - yy) < 0.1 || Math.abs((((1 - uu) * 2 * nn) % 1) - yy) < 0.1) return band;
      if (yy < 0.07) return band;
    }
  }
  return null;
}

/* ── the Atomium: nine bright spheres, eight at the corners of a cube standing on its point and one in its middle, joined by tubes along the cube's edges and through its heart, on three legs ── */
const AT_R = 1.45, AT_A = 5.6;
const AT_C = (() => {
  const c = [[0, AT_R + 0.0, 0]];
  const hh = AT_A * Math.sqrt(3) / 2, off = AT_A * Math.sqrt(3) / 6, rr = AT_A * Math.sqrt(2 / 3) * 1.0;
  const y0 = AT_R + 0.0; const mid = y0 + hh;
  const sp = [];
  sp.push([0, y0 + 0, 0]); sp.push([0, y0 + 2 * hh, 0]); sp.push([0, mid, 0]);
  for (let k = 0; k < 3; k++) { const a = k * 2.0944, b = a + 1.0472; sp.push([Math.sin(a) * AT_A * 0.8165, mid - off, Math.cos(a) * AT_A * 0.8165]); sp.push([Math.sin(b) * AT_A * 0.8165, mid + off, Math.cos(b) * AT_A * 0.8165]); }
  return sp;
})();
function atomium(x, y, z) {
  for (const [cx, cy, cz] of AT_C) {
    const d = Math.hypot(x - cx, y - cy, z - cz);
    if (d < AT_R && d > AT_R - 0.28) return 'steel:SILVER';
  }
  /* the tubes: every sphere to its neighbours at the cube's edge length, and through the middle to the opposite corner */
  const P = [x, y, z];
  for (let i = 0; i < AT_C.length; i++) for (let j = i + 1; j < AT_C.length; j++) {
    const a = AT_C[i], b = AT_C[j], L = Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
    const edge = Math.abs(L - AT_A) < 0.6, diag = Math.abs(L - AT_A * Math.sqrt(3) / 2) < 0.6 && (i === 2 || j === 2), axis = (a[0] === 0 && b[0] === 0 && a[2] === 0 && b[2] === 0);
    if (!(edge || diag || axis)) continue;
    const t = cl(((x - a[0]) * (b[0] - a[0]) + (y - a[1]) * (b[1] - a[1]) + (z - a[2]) * (b[2] - a[2])) / (L * L), 0, 1);
    const d = Math.hypot(x - a[0] - t * (b[0] - a[0]), y - a[1] - t * (b[1] - a[1]), z - a[2] - t * (b[2] - a[2]));
    if (d < 0.27 && d > 0.0 && Math.hypot(x - a[0], y - a[1], z - a[2]) > AT_R - 0.35 && Math.hypot(x - b[0], y - b[1], z - b[2]) > AT_R - 0.35) return 'steel:SILVER';
  }
  /* the three legs: from the lower ring of spheres to the ground */
  for (let k = 0; k < 3; k++) { const c = AT_C[3 + 2 * k]; if (y < c[1] - AT_R + 0.1 && Math.hypot(x - c[0], z - c[2]) < 0.34) return 'steel:SILVER'; }
  if (y < 0.25 && AT_C.slice(3).some((c, i) => i % 2 === 0 && Math.hypot(x - c[0], z - c[2]) < 0.7)) return 'stone:CONCRETE';
  return null;
}

/* ── the Great Wall: a wall winding along a steep ridge of rock, climbing four peaks, its parapet crenellated, a watchtower with arched windows on each peak ── */
const GW_PK = [[-11, 2.4], [-3, 3.8], [5, 2.8], [12.5, 4.0]];
const gwH = (x) => 0.8 + GW_PK.reduce((m, [px, ph]) => Math.max(m, (ph - 0.8) * Math.exp(-sq((x - px) / 5.2))), 0);
const gwZ = (x) => 2.0 * Math.sin(x * 0.23 + 0.5);
function greatWall(x, y, z) {
  if (Math.abs(x) > 15.6) return null;
  const H = gwH(x), dz = z - gwZ(x), az = Math.abs(dz);
  const w = 1.0 * Math.max(0, 1 - y / (H + 0.2)) + 0.62; /* the ridge: a steep rib of rock, the wall's own battered foot */
  if (y < H - 0.3 && az < w) return 'stone:ROCK';
  if (az < 0.62 && y >= H - 0.3 && y < H + 1.5) return 'stone:LIMESTONE'; /* the wall: a walk between parapets */
  if (az < 0.6 && y >= H + 1.5 && y < H + 2.0 && Math.floor((x + 100) / 0.36) % 2) return 'stone:LIMESTONE'; /* the merlons */
  for (const [px, ph] of GW_PK) {
    const X = Math.abs(x - px), Z = Math.abs(z - gwZ(px));
    if (X < 1.15 && Z < 1.15 && y >= ph && y < ph + 2.7) { /* the watchtower: arched windows on the four faces */
      const wall = X > 0.9 || Z > 0.9;
      if (!wall) return null;
      const win = ((X > 0.9 && Z < 0.3) || (Z > 0.9 && X < 0.3)) && y > ph + 1.1 && y < ph + 2.0 && (y < ph + 1.7 || (X > 0.9 ? Z : X) < 0.18);
      return win ? 'glass:DARK_GLASS' : 'stone:LIMESTONE';
    }
    if (X < 1.3 && Z < 1.3 && y >= ph + 2.7 && y < ph + 3.0) return 'stone:SAND_STONE'; /* the cornice */
    if (X < 1.3 && Z < 1.3 && y >= ph + 3.0 && y < ph + 3.5 && Math.floor((x - px + z + 100) / 0.4) % 2) return 'stone:LIMESTONE'; /* the battlements */
    if (X < 1.0 && Z < 1.0 && y >= ph && y < ph + 0.1) return 'stone:LIMESTONE';
  }
  return null;
}

/* ── the Treasury of Petra: a rose-red cliff with the façade carved in it: two storeys of columns, the broken pediment, the round tholos between, the doorway ── */
function petra(x, y, z) {
  const ax = Math.abs(x), R = 'stone:ROSE_STONE';
  const fr = 3.2 - (y > 6 ? (y - 6) * 0.35 : 0);
  if (ax >= 5.0 && ax < 6.2 && z > -2.2 && z < 3.4 && y < 8.4 + Math.sin(z * 1.3 + x) * 0.5 - (ax - 5) * 0.4) return 'stone:SAND_STONE'; /* the gorge's walls, darker rock, standing out in front of the carved cliff */
  if (z < -2.2 || ax > 5.0 || y > 9.6 + Math.sin(x * 1.7) * 0.25) return null;
  if (z > 0.8 + (y < 1.2 ? 1.0 - y * 0.8 : 0)) { /* standing out from the cliff: the columns, the tholos */ }
  if (z > 1.2) return null;
  const face = 1.0;
  if (z <= 0.15) { /* the cliff, with the doorway and the court cut into it */
    if (ax < 0.7 && y < 2.3 && z > -1.2) return null;
    return R;
  }
  /* the façade in relief, between z = 0.15 and z = 1.2 */
  if (y < 0.3 && ax < 4.5) return 'stone:SAND_STONE';
  if (y < 4.5) { /* the lower order: six columns, the porch, the entablature at 4.1 */
    if (y >= 4.1) return ax < 3.9 && z < 0.9 ? R : null;
    for (const cx of [-3.3, -2.0, -0.6, 0.6, 2.0, 3.3]) if (Math.hypot(x - cx, z - 0.75) < 0.2) return y > 3.7 ? 'stone:SAND_STONE' : R;
    if (ax < 3.8 && z < 0.4 && y < 4.1) return ax < 0.7 && y < 2.2 ? null : R;
    return null;
  }
  /* the upper order: pediment halves, the tholos, columns beside it */
  if (y < 4.7 && ax < 3.9) return z < 0.9 ? R : null;
  const up = y - 4.7;
  for (const cx of [-3.1, -2.2, 2.2, 3.1]) if (Math.hypot(x - cx, z - 0.65) < 0.18 && up < 2.6) return R;
  if (ax < 3.6 && up >= 2.6 && up < 3.0 && z < 0.9) return ax > 0.9 ? R : null; /* the broken pediment's floor */
  if (ax > 0.95 && ax < 3.6 && up >= 3.0 && up < 3.0 + (3.6 - ax) * 0.45 && z < 0.8) return R;
  /* the tholos: a round pavilion with a conical roof and an urn */
  if (ax < 0.95) {
    const r = Math.hypot(x, z - 0.2);
    if (up < 2.8) return r < 0.9 && r > 0.55 + (up < 0.3 ? -0.2 : 0) || (up < 2.6 && r < 0.9 && z > 0.45) ? R : null;
    if (up < 4.0) return r < 0.95 * (1 - (up - 2.8) / 1.5) ? 'stone:ROSE_STONE' : null;
    return null;
  }
  return null;
}

/* ── Machu Picchu: the saddle between the sharp peak behind (Huayna Picchu) and the long ridge, its farming terraces rising in steps to the back, the stone huts with their thatch roofs ── */
function machu(x, y, z) {
  /* the peak */
  const r = Math.hypot(x + 4.2, z + 3.6), hp = 10.5 * Math.pow(Math.max(0, 1 - r / 5.4), 0.85) + 0.25 * Math.sin(x * 3 + z * 2);
  const strata = Math.floor(y * 1.7 + Math.sin(x * 0.8 + z) * 0.6) % 2 ? 'stone:DARK_STONE' : 'stone:ROCK';
  if (y < hp) return strata;
  /* the ridge on the right, falling away along x */
  if (x > 2.5 && z < -0.3 && z > -6.2) { const hr = 6.4 - 0.45 * (x - 2.5) - 0.9 * Math.max(0, z + 3.4) * 0.5 + 0.3 * Math.sin(x * 2.1); if (y < hr && y < 6.4 - Math.abs(z + 3.4) * 0.35) return strata; }
  /* the terraces across the saddle: each a step up toward the back, green on top, stone-faced */
  if (x > -2.2 && x < 7.6 && z > -0.6 && z < 6.6) {
    const k = Math.floor((6.6 - z) / 0.95), ty = 0.5 + 0.5 * k;
    if (y < ty) return y > ty - 0.18 ? 'stone:VERDIGRIS' : 'stone:LIMESTONE';
    /* the huts: a stone box and a thatched gable, on some terraces */
    for (const [hx, hk] of [[0.4, 2], [2.4, 3], [4.6, 2], [6.2, 4], [1.2, 4]]) {
      const hz = 6.6 - (hk + 0.5) * 0.95, hty = 0.5 + 0.5 * hk;
      if (Math.abs(x - hx) < 0.55 && Math.abs(z - hz) < 0.36) { if (y < hty + 0.55) return 'stone:LIMESTONE'; const t = (y - hty - 0.55) / 0.4; if (t < 1 && Math.abs(z - hz) < 0.4 * (1 - t)) return 'stone:TIMBER'; }
    }
  }
  return null;
}

/* ── the Hoover Dam: the curved concrete arch-gravity dam across a red canyon, its crest road, the four round intake towers standing in the lake
 * before the canyon walls, the lake behind it (its surface is drawn by the game: src/water.c, view.c), the powerhouse at the foot downstream.
 * The canyon is a V cut in a ridge that falls away to the plain, flaring upstream where the lake lies; a low ridge closes the lake's far end. ── */
function hoover(x, y, z) {
  const ax = Math.abs(x), H = 9.0;
  const flare = Math.min(2.0, 0.3 * Math.max(0, -z - 2.2));
  const inner = 3.6 + 0.4 * y + flare; /* the canyon: its walls close in toward the floor */
  const zc = 0.08 * x * x - 1.2; /* the dam's upstream face: the arch bulges toward the lake (-z) */
  const band = (Math.floor(y / 0.9) & 1) ? 'stone:SAND_STONE' : 'stone:ROSE_STONE'; /* the canyon's strata, red and tan */
  /* the dam: thick at the foot, thin at the crest, its downstream face stepped */
  if (y < H && ax <= 3.6 + 0.4 * y + 0.4) {
    const t = 0.8 + (H - y) * 0.3, step = Math.round(t * 3) / 3;
    if (z > zc - 0.25 && z < zc + step - 0.25) return y > H - 0.35 ? 'stone:TRIM_CREAM' : 'stone:LIMESTONE';
  }
  /* the crest road's parapets, a cell high, along the arch */
  if (y >= H && y < H + 0.35 && ax <= 3.6 + 0.4 * H + 0.4 && (Math.abs(z - (zc - 0.1)) < 0.17 || Math.abs(z - (zc + 0.8)) < 0.17)) return 'stone:TRIM_CREAM';
  /* the four intake towers, two to a side, standing in the lake upstream of the crest: tapering drums, a cornice, a dark cap, a walk to the crest */
  for (const sx of [-1, 1]) for (const tx of [3.9, 5.7]) {
    const cx = sx * tx, cz = 0.08 * cx * cx - 1.2 - 1.9, rr = Math.hypot(x - cx, z - cz);
    const rad = 0.78 - 0.02 * Math.max(0, y - 2.0);
    if (y < H + 3.1 && rr < rad) return y > H + 2.35 && y < H + 2.7 ? 'stone:SAND_STONE' : y >= H + 2.7 ? 'stone:DARK_STONE' : 'stone:TRIM_CREAM';
    if (y >= H - 0.3 && y < H && Math.abs(x - cx) < 0.18 && z > cz && z < zc) return 'stone:TRIM_CREAM';
  }
  /* the powerhouse at the foot downstream: a long low hall of two wings */
  if (z > 1.6 + 0.08 * x * x * 0.2 && z < 4.0 && ax < 4.4 && y < 1.5 && !(ax < 0.6 && z < 2.6)) return y > 1.1 ? 'stone:CONCRETE' : 'stone:TRIM_CREAM';
  /* the rock: a ridge, its crest at the canyon's rim, falling to the plain on both sides, the canyon cut into it; the lake's far end closed by a low ridge */
  const rim = 3.6 + 0.4 * 11 + flare;
  let gh = 9.8 - 3.2 * Math.max(0, ax - rim) + 0.4 * Math.sin(z * 0.9 + x * 0.7) - Math.max(0, z - 1.2) * 4.5 - Math.max(0, -z - 2.0) * 0.12 - Math.max(0, -z - 14.5) * 4.0;
  if (z < -12.4) gh = Math.min(gh + 1, 8.6 + 0.3 * Math.sin(x * 1.1));
  if (y < gh && (ax > inner + 0.2 || z < -12.4)) return band;
  return null;
}

/* ── Abu Simbel's Great Temple: the rock-cut façade, battered, four seated colossi two each side of the doorway, the frieze of baboons under the cornice, the cliff above ── */
function abuSimbel(x, y, z) {
  const ax = Math.abs(x), R = 'stone:LIMESTONE', C = 'stone:ROSE_STONE'; /* the colossi in pale stone, the cliff in rose */
  for (const cx of [-4.0, -1.5, 1.5, 4.0]) {
    const X = x - cx, Z = z - 0.9;
    if (Math.abs(X) < 0.95 && y < 1.4 && z > 0.2 && z < 2.7) return R; /* the throne */
    if (Math.abs(X) < 0.6 && y >= 1.4 && y < 3.3 && z > 0.3 && z < 2.0) return R; /* the body */
    if (Math.abs(X) < 0.85 && y >= 2.6 && y < 3.3 && z > 0.3 && z < 1.5) return R; /* the shoulders */
    if (Math.abs(X) < 0.34 && y >= 3.3 && y < 4.2 && z > 0.5 && z < 1.4) { /* the head: the nose stands out */
      if (Math.abs(X) < 0.1 && y > 3.55 && y < 3.9 && z > 1.4) return R; return R; }
    if (Math.abs(X) < 0.34 && y >= 3.55 && y < 3.9 && z >= 1.4 && z < 1.65 && Math.abs(X) < 0.1) return R; /* the nose */
    if (Math.abs(X) < 0.56 && y >= 3.3 && y < 4.5 && z > 0.45 && z < 1.35 && Math.abs(X) > 0.32 && y < 4.0) return R; /* the headdress's lappets */
    if (Math.abs(X) < 0.5 && y >= 4.2 && y < 4.6 && z > 0.5 && z < 1.3) return R;
  }
  /* the façade between and above them, battered, the doorway at the middle */
  if (z < 0.3 && ax < 6.2 - 0.0 && z > -4.0) {
    const top = 8.8 - ax * 0.12 + 0.3 * Math.sin(x * 1.9);
    if (y > top) return null;
    if (ax < 0.55 && y < 1.9 && z > -1.6) return null; /* the door, cut into the rock */
    if (y >= 5.9 && y < 6.4 && z > -0.2) return 'stone:TRIM_CREAM'; /* the cornice */
    if (y >= 5.2 && y < 5.9 && Math.floor((x + 100) / 0.42) % 2 && z > -0.1) return 'stone:LIMESTONE'; /* the baboons */
    return C;
  }
  if (z >= 0.3 && z < 0.8 && ax < 5.6 && y < 5.9 && y > 4.9) return R; /* the frieze's footing */
  return null;
}

/* ── the Sydney Harbour Bridge: the steel through-arch, its upper and lower chords closing to the springs, the web of posts and the deck hung from it, the four stone pylons ── */
function sydneyBridge(x, y, z) {
  const ax = Math.abs(x), az = Math.abs(z), A = 'steel:SILVER';
  const u = ax < 11.6 ? 8.0 * (1 - sq(ax / 11.6)) + 0.6 : null; /* the arch's top line */
  if (u !== null) {
    const th = 0.45 + 0.7 * (1 - sq(ax / 11.6)), l = u - th;
    if (Math.abs(az - 1.1) < 0.14 && y <= u && y >= l) { /* the two arch ribs, 2.2 apart, each a chord pair */
      if (y > u - 0.18 || y < l + 0.18) return A;
      if (Math.floor((x + 100) / 0.8) % 2 === 0 || Math.abs(((x + 100) % 1.6) - 0.8) < 0.12) return A; /* the web */
    }
    if (az < 1.1 && Math.abs(y - (u - 0.1)) < 0.1 && Math.floor((x + 100) / 1.3) % 2 === 0) return A; /* the cross bracing at the top */
    /* the hangers from the arch's lower chord to the deck */
    if (Math.abs(az - 1.1) < 0.1 && l > 3.3 && y > 3.3 && y < l && Math.abs(((ax + 100) % 1.15) - 0.57) < 0.1) return A;
    if (ax < 11.6 && Math.abs(az - 1.1) < 0.1 && l < 3.3 && y < 3.0 && y > 0.8) return null;
  }
  /* the deck, hung at 3.3 m, running onto the approaches */
  if (ax < 14.0 && az < 1.45 && y > 3.0 && y < 3.35) return 'steel:DARK_STONE';
  if (ax < 14.0 && az < 1.45 && y >= 3.35 && y < 3.5 && az > 1.3) return A; /* the rails */
  /* the approach spans: trestles under the deck from the pylons out to the ends */
  if (ax > 11.6 && ax < 14.0 && az < 1.45 && y < 3.0 && (az > 1.2 || Math.floor((ax + 100) / 0.9) % 2)) return A;
  /* the four pylons: granite towers at the arch's feet, with their caps */
  for (const sx of [-1, 1]) for (const sz of [-1, 1]) {
    const px = sx * 12.4, pz = sz * 1.15, X = Math.abs(x - px), Z = Math.abs(z - pz);
    if (X < 0.75 && Z < 0.6 && y < 4.7) return y > 4.3 ? 'stone:TRIM_CREAM' : 'stone:SAND_STONE';
    if (X < 0.6 && Z < 0.5 && y >= 4.7 && y < 5.4) return 'stone:SAND_STONE';
    if (X < 0.7 && Z < 0.6 && y >= 5.4 && y < 5.6) return 'stone:TRIM_CREAM';
  }
  return null;
}

/* ── Borobudur: five square terraces of dark volcanic stone with their balustrades, three round terraces carrying rings of bell stupas, the great stupa on top ── */
function borobudur(x, y, z) {
  const ax = Math.abs(x), az = Math.abs(z), r = Math.hypot(x, z), D = 'stone:DARK_STONE', L = 'stone:LIMESTONE';
  const HW = [8.6, 7.4, 6.2, 5.0, 3.8];
  for (let k = 0; k < 5; k++) {
    const y0 = k * 0.9, hw = HW[k];
    if (y >= y0 && y < y0 + 0.9 && Math.max(ax, az) < hw) {
      /* the stairs on the four sides, cut into each terrace's front */
      if (Math.min(ax, az) < 0.55 && y > y0 + 0.2 && Math.max(ax, az) > hw - 0.9) return k % 2 ? D : 'stone:ROCK';
      return Math.max(ax, az) > hw - 0.32 && y > y0 + 0.55 ? L : D;
    }
  }
  /* the rows of bell stupas along the square terraces' edges */
  for (let k = 0; k < 5; k++) { const y0 = k * 0.9 + 0.9, Lk = HW[k] - 0.7; if (y >= y0 && y < y0 + 0.8 && k < 4 && Math.abs(Math.max(ax, az) - Lk) < 0.5) { const along = ax > az ? z : x, c = Math.round(along / 0.95) * 0.95, perp = ax > az ? Math.sign(x) * Lk : Math.sign(z) * Lk; const rr = ax > az ? Math.hypot(x - perp, z - c) : Math.hypot(x - c, z - perp); if (Math.abs(c) < Lk && rr < 0.34 * Math.sqrt(Math.max(0, 1 - sq((y - y0) / 0.8))) + 0.04) return 'stone:LIMESTONE'; } }
  if (y >= 4.5 && y < 6.0) { const k = Math.min(2, Math.floor((y - 4.5) / 0.5)), R = [3.7, 3.0, 2.3][k]; if (r < R) return D; }
  /* the rings of small bell stupas on the three round terraces */
  const rings = [[4.5, 3.2, 16], [5.0, 2.55, 12], [5.5, 1.9, 8]];
  for (const [y0, R, n] of rings) {
    const a = Math.round(Math.atan2(z, x) / (6.2832 / n)) * (6.2832 / n), sx = Math.cos(a) * R, sz = Math.sin(a) * R, rr = Math.hypot(x - sx, z - sz);
    if (y >= y0 + 0.5 && y < y0 + 1.15 && rr < 0.34 * Math.sqrt(Math.max(0, 1 - sq((y - y0 - 0.5) / 0.7))) + 0.05 * (y < y0 + 0.9 ? 1 : 0)) return L;
  }
  /* the great stupa and its spire */
  if (y >= 6.0 && y < 8.4) { const t = (y - 6.0) / 2.4, rr = 1.35 * Math.sqrt(Math.max(0, 1 - sq(t * 0.9))); if (r < rr) return L; }
  if (y >= 8.4 && y < 9.4 && r < 0.2 * (1.2 - (y - 8.4))) return L;
  return null;
}

/* ── the Shwedagon Pagoda: the golden bell stupa on its broad terraces, the rings and the lotus, the banana-bud spire, the umbrella and the vane, the small stupas about it ── */
function shwedagon(x, y, z) {
  const r = Math.hypot(x, z), G = 'stone:GOLD', ax = Math.abs(x), az = Math.abs(z);
  if (y < 0.7) return Math.max(ax, az) < 4.7 ? 'stone:WHITE_PAINT' : null;
  /* the small stupas: the four corners and the middle of each side */
  for (const [sx, sz, s] of [[3.7, 3.7, 1], [-3.7, 3.7, 1], [3.7, -3.7, 1], [-3.7, -3.7, 1], [0, 4.0, 0.7], [0, -4.0, 0.7], [4.0, 0, 0.7], [-4.0, 0, 0.7]]) {
    const rr = Math.hypot(x - sx, z - sz);
    if (y >= 0.7 && y < 0.7 + 2.4 * s) { const t = (y - 0.7) / (2.4 * s), w = (0.62 * (1 - 0.55 * t) + (t < 0.15 ? 0.15 : 0)) * s; if (rr < w) return G; }
  }
  let R = 0;
  if (y < 1.3) R = 3.5; else if (y < 2.6) R = 3.1 - 0.5 * (y - 1.3) / 1.3; else if (y < 3.4) R = 2.7 - 0.3 * (y - 2.6) / 0.8;
  else if (y < 8.2) { const t = (y - 3.4) / 4.8; R = 2.4 * Math.sqrt(Math.max(0, 1 - sq(t * 1.0))) + 0.35 + 0.15 * t; } /* the great bell */
  else if (y < 9.4) R = 0.5 + 0.12 * Math.cos((y - 8.2) * 10); /* the rings */
  else if (y < 11.4) R = 0.58 * (1 - (y - 9.4) / 2.1) + 0.05; /* the banana bud */
  else if (y < 11.8) R = 0.05;
  else if (y < 13.4) R = 0.9 * (1 - (y - 11.8) / 1.6) * (0.6 + 0.4 * Math.abs(Math.cos((y - 11.8) * 5))) + 0.04; /* the umbrella: stacked rings tapering to the vane */
  return R > 0 && r < R ? G : null;
}

/* ── the Temple of Heaven: three round marble terraces, the round hall of red columns and gold bands, three tiers of blue conical roofs, the gold finial ── */
function templeHeaven(x, y, z) {
  const r = Math.hypot(x, z);
  for (let k = 0; k < 3; k++) if (y >= k * 0.7 && y < k * 0.7 + 0.7 && r < 5.8 - k * 0.8) return y > k * 0.7 + 0.5 && r > 5.8 - k * 0.8 - 0.3 ? 'stone:MARBLE' : 'stone:WHITE_STONE';
  if (y >= 2.1 && y < 4.7 && r < 2.5 && r > 1.5) return Math.abs(((Math.atan2(z, x) + 6.2832) % 0.5236) - 0.26) < 0.17 ? 'stone:RED_BRICK' : (y > 3.7 && y < 4.0 ? 'stone:GOLD' : 'glass:DARK_GLASS'); /* the hall's wall of red columns */
  const tiers = [[4.7, 6.2, 3.8, 2.3], [6.2, 7.6, 3.1, 1.7], [7.6, 8.9, 2.4, 0.5]];
  for (const [y0, y1, r0, r1] of tiers) if (y >= y0 && y < y1) { const t = (y - y0) / (y1 - y0), R = r0 + (r1 - r0) * t; if (r < R && r > R - 0.38) return 'glass:BLUE_GLASS'; if (y < y0 + 0.24 && r < r0 && r > 1.4) return 'stone:RED_BRICK'; }
  if (y >= 8.9 && y < 9.9 && Math.hypot(r, y - 9.4) < 0.55) return 'stone:GOLD';
  return null;
}

/* ── the Sagrada Familia: the long nave, the four tall towers of the Passion front pierced and ribbed, the great central tower behind, each tower ending in a pinnacle ── */
function sagrada(x, y, z) {
  const ax = Math.abs(x), S = 'stone:SAND_STONE';
  /* the nave: a long block with pierced windows, its roof */
  if (z < 1.9 && z > -3.0 && ax < 6.4) {
    if (y < 4.4) { const win = y > 1.4 && y < 3.2 && Math.floor((x + 100) / 0.9) % 2 && (z > 1.5 || z < -2.6); return win ? 'glass:DARK_GLASS' : S; }
    if (y < 5.0 && ax < 6.0 && z < 1.5) return 'stone:TRIM_CREAM';
  }
  /* the four towers of the front: hyperboloid shafts that narrow, ribbed, with a bulbous pinnacle */
  for (const tx of [-4.8, -1.6, 1.6, 4.8]) {
    const r = Math.hypot(x - tx, z - 0.4), h = 9.6 + (Math.abs(tx) > 3 ? 0 : 0.8);
    if (y >= 3.8 && y < h) { const t = (y - 3.8) / (h - 3.8), R = 0.78 * (1 - 0.35 * t) + 0.12 * Math.cos(Math.atan2(z - 0.4, x - tx) * 6) * (1 - t); if (r < R && (r > R - 0.3 || t > 0.97)) return (y % 1.2 < 0.25 && t > 0.1) ? 'glass:DARK_GLASS' : S; }
    if (y >= h && y < h + 0.9) { const t = (y - h) / 0.9; if (r < 0.62 * Math.sqrt(Math.max(0, 1 - sq(t * 1.05))) + 0.04) return t > 0.5 ? 'stone:GOLD' : S; }
  }
  /* the central tower (the Jesus tower): taller, stars at its top */
  const rc = Math.hypot(x, z + 1.6);
  if (y >= 4.4 && y < 11.8) { const t = (y - 4.4) / 7.4, R = 1.2 * (1 - 0.55 * t); if (rc < R && rc > R - 0.38) return S; }
  if (y >= 11.8 && y < 12.9 && rc < 0.5 * (1 - (y - 11.8) / 1.1) + 0.04) return 'stone:GOLD';
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
  rushmore: { ext: [20, 11.6, 8], cell: rushmore },
  kremlin: { ext: [20, 12.4, 11], cell: kremlin },
  taipei: { ext: [3.8, 17, 3.8], cell: taipei },
  golden_gate: { ext: [36, 8, 3.2], cell: goldenGate },
  brooklyn: { ext: [36, 7.2, 3.2], cell: brooklyn },
  chrysler: { ext: [3.2, 17, 3.2], cell: chrysler },
  gateway: { ext: [20, 19.6, 2.4], cell: gateway },
  arc: { ext: [11, 10, 5], cell: arc },
  brandenburg: { ext: [13, 9.6, 3.4], cell: brandenburg },
  pantheon: { ext: [9, 7.5, 12], cell: pantheon },
  notre_dame: { ext: [9, 13.4, 14], cell: notreDame },
  tokyo_tower: { ext: [5.6, 18.4, 5.6], cell: tokyoTower },
  atomium: { ext: [9, 12, 9], cell: atomium },
  great_wall: { ext: [32, 9.0, 11.5], cell: greatWall },
  petra: { ext: [15, 11.5, 6.4], cell: petra },
  machu: { ext: [18, 11, 12.4], cell: machu },
  hoover: { ext: [29, 13.2, 32], cell: hoover },
  abu_simbel: { ext: [14, 9.2, 6.8], cell: abuSimbel },
  sydney_bridge: { ext: [32, 9.2, 4.4], cell: sydneyBridge },
  borobudur: { ext: [18, 9.6, 18], cell: borobudur },
  shwedagon: { ext: [10, 13.6, 10], cell: shwedagon },
  temple_heaven: { ext: [12, 10.2, 12], cell: templeHeaven },
  sagrada: { ext: [14, 13.2, 6.4], cell: sagrada },
};
