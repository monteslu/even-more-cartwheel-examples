/* lanternfall: the game's rules (sim.h): strikes and sparks, the enemies and their tells, the wardens, pickups, shrines,
 * the tiles that break or open, rooms, events. Simulation code: + - * / sqrt and the engine's cw_ math only. */
#include <math.h>
#include <string.h>

#include "cartwheel/math.h"
#include "sim.h"

const char *const ABILITY_NAME[NABILITIES] = { "SPARK", "SECOND JUMP", "DASH", "GRAPPLE LINE", "WALL CLIMB", "PLUNGE", "TIDE CHARM" };
const char *const CHARM_NAME[CH_KINDS] = { "LONG WICK", "CUTTING DASH", "RUNNER'S STRIDE", "EMBER HARVEST", "SWIFT MEND", "UPDRAFT", "THORN WARD",
                                         "QUICK HAND", "TWIN SPARK", "QUAKE", "SECOND WIND" };
const char *const CHARM_LINE[CH_KINDS] = { "your strike reaches further", "{dash} cuts through what it passes, and nothing touches you in it",
                                         "hold {dash} and run on: a sprint, and {jump} keeps its speed", "every kill fills a measure of energy",
                                         "the mend takes half as long", "{attack} downward in the air bounces you higher", "a blow you take bursts out at all near",
                                         "your strikes come faster", "{shoot} sends the spark as two", "the plunge's shock reaches twice as far",
                                         "once a rest, a killing blow leaves you a leaf" };
const char *const KEEPSAKE_LINE[NKEEPSAKE] = {
  "a lamplighter's tally stick: a notch for every lamp lit, and the notches never end",
  "a brass wick trimmer, worn bright where a thumb rested on it for forty years",
  "a child's first lantern, no bigger than a fist, its glass painted with stars",
  "a ledger of oil: every flask that came down the Rootway, signed for in a careful hand",
  "a wick of braided hair, kept in a tin: someone's mother, someone's light",
  "a map of the lamp rounds, the safe ways inked in gold and the bad ways in red",
  "a ring of keys to doors that no longer stand",
  "a pressed flower from the garden that grew under the great lamp",
  "a glass bead from the Glasswater kilns, still warm when held to the cheek",
  "a lamplighter's badge: a flame inside a ring, the enamel half gone",
  "a letter never sent: the lamps are failing faster than we can climb to them",
  "a cog from the clockwork that once raised the lamps at dusk by itself",
  "a shell whistle the tide keepers blew when the water rose",
  "a star chart scratched into slate, with one star circled again and again",
  "a page of a hymn sung while the lamps were lit, the last verse torn away",
  "a forge token, good for one hot meal at the Forge in its busy years",
  "a glove stiff with frost: the Frostvault crews worked in pairs, and one came back",
  "a seed of the Bloom, hard as stone, that may grow again when the light comes back",
  "a carved lantern hook, the wood dark with handling",
  "a list of names: the last lamplighters, each crossed out but one",
  "a candle stub from the night the great lamp went dark",
  "a warden's scale, shed long before it forgot what it guarded",
  "a tin of lamp black, used to write the year over every door",
  "a brass bell that rang the hours from the beacon towers",
  "a drawing of the city lit end to end, done by a child from memory",
  "a spool of fuse cord from the Sky Warden's eyrie",
  "the last lamplighter's oath: keep one light burning, and the rest will follow it home" };
const char *const DYE_NAME[NDYES] = { "", "ember red", "moss green", "glass blue", "brass gold", "tide teal", "starlight violet", "forge orange", "frost white", "bloom rose" };
const char *const BOSS_NAME[NBOSSES] = { "THE MOSS WARDEN", "THE GLASS MANTIS", "THE BRASS SENTINEL", "THE TIDE SERPENT", "THE SKY WARDEN", "THE HEART OF THORNS" };

int sim_keepsakes(const sim *s) { return s->relics > CH_KINDS ? s->relics - CH_KINDS : 0; }
int sim_scarf_dye(const sim *s) {
  int d = sim_keepsakes(s) / KEEP_PER_DYE;
  return d < NDYES ? d : NDYES - 1;
}

/* rules a test can turn off (tools/rules.c --control): each check there must then fail */
int g_rules_off;

/* how hard each region is (the stage it is reached at) */
static const uint8_t TIER[NREGIONS] = { [R_GATE] = 0, [R_ROOTWAY] = 0, [R_GLASS] = 1, [R_FEN] = 2, [R_CLOCK] = 3, [R_TIDAL] = 4, [R_STAR] = 5,
                                        [R_SKY] = 6, [R_ARCHIVE] = 2, [R_FORGE] = 3, [R_FROST] = 5, [R_BLOOM] = 5, [R_HEART] = 6 };

/* ── small helpers ── */
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float absf(float v) { return v < 0 ? -v : v; }
static inline float signf(float v) { return v < 0 ? -1.0f : 1.0f; }
static inline bool overlap(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh) {
  /* boxes by centre and half size */
  return absf(ax - bx) < aw + bw && absf(ay - by) < ah + bh;
}
static void ev(sim *s, int kind, int a, int b, float x, float y) {
  if (s->nev < MAXEV) s->ev[s->nev++] = (event){ (uint8_t)kind, (uint8_t)a, (uint8_t)b, 0, x, y };
}
static uint32_t srand_(sim *s, uint32_t k) { /* a deterministic choice from the tick */
  uint32_t h = (s->tick * 2654435761u) ^ (k * 2246822519u);
  h ^= h >> 15;
  h *= 2246822519u;
  h ^= h >> 13;
  return h;
}
static int tile(const sim *s, int x, int y) { return w_in(x, y) ? s->t[y][x] : T_SOLID; }
static bool solid(const sim *s, float x, float y) { return t_solid(tile(s, (int)floorf(x), (int)floorf(y))); }

/* ── tiles that break or open: kept as seeds, replayed by a restore ── */
static uint32_t seeds[256];
static int nseeds;
static void flood(sim *s, int x, int y, int kind, bool record) {
  static int32_t st[8192];
  int n = 0;
  if (!w_in(x, y) || s->t[y][x] != kind) return;
  if (record && nseeds < 256) seeds[nseeds++] = (uint32_t)(y * TW + x) | ((uint32_t)kind << 24);
  st[n++] = y * TW + x;
  while (n) {
    int i = st[--n], cx = i % TW, cy = i / TW;
    if (!w_in(cx, cy) || s->t[cy][cx] != kind) continue;
    s->t[cy][cx] = T_EMPTY;
    s->broken[i >> 3] |= (uint8_t)(1 << (i & 7));
    if (n < 8188) st[n++] = i + 1, st[n++] = i - 1, st[n++] = i + TW, st[n++] = i - TW;
  }
}

static void open_kind_near_room(sim *s, int ri, int kind) {
  const room *r = &s->w->rooms[ri];
  for (int y = r->y * CELL_H - 2; y < (r->y + r->h) * CELL_H + 2; y++)
    for (int x = r->x * CELL_W - 2; x < (r->x + r->w) * CELL_W + 2; x++)
      if (w_in(x, y) && s->t[y][x] == kind) flood(s, x, y, kind, true);
}

static void open_seals(sim *s) {
  for (int y = 0; y < TH; y++)
    for (int x = 0; x < TW; x++)
      if (s->t[y][x] == T_SEAL) flood(s, x, y, T_SEAL, true);
}

/* ── enemies ── */
typedef struct enemy_def { const char *name; int hp; float hw, hh, speed; int tell; } enemy_def;
static const enemy_def EDEF[EN_KINDS] = {
  [EN_CRAWLER] = { "crawler", 3, 0.6f, 0.45f, 2.6f, 26 }, [EN_SPITTER] = { "spitter", 3, 0.55f, 0.7f, 0, 36 },
  [EN_FLYER] = { "flyer", 2, 0.55f, 0.45f, 3.5f, 28 },    [EN_HOPPER] = { "hopper", 3, 0.55f, 0.5f, 0, 22 },
  [EN_KNIGHT] = { "knight", 6, 0.55f, 0.95f, 1.8f, 30 },  [EN_PUFF] = { "puff", 2, 0.6f, 0.55f, 1.2f, 0 },
  [EN_DROPPER] = { "dropper", 2, 0.5f, 0.5f, 0, 24 },      [EN_BURROWER] = { "burrower", 3, 0.6f, 0.6f, 5.0f, 40 },
  [EN_EYE] = { "eye", 3, 0.5f, 0.75f, 0, 46 },
};
int enemy_tell_ticks(int k) { return EDEF[k].tell; }
/* what a body can be hit by: a burrower under the floor is out of reach */
static bool enemy_exposed(const enemy *e) { return !(e->kind == EN_BURROWER && !(e->aux & 1)); }
float enemy_hw(int k) { return EDEF[k].hw; }
float enemy_hh(int k) { return EDEF[k].hh; }

static bool enemy_ground(const sim *s, const enemy *e) {
  float hw = EDEF[e->kind].hw * 0.9f;
  return solid(s, e->x - hw, e->y + 0.05f) || solid(s, e->x + hw, e->y + 0.05f) || tile(s, (int)floorf(e->x), (int)floorf(e->y + 0.05f)) == T_ONEWAY;
}
/* move a ground enemy (feet at x, y): returns true when it hit a wall */
static bool enemy_move(sim *s, enemy *e, float dx, float dy) {
  float hw = EDEF[e->kind].hw, h = EDEF[e->kind].hh * 2;
  bool wall = false;
  float nx = e->x + dx;
  float lead = nx + signf(dx) * hw;
  if (dx != 0 && (solid(s, lead, e->y - 0.1f) || solid(s, lead, e->y - h + 0.1f))) wall = true;
  else e->x = nx;
  float ny = e->y + dy;
  if (dy > 0) {
    int row = (int)floorf(ny);
    bool land = false;
    for (float xx = e->x - hw * 0.9f; xx <= e->x + hw * 0.9f + 0.001f; xx += hw * 0.9f) {
      int t = tile(s, (int)floorf(xx), row);
      if (t_solid(t) || (t == T_ONEWAY && e->y <= (float)row + 0.001f)) land = true;
    }
    if (land) ny = (float)row, e->vy = 0;
  } else if (dy < 0 && (solid(s, e->x, ny - h))) ny = e->y, e->vy = 0;
  e->y = ny;
  return wall;
}

