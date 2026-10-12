// Things a Pet holds, in the browser (as src/apps/pet/items_logic.h draws them on the X4): 16x16 one-colour pictures,
// 32 bytes (two a row, the leftmost pixel in the top bit), drawn in the Pet's hand at its lower right with a paper edge.
// Also: turning a picture (an uploaded image, or a tile of Kenney's 1-Bit Pack) into one.

export const SIDE = 16;
export const bytesOf = hex => Uint8Array.from((hex || '').match(/../g) || [], h => parseInt(h, 16));
export const hexOf = bytes => [...bytes].map(b => b.toString(16).padStart(2, '0')).join('');
const inkAt = (bits, x, y) => (bits[y * 2 + (x >> 3)] >> (7 - (x & 7))) & 1;

// The item on a canvas holding a 32x32 Pet (scale: canvas pixels per Pet pixel), or alone (alone: fills the canvas).
export function drawHeld(ctx, bits, scale, alone = false) {
  if (!bits || bits.length !== 32) return;
  const X0 = alone ? 0 : 16, Y0 = alone ? 0 : 16;
  for (let y = -1; y <= SIDE; y++) for (let x = -1; x <= SIDE; x++) {
    const px = X0 + x, py = Y0 + y;
    if (px < 0 || py < 0 || px >= (alone ? SIDE : 32) || py >= (alone ? SIDE : 32)) continue;
    const here = x >= 0 && y >= 0 && x < SIDE && y < SIDE && inkAt(bits, x, y);
    let near = false;
    for (let dy = -1; dy <= 1 && !near; dy++) for (let dx = -1; dx <= 1 && !near; dx++) {
      const nx = x + dx, ny = y + dy;
      near = nx >= 0 && ny >= 0 && nx < SIDE && ny < SIDE && !!inkAt(bits, nx, ny);
    }
    if (!here && !near) continue;
    ctx.fillStyle = here ? '#111' : '#fff';
    ctx.fillRect(px * scale, py * scale, scale, scale);
  }
}

// A 16x16 picture from any image (or canvas): what isn't transparent or near white is ink, cropped to what's drawn
// and scaled to fit, keeping its shape. whiteIsInk: for Kenney's tiles, which are white on transparent.
export function bitsFromImage(img, { whiteIsInk = false, sx = 0, sy = 0, sw = img.width, sh = img.height } = {}) {
  const c = document.createElement('canvas');
  c.width = sw, c.height = sh;
  const g = c.getContext('2d');
  g.drawImage(img, sx, sy, sw, sh, 0, 0, sw, sh);
  const d = g.getImageData(0, 0, sw, sh).data;
  const isInk = (x, y) => { const i = (y * sw + x) * 4; return d[i + 3] > 127 && (whiteIsInk || d[i] + d[i + 1] + d[i + 2] < 600); };
  let x0 = sw, y0 = sh, x1 = -1, y1 = -1;
  for (let y = 0; y < sh; y++) for (let x = 0; x < sw; x++) if (isInk(x, y)) x0 = Math.min(x0, x), y0 = Math.min(y0, y), x1 = Math.max(x1, x), y1 = Math.max(y1, y);
  const bits = new Uint8Array(32);
  if (x1 < 0) return bits;
  if (sw === SIDE && sh === SIDE) x0 = y0 = 0, x1 = y1 = SIDE - 1;  // already the right size: as it is
  const w = x1 - x0 + 1, h = y1 - y0 + 1, k = Math.max(w, h) / SIDE, ox = (SIDE - w / k) / 2, oy = (SIDE - h / k) / 2;
  for (let y = 0; y < SIDE; y++) for (let x = 0; x < SIDE; x++) {
    // How much of this cell is ink, from the source pixels it covers: half or more is ink.
    const ax = x0 + (x - ox) * k, ay = y0 + (y - oy) * k;
    let inks = 0, all = 0;
    for (let yy = Math.floor(ay); yy < Math.ceil(ay + k); yy++) for (let xx = Math.floor(ax); xx < Math.ceil(ax + k); xx++) {
      if (xx < x0 || yy < y0 || xx > x1 || yy > y1) continue;
      all++, inks += isInk(xx, yy);
    }
    if (all && inks * 2 >= all) bits[y * 2 + (x >> 3)] |= 0x80 >> (x & 7);
  }
  return bits;
}
