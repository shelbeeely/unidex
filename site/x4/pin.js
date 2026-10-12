// The Pet's pin, drawn as the X4 draws it: the pronoun pin (src/apps/pet/pronouns.h), a badge on its chest with the
// pronouns in 3x5 capitals at twice the Pet's resolution, so "THEY/THEM" fits; or the flag pin (flags.h). Pride
// flags are drawn here in colour (the X4 draws them in black-and-white textures).
import { PIN_CHARS, PIN_GLYPHS, FLAGS, FLAG_CHEVRON, FLAG_RING } from './pet-art.js';

export const ON_PIN = 0x80;  // in a flag byte: the pin shows the flag
export const flagIndex = b => ((b & 0x7f) < FLAGS.length ? b & 0x7f : 0);

// The colour at (x, y) of flag f drawn w x h, as flags.h's colourAt.
function colourAt(f, x, y, w, h) {
  const fl = FLAGS[f];
  if (fl.shape === 'intersex') {
    const dx = 2 * x + 1 - w, dy = 2 * y + 1 - h, d2 = dx * dx + dy * dy, r = Math.floor(h * 2 / 3), t = Math.max(1, Math.floor(h / 6));
    return d2 >= (r - t) ** 2 && d2 <= (r + t) ** 2 ? FLAG_RING : fl.colours[0];
  }
  if (fl.shape === 'progress') {
    const fromMiddle = Math.abs(2 * y + 1 - h), v = (h - fromMiddle) - 2 * x;
    if (v > 0) return FLAG_CHEVRON[Math.max(0, 4 - Math.floor(v * 5 / (h + 1)))];
  }
  return fl.colours[Math.min(fl.colours.length - 1, Math.floor(y * fl.colours.length / h))];
}

// A flag in colour, w x h canvas pixels at (x, y), drawn from a grid of gw x gh cells (flags.h's geometry).
export function drawFlag(ctx, f, x, y, w, h, gw = 52, gh = 32) {
  f = flagIndex(f);
  if (!f) return;
  const cw = w / gw, ch = h / gh;
  for (let gy = 0; gy < gh; gy++) for (let gx = 0; gx < gw; gx++) {
    ctx.fillStyle = colourAt(f, gx, gy, gw, gh);
    ctx.fillRect(x + gx * cw, y + gy * ch, Math.ceil(cw), Math.ceil(ch));
  }
  ctx.strokeStyle = '#111';
  ctx.lineWidth = Math.max(1, Math.round(w / 40));
  ctx.strokeRect(x + ctx.lineWidth / 2, y + ctx.lineWidth / 2, w - ctx.lineWidth, h - ctx.lineWidth);
}

// A flag as the X4 draws it, in black and white (flags.h's textures): w x h cells of `cell` pixels at (x, y).
const DARK = c => { const n = parseInt(c.slice(1), 16); return 255 - Math.floor(((n >> 16 & 255) * 299 + (n >> 8 & 255) * 587 + (n & 255) * 114) / 1000); };
const TEX_DARK = [0, 32, 64, 128, 176, 200, 255];
const textureOf = c => { const d = DARK(c); return TEX_DARK.reduce((b, t, i) => (Math.abs(d - t) < Math.abs(d - TEX_DARK[b]) ? i : b), 0); };
const textureInk = (t, x, y) => [false, (x & 1) === 0 && (y & 1) === 0 && ((x >> 1) + y) % 2 === 0, (x & 3) === 0,
  ((x + y) & 1) === 0, (x + y) % 4 !== 0, !((x & 1) === 0 && (y & 1) === 0), true][t];
function stripeTexture(f, i) {
  const cs = FLAGS[f].colours;
  let t = textureOf(cs[0]);
  for (let k = 1; k <= i; k++) {
    let next = textureOf(cs[k]);
    if (next === t && cs[k] !== cs[k - 1]) {
      const darker = DARK(cs[k]) >= DARK(cs[k - 1]);
      next = darker ? (next < 6 ? next + 1 : next - 1) : (next > 0 ? next - 1 : next + 1);
    }
    t = next;
  }
  return t;
}
export function drawFlagInk(ctx, f, x0, y0, w, h, cell) {
  f = flagIndex(f);
  if (!f) return;
  const fl = FLAGS[f], iw = w - 2, ih = h - 2;
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    let ink = x === 0 || y === 0 || x === w - 1 || y === h - 1;
    if (!ink) {
      const px = x - 1, py = y - 1, c = colourAt(f, px, py, iw, ih);
      const i = Math.min(fl.colours.length - 1, Math.floor(py * fl.colours.length / ih));
      if (fl.shape === 'intersex') ink = c === FLAG_RING;
      else if (c === fl.colours[i]) ink = textureInk(stripeTexture(f, i), px, py);
      else ink = textureInk(textureOf(c), px, py);
    }
    ctx.fillStyle = ink ? '#111' : '#fff';
    ctx.fillRect(x0 + x * cell, y0 + y * cell, cell, cell);
  }
}

// The flag as a small canvas (for beside a name).
export function flagIcon(f, w = 24, h = 15) {
  const c = document.createElement('canvas');
  c.width = w * 2, c.height = h * 2;
  c.style.width = w + 'px', c.style.height = h + 'px';
  c.className = 'flag';
  drawFlag(c.getContext('2d'), f, 0, 0, w * 2, h * 2, w * 2, h * 2);
  return c;
}

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

// Draws the Pet's pin on a canvas holding a 32x32 Pet (scale: canvas pixels per Pet pixel): the flag when the flag
// byte says the pin shows it, else the pronouns. Nothing without them.
export function drawPin(ctx, pronouns, scale, flag = 0) {
  if ((flag & ON_PIN) && flagIndex(flag)) {
    const half = scale / 2, w = 26, h = 14;  // on the 64x64 grid, as flags.h's drawPin
    drawFlag(ctx, flagIndex(flag), ((64 - w) >> 1) * half, 48 * half, w * half, h * half);
    return;
  }
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