static void spawn_shot(sim *s, int kind, bool enemy, float x, float y, float vx, float vy, float w, float h, int life, int dmg) {
  for (int i = 0; i < MAXSHOTS; i++)
    if (!s->sh[i].alive) {
      s->sh[i] = (shot){ .kind = (uint8_t)kind, .alive = 1, .enemy = enemy, .x = x, .y = y, .vx = vx, .vy = vy, .w = w, .h = h, .life = (int16_t)life, .dmg = (int16_t)dmg };
      return;
    }
}

static player *nearest_player(sim *s, float x, float y, float *d2) {
  player *best = NULL;
  float bd = 1e30f;
  for (int i = 0; i < s->players; i++) {
    player *p = &s->p[i];
    if (!p->active || p->state == PS_DEAD) continue;
    float dx = p->x - x, dy = p->y - 0.9f - y, d = dx * dx + dy * dy;
    if (d < bd) bd = d, best = p;
  }
  if (d2) *d2 = bd;
  return best;
}

static void spawn_enemy(sim *s, int kind, float x, float y, int ent, int tier) {
  if (s->nen >= MAXEN) return;
  enemy *e = &s->en[s->nen++];
  memset(e, 0, sizeof *e);
  e->kind = (uint8_t)kind, e->alive = 1, e->x = x, e->y = y, e->hx = x, e->hy = y, e->ent = (int16_t)ent;
  e->hp = e->hp_max = (int16_t)(EDEF[kind].hp + tier / 2);
  e->facing_left = (uint8_t)((ent * 7 + kind) & 1);
  if (kind == EN_DROPPER) e->hy = y; /* (its perch on the ceiling) */
}

static void enemy_update(sim *s, enemy *e) {
  const enemy_def *d = &EDEF[e->kind];
  float pd2;
  player *p = nearest_player(s, e->x, e->y - d->hh, &pd2);
  float px = p ? p->x : e->x, py = p ? p->y - 0.9f : e->y;
  float dx = px - e->x, dy = py - (e->y - d->hh);
  float far = e->ent < 0 ? 4.0f : 1.0f; /* (a trial's guardians and a warden's helpers come for you from across the room) */
  if (e->flash > 0) e->flash--;
  e->t++;
  bool floats = e->kind == EN_FLYER || e->kind == EN_PUFF || (e->kind == EN_DROPPER && e->state != ES_ACT && e->state != ES_REST);
  bool ground = !floats && enemy_ground(s, e);
  if (!floats && !ground) e->vy = fminf(e->vy + 60 * DT, 20);
  /* knockback: a blow pushes it away and bleeds off (never off its ledge) */
  if (e->kb != 0) {
    float dir = e->kb > 0 ? 1.0f : -1.0f, ahead = e->x + dir * (d->hw + 0.3f);
    if (floats) {
      if (!solid(s, ahead, e->y - d->hh)) e->x += e->kb * DT;
    } else if (!(ground && !solid(s, ahead, e->y + 0.5f) && tile(s, (int)floorf(ahead), (int)floorf(e->y + 0.5f)) != T_ONEWAY)) enemy_move(s, e, e->kb * DT, 0);
    e->kb *= 0.82f;
    if (absf(e->kb) < 0.3f) e->kb = 0;
  }
  switch (e->kind) {
  case EN_CRAWLER: /* walks its floor; seeing you ahead it rears up (the tell), then charges */
    if (e->state == ES_IDLE) {
      float dir = e->facing_left ? -1.0f : 1.0f;
      float ahead = e->x + dir * (d->hw + 0.3f);
      bool edge = ground && !solid(s, ahead, e->y + 0.5f) && tile(s, (int)floorf(ahead), (int)floorf(e->y + 0.5f)) != T_ONEWAY;
      if (enemy_move(s, e, dir * d->speed * DT, e->vy * DT) || edge) e->facing_left ^= 1;
      if (p && absf(dy) < 1.5f && absf(dx) < 7 && (dx < 0) == (bool)e->facing_left) e->state = ES_TELL, e->t = 0, ev(s, EV_ENEMY_TELL, e->kind, 0, e->x, e->y);
    } else if (e->state == ES_TELL) {
      enemy_move(s, e, 0, e->vy * DT);
      if (e->t >= d->tell) e->state = ES_ACT, e->t = 0;
    } else if (e->state == ES_ACT) {
      float dir = e->facing_left ? -1.0f : 1.0f;
      float ahead = e->x + dir * (d->hw + 0.3f);
      bool edge = ground && !solid(s, ahead, e->y + 0.5f) && tile(s, (int)floorf(ahead), (int)floorf(e->y + 0.5f)) != T_ONEWAY;
      if (edge || enemy_move(s, e, dir * 9.5f * DT, e->vy * DT) || e->t > 45) e->state = ES_REST, e->t = 0;
    } else {
      enemy_move(s, e, 0, e->vy * DT);
      if (e->t > 40) e->state = ES_IDLE, e->t = 0;
    }
    break;
  case EN_SPITTER: /* rooted; it swells (the tell), then spits a slow orb at you */
    enemy_move(s, e, 0, e->vy * DT);
    e->facing_left = dx < 0;
    if (e->state == ES_IDLE && p && pd2 < 13 * 13 && e->t > 70) e->state = ES_TELL, e->t = 0, ev(s, EV_ENEMY_TELL, e->kind, 0, e->x, e->y);
    else if (e->state == ES_TELL && e->t >= d->tell) {
      float l = sqrtf(dx * dx + dy * dy) + 0.001f;
      spawn_shot(s, SH_ORB, true, e->x, e->y - d->hh * 1.5f, dx / l * 7.5f, dy / l * 7.5f, 0.35f, 0.35f, 200, 1);
      ev(s, EV_SHOT, SH_ORB, 1, e->x, e->y - d->hh);
      e->state = ES_IDLE, e->t = 0;
    }
    break;
  case EN_FLYER: /* hovers by its home; its wings flare (the tell), then it swoops to where you stood */
    if (e->state == ES_IDLE) {
      float bob = cw_sin((float)e->t * 0.07f + (float)e->ent);
      e->x += (e->hx + bob * 2.5f - e->x) * 0.04f;
      e->y += (e->hy + cw_sin((float)e->t * 0.11f) * 0.8f - e->y) * 0.06f;
      e->facing_left = dx < 0;
      if (p && e->ent < 0) e->hx += clampf(px - e->hx, -4 * DT, 4 * DT), e->hy += clampf(py - 3.5f - e->hy, -3 * DT, 3 * DT); /* (a guardian hunts) */
      if (p && pd2 < 81 && e->t > 50) e->state = ES_TELL, e->t = 0, e->vx = px, e->vy = py + 0.5f, ev(s, EV_ENEMY_TELL, e->kind, 0, e->x, e->y);
    } else if (e->state == ES_TELL) {
      if (e->t >= d->tell) e->state = ES_ACT, e->t = 0;
    } else if (e->state == ES_ACT) {
      float tx = e->vx - e->x, ty = e->vy - e->y, l = sqrtf(tx * tx + ty * ty);
      if (l < 0.4f || e->t > 50 || solid(s, e->x + tx / (l + 0.01f) * 0.8f, e->y + ty / (l + 0.01f) * 0.8f)) e->state = ES_REST, e->t = 0;
      else e->x += tx / l * 13 * DT, e->y += ty / l * 13 * DT;
    } else {
      e->x += (e->hx - e->x) * 0.03f, e->y += (e->hy - e->y) * 0.03f;
      if (e->t > 60) e->state = ES_IDLE, e->t = 0;
    }
    break;
  case EN_HOPPER: /* crouches (the tell), then leaps at you */
    if (e->state == ES_IDLE) {
      enemy_move(s, e, 0, e->vy * DT);
      e->facing_left = dx < 0;
      if (p && pd2 < 64 * far * far && e->t > 40 && ground) e->state = ES_TELL, e->t = 0, ev(s, EV_ENEMY_TELL, e->kind, 0, e->x, e->y);
    } else if (e->state == ES_TELL) {
      if (e->t >= d->tell) e->state = ES_ACT, e->t = 0, e->vy = -16, e->vx = clampf(dx * 1.4f, -7, 7);
    } else if (e->state == ES_ACT) {
      if (enemy_move(s, e, e->vx * DT, e->vy * DT)) e->vx = 0;
      if (e->t > 4 && enemy_ground(s, e)) e->state = ES_IDLE, e->t = 0, e->vx = 0;
      if (e->t > 120) e->state = ES_IDLE, e->t = 0;
    }
    break;
  case EN_KNIGHT: /* walks at you behind its shield; raises its spear (the tell), then thrusts */
    if (e->state == ES_IDLE) {
      e->facing_left = dx < 0;
      float dir = e->facing_left ? -1.0f : 1.0f, ahead = e->x + dir * (d->hw + 0.3f);
      bool edge = ground && !solid(s, ahead, e->y + 0.5f);
      if (p && absf(dx) < 10 * far && absf(dx) > 2.2f && absf(dy) < 3 && !edge) enemy_move(s, e, dir * d->speed * DT, e->vy * DT);
      else enemy_move(s, e, 0, e->vy * DT);
      if (p && absf(dx) < 3.2f && absf(dy) < 2 && e->t > 50) e->state = ES_TELL, e->t = 0, ev(s, EV_ENEMY_TELL, e->kind, 0, e->x, e->y);
    } else if (e->state == ES_TELL) {
      enemy_move(s, e, 0, e->vy * DT);
      if (e->t >= d->tell) e->state = ES_ACT, e->t = 0;
    } else if (e->state == ES_ACT) {
      enemy_move(s, e, 0, e->vy * DT);
      if (e->t > 14) e->state = ES_IDLE, e->t = 0;
    } else { /* staggered: its guard drops a moment */
      enemy_move(s, e, 0, e->vy * DT);
      if (e->t > 30) e->state = ES_IDLE, e->t = 0;
    }
    break;
  case EN_PUFF: { /* drifts about its home, soft (a touch only bumps you), then it swells (the tell) and bristles with
                  * thorns a while; only a strike from above hurts it, and that bounces you */
    float bob = cw_sin((float)e->t * 0.05f + (float)e->ent);
    e->x += (e->hx + bob * 2.2f - e->x) * 0.03f;
    e->y += (e->hy + cw_sin((float)e->t * 0.09f) * 0.5f - e->y) * 0.05f;
    e->facing_left = dx < 0;
    { /* every puff keeps the same breath (the world's tick), so a room of them is one rhythm to read */
      int ph = (int)(s->tick % PUFF_CYCLE), st = ph < PUFF_SOFT ? ES_IDLE : ph < PUFF_SOFT + 30 ? ES_TELL : ES_ACT;
      if (e->state == ES_REST && e->t < 20) break; /* (struck: a moment soft) */
      if (st == ES_TELL && e->state != ES_TELL && p && pd2 < 14 * 14) ev(s, EV_ENEMY_TELL, e->kind, 0, e->x, e->y);
      if (st != e->state) e->state = (uint8_t)st, e->t = 0;
    }
    break;
  }
  case EN_DROPPER: /* hangs from the ceiling; something passing under and it shivers, then drops */
    if (e->state == ES_IDLE) {
      e->y += (e->hy - e->y) * 0.2f;
      if (p && absf(dx) < 1.8f && dy > 0.5f && dy < 13) {
        bool clear = true;
        for (float yy = e->y + 0.3f; yy < py - 0.5f && clear; yy += 0.5f) clear = !solid(s, e->x, yy);
        if (clear) e->state = ES_TELL, e->t = 0, ev(s, EV_ENEMY_TELL, e->kind, 0, e->x, e->y);
      }
    } else if (e->state == ES_TELL) {
      if (e->t >= d->tell) e->state = ES_ACT, e->t = 0, e->vy = 2;
    } else if (e->state == ES_ACT) { /* falling: it bursts on the floor */
      e->vy = fminf(e->vy + 70 * DT, 24);
      enemy_move(s, e, 0, e->vy * DT);
      if (enemy_ground(s, e) && e->t > 2) {
        for (int k = -1; k <= 1; k += 2) spawn_shot(s, SH_ORB, true, e->x + (float)k * 0.4f, e->y - 0.4f, (float)k * 6, -7, 0.3f, 0.3f, 50, 1);
        ev(s, EV_SHOT, SH_ORB, 1, e->x, e->y);
        e->state = ES_REST, e->t = 0, e->vy = 0;
      }
      if (e->t > 120) e->state = ES_REST, e->t = 0;
    } else { /* stunned on the floor, then it climbs back up its thread */
      if (e->t < 90) enemy_move(s, e, 0, e->vy * DT);
      else {
        e->y -= 6 * DT;
        if (e->y <= e->hy) e->y = e->hy, e->state = ES_IDLE, e->t = 0, e->vy = 0;
      }
    }
    break;
  case EN_BURROWER: /* under the floor: it follows you along it (a ripple of dust), then bursts up where you stand */
    if (e->state == ES_IDLE) {
      enemy_move(s, e, 0, e->vy * DT);
      e->aux &= ~1;
      if (p && p->on_ground && absf(dx) < 9 * far && absf(p->y - e->y) < 0.6f && e->t > 50) e->state = ES_TELL, e->t = 0, ev(s, EV_ENEMY_TELL, e->kind, 0, e->x, e->y);
      else if (e->t > 200) e->state = ES_REST, e->t = 0, e->aux |= 1; /* (it comes up for air now and then) */
    } else if (e->state == ES_TELL) {
      if (e->t < d->tell - 16 && p) { /* tracks you, along firm floor only */
        float step = clampf(px - e->x, -d->speed * DT, d->speed * DT), nx = e->x + step;
        if (solid(s, nx + signf(step) * d->hw, e->y + 0.5f) && !solid(s, nx + signf(step) * d->hw, e->y - 0.5f)) e->x = nx;
      }
      if (e->t >= d->tell) e->state = ES_ACT, e->t = 0, e->aux |= 1, ev(s, EV_BREAK, 2, 0, e->x, e->y);
    } else if (e->state == ES_ACT) {
      if (e->t > 10) e->state = ES_REST, e->t = 0;
    } else if (e->state == ES_REST) { /* out, and open to blows, then it digs back down */
      e->aux |= 1;
      if (e->t > 80) e->state = ES_IDLE, e->t = 0, e->aux &= ~1;
    }
    break;
  case EN_EYE: /* a lantern eye on a stalk: it looks along its row, charges (a thin line shows), and burns a beam down it */
    enemy_move(s, e, 0, e->vy * DT);
    if (e->state == ES_IDLE) {
      e->facing_left = dx < 0;
      if (p && absf(dx) < 15 && absf(dy) < 2.2f && e->t > 80) e->state = ES_TELL, e->t = 0, ev(s, EV_ENEMY_TELL, e->kind, 0, e->x, e->y);
    } else if (e->state == ES_TELL) {
      if (e->t >= d->tell) {
        float dir = e->facing_left ? -1.0f : 1.0f, yy = e->y - 1.15f, len = 0.6f;
        while (len < 16 && !solid(s, e->x + dir * len, yy)) len += 0.5f;
        spawn_shot(s, SH_BEAM, true, e->x + dir * (0.5f + len * 0.5f), yy, 0, 0, len * 0.5f, 0.3f, 22, 1);
        ev(s, EV_SHOT, SH_BEAM, 1, e->x, yy);
        e->state = ES_REST, e->t = 0;
      }
    } else if (e->t > 50) e->state = ES_IDLE, e->t = 0;
    break;
  }
}

