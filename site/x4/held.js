// The catalog's things to hold and clothes to wear, in the browser, as src/apps/pet/items_logic.h draws them on the
// X4. An item: { id, name, kind, shape, top, rows, data (bytes) }.
//   kind   0 held in the hand, 1 a hat, 2 an accessory (at the neck), 3 hair, 4 an outfit
//   shape  0 an icon: 16x16, one bit a pixel (two bytes a row, the leftmost pixel in the top bit), placed by its kind
//          with a paper edge; 1 a piece: rows top.. of the Pet's own 32x32 grid, two bits a pixel (0 see-through,
//          1 ink, 2 paper), 8 bytes a row, worn in place of the wardrobe's part for its layer.
// Also: making items from pictures (an upload, a tile of Kenney's 1-Bit Pack) and from the piece editor.

export const SIDE = 16, MAX_ROWS = 27;
export const KINDS = ['Holding', 'Hat', 'Accessory', 'Hair', 'Outfit'];
export const HAND = 0, HAT = 1, ACCESSORY = 2, HAIR = 3, OUTFIT = 4, ICON = 0, PIECE = 1;
export const LAYER_OF = [-1, 3, 4, 5, 6];  // the wardrobe layer (pet-art.js) a kind is worn in place of
export const KIND_OF_LAYER = { 3: HAT, 4: ACCESSORY, 5: HAIR, 6: OUTFIT };
export const ICON_KINDS = [HAND, HAT, ACCESSORY];  // an icon goes in the hand, on the head or at the neck
export const bytesOf = hex => Uint8Array.from((hex || '').match(/../g) || [], h => parseInt(h, 16));
export const hexOf = bytes => [...bytes].map(b => b.toString(16).padStart(2, '0')).join('');

// An item from items.js, "I LIST" or the mock, with its picture as bytes.
export const itemOf = it => ({ ...it, data: typeof it.data === 'string' ? bytesOf(it.data) : it.data });

const iconInk = (it, x, y) => (it.data[y * 2 + (x >> 3)] >> (7 - (x & 7))) & 1;
export const pieceAt = (it, x, y) =>
  y < it.top || y >= it.top + it.rows ? 0 : (it.data[(y - it.top) * 8 + (x >> 2)] >> (6 - 2 * (x & 3))) & 3;

// Where an icon goes on the Pet's grid: in the hand, the lower right; a hat, centred with its lowest ink on the top of
// the head (row 9); an accessory, centred with its highest ink just under the chin (row 23).
function iconPlace(it) {
  let top = SIDE, bottom = -1;
  for (let y = 0; y < SIDE; y++) for (let x = 0; x < SIDE; x++) if (iconInk(it, x, y)) top = Math.min(top, y), bottom = y;
  if (bottom < 0) top = bottom = 0;
  return it.kind === HAND ? [16, 16] : [8, it.kind === HAT ? 9 - bottom : 23 - top];
}

// The item on a canvas holding a 32x32 Pet, `scale` canvas pixels a Pet pixel.
export function drawItem(ctx, it, scale) {
  const px = (x, y, ink) => { ctx.fillStyle = ink ? '#111' : '#fff'; ctx.fillRect(x * scale, y * scale, scale, scale); };
  if (it.shape === PIECE) {
    for (let y = it.top; y < it.top + it.rows && y < 32; y++) for (let x = 0; x < 32; x++) {
      const c = pieceAt(it, x, y);
      if (c) px(x, y, c === 1);
    }
    return;
  }
  const [X0, Y0] = iconPlace(it);
  for (let y = -1; y <= SIDE; y++) for (let x = -1; x <= SIDE; x++) {
    const X = X0 + x, Y = Y0 + y;
    if (X < 0 || Y < 0 || X >= 32 || Y >= 32) continue;
    const here = x >= 0 && y >= 0 && x < SIDE && y < SIDE && iconInk(it, x, y);
    let near = false;
    for (let dy = -1; dy <= 1 && !near; dy++) for (let dx = -1; dx <= 1 && !near; dx++) {
      const nx = x + dx, ny = y + dy;
      near = nx >= 0 && ny >= 0 && nx < SIDE && ny < SIDE && !!iconInk(it, nx, ny);
    }
    if (here || near) px(X, Y, here);
  }
}

