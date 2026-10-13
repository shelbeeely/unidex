// The virtual household in the browser (RoomiePet): the scene house.wasm works out (world_logic.h's, as JSON from
// scene()), drawn as the X4 draws it (src/apps/house/world_draw.h) with the same art (home-art.js). Shared by the web
// app, the website's demo and the home server's page.
//
// The foundation's simple version: floor patterns, walls with door gaps, furniture (not yet turned), clutter, and the
// Pets standing on their tiles.
import { SPRITES, FURNITURE, CLUTTER, FLOOR, WALL } from './home-art.js';
import { drawPetWorn } from './held.js';

const bits = SPRITES.map(h => Uint8Array.from(h.match(/../g), x => parseInt(x, 16)));
const ink = (s, x, y) => (bits[s][y * 2 + (x >> 3)] >> (7 - (x & 7))) & 1;
const furniture = id => FURNITURE.find(f => f.id === id);
const clutterArt = kind => CLUTTER.find(c => c.kind === kind);
const isDoor = (r, tx, ty) => r.doors.some(([side, at]) =>
  (side === 0 && ty === 0 && tx === at) || (side === 2 && ty === r.h - 1 && tx === at) ||
  (side === 3 && tx === 0 && ty === at) || (side === 1 && tx === r.w - 1 && ty === at));

// One sprite at canvas pixel (x0, y0), `tile` canvas pixels a side; paper: draw its white too.
export function drawSprite(ctx, sprite, x0, y0, tile, paper = true) {
  const px = tile / 16;
  for (let y = 0; y < 16; y++) for (let x = 0; x < 16; x++) {
    const k = ink(sprite, x, y);
    if (!k && !paper) continue;
    ctx.fillStyle = k ? '#111' : '#fff';
    ctx.fillRect(x0 + x * px, y0 + y * px, Math.ceil(px), Math.ceil(px));
  }
}

// A room from the scene, its top left at canvas (x0, y0), `tile` canvas pixels a tile.
export function drawRoom(ctx, room, tile, x0 = 0, y0 = 0) {
  const floor = FLOOR[room.type] ?? FLOOR[FLOOR.length - 1];
  for (let ty = 0; ty < room.h; ty++) for (let tx = 0; tx < room.w; tx++) {
    const wall = (tx === 0 || ty === 0 || tx === room.w - 1 || ty === room.h - 1) && !isDoor(room, tx, ty);
    drawSprite(ctx, wall ? WALL : floor, x0 + tx * tile, y0 + ty * tile, tile);
  }
  for (const [id, x, y] of room.pieces) {
    const f = furniture(id);
    if (!f) continue;
    for (let dy = 0; dy < f.h; dy++) for (let dx = 0; dx < f.w; dx++)
      if (x + dx < room.w && y + dy < room.h) drawSprite(ctx, f.sprite + dy * f.w + dx, x0 + (x + dx) * tile, y0 + (y + dy) * tile, tile);
  }
  for (const [kind, x, y] of room.clutter || []) {
    const c = clutterArt(kind);
    if (c) drawSprite(ctx, c.sprite, x0 + x * tile, y0 + y * tile, tile, false);
  }
}

// Every room on one floor of the scene, the plan's grid at `tile` canvas pixels from (x0, y0).
export function drawHouse(ctx, scene, { floor = 0, tile = 16, x0 = 0, y0 = 0 } = {}) {
  scene.rooms.forEach(r => { if (r.floor === floor) drawRoom(ctx, r, tile, x0 + r.x * tile, y0 + r.y * tile); });
}

// The Pets at home, on their tiles: members[pet] = {parts, worn} (pet-art.js layers), art = {LAYERS, DRAW_ORDER},
// findItem(id) as held.js wants. Each Pet is two tiles tall, standing on its tile. Only rooms on `floor`.
export function drawScenePets(ctx, scene, members, art, findItem, { floor = 0, tile = 16, x0 = 0, y0 = 0 } = {}) {
  for (const p of scene.pets) {
    const r = scene.rooms[p.room], m = members[p.pet];
    if (!r || !m || r.floor !== floor) continue;
    const size = tile * 2, px = x0 + (r.x + p.x) * tile + tile / 2 - size / 2, py = y0 + (r.y + p.y + 1) * tile - size;
    const c = document.createElement('canvas');
    c.width = c.height = 32;
    drawPetWorn(c.getContext('2d'), art.LAYERS, art.DRAW_ORDER, m.parts, 1, m.worn || [], findItem);
    const g = c.getContext('2d').getImageData(0, 0, 32, 32).data;
    for (let y = 0; y < 32; y++) for (let x = 0; x < 32; x++) {
      const i = (y * 32 + x) * 4;
      if (g[i] > 128) continue;  // the Pet's paper stays see-through: only its ink over the room
      ctx.fillStyle = '#111';
      ctx.fillRect(px + x * size / 32, py + y * size / 32, Math.ceil(size / 32), Math.ceil(size / 32));
    }
  }
}