/* where the eye will burn: its row in front, while it charges (the view draws it) */
bool enemy_eye_line(const sim *s, const enemy *e, float *x0, float *x1, float *y);
bool enemy_eye_line(const sim *s, const enemy *e, float *x0, float *x1, float *y) {
  if (e->kind != EN_EYE || e->state != ES_TELL) return false;
  float dir = e->facing_left ? -1.0f : 1.0f, yy = e->y - 1.15f, len = 0.6f;
  while (len < 16 && !solid(s, e->x + dir * len, yy)) len += 0.5f;
  *x0 = e->x + dir * 0.5f, *x1 = e->x + dir * (0.5f + len), *y = yy;
  return true;
}

/* the knight's spear while it thrusts: a box in front */
static bool knight_spear(const enemy *e, float *x, float *y, float *w, float *h) {
  if (e->kind != EN_KNIGHT || e->state != ES_ACT) return false;
  *x = e->x + (e->facing_left ? -2.0f : 2.0f), *y = e->y - 1.1f, *w = 1.3f, *h = 0.25f;
  return true;
}

/* ── the wardens ── */
/* moves: the first phase's; at half health the warden staggers, roars, and adds its second phase's move (the last in
 * the list) and starts chaining one move straight into the next */
typedef struct boss_def { int hp; float hw, hh; bool flies; int moves[6], nmoves; int dmg; } boss_def;
static const boss_def BDEF[NBOSSES] = {
  { 28, 1.5f, 1.2f, false, { BM_CHARGE, BM_LEAP, BM_VOLLEY, BM_RAIN }, 3, 1 },
  { 40, 1.3f, 1.8f, false, { BM_SWEEP_LOW, BM_RAIN, BM_LEAP, BM_SWEEP_HIGH, BM_CHARGE }, 4, 1 },
  { 52, 1.6f, 2.0f, false, { BM_SWEEP_HIGH, BM_VOLLEY, BM_CHARGE, BM_SUMMON, BM_PILLARS }, 4, 1 },
  { 58, 1.5f, 1.2f, true, { BM_DIVE, BM_PILLARS, BM_VOLLEY, BM_RAIN }, 3, 1 },
  { 66, 1.4f, 1.2f, true, { BM_DIVE, BM_VOLLEY, BM_RAIN, BM_DIVE, BM_PILLARS }, 4, 2 },
  { 105, 2.2f, 2.6f, false, { BM_PILLARS, BM_VOLLEY, BM_SUMMON, BM_SWEEP_LOW, BM_RAIN, BM_SWEEP_HIGH }, 5, 2 },
};
static const int TELL_T[BM_KINDS] = { 44, 34, 34, 46, 40, 40, 40, 40, 50 };
int boss_tell_ticks(int move) { return TELL_T[move]; }
float boss_hw(int i) { return BDEF[i].hw; }
float boss_hh(int i) { return BDEF[i].hh; }

static void lock_doors(sim *s, int room, bool lock) {
  if (lock) {
    s->ndoor_lock = 0;
    for (int i = 0; i < s->w->ndoors && s->ndoor_lock < 8; i++) {
      const door *d = &s->w->doors[i];
      if (d->a != room && d->b != room) continue;
      if (d->pad == T_BOSSDOOR) continue; /* the prize door stays shut until the end */
      s->door_lock[s->ndoor_lock][0] = d->x, s->door_lock[s->ndoor_lock][1] = d->y, s->door_lock[s->ndoor_lock][2] = d->len | (d->vertical << 8);
      s->ndoor_lock++;
      for (int k = 0; k < d->len; k++)
        for (int j = 0; j < 4; j++) {
          int x = d->vertical ? d->x + k : d->x + j, y = d->vertical ? d->y + j : d->y + k;
          if (w_in(x, y) && (s->t[y][x] == T_EMPTY || s->t[y][x] == T_ONEWAY)) s->t[y][x] = T_BOSSDOOR;
        }
    }
  } else {
    for (int i = 0; i < s->ndoor_lock; i++) {
      int x0 = s->door_lock[i][0], y0 = s->door_lock[i][1], len = s->door_lock[i][2] & 255, vert = s->door_lock[i][2] >> 8;
      for (int k = 0; k < len; k++)
        for (int j = 0; j < 4; j++) {
          int x = vert ? x0 + k : x0 + j, y = vert ? y0 + j : y0 + k;
          if (w_in(x, y) && s->t[y][x] == T_BOSSDOOR) s->t[y][x] = s->w->t[y][x] == T_BOSSDOOR ? T_EMPTY : s->w->t[y][x];
        }
    }
    s->ndoor_lock = 0;
  }
}

static void boss_start(sim *s, int ri, int ei) {
  const ent *e = &s->w->ents[ei];
  const room *r = &s->w->rooms[ri];
  boss *b = &s->b;
  memset(b, 0, sizeof *b);
  b->idx = e->sub;
  const boss_def *d = &BDEF[b->idx];
  b->active = b->alive = true;
  b->hp = b->hp_max = (int16_t)(d->hp + s->deaths / 3 * 0); /* (no softening: the fight is the same every time) */
  b->room = ri, b->ent = ei;
  b->ax0 = (float)(r->x * CELL_W + 3), b->ax1 = (float)((r->x + r->w) * CELL_W - 3);
  b->ay0 = (float)(r->y * CELL_H + 2), b->floor_y = (float)(r->y * CELL_H + CELL_H * r->h - 2), b->ay1 = b->floor_y;
  b->x = e->x, b->y = d->flies ? b->floor_y - 7 : b->floor_y;
  b->state = ES_REST, b->t = 0;
  s->boss_ticks = 1;
  lock_doors(s, ri, true);
  ev(s, EV_BOSS_START, b->idx, 0, b->x, b->y);
}

