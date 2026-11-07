// The game's colour grade (PostProcess grade, a .cube 3D LUT read after the tone curve): a gentle S-curve on each
// channel (the mid tones steeper, nothing clipped: deeper shade, brighter light) and a little more saturation.
//   node games/skate/tools/make-lut.mjs   (writes games/skate/assets/grade.cube)
import { writeFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const N = 17, S = 0.6, SAT = 1.1;
const curve = (c) => c + S * (c * c * (3 - 2 * c) - c);
const lines = ['TITLE "skate grade"', `LUT_3D_SIZE ${N}`, 'DOMAIN_MIN 0 0 0', 'DOMAIN_MAX 1 1 1'];
for (let b = 0; b < N; b++)
  for (let g = 0; g < N; g++)
    for (let r = 0; r < N; r++) {
      let c = [r, g, b].map((x) => curve(x / (N - 1)));
      const y = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
      c = c.map((x) => Math.min(1, Math.max(0, y + (x - y) * SAT)));
      lines.push(c.map((x) => x.toFixed(6)).join(' '));
    }
writeFileSync(join(dirname(fileURLToPath(import.meta.url)), '..', 'assets', 'grade.cube'), lines.join('\n') + '\n');
