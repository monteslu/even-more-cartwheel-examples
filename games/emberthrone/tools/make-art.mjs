// The launcher art (art/): background, title, cover, icon, logo and the screenshots, composed with ImageMagick from
// the game's own frames (3840x2160 from the cart through romdev, no HUD) that tools/capture-art.sh writes to out/art/.
// The name is set in DejaVu Serif Bold (the system's free font). Run after capture-art.sh:
//   node games/emberthrone/tools/make-art.mjs
import { execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, writeFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const G = join(dirname(fileURLToPath(import.meta.url)), '..');
const ROOT = join(G, '..', '..');
const SRC = join(ROOT, 'out', 'art'), ART = join(G, 'art');
mkdirSync(ART, { recursive: true });
const FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf';
const im = (...a) => execFileSync('convert', a.flat(), { stdio: 'inherit' });
const src = (n) => { const p = join(SRC, `${n}.png`); if (!existsSync(p)) throw new Error(`no ${p}: run tools/capture-art.sh`); return p; };
const TITLE = 'EMBER\nTHRONE';
// the logo: the name in two lines stacked (near 2:1, so it is large in a launcher's marquee row), thickened: the warm
// gold inside a darker gold rim and a wide dark outline, with a drop shadow, on transparency (the art check wants it
// 1280 wide or more; it must read at a launcher grid's size over any picture)
const word = (fill, stroke, offset) => ['-fill', fill, '-stroke', fill, '-strokewidth', String(stroke), '-annotate', offset, TITLE];
im('-size', '2600x1400', 'xc:none', '-font', FONT, '-pointsize', '430', '-gravity', 'center', '-interline-spacing', '-70',
  ...word('#160903', 34, '+16+22'), ...word('#3a1a08', 44, '+0+0'), ...word('#b8742a', 18, '+0+0'), ...word('#ffe2a0', 4, '+0+0'),
  '-trim', '+repage', '-bordercolor', 'none', '-border', '24', join(ART, 'logo.png'));

// the screenshots, as they are
const SHOTS = [
  ['meadow', 'The wayfarer on the road through the Green Hollow, the canyon stairs ahead'],
  ['fight', 'Locked on to a raider in the canyon, a cleaver coming round'],
  ['marsh', 'Wading the Tidemarsh toward the temple, stilt trees in the teal mist'],
  ['keep', 'The Ember Keep at sunset: the courtyard, the long walled ramp to the throne'],
  ['boss', 'The Tidecaller in her temple, a ring on the stone where the blow will land'],
];
SHOTS.forEach(([n], i) => im(src(n), '-resize', '1920x1080!', '-quality', '88', join(ART, `shot-${i + 1}.jpg`)));
// background: the widest view; title: the same, a little darker, with the logo over its sky
im(src('vista'), '-resize', '1920x1080!', '-quality', '88', join(ART, 'background.jpg'));
im(src('vista'), '-resize', '1920x1080!', '-brightness-contrast', '-12x-3',
  '(', join(ART, 'logo.png'), '-resize', '1100x', ')', '-gravity', 'north', '-geometry', '+0+70', '-composite', '-quality', '88', join(ART, 'title.jpg'));
// cover: portrait 2:3 from the hero's frame (the middle of the 4K frame, 1200x1800), the name across the top
im(src('hero'), '-gravity', 'center', '-crop', '1440x2160+0+0', '+repage', '-resize', '1200x1800!', '-quality', '90', join(ART, 'cover.jpg'));
execFileSync(join(ROOT, 'scripts', 'cover-name.sh'), [join(ART, 'cover.jpg'), join(ART, 'logo.png')], { stdio: 'inherit' });
// icon: the hero's head and shoulders, square
im(src('hero'), '-gravity', 'center', '-crop', '1040x1040-140-60', '+repage', '-resize', '512x512', join(ART, 'icon.png'));
writeFileSync(join(ART, 'art.json'), JSON.stringify({
  icon: 'icon.png', cover: 'cover.jpg', background: 'background.jpg', logo: 'logo.png', title: 'title.jpg',
  screenshots: SHOTS.map(([, label], i) => ({ path: `shot-${i + 1}.jpg`, label })),
}, null, 2) + '\n');
console.log('art written to', ART);