static void boss_down(sim *s) {
  boss *b = &s->b;
  b->alive = false, b->active = false;
  s->bosses_down |= (uint8_t)(1 << b->idx);
  lock_doors(s, b->room, false);
  open_kind_near_room(s, b->room, T_BOSSDOOR);
  s->boss_ticks = 0;
  for (int i = 0; i < MAXSHOTS; i++)
    if (s->sh[i].enemy) s->sh[i].alive = 0;
  for (int i = 0; i < s->nen; i++) s->en[i].alive = 0;
  ev(s, EV_BOSS_DOWN, b->idx, 0, b->x, b->y - BDEF[b->idx].hh);
  if (sim_beacons(s) >= 5) open_seals(s), ev(s, EV_SEAL, 0, 0, b->x, b->y);
  if (b->idx == NBOSSES - 1) s->won = true;
}

static void boss_update(sim *s) {
  boss *b = &s->b;
  if (!b->active) return;
  const boss_def *d = &BDEF[b->idx];
  s->boss_ticks++;
  if (b->flash > 0) b->flash--;
  b->t++;
  float pd2;
  player *p = nearest_player(s, b->x, b->y - d->hh, &pd2);
  float px = p ? p->x : b->x, py = p ? p->y : b->floor_y;
  bool late = b->phase > 0; /* the second phase: tells a little shorter (never under 26 ticks) */
  if (b->phase == 0 && b->hp * 2 < b->hp_max && b->state != ES_ACT && !(g_rules_off & RULE_PHASE)) { /* half health: it staggers and roars */
    b->phase = 1, b->state = ES_HURT, b->t = 0, b->vx = 0;
    for (int i = 0; i < MAXSHOTS; i++)
      if (s->sh[i].enemy) s->sh[i].alive = 0;
    s->hitstop = 10;
    ev(s, EV_BOSS_PHASE, b->idx, 0, b->x, b->y - d->hh);
    return;
  }
  if (b->state == ES_HURT) { /* the stagger: open to blows, then the second phase */
    if (!d->flies) {
      b->vy = fminf(b->vy + 60 * DT, 22);
      b->y = fminf(b->y + b->vy * DT, b->floor_y);
    } else b->y += (b->floor_y - 3 - b->y) * 0.05f; /* (it sinks within reach) */
    if (b->t > 75) b->state = ES_REST, b->t = 0;
    return;
  }
  if (b->state == ES_REST) {
    if (!d->flies) {
      b->vy = fminf(b->vy + 60 * DT, 22);
      b->y = fminf(b->y + b->vy * DT, b->floor_y);
      if (b->y >= b->floor_y) b->vy = 0;
      b->facing_left = px < b->x;
    } else {
      float hy = b->ay0 + 5 + cw_sin((float)s->tick * 0.05f) * 0.7f;
      b->y += (hy - b->y) * 0.05f;
      b->x += clampf((px - b->x) * 0.02f, -0.12f, 0.12f);
      b->facing_left = px < b->x;
    }
    bool chain = late && (b->moves_done % 3) == 1; /* (the second phase strings two moves together) */
    if (b->t > (chain ? 12 : late ? 34 : 50)) {
      int nm = d->nmoves + (late ? 1 : 0);
      int pick = d->moves[(b->moves_done + (int)(srand_(s, b->moves_done) % 2u)) % nm];
      b->move = (uint8_t)pick;
      b->state = ES_TELL, b->t = 0;
      b->nmarks = 0;
      if (pick == BM_RAIN || pick == BM_PILLARS || (pick == BM_VOLLEY && b->idx == 0)) { /* where it will strike: shown during the tell */
        int n = pick == BM_RAIN ? 4 : 3;
        if (pick == BM_VOLLEY) n = late ? 4 : 3;
        for (int i = 0; i < n; i++) {
          float x = i == 0 ? px : b->ax0 + 2 + (float)(srand_(s, (uint32_t)i + 7) % (uint32_t)(b->ax1 - b->ax0 - 4));
          b->marks[b->nmarks++] = clampf(x, b->ax0 + 1, b->ax1 - 1);
        }
      }
      if (pick == BM_DIVE) b->tx = px, b->ty = py - 0.9f;
      ev(s, EV_BOSS_TELL, b->idx, pick, b->x, b->y);
    }
    return;
  }
  if (b->state == ES_TELL) {
    int tell = TELL_T[b->move];
    if (late) tell = tell * 4 / 5 < 26 ? 26 : tell * 4 / 5;
    if (b->move == BM_DIVE) b->tx += clampf(px - b->tx, -0.05f, 0.05f);
    if (b->t >= tell) {
      b->state = ES_ACT, b->t = 0;
      ev(s, EV_BOSS_ACT, b->idx, b->move, b->x, b->y);
      float dir = b->facing_left ? -1.0f : 1.0f;
      switch (b->move) {
      case BM_LEAP: {
        float air = 2 * 22.0f / 60.0f;
        b->vy = -22, b->vx = clampf((px - b->x) / air, -16, 16);
        break;
      }
      case BM_VOLLEY: {
        if (b->idx == 0) { /* the moss warden lobs spores at the marked spots: they fall where the marks glow */
          for (int i = 0; i < b->nmarks; i++) {
            float T = 1.0f, x0 = b->x + dir * d->hw, y0 = b->y - d->hh * 1.6f;
            spawn_shot(s, SH_SPORE, true, x0, y0, (b->marks[i] - x0) / T, (b->floor_y - 0.4f - y0 - 0.5f * 30 * T * T) / T, 0.45f, 0.45f, 160, 1);
          }
          break;
        }
        float dx = px - b->x, dy = py - 0.9f - (b->y - d->hh), l = sqrtf(dx * dx + dy * dy) + 0.01f;
        int n = late ? 5 : 3;
        for (int i = 0; i < n; i++) {
          float a = ((float)i - (float)(n - 1) * 0.5f) * 0.28f, cs, sn;
          cw_sincos(a, &sn, &cs);
          float vx = (dx * cs - dy * sn) / l * 9, vy = (dx * sn + dy * cs) / l * 9;
          spawn_shot(s, b->idx == 0 ? SH_SPORE : b->idx == 4 ? SH_FEATHER : SH_ORB, true, b->x + dir * d->hw, b->y - d->hh * 1.3f, vx, vy, 0.4f, 0.4f, 220, 1);
        }
        break;
      }
      case BM_RAIN:
        for (int i = 0; i < b->nmarks; i++) spawn_shot(s, SH_SHARD, true, b->marks[i], b->ay0 + 1, 0, 16, 0.35f, 0.7f, 160, 1);
        break;
      case BM_SWEEP_LOW: spawn_shot(s, SH_BEAM, true, (b->ax0 + b->ax1) * 0.5f, b->floor_y - 0.7f, 0, 0, (b->ax1 - b->ax0) * 0.5f, 0.55f, 34, 1); break;
      case BM_SWEEP_HIGH: spawn_shot(s, SH_BEAM, true, (b->ax0 + b->ax1) * 0.5f, b->floor_y - 2.75f, 0, 0, (b->ax1 - b->ax0) * 0.5f, 0.5f, 34, 1); break;
      case BM_PILLARS:
        for (int i = 0; i < b->nmarks; i++) spawn_shot(s, SH_PILLAR, true, b->marks[i], (b->ay0 + b->floor_y) * 0.5f, 0, 0, 0.8f, (b->floor_y - b->ay0) * 0.5f, 40, 1);
        break;
      case BM_SUMMON:
        spawn_enemy(s, b->idx == 2 ? EN_CRAWLER : EN_FLYER, b->ax0 + 3, b->idx == 2 ? b->floor_y : b->floor_y - 5, -1, 2);
        spawn_enemy(s, b->idx == 2 ? EN_CRAWLER : EN_HOPPER, b->ax1 - 3, b->floor_y, -1, 2);
        break;
      default: break;
      }
    }
    return;
  }
  /* acting */
  float dir = b->facing_left ? -1.0f : 1.0f;
  bool done = false;
  switch (b->move) {
  case BM_CHARGE:
    b->x += dir * 13 * DT;
    if (b->x < b->ax0 + d->hw || b->x > b->ax1 - d->hw) b->x = clampf(b->x, b->ax0 + d->hw, b->ax1 - d->hw), done = true;
    break;
  case BM_LEAP:
    b->vy += 60 * DT;
    b->x = clampf(b->x + b->vx * DT, b->ax0 + d->hw, b->ax1 - d->hw);
    b->y += b->vy * DT;
    if (b->y >= b->floor_y && b->vy > 0) {
      b->y = b->floor_y, b->vy = 0, done = true;
      spawn_shot(s, SH_WAVE, true, b->x - d->hw, b->floor_y - 0.5f, -12, 0, 0.5f, 0.5f, 120, 1);
      spawn_shot(s, SH_WAVE, true, b->x + d->hw, b->floor_y - 0.5f, 12, 0, 0.5f, 0.5f, 120, 1);
      ev(s, EV_BREAK, 1, 0, b->x, b->floor_y);
    }
    break;
  case BM_DIVE: {
    float tx = b->tx - b->x, ty = b->ty - b->y, l = sqrtf(tx * tx + ty * ty);
    if (l < 0.5f || b->t > 60 || b->y >= b->floor_y - 0.2f) done = true;
    else b->x += tx / l * 21 * DT, b->y += ty / l * 21 * DT;
    break;
  }
  default: done = b->t > 30; break;
  }
  if (done) b->state = ES_REST, b->t = 0, b->moves_done++;
}

/* ── the game ── */

void sim_init(sim *s, const world *w, int players) {
  memset(s, 0, sizeof *s);
  s->w = w;
  memcpy(s->t, w->t, sizeof s->t);
  nseeds = 0;
  s->players = players < 1 ? 1 : players > MAXP ? MAXP : players;
  for (int i = 0; i < MAXP; i++) {
    s->p[i].hp_max = 5, s->p[i].en_max = 3;
    player_spawn(&s->p[i], w->start_x + (float)i * 1.2f, w->start_y);
    s->p[i].active = i < s->players;
  }
  s->save_ent = -1;
  s->trial_room = -1;
  for (int i = 0; i < MAXP; i++) s->p[i].second_wind = true;
  for (int i = 0; i < w->nents; i++)
    if (w->ents[i].kind == EK_START) s->save_ent = -1;
  s->pw.t = (const uint8_t(*)[TW])s->t;
  s->room = -1;
  sim_enter_room(s, w_room_at(w, w->start_x, w->start_y - 0.5f));
}