// The item by itself, filling the canvas: an icon at 16x16, a piece on the Pet's 32x32 grid.
export function drawAlone(ctx, it, size) {
  ctx.fillStyle = '#fff', ctx.fillRect(0, 0, size, size);
  if (!it) return;
  if (it.shape === PIECE) {
    ctx.fillStyle = '#eee', ctx.fillRect(0, 0, size, size);  // see-through: grey
    return drawItem(ctx, it, size / 32);
  }
  const s = size / SIDE;
  ctx.fillStyle = '#111';
  for (let y = 0; y < SIDE; y++) for (let x = 0; x < SIDE; x++) if (iconInk(it, x, y)) ctx.fillRect(x * s, y * s, s, s);
}

// A whole Pet, as the X4 draws it: the wardrobe's parts (art: pet-art.js's LAYERS, order: DRAW_ORDER), a catalog
// piece of clothing in place of its layer's part, then what it holds. worn: ids [held, hat, accessory, hair, outfit];
// find(id): the item, or nothing for one this device doesn't have (the wardrobe's part shows instead).
export function drawPetWorn(ctx, art, order, parts, scale, worn = [], find = () => null) {
  ctx.fillStyle = '#fff', ctx.fillRect(0, 0, 32 * scale, 32 * scale);
  const over = {};
  for (let k = HAT; k <= OUTFIT; k++) {
    const it = worn[k] && find(worn[k]);
    if (it && it.kind === k) over[LAYER_OF[k]] = it;
  }
  for (const layer of order) {
    if (over[layer]) { drawItem(ctx, over[layer], scale); continue; }
    const part = art[layer].parts[parts[layer]] || art[layer].parts[0];
    part.rows.forEach((row, y) => [...row].forEach((c, x) => {
      if (c === '.') return;
      ctx.fillStyle = c === '#' ? '#111' : '#fff';
      ctx.fillRect(x * scale, y * scale, scale, scale);
    }));
  }
  const held = worn[HAND] && find(worn[HAND]);
  if (held && held.kind === HAND) drawItem(ctx, held, scale);
}

function pixelsOf(img, sx, sy, sw, sh, w = sw, h = sh) {
  const c = document.createElement('canvas');
  c.width = w, c.height = h;
  const g = c.getContext('2d');
  g.imageSmoothingEnabled = false;
  g.drawImage(img, sx, sy, sw, sh, 0, 0, w, h);
  return g.getImageData(0, 0, w, h).data;
}

// A 16x16 icon's picture from any image (or canvas): what isn't transparent or near white is ink, cropped to what's
// drawn and scaled to fit, keeping its shape. whiteIsInk: for Kenney's tiles, which are white on transparent.
export function bitsFromImage(img, { whiteIsInk = false, sx = 0, sy = 0, sw = img.width, sh = img.height } = {}) {
  const d = pixelsOf(img, sx, sy, sw, sh);
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

// The piece editor's grid (32x32 codes: 0 see-through, 1 ink, 2 paper) from a picture drawn at the Pet's size (any
// size is shrunk to 32x32): dark is ink, light is paper, transparent is see-through.
export function gridFromImage(img) {
  const d = pixelsOf(img, 0, 0, img.width, img.height, 32, 32), grid = new Uint8Array(32 * 32);
  for (let i = 0; i < 32 * 32; i++) grid[i] = d[i * 4 + 3] < 128 ? 0 : d[i * 4] + d[i * 4 + 1] + d[i * 4 + 2] < 384 ? 1 : 2;
  return grid;
}
// A wardrobe part (pet-art.js: 32 rows of '.', '#' ink and 'o' paper) as a grid, to start a piece from.
export const gridFromPart = part => Uint8Array.from(part.rows.join(''), c => (c === '#' ? 1 : c === 'o' ? 2 : 0));

// A grid as a piece: trimmed to the rows it uses. Null when nothing is drawn or it spans more than MAX_ROWS rows.
export function pieceFromGrid(grid) {
  let top = -1, bottom = -1;
  for (let y = 0; y < 32; y++) if (grid.subarray(y * 32, y * 32 + 32).some(c => c)) { if (top < 0) top = y; bottom = y; }
  if (top < 0 || bottom - top + 1 > MAX_ROWS) return null;
  const rows = bottom - top + 1, data = new Uint8Array(rows * 8);
  for (let y = 0; y < rows; y++) for (let x = 0; x < 32; x++) data[y * 8 + (x >> 2)] |= grid[(top + y) * 32 + x] << (6 - 2 * (x & 3));
  return { top, rows, data };
}
