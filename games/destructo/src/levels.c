/* destructo's levels: the famous real landmarks (landmarks.c builds each from its measured shape), by difficulty.
 * Every level is checked (tools/levelcheck.c): it stands still for a minute when left alone, nothing in it floats,
 * and the bot clears it within par. */
#include <string.h>

#include "cartwheel/math.h"
#include "game.h"

/* name, seed, targets, par, ammo {shot, heavy, charge}, range; which landmark, where, setting + 1, mark, orbit.
 * A landmark whose name or image is claimed goes by a nickname with its city (the shape is the real one) */
/* orbit: where the launcher (and the camera) starts round the landmark, radians from +z: the view that names it */
/* a landmark built but held out of the game until its gate is green (tests/destructo-landmarks.mjs: standing as built,
 * still in the settle, no overlaps); its row stays here so it comes back by changing one word */
#define LANDMARK_HELD(...) /* (no comma after a held row) */
/* (the seed keeps the value it had when one more landmark stood before the Tower in the list: the seed shapes each landmark's ground, trees and brick tints) */
#define LANDMARK_CAM(name, lm, targets, par, s, h, c, range, place, setting, mark, orbit, ...) { name, 1000 + lm + (lm >= LM_CN_TOWER), targets, par, { s, h, c }, range, lm, place, setting + 1, mark, orbit, { __VA_ARGS__ } }
#define LANDMARK(name, lm, targets, par, s, h, c, range, place, setting, mark, orbit) { name, 1000 + lm + (lm >= LM_CN_TOWER), targets, par, { s, h, c }, range, lm, place, setting + 1, mark, orbit }
const level_def LEVELS[NLEVELS] = {
  /* the real landmarks, by difficulty and world region */
  LANDMARK("Leaning Tower of Pisa", LM_PISA, 1, 2, 4, 1, 0, 28, "Pisa, Italy", ST_MEADOW, 0.01f, 0.0f),
  LANDMARK("Great Sphinx", LM_SPHINX, 1, 2, 4, 0, 0, 22, "Giza, Egypt", ST_DESERT, 0.03f, 0.3f),
  LANDMARK("The Needle", LM_SPACE_NEEDLE, 1, 2, 4, 1, 0, 28, "Seattle, United States", ST_CITY, 0.4f, 0.0f),
  LANDMARK("Iron Lady", LM_EIFFEL, 2, 2, 5, 1, 1, 32, "Paris, France", ST_CITY, 0.05f, 0.35f),
  LANDMARK("The Tower", LM_CN_TOWER, 1, 2, 5, 1, 1, 34, "Toronto, Canada", ST_HARBOUR, 0.01f, 0.0f),
  LANDMARK("The Great Clock", LM_BIG_BEN, 2, 2, 5, 1, 1, 30, "London, United Kingdom", ST_HARBOUR, 0.01f, 0.4f),
  LANDMARK("Statue of Liberty", LM_LIBERTY, 2, 8, 8, 2, 6, 32, "New York, United States", ST_HARBOUR, 0.09f, 0.3f),
  LANDMARK("Taj Mahal", LM_TAJ_MAHAL, 3, 3, 5, 2, 1, 34, "Agra, India", ST_MEADOW, 0.2f, 0.0f),
  LANDMARK("Harbour Sails", LM_OPERA_HOUSE, 2, 2, 5, 1, 1, 27, "Sydney, Australia", ST_HARBOUR, 0.07f, -0.15f),
  LANDMARK("The Empire", LM_EMPIRE_STATE, 2, 2, 5, 1, 1, 32, "New York, United States", ST_CITY, 0.17f, 0.45f),
  LANDMARK("Parthenon", LM_PARTHENON, 2, 3, 6, 2, 1, 32, "Athens, Greece", ST_HEADLAND, 0.3f, 0.5f),
  LANDMARK("Pyramids of Giza", LM_GIZA, 3, 3, 6, 2, 1, 38, "Giza, Egypt", ST_DESERT, 0.01f, 0.3f), /* the core of each pyramid is not counted (only its skin of bricks): an eighth of the skin */
  LANDMARK("Chichen Itza", LM_CHICHEN_ITZA, 2, 3, 6, 2, 1, 34, "Yucatan, Mexico", ST_MEADOW, 0.01f, 0.6f),
  LANDMARK("Angkor Wat", LM_ANGKOR_WAT, 3, 5, 6, 2, 3, 36, "Siem Reap, Cambodia", ST_MEADOW, 0.25f, 0.55f),
  LANDMARK("Moai of Ahu Tongariki", LM_MOAI, 3, 4, 6, 1, 0, 30, "Rapa Nui, Chile", ST_HEADLAND, 0.26f, -0.35f),
  LANDMARK("The Spire", LM_BURJ_KHALIFA, 2, 3, 6, 2, 2, 40, "Dubai, United Arab Emirates", ST_DESERT, 0.27f, 0.3f),
  LANDMARK("The Black Tower", LM_WILLIS, 2, 5, 5, 2, 1, 32, "Chicago, United States", ST_CITY, 0.35f, 0.5f),
  LANDMARK("Tower Bridge", LM_TOWER_BRIDGE, 2, 8, 6, 2, 3, 36, "London, United Kingdom", ST_HARBOUR, 0.3f, 0.25f),
  LANDMARK("Mount Rushmore", LM_RUSHMORE, 3, 4, 6, 2, 2, 24, "South Dakota, United States", ST_HIGHLAND, 0.1f, -0.4f),
  LANDMARK("The Kremlin", LM_KREMLIN, 2, 3, 6, 2, 2, 27, "Moscow, Russia", ST_SNOW, 0.04f, 0.25f),
  LANDMARK("Stacked Tower", LM_TAIPEI, 2, 3, 6, 2, 2, 36, "Taipei, Taiwan", ST_CITY, 0.3f, 0.3f),
  LANDMARK("Golden Span", LM_GOLDEN_GATE, 2, 3, 6, 2, 2, 46, "San Francisco, United States", ST_HARBOUR, 0.06f, 0.35f),
  LANDMARK("Brooklyn Bridge", LM_BROOKLYN, 3, 4, 6, 2, 2, 38, "New York, United States", ST_HARBOUR, 0.08f, 0.9f),
  LANDMARK("Crown Tower", LM_CHRYSLER, 2, 3, 6, 2, 1, 34, "New York, United States", ST_CITY, 0.3f, 0.4f),
  LANDMARK("The Gateway", LM_GATEWAY, 2, 3, 6, 2, 1, 33, "St. Louis, United States", ST_MEADOW, 0.002f, 0.2f),
  LANDMARK("Arc de Triomphe", LM_ARC, 2, 3, 6, 2, 1, 28, "Paris, France", ST_CITY, 0.07f, 0.3f),
  LANDMARK("Brandenburg Gate", LM_BRANDENBURG, 2, 3, 6, 2, 1, 27, "Berlin, Germany", ST_CITY, 0.3f, -0.5f),
  LANDMARK("Pantheon", LM_PANTHEON, 2, 3, 6, 2, 1, 30, "Rome, Italy", ST_CITY, 0.12f, 0.5f),
  LANDMARK("Notre-Dame", LM_NOTRE_DAME, 2, 3, 6, 2, 1, 32, "Paris, France", ST_CITY, 0.3f, -0.45f),
  LANDMARK("Red Tower", LM_TOKYO_TOWER, 1, 3, 6, 2, 1, 34, "Tokyo, Japan", ST_CITY, 0.3f, 0.3f),
  LANDMARK("The Atoms", LM_ATOMIUM, 2, 3, 6, 2, 1, 29, "Brussels, Belgium", ST_CITY, 0.002f, 0.3f),
  LANDMARK_HELD("Great Wall of China", LM_GREAT_WALL, 2, 3, 6, 2, 1, 34, "Beijing, China", ST_HIGHLAND, 0.3f, 0.2f)
  LANDMARK_HELD("Petra Treasury", LM_PETRA, 2, 3, 6, 2, 1, 26, "Petra, Jordan", ST_DESERT, 0.3f, 0.2f)
  LANDMARK("Hagia Sophia", LM_HAGIA_SOPHIA, 2, 3, 6, 2, 1, 34, "Istanbul, Turkey", ST_CITY, 0.12f, 0.5f),
  LANDMARK("Blue Mosque", LM_BLUE_MOSQUE, 1, 3, 6, 2, 1, 34, "Istanbul, Turkey", ST_CITY, 0.06f, 0.5f),
  LANDMARK_HELD("Mont Saint-Michel", LM_MONT_ST_MICHEL, 2, 3, 6, 2, 1, 21, "Normandy, France", ST_HEADLAND, 0.3f, 0.5f)
  LANDMARK("Japanese Castle", LM_JAPANESE_CASTLE, 2, 3, 6, 2, 1, 32, "Japan", ST_MEADOW, 0.03f, 0.4f),
  LANDMARK_HELD("Machu Picchu", LM_MACHU_PICCHU, 2, 3, 6, 2, 1, 26, "Cusco Region, Peru", ST_HIGHLAND, 0.3f, 0.4f)
  LANDMARK("Television Tower", LM_BERLIN_TV, 1, 2, 5, 1, 1, 34, "Berlin, Germany", ST_CITY, 0.3f, 0.3f),
  LANDMARK("Sail Hotel", LM_BURJ_ARAB, 2, 3, 6, 2, 1, 34, "Dubai, United Arab Emirates", ST_HEADLAND, 0.3f, 1.3f),
  LANDMARK_HELD("Neuschwanstein Castle", LM_NEUSCHWANSTEIN, 3, 3, 6, 2, 2, 34, "Bavaria, Germany", ST_HIGHLAND, 0.15f, 0.2f)
  LANDMARK("Colosseum", LM_COLOSSEUM, 2, 3, 6, 2, 1, 36, "Rome, Italy", ST_CITY, 0.3f, 0.4f),
  LANDMARK("Stonehenge", LM_STONEHENGE, 2, 3, 6, 2, 0, 28, "Wiltshire, United Kingdom", ST_MEADOW, 0.02f, 0.3f),
  LANDMARK("Saint Basil's Cathedral", LM_ST_BASILS, 3, 3, 6, 2, 1, 32, "Moscow, Russia", ST_SNOW, 0.3f, 0.4f),
  LANDMARK_HELD("Twin Spires", LM_PETRONAS, 2, 3, 6, 2, 2, 40, "Kuala Lumpur, Malaysia", ST_CITY, 0.35f, 0.25f)
  LANDMARK("Five-Storey Pagoda", LM_PAGODA, 1, 2, 5, 1, 0, 22, "Kyoto, Japan", ST_MEADOW, 0.35f, 0.4f),
  LANDMARK_CAM("Hoover Dam", LM_HOOVER, 2, 2, 6, 2, 2, 48, "Nevada, United States", ST_DESERT, 0.01f, 0.0f, -4.0f, 36.0f, 2.0f, -4.0f, 8.0f, 50.0f),
  LANDMARK_HELD("Abu Simbel", LM_ABU_SIMBEL, 2, 3, 6, 2, 1, 26, "Nubia, Egypt", ST_DESERT, 0.05f, 0.25f)
  LANDMARK_HELD("Sydney Harbour Bridge", LM_SYDNEY_BRIDGE, 2, 3, 6, 2, 1, 40, "Sydney, Australia", ST_HARBOUR, 0.05f, 0.5f)
  LANDMARK_HELD("Borobudur", LM_BOROBUDUR, 2, 3, 6, 2, 1, 34, "Java, Indonesia", ST_MEADOW, 0.02f, 0.5f)
  LANDMARK_HELD("Shwedagon Pagoda", LM_SHWEDAGON, 2, 3, 6, 2, 1, 28, "Yangon, Myanmar", ST_MEADOW, 0.1f, 0.3f)
  LANDMARK("Temple of Heaven", LM_TEMPLE_HEAVEN, 2, 3, 6, 2, 1, 28, "Beijing, China", ST_MEADOW, 0.1f, 0.3f),
  LANDMARK_HELD("Sagrada Familia", LM_SAGRADA, 2, 3, 6, 2, 1, 32, "Barcelona, Spain", ST_CITY, 0.1f, 0.3f)
};

int level_find(const char *name) {
  for (int i = 0; i < NLEVELS; i++) if (!strcmp(LEVELS[i].name, name)) return i;
  return -1;
}
int level_setting(const level_def *L) { return L->setting - 1; }
int level_settle_ticks(const level_def *L) { (void)L; return 330; }

const level_def *level_get(int i) {
  if (i < 0) i = 0;
  if (i >= NLEVELS) i = NLEVELS - 1;
  return &LEVELS[i];
}

void level_build(game *g, const level_def *L) {
  g->paint_now = P_NONE;
  landmark_build(g, L);
}