void sim_enter_room(sim *s, int room) {
  if (room < 0 || room == s->room) return;
  s->prev_room = s->room;
  s->room = room;
  s->seen[room >> 3] |= (uint8_t)(1 << (room & 7));
  s->nen = 0;
  for (int i = 0; i < MAXSHOTS; i++)
    if (s->sh[i].enemy) s->sh[i].alive = 0;
  int tier = TIER[s->w->rooms[room].region];
  for (int i = 0; i < s->w->nents; i++) {
    const ent *e = &s->w->ents[i];
    if (e->room != room) continue;
    if (e->kind == EK_ENEMY && !((s->slain[i >> 3] >> (i & 7)) & 1)) spawn_enemy(s, e->sub, e->x, e->y, i, tier);
    (void)0; /* (a warden wakes once you are well inside its arena: sim_step) */
  }
  pworld_anchors(&s->pw, s->w, room);
  ev(s, EV_ROOM, (int)(room & 255), (int)(room >> 8), 0, 0);
}

void sim_teleport(sim *s, float x, float y) {
  for (int i = 0; i < s->players; i++) {
    player *p = &s->p[i];
    int hp = p->hp, en = p->en;
    bool sw = p->second_wind;
    player_spawn(p, x + (float)i * 1.2f, y);
    p->hp = (int16_t)hp, p->en = (int16_t)en, p->second_wind = sw;
    p->active = 1;
  }
  int r = w_room_at(s->w, x, y - 0.5f);
  s->room = -1;
  sim_enter_room(s, r);
}

bool sim_got(const sim *s, int id) { return id >= 0 && (s->got[id >> 3] >> (id & 7)) & 1; }
int sim_beacons(const sim *s) {
  int n = 0;
  for (int i = 0; i < 5; i++) n += (s->bosses_down >> i) & 1;
  return n;
}
int sim_pickups_total(const world *w, int pk) {
  int n = 0;
  for (int i = 0; i < w->nents; i++) n += w->ents[i].kind == EK_PICKUP && w->ents[i].sub == pk;
  return n;
}
bool sim_pk_seen(const sim *s, int id) { return id >= 0 && id < MAXENTS && ((s->pk_seen[id >> 3] >> (id & 7)) & 1); }
int sim_finds(const sim *s) {
  int n = 0;
  for (int k = 0; k < PK_KINDS; k++) n += sim_pickups_got(s, k);
  return n;
}
int sim_lore_found(const sim *s) {
  int n = 0;
  for (int i = 0; i < NLORE; i++) n += (s->lore[i >> 3] >> (i & 7)) & 1;
  return n;
}
bool sim_in_view(const world *w, int ri, float fx, float fy, float px, float py) {
  if (ri < 0 || w_room_at(w, px, py) != ri) return false;
  const room *rm = &w->rooms[ri];
  float tx = fx, ty = fy - 1.2f;
  float x0 = (float)(rm->x * CELL_W) + 16, x1 = (float)((rm->x + rm->w) * CELL_W) - 16;
  float y0 = (float)(rm->y * CELL_H) + 9, y1 = (float)((rm->y + rm->h) * CELL_H) - 9;
  tx = x1 < x0 ? (x0 + x1) * 0.5f : tx < x0 ? x0 : tx > x1 ? x1 : tx;
  ty = y1 < y0 ? (y0 + y1) * 0.5f : ty < y0 ? y0 : ty > y1 ? y1 : ty;
  return absf(px - tx) <= 15.6f && absf(py - ty) <= 8.6f;
}
int sim_pickups_got(const sim *s, int pk) {
  int n = 0;
  for (int i = 0; i < s->w->nents; i++) n += s->w->ents[i].kind == EK_PICKUP && s->w->ents[i].sub == pk && sim_got(s, s->w->ents[i].id);
  return n;
}

static void enemy_damage(sim *s, enemy *e, int dmg, float from_x, float push, int pi);
static void hurt(sim *s, player *p, int dmg, float from_x) {
  if (p->invuln > 0 || p->state == PS_DEAD || s->god) return;
  if (p->state == PS_DASH && (s->charms & CHARM_BIT(CH_DASHSTRIKE))) return; /* (the cutting dash: nothing touches you) */
  if (s->assist > 1 && (++p->assist_hits % s->assist) != 0) dmg = 0; /* assist: only every n-th hit costs a leaf */
  if (dmg > 0 && p->hp - dmg <= 0 && p->second_wind && (s->charms & CHARM_BIT(CH_SECONDWIND))) /* the second wind: a leaf left */
    dmg = p->hp - 1, p->second_wind = false, ev(s, EV_CHARM, CH_SECONDWIND, 1, p->x, p->y - 0.9f);
  p->hp = (int16_t)(p->hp - dmg);
  p->invuln = 70;
  s->hitstop = s->hitstop > 7 ? s->hitstop : 7; /* (a blow taken holds the world a moment: you feel it land) */
  if (s->charms & CHARM_BIT(CH_THORNWARD)) /* the thorn ward: the blow bursts out at everything near */
    for (int i = 0; i < s->nen; i++) {
      enemy *e = &s->en[i];
      if (e->alive && enemy_exposed(e) && absf(e->x - p->x) < 3.5f && absf(e->y - p->y) < 3) enemy_damage(s, e, 2, p->x, 10, (int)(p - s->p));
    }
  if (p->state == PS_GRAPPLE || p->state == PS_CLING || p->state == PS_DASH || p->state == PS_POUND) p->state = PS_NORMAL, p->anchor = -1;
  if (p->state != PS_SWIM) {
    p->state = PS_HURT, p->hurt_t = 12;
    p->vx = p->x < from_x ? -9.0f : 9.0f;
    p->vy = -11;
  }
  ev(s, EV_HURT, (int)(p - s->p), dmg, p->x, p->y - 0.9f);
}

static void die_and_return(sim *s, player *p) {
  int i = (int)(p - s->p);
  ev(s, EV_DIE, i, 0, p->x, p->y - 0.9f);
  if (i == 0 || s->players == 1) {
    s->deaths++;
    /* the flame stays where you fell, with a measure of your energy (a second fall moves it here) */
    if (s->echo) for (int k = 0; k < MAXP; k++) s->p[k].en_max = (int16_t)(s->p[k].en_max + 1);
    s->echo = false;
    if (p->en_max > 1 && !(g_rules_off & RULE_ECHO)) {
      s->echo = true, s->echo_x = p->safe_x, s->echo_y = p->safe_y;
      for (int k = 0; k < MAXP; k++) s->p[k].en_max = (int16_t)(s->p[k].en_max - 1);
      ev(s, EV_ECHO, 0, 0, s->echo_x, s->echo_y);
    }
    if (s->b.active) lock_doors(s, s->b.room, false), s->b.active = false, s->boss_ticks = 0;
    if (s->trial_room >= 0) lock_doors(s, s->trial_room, false), s->trial_room = -1; /* (the trial starts over next time) */
    float x = s->w->start_x, y = s->w->start_y;
    if (s->save_ent >= 0) x = s->w->ents[s->save_ent].x, y = s->w->ents[s->save_ent].y;
    for (int k = 0; k < s->players; k++) s->p[k].hp = s->p[k].hp_max, s->p[k].en = s->p[k].en_max, s->p[k].second_wind = true;
    memset(s->slain, 0, sizeof s->slain);
    sim_teleport(s, x, y);
  } else { /* the second player comes back beside the first */
    int16_t hpm = p->hp_max, enm = p->en_max;
    player_spawn(p, s->p[0].x, s->p[0].y);
    p->hp_max = hpm, p->en_max = enm, p->hp = hpm, p->en = enm, p->active = 1;
    p->invuln = 90;
  }
}

/* a blow lands on an enemy: it flashes, is pushed away (harder by a heavier blow), and a kill pays */
static void enemy_damage(sim *s, enemy *e, int dmg, float from_x, float push, int pi) {
  float hh = EDEF[e->kind].hh;
  e->hp = (int16_t)(e->hp - dmg), e->flash = 8;
  if (e->state == ES_TELL || (e->state == ES_ACT && e->kind != EN_DROPPER)) e->state = ES_REST, e->t = 0; /* a blow staggers: the attack is broken off */
  bool rooted = e->kind == EN_SPITTER || e->kind == EN_EYE || e->kind == EN_BURROWER || (e->kind == EN_DROPPER && e->state != ES_REST);
  float heft = e->kind == EN_KNIGHT ? 0.45f : rooted ? 0 : 1;
  e->kb = (g_rules_off & RULE_KNOCKBACK) ? 0 : (e->x < from_x ? -1.0f : 1.0f) * push * heft * (e->hp <= 0 ? 1.6f : 1);
  if (e->kind == EN_FLYER || e->kind == EN_PUFF) e->y -= 0.25f * heft;
  if (e->hp <= 0) {
    e->alive = 0, ev(s, EV_KILL, e->kind, pi, e->x, e->y - hh);
    if (e->ent >= 0 && !(g_rules_off & RULE_SLAIN)) s->slain[e->ent >> 3] |= (uint8_t)(1 << (e->ent & 7));
    if ((s->charms & CHARM_BIT(CH_HARVEST)) && pi >= 0 && pi < MAXP && s->p[pi].en < s->p[pi].en_max) s->p[pi].en++;
  } else ev(s, EV_HIT, 0, pi, e->x, e->y - hh);
}

