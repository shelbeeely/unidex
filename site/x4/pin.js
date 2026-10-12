// The pronoun pin, drawn as the X4 draws it (src/apps/pet/pronouns.h): a badge on the Pet's chest with the pronouns
// in 3x5 capitals, at twice the Pet's resolution, so "THEY/THEM" fits.
import { PIN_CHARS, PIN_GLYPHS } from './pet-art.js';

const MAX = 12;
export function cleanPronouns(s) {
  let out = '';
  for (const ch of String(s).toLowerCase()) {
    if (out.length >= MAX) break;
    if (!PIN_CHARS.includes(ch) || (ch === ' ' && (!out || out.endsWith(' ')))) continue;
    out += ch;
  }
  return out.trimEnd();
}

// Draws the pin on a canvas holding a 32x32 Pet (scale: canvas pixels per Pet pixel). Nothing without pronouns.
export function drawPin(ctx, pronouns, scale) {
  const text = cleanPronouns(pronouns).toUpperCase();
  if (!text) return;
  const half = scale / 2, n = text.length, w = n * 4 - 1 + 6, h = 11, x0 = (64 - w) >> 1, y0 = 49;
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    if ((x === 0 || x === w - 1) && (y === 0 || y === h - 1)) continue;
    const edge = x === 0 || y === 0 || x === w - 1 || y === h - 1, tx = x - 3, ty = y - 3;
    let ink = edge;
    if (!edge && tx >= 0 && ty >= 0 && ty < 5 && tx < n * 4 - 1 && tx % 4 !== 3) {
      const g = PIN_GLYPHS[PIN_CHARS.indexOf(text[tx >> 2].toLowerCase())] || PIN_GLYPHS[28];
      ink = g[ty * 3 + tx % 4] === '#';
    }
    ctx.fillStyle = ink ? '#111' : '#fff';
    ctx.fillRect((x0 + x) * half, (y0 + y) * half, half, half);
  }
}
