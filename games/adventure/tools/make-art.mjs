// The launcher art (art/): background, title, cover, icon, logo and the screenshots, composed with ImageMagick from
// the game's own frames (1920x1080, no HUD) that tools/capture-art.sh writes to out/art/. The words are set in
// DejaVu Serif Bold (the system's free font). Run after capture-art.sh:
//   node games/adventure/tools/make-art.mjs
import { execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, writeFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const G = join(dirname(fileURLToPath(import.meta.url)), '..');
const SRC = join(G, '..', '..', 'out', 'art'), ART = join(G, 'art');
mkdirSync(ART, { recursive: true });
const FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf';
const im = (...a) => execFileSync('convert', a.flat(), { stdio: 'inherit' });
const src = (n) => { const p = join(SRC, `${n}.png`); if (!existsSync(p)) throw new Error(`no ${p}: run tools/capture-art.sh`); return p; };
const TITLE = 'ADVENTURE';
const words = (size, extra = []) => ['-font', FONT, '-pointsize', String(size), '-fill', '#ffe2a0', '-stroke', '#3a1a08', '-strokewidth', String(Math.round(size / 24)), ...extra];

// the screenshots, as they are
const SHOTS = [
  ['meadow', 'The wayfarer on the road through the Green Hollow, the canyon stairs ahead'],
  ['fight', 'Locked on to a raider in the canyon, a cleaver coming round'],
  ['marsh', 'Wading the Tidemarsh toward the temple, stilt trees in the teal mist'],
  ['keep', 'The Ember Keep at sunset: the courtyard, the long walled ramp to the throne'],
  ['boss', 'The Tidecaller in her temple, a ring on the stone where the blow will land'],
];
SHOTS.forEach(([n], i) => im(src(n), '-quality', '88', join(ART, `shot-${i + 1}.jpg`)));
// background: the widest view; title: the same with the name over it
im(src('vista'), '-quality', '88', join(ART, 'background.jpg'));
im(src('vista'), ...words(150), '-gravity', 'north', '-annotate', '+0+120', TITLE, '-quality', '88', join(ART, 'title.jpg'));
// cover: portrait 2:3 from the hero's frame, the name across the top
im(src('hero'), '-gravity', 'center', '-crop', '720x1080+0+0', '+repage', '-resize', '800x1200!', ...words(118), '-gravity', 'north', '-annotate', '+0+70', TITLE,
  '-quality', '90', join(ART, 'cover.jpg'));
// icon: the hero's head and shoulders, square
im(src('hero'), '-gravity', 'center', '-crop', '640x640+0-120', '+repage', '-resize', '512x512', join(ART, 'icon.png'));
// logo: the name on transparency
im('-size', '1400x300', 'xc:none', ...words(190), '-gravity', 'center', '-annotate', '+0+0', TITLE, '-trim', '+repage', join(ART, 'logo.png'));
writeFileSync(join(ART, 'art.json'), JSON.stringify({
  icon: 'icon.png', cover: 'cover.jpg', background: 'background.jpg', logo: 'logo.png', title: 'title.jpg',
  screenshots: SHOTS.map(([, label], i) => ({ path: `shot-${i + 1}.jpg`, label })),
}, null, 2) + '\n');
console.log('art written to', ART);