static void strike(sim *s, player *p, int pi) {
  /* the strike's box: in front, above, or below (in the air); the long wick reaches further */
  float bx, by, bw, bh, reach = (s->charms & CHARM_BIT(CH_REACH)) ? 1.35f : 1.0f;
  if (p->atk_dir == 1) bx = p->x, by = p->y - 2.5f - (reach - 1) * 1.2f, bw = 1.1f, bh = 0.95f * reach;
  else if (p->atk_dir == 2) bx = p->x, by = p->y + 0.7f + (reach - 1) * 1.0f, bw = 1.1f, bh = 0.9f * reach;
  else bx = p->x + (p->facing_left ? -1.3f : 1.3f) * reach, by = p->y - 0.85f, bw = 1.15f * reach, bh = 1.15f;
  bool hit = false, landed = false, killed = false;
  for (int i = 0; i < s->nen; i++) {
    enemy *e = &s->en[i];
    if (!e->alive || !enemy_exposed(e)) continue;
    float hw = EDEF[e->kind].hw, hh = EDEF[e->kind].hh;
    if (!overlap(bx, by, bw, bh, e->x, e->y - hh, hw, hh)) continue;
    /* a knight's shield turns a strike from the front; a puff gives only to a blow from above */
    if ((e->kind == EN_KNIGHT && p->atk_dir == 0 && e->state != ES_ACT && ((p->x < e->x) == !e->facing_left)) || (e->kind == EN_PUFF && p->atk_dir != 2 && !(g_rules_off & RULE_PUFF))) {
      ev(s, EV_HIT, 2, 0, e->x, e->y - hh);
      ev(s, EV_PARRY, pi, e->kind, (bx + e->x) * 0.5f, by);
      p->vx = p->x < e->x ? -8.0f : 8.0f, p->recoil_t = 10;
      if (e->kind == EN_PUFF) e->kb = p->x < e->x ? 6.0f : -6.0f;
      s->hitstop = s->hitstop > 3 ? s->hitstop : 3;
      hit = true;
      continue;
    }
    enemy_damage(s, e, 1, p->x, 13, pi);
    hit = landed = true;
    killed |= !e->alive;
    p->en_frac = (int16_t)(p->en_frac + 90);
  }
  if (s->b.active) {
    boss *b = &s->b;
    float hw = BDEF[b->idx].hw, hh = BDEF[b->idx].hh;
    if (overlap(bx, by, bw, bh, b->x, b->y - hh, hw, hh)) {
      b->hp--, b->flash = 6, hit = landed = true;
      p->en_frac = (int16_t)(p->en_frac + 60);
      ev(s, EV_BOSS_HIT, b->idx, 0, bx, by);
      if (b->hp <= 0) boss_down(s), killed = true;
    }
  }
  /* a strike cuts the small things thrown at you out of the air */
  for (int i = 0; i < MAXSHOTS; i++) {
    shot *h = &s->sh[i];
    if (h->alive && h->enemy && (h->kind == SH_ORB || h->kind == SH_SPORE || h->kind == SH_FEATHER || h->kind == SH_SHARD) && overlap(bx, by, bw, bh, h->x, h->y, h->w, h->h))
      h->alive = 0, ev(s, EV_HIT, 1, pi, h->x, h->y), hit = true;
  }
  /* hidden walls break under a strike */
  for (int ty = (int)floorf(by - bh); ty <= (int)floorf(by + bh); ty++)
    for (int tx = (int)floorf(bx - bw); tx <= (int)floorf(bx + bw); tx++)
      if (tile(s, tx, ty) == T_SECRET) flood(s, tx, ty, T_SECRET, true), ev(s, EV_SECRET, 0, 0, (float)tx, (float)ty);
  /* a blow that lands holds the world still a moment (longer for a kill) and pushes you back from what you hit */
  if (landed) {
    int hs = killed ? 6 : 4;
    s->hitstop = s->hitstop > hs ? s->hitstop : hs;
    if (p->atk_dir == 0 && !(g_rules_off & RULE_RECOIL)) p->vx = p->facing_left ? 5.5f : -5.5f, p->recoil_t = 8;
  }
  /* a downward strike bounces off what it hits, thorns included */
  if (p->atk_dir == 2) {
    bool thorn = false;
    for (int tx = (int)floorf(bx - bw); tx <= (int)floorf(bx + bw); tx++)
      for (int ty = (int)floorf(by - bh); ty <= (int)floorf(by + bh); ty++) thorn |= tile(s, tx, ty) == T_SPIKE;
    if (hit || thorn)
      p->vy = (s->charms & CHARM_BIT(CH_UPDRAFT)) ? -21.0f : -17.0f, p->jump_rising = false, p->air_jumps = (s->abilities & AB_DJUMP) ? 1 : 0, p->dash_ready = true,
      p->dash_cd = 0, ev(s, EV_BOUNCE, pi, 0, p->x, p->y);
  }
}

/* ── trials: an upgrade's room locks, its guardians come in two waves, and the find is yours once they fall ── */
bool sim_trial_room(const sim *s, int room) {
  if ((g_rules_off & RULE_TRIAL) || room < 0 || s->w->rooms[room].kind != RK_ITEM || ((s->trials_done[room >> 3] >> (room & 7)) & 1)) return false;
  for (int i = 0; i < s->w->nents; i++) {
    const ent *e = &s->w->ents[i];
    if (e->kind == EK_PICKUP && e->room == room) return e->sub != PK_ABILITY && !sim_got(s, e->id);
  }
  return false;
}
static int floor_under(const sim *s, int x, int y0, int y1) { /* the first standable floor going down x, or -1 */
  for (int y = y0; y < y1; y++)
    if (t_solid(tile(s, x, y)) && !t_solid(tile(s, x, y - 1)) && !t_solid(tile(s, x, y - 2)) && tile(s, x, y - 1) != T_SPIKE) return y;
  return -1;
}
static void trial_wave(sim *s) {
  const room *rm = &s->w->rooms[s->trial_room];
  int tier = TIER[rm->region], X0 = rm->x * CELL_W, Y0 = rm->y * CELL_H;
  /* each region's guardians: its own kinds */
  static const uint8_t GUARD[NREGIONS][3] = {
    [R_GATE] = { EN_CRAWLER, EN_FLYER, EN_CRAWLER }, [R_ROOTWAY] = { EN_CRAWLER, EN_FLYER, EN_HOPPER }, [R_GLASS] = { EN_HOPPER, EN_FLYER, EN_DROPPER },
    [R_FEN] = { EN_BURROWER, EN_FLYER, EN_HOPPER }, [R_CLOCK] = { EN_KNIGHT, EN_FLYER, EN_CRAWLER }, [R_TIDAL] = { EN_BURROWER, EN_FLYER, EN_HOPPER },
    [R_STAR] = { EN_KNIGHT, EN_FLYER, EN_BURROWER }, [R_SKY] = { EN_HOPPER, EN_FLYER, EN_KNIGHT }, [R_ARCHIVE] = { EN_KNIGHT, EN_FLYER, EN_HOPPER },
    [R_FORGE] = { EN_CRAWLER, EN_FLYER, EN_KNIGHT }, [R_FROST] = { EN_BURROWER, EN_FLYER, EN_KNIGHT }, [R_BLOOM] = { EN_HOPPER, EN_FLYER, EN_BURROWER },
    [R_HEART] = { EN_KNIGHT, EN_FLYER, EN_CRAWLER },
  };
  int n = s->trial_wave == 0 ? 2 : 3 + (tier >= 3);
  for (int k = 0; k < n; k++) {
    int kind = GUARD[rm->region][(k + s->trial_wave) % 3];
    if (kind == EN_DROPPER) kind = EN_CRAWLER;
    int x = X0 + (k & 1 ? CELL_W * rm->w - 6 - k : 5 + k);
    if (kind == EN_FLYER || kind == EN_PUFF) { spawn_enemy(s, kind, (float)x + 0.5f, (float)(Y0 + 6), -1, tier); continue; }
    int fy = floor_under(s, x, Y0 + 3, Y0 + CELL_H * rm->h);
    if (fy > 0) spawn_enemy(s, kind, (float)x + 0.5f, (float)fy, -1, tier);
    else spawn_enemy(s, EN_FLYER, (float)x + 0.5f, (float)(Y0 + 6), -1, tier);
  }
  ev(s, EV_TRIAL_WAVE, s->trial_wave, 0, (float)(X0 + CELL_W * rm->w / 2), (float)(Y0 + CELL_H / 2));
}
static void trial_update(sim *s) {
  if (s->trial_room < 0) {
    if (s->room < 0 || !sim_trial_room(s, s->room) || s->b.active) return;
    const room *rm = &s->w->rooms[s->room];
    float x0 = (float)(rm->x * CELL_W + 5), x1 = (float)((rm->x + rm->w) * CELL_W - 5);
    const player *p = &s->p[0];
    if (p->x > x0 && p->x < x1 && p->y > (float)(rm->y * CELL_H) + 5.5f && p->on_ground) {
      s->trial_room = s->room, s->trial_wave = 0, s->trial_t = 0;
      lock_doors(s, s->room, true);
      ev(s, EV_TRIAL, 0, 0, p->x, p->y);
    }
    return;
  }
  s->trial_t++;
  int alive = 0;
  for (int i = 0; i < s->nen; i++) alive += s->en[i].alive && s->en[i].kind != EN_PUFF; /* (a puff is no guardian) */
  if (s->trial_t == 50) trial_wave(s);
  else if (s->trial_t > 60 && alive == 0) {
    if (s->trial_wave == 0) s->trial_wave = 1, s->trial_t = 20; /* (the second wave after a breath) */
    else {
      s->trials_done[s->trial_room >> 3] |= (uint8_t)(1 << (s->trial_room & 7));
      lock_doors(s, s->trial_room, false);
      ev(s, EV_TRIAL_DONE, 0, 0, s->p[0].x, s->p[0].y);
      s->trial_room = -1;
    }
  }
}

void sim_step(sim *s, const pinput in[MAXP]) {
  s->nev = 0;
  if (g_rules_off & RULE_HITSTOP) s->hitstop = 0;
  if (s->hitstop > 0) { /* the world holds still; presses made in the hold are kept for the tick after */
    s->hitstop--;
    for (int i = 0; i < s->players; i++) {
      s->p[i].ev = 0;
      s->held_jump[i] |= in[i].jump, s->held_attack[i] |= in[i].attack, s->held_dash[i] |= in[i].dash, s->held_shoot[i] |= in[i].shoot;
      s->held_grapple[i] |= in[i].grapple;
    }
    return;
  }
  s->tick++;
  s->pw.abilities = s->abilities;
  s->pw.charms = s->charms;
  for (int i = 0; i < s->players; i++) {
    player *p = &s->p[i];
    if (!p->active) continue;
    pinput q = in[i];
    q.jump |= s->held_jump[i], q.attack |= s->held_attack[i], q.dash |= s->held_dash[i], q.shoot |= s->held_shoot[i], q.grapple |= s->held_grapple[i];
    s->held_jump[i] = s->held_attack[i] = s->held_dash[i] = s->held_shoot[i] = s->held_grapple[i] = false;
    if (p->invuln > 0) p->invuln--;
    if (p->atk_cd > 0) p->atk_cd--;
    if (p->shot_cd > 0) p->shot_cd--;
    /* striking (not while hurt, plunging or on the line) */
    bool can_act = p->state == PS_NORMAL || p->state == PS_SWIM || p->state == PS_CLING;
    if (q.attack && p->atk_cd == 0 && can_act && !(q.my > 0 && q.dash)) {
      p->atk_t = 14, p->atk_cd = (s->charms & CHARM_BIT(CH_QUICK)) ? 13 : 19;
      p->atk_dir = (int16_t)(q.my < 0 ? 1 : (q.my > 0 && !p->on_ground) ? 2 : 0);
      ev(s, EV_STRIKE, i, p->atk_dir, p->x, p->y - 0.9f);
    }
    /* mending: down + spark, standing still, two energy: a leaf of health after a short channel */
    if (p->mend_t > 0) {
      memset(&q, 0, sizeof q);
      if (p->state == PS_HURT) p->mend_t = 0;
      else if (--p->mend_t == 0 && p->hp < p->hp_max) p->hp++, ev(s, EV_MEND, i, 1, p->x, p->y - 0.9f);
    } else if (q.shoot && q.my > 0 && p->on_ground && p->state == PS_NORMAL && p->en >= 2 && p->hp < p->hp_max && fabsf(p->vx) < 0.5f) {
      p->en = (int16_t)(p->en - 2), p->mend_t = (s->charms & CHARM_BIT(CH_SWIFTMEND)) ? 22 : 45;
      ev(s, EV_MEND, i, 0, p->x, p->y - 0.9f);
      q.shoot = false;
    }
    if (q.shoot && q.my > 0 && p->on_ground) q.shoot = false;
    if (!(s->abilities & AB_BOLT)) q.shoot = false; /* (the spark comes from the first shrine of light) */
    if (q.shoot && p->shot_cd == 0 && p->en > 0 && can_act) {
      p->en--, p->shot_cd = 16;
      bool up = q.my < 0;
      spawn_shot(s, SH_BOLT, false, p->x + (up ? 0 : p->facing_left ? -0.6f : 0.6f), p->y - (up ? 1.9f : 1.15f), up ? 0 : p->facing_left ? -24.0f : 24.0f, up ? -24.0f : 0, 0.45f, 0.3f, 34, 2);
      if (s->charms & CHARM_BIT(CH_TWINSPARK)) /* the twin spark: a second one, rising (or leaning, shot upward) */
        spawn_shot(s, SH_BOLT, false, p->x, p->y - 1.4f, up ? (p->facing_left ? -6.0f : 6.0f) : p->facing_left ? -22.0f : 22.0f, up ? -23.0f : -7.0f, 0.45f, 0.3f, 34, 2);
      ev(s, EV_BOLT, i, 0, p->x, p->y - 1.1f);
    }
    if (p->en < p->en_max && ++p->en_frac >= 300) p->en++, p->en_frac = 0;
    if (p->en >= p->en_max) p->en_frac = 0;
    int broke = player_step(p, &q, &s->pw);
    if (p->ev & PE_DASH)
      for (int k = 0; k < s->nen; k++) s->en[k].aux &= ~2; /* (a new dash: everything can be cut again) */
    if (p->state == PS_DASH && (s->charms & CHARM_BIT(CH_DASHSTRIKE))) /* the cutting dash */
      for (int k = 0; k < s->nen; k++) {
        enemy *e = &s->en[k];
        if (e->alive && enemy_exposed(e) && !(e->aux & 2) && e->kind != EN_PUFF &&
            overlap(p->x, p->y - PH * 0.5f, PW + 0.3f, PH * 0.5f, e->x, e->y - EDEF[e->kind].hh, EDEF[e->kind].hw, EDEF[e->kind].hh)) {
          e->aux |= 2;
          enemy_damage(s, e, 2, p->x, 7, i);
          s->hitstop = s->hitstop > 3 ? s->hitstop : 3;
        }
      }
    if (p->atk_t > 0) {
      p->atk_t--;
      if (p->atk_t == 11) strike(s, p, i);
    }
    if (broke >= 0) {
      flood(s, broke % TW, broke / TW, T_CRACKED, true);
      ev(s, EV_BREAK, 0, 0, p->x, p->y);
    }
    if (p->ev & PE_POUND_LAND) { /* the plunge's shock hits what stands near */
      bool quake = (s->charms & CHARM_BIT(CH_QUAKE)) != 0;
      for (int k = 0; k < s->nen; k++) {
        enemy *e = &s->en[k];
        if (e->alive && absf(e->x - p->x) < (quake ? 7.0f : 3.5f) && absf(e->y - p->y) < (quake ? 2.5f : 1.5f)) enemy_damage(s, e, quake ? 4 : 2, p->x, 12, i);
      }
      if (s->b.active && absf(s->b.x - p->x) < (quake ? 7 : 4) && absf(s->b.y - p->y) < 2) {
        s->b.hp = (int16_t)(s->b.hp - (quake ? 5 : 3)), s->b.flash = 6, s->hitstop = 5;
        if (s->b.hp <= 0) boss_down(s);
      }
    }
    if (p->ev & (PE_HAZARD | PE_COLD)) {
      if (!(s->assist > 1 && (++p->assist_hits % s->assist) != 0)) p->hp--;
      p->invuln = 50;
      ev(s, EV_HURT, i, 1, p->x, p->y);
    }
    /* your flame, where you fell: touch it and its measure comes back */
    if (s->echo && absf(s->echo_x - p->x) < 1.3f && absf(s->echo_y - 0.9f - (p->y - 0.9f)) < 1.8f) {
      s->echo = false;
      for (int j = 0; j < MAXP; j++) s->p[j].en_max++, s->p[j].en = s->p[j].en_max;
      ev(s, EV_ECHO, 1, 0, p->x, p->y - 0.9f);
    }
    /* pickups: those on screen are marked seen (the map shows them until taken), then the ones touched are taken */
    if (i == 0)
      for (int k = 0; k < s->w->nents; k++) {
        const ent *e = &s->w->ents[k];
        if (e->kind != EK_PICKUP || e->room != s->room || sim_pk_seen(s, e->id)) continue;
        int ex = (int)floorf(e->x), ey = (int)roundf(e->y);
        if (tile(s, ex, ey - 1) == T_SECRET) continue; /* (inside hidden rock) */
        if (sim_in_view(s->w, s->room, p->x, p->y, e->x, e->y - 0.6f)) s->pk_seen[e->id >> 3] |= (uint8_t)(1 << (e->id & 7));
      }
    /* pickups */
    for (int k = 0; k < s->w->nents; k++) {
      const ent *e = &s->w->ents[k];
      if (e->kind != EK_PICKUP || e->room != s->room || sim_got(s, e->id) || s->trial_room == e->room || sim_trial_room(s, e->room)) continue;
      if (absf(e->x - p->x) < 1.1f && absf(e->y - 0.8f - (p->y - 0.9f)) < 1.6f) {
        s->got[e->id >> 3] |= (uint8_t)(1 << (e->id & 7));
        if (e->sub == PK_ABILITY) s->abilities |= e->arg;
        for (int j = 0; j < MAXP; j++) {
          if (e->sub == PK_HEALTH) s->p[j].hp_max++, s->p[j].hp = s->p[j].hp_max;
          if (e->sub == PK_ENERGY) s->p[j].en_max++, s->p[j].en = s->p[j].en_max;
        }
        if (e->sub == PK_MAPSTONE && e->arg < 16) s->maps |= (uint16_t)(1u << e->arg);
        if (e->sub == PK_LORE && e->arg < NLORE) s->lore[e->arg >> 3] |= (uint8_t)(1 << (e->arg & 7));
        ev(s, EV_PICKUP, e->sub, e->arg, e->x, e->y);
        if (e->sub == PK_RELIC) { /* a relic: the next charm (a new move); past the last, a keepsake (a story line, a scarf dye) */
          int c = s->relics++;
          if (c < CH_KINDS && !(g_rules_off & RULE_CHARMS)) {
            s->charms |= CHARM_BIT(c);
            if (c == CH_SECONDWIND) for (int j = 0; j < MAXP; j++) s->p[j].second_wind = true;
            ev(s, EV_CHARM, c, 0, e->x, e->y);
          } else if (c >= CH_KINDS && (g_rules_off & RULE_KEEPSAKE)) /* (the control: the old rule, a leaf or a measure each) */
            for (int j = 0; j < MAXP; j++) {
              if (c & 1) s->p[j].en_max++, s->p[j].en = s->p[j].en_max;
              else s->p[j].hp_max++, s->p[j].hp = s->p[j].hp_max;
            }
          else if (c >= CH_KINDS)
            ev(s, EV_KEEPSAKE, c - CH_KINDS, (c - CH_KINDS + 1) % KEEP_PER_DYE == 0 ? sim_scarf_dye(s) : 0, e->x, e->y);
        }
      }
    }
    /* shrines: rest at one (up beside it): the save point, full health, its waystone lit */
    if (q.interact)
      for (int k = 0; k < s->w->nents; k++) {
        const ent *e = &s->w->ents[k];
        if (e->kind == EK_SAVE && absf(e->x - p->x) < 1.8f && absf(e->y - p->y) < 1.2f) {
          s->save_ent = k;
          s->saves_lit[e->id >> 3] |= (uint8_t)(1 << (e->id & 7));
          for (int j = 0; j < s->players; j++) s->p[j].hp = s->p[j].hp_max, s->p[j].en = s->p[j].en_max, s->p[j].second_wind = true;
          memset(s->slain, 0, sizeof s->slain); /* (a rest wakes the city's creatures again) */
          ev(s, EV_SAVE, e->id & 255, e->id >> 8, e->x, e->y);
        }
      }
  }
  /* the room the camera shows follows player 1; player 2 is brought along */
  int r = w_room_at(s->w, s->p[0].x, s->p[0].y - 0.9f);
  if (r >= 0 && r != s->room && !s->b.active && s->trial_room < 0) sim_enter_room(s, r);
  trial_update(s);
  for (int i = 1; i < s->players; i++) {
    player *p = &s->p[i];
    if (!p->active) continue;
    int pr = w_room_at(s->w, p->x, p->y - 0.9f);
    if (pr != s->room) {
      if (++p->drop_t > 50) player_spawn(p, s->p[0].x, s->p[0].y), p->hp = p->hp_max, p->active = 1, p->invuln = 60;
    }
  }
  /* a warden wakes when player 1 is well inside its arena */
  if (!s->b.active && s->room >= 0 && s->w->rooms[s->room].kind == RK_BOSS) {
    const room *rm = &s->w->rooms[s->room];
    float x0 = (float)(rm->x * CELL_W + 7), x1 = (float)((rm->x + rm->w) * CELL_W - 7);
    if (s->p[0].x > x0 && s->p[0].x < x1 && s->p[0].y > (float)(rm->y * CELL_H) + 5.5f) /* (clear of a door in the ceiling) */
      for (int i = 0; i < s->w->nents; i++)
        if (s->w->ents[i].kind == EK_BOSS && s->w->ents[i].room == s->room && !(s->bosses_down & (1 << s->w->ents[i].sub))) boss_start(s, s->room, i);
  }
  /* enemies, the warden, shots */
  for (int i = 0; i < s->nen; i++)
    if (s->en[i].alive) enemy_update(s, &s->en[i]);
  boss_update(s);
  for (int i = 0; i < MAXSHOTS; i++) {
    shot *h = &s->sh[i];
    if (!h->alive) continue;
    h->t++;
    if (h->kind == SH_SPORE && h->enemy && s->b.active && s->b.idx == 0) h->vy += 30 * DT; /* lobbed */
    h->x += h->vx * DT, h->y += h->vy * DT;
    if (--h->life <= 0) { h->alive = 0; continue; }
    bool walls = h->kind != SH_BEAM && h->kind != SH_PILLAR;
    if (walls && solid(s, h->x, h->y)) {
      h->alive = 0;
      ev(s, EV_SHOT_HIT, h->kind, 0, h->x, h->y);
      continue;
    }
    if (!h->enemy) {
      for (int k = 0; k < s->nen; k++) {
        enemy *e = &s->en[k];
        if (!e->alive || !enemy_exposed(e) || !overlap(h->x, h->y, h->w, h->h, e->x, e->y - EDEF[e->kind].hh, EDEF[e->kind].hw, EDEF[e->kind].hh)) continue;
        h->alive = 0;
        if (e->kind == EN_PUFF) { ev(s, EV_HIT, 2, 0, h->x, h->y); break; } /* (a spark glances off a puff) */
        enemy_damage(s, e, h->dmg, h->x - h->vx, 6, 0);
        s->hitstop = s->hitstop > 2 ? s->hitstop : 2;
        break;
      }
      if (h->alive && s->b.active && overlap(h->x, h->y, h->w, h->h, s->b.x, s->b.y - BDEF[s->b.idx].hh, BDEF[s->b.idx].hw, BDEF[s->b.idx].hh)) {
        s->b.hp = (int16_t)(s->b.hp - h->dmg), s->b.flash = 6, h->alive = 0;
        ev(s, EV_BOSS_HIT, s->b.idx, 1, h->x, h->y);
        if (s->b.hp <= 0) boss_down(s);
      }
      for (int ty = (int)floorf(h->y - h->h); h->alive && ty <= (int)floorf(h->y + h->h); ty++)
        if (tile(s, (int)floorf(h->x), ty) == T_SECRET) flood(s, (int)floorf(h->x), ty, T_SECRET, true), h->alive = 0, ev(s, EV_SECRET, 0, 0, h->x, h->y);
    } else {
      if (h->kind == SH_WAVE && !solid(s, h->x, h->y + 0.8f)) h->alive = 0; /* a ground wave runs out at a ledge */
      for (int k = 0; k < s->players; k++) {
        player *p = &s->p[k];
        if (p->active && overlap(h->x, h->y, h->w, h->h, p->x, p->y - PH * 0.5f, PW, PH * 0.5f)) {
          bool beam_wait = (h->kind == SH_BEAM && h->t < 6) || (h->kind == SH_PILLAR && h->t < 6);
          if (!beam_wait) hurt(s, p, h->dmg, h->x - h->vx);
          if (h->kind == SH_ORB || h->kind == SH_SPORE || h->kind == SH_FEATHER || h->kind == SH_SHARD) h->alive = 0;
        }
      }
    }
  }
  /* bodies */
  for (int k = 0; k < s->players; k++) {
    player *p = &s->p[k];
    if (!p->active) continue;
    for (int i = 0; i < s->nen; i++) {
      enemy *e = &s->en[i];
      if (!e->alive) continue;
      bool harmless = (e->kind == EN_FLYER && e->state == ES_REST) || !enemy_exposed(e); /* a moth flying home; a burrower under the floor */
      if (e->kind == EN_PUFF && e->state != ES_ACT) { /* a soft puff only bumps you away */
        harmless = true;
        if (overlap(p->x, p->y - PH * 0.5f, PW, PH * 0.5f, e->x, e->y - EDEF[e->kind].hh, EDEF[e->kind].hw * 0.9f, EDEF[e->kind].hh * 0.9f) && p->state == PS_NORMAL) {
          if (p->vy > 2 && p->y < e->y - EDEF[e->kind].hh) p->vy = -12, p->jump_rising = false, ev(s, EV_BOUNCE, k, 1, p->x, p->y); /* (onto it: a bounce) */
          else p->vx = p->x < e->x ? -4.0f : 4.0f, p->recoil_t = 6;                                                                  /* (a soft bump) */
        }
      }
      if (e->kind == EN_BURROWER && e->state == ES_ACT && e->t < 8 && overlap(p->x, p->y - PH * 0.5f, PW, PH * 0.5f, e->x, e->y - 1.2f, 0.8f, 1.2f)) hurt(s, p, 1, e->x); /* (the burst reaches up) */
      if (!harmless && overlap(p->x, p->y - PH * 0.5f, PW, PH * 0.5f, e->x, e->y - EDEF[e->kind].hh, EDEF[e->kind].hw * 0.85f, EDEF[e->kind].hh * 0.85f)) hurt(s, p, 1, e->x);
      float sx, sy, sw, shh;
      if (knight_spear(e, &sx, &sy, &sw, &shh) && overlap(p->x, p->y - PH * 0.5f, PW, PH * 0.5f, sx, sy, sw, shh)) hurt(s, p, 1, e->x);
    }
    if (s->b.active && overlap(p->x, p->y - PH * 0.5f, PW, PH * 0.5f, s->b.x, s->b.y - BDEF[s->b.idx].hh, BDEF[s->b.idx].hw * 0.85f, BDEF[s->b.idx].hh * 0.9f))
      hurt(s, p, BDEF[s->b.idx].dmg, s->b.x);
    if (p->hp <= 0) die_and_return(s, p);
  }
  s->hash = sim_hash(s);
}

static uint32_t mixf(uint32_t h, float v) {
  uint32_t u;
  memcpy(&u, &v, 4);
  return (h ^ u) * 16777619u;
}
uint32_t sim_hash(const sim *s) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < s->players; i++) {
    const player *p = &s->p[i];
    h = mixf(h, p->x), h = mixf(h, p->y), h = mixf(h, p->vx), h = mixf(h, p->vy), h = mixf(h, (float)p->hp), h = mixf(h, (float)p->en);
  }
  for (int i = 0; i < s->nen; i++) h = mixf(h, s->en[i].x), h = mixf(h, s->en[i].y), h = mixf(h, (float)s->en[i].hp), h = mixf(h, (float)s->en[i].alive);
  h = mixf(h, s->b.x), h = mixf(h, s->b.y), h = mixf(h, (float)s->b.hp), h = mixf(h, (float)s->abilities), h = mixf(h, (float)s->room);
  h = mixf(h, (float)s->tick), h = mixf(h, (float)s->bosses_down), h = mixf(h, (float)s->charms), h = mixf(h, (float)s->trial_room);
  return h;
}

void sim_save(const sim *s, save_state *o) {
  memset(o, 0, sizeof *o);
  o->world_hash = s->w->hash;
  o->abilities = s->abilities;
  o->hp_max = s->p[0].hp_max, o->en_max = s->p[0].en_max, o->save_ent = (int16_t)s->save_ent;
  o->relics = (int16_t)s->relics, o->deaths = (int16_t)s->deaths, o->bosses_down = s->bosses_down;
  memcpy(o->got, s->got, sizeof o->got);
  memcpy(o->seen, s->seen, sizeof o->seen);
  memcpy(o->lit, s->saves_lit, sizeof o->lit);
  o->broken_n = (uint16_t)nseeds;
  memcpy(o->broken, seeds, sizeof(uint32_t) * (size_t)nseeds);
  o->play_ticks = s->tick;
  o->charms = s->charms;
  o->maps = s->maps;
  memcpy(o->lore, s->lore, sizeof o->lore);
  memcpy(o->pk_seen, s->pk_seen, sizeof o->pk_seen);
  o->echo = s->echo, o->echo_x = s->echo_x, o->echo_y = s->echo_y;
  memcpy(o->trials_done, s->trials_done, sizeof o->trials_done);
}

void sim_restore(sim *s, const save_state *in) {
  if (in->world_hash != s->w->hash) return;
  s->abilities = in->abilities;
  for (int i = 0; i < MAXP; i++) s->p[i].hp_max = in->hp_max, s->p[i].en_max = in->en_max, s->p[i].hp = in->hp_max, s->p[i].en = in->en_max;
  s->save_ent = in->save_ent, s->relics = in->relics, s->deaths = in->deaths, s->bosses_down = (uint8_t)in->bosses_down;
  memcpy(s->got, in->got, sizeof in->got);
  memcpy(s->seen, in->seen, sizeof in->seen);
  memcpy(s->saves_lit, in->lit, sizeof in->lit);
  nseeds = 0;
  for (int i = 0; i < in->broken_n && i < 256; i++) {
    int idx = (int)(in->broken[i] & 0xffffff), kind = (int)(in->broken[i] >> 24);
    flood(s, idx % TW, idx / TW, kind, true);
  }
  s->tick = in->play_ticks;
  s->charms = in->charms;
  s->maps = in->maps;
  memcpy(s->lore, in->lore, sizeof s->lore);
  memcpy(s->pk_seen, in->pk_seen, sizeof in->pk_seen);
  s->echo = in->echo != 0, s->echo_x = in->echo_x, s->echo_y = in->echo_y;
  memcpy(s->trials_done, in->trials_done, sizeof in->trials_done);
  if (s->save_ent >= 0 && s->save_ent < s->w->nents) sim_teleport(s, s->w->ents[s->save_ent].x, s->w->ents[s->save_ent].y);
}
