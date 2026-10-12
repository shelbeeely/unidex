// The RoomiePet page: the styles, a try-on and the whole wardrobe, drawn from the same parts as the X4
// (x4/pet-art.js, generated from the firmware by tools/pet/export.py).
import { LAYERS, DRAW_ORDER, STYLES, DROPS, RARITIES } from './x4/pet-art.js';

const $ = id => document.getElementById(id);
const esc = s => String(s).replace(/[&<>"']/g, c => '&#' + c.charCodeAt(0) + ';');
const DEFAULT = [0, 0, 0, 0, 1, 0, 0];
const rarity = (layer, part) => (DROPS.find(d => d.layer === layer && d.part === part) || {}).rarity || 0;  // 0: a starter
const withStyle = (look, style) => look.map((p, i) => (style.parts[i] >= 0 ? style.parts[i] : p));

function draw(canvas, look, only) {
  const ctx = canvas.getContext('2d'), s = canvas.width / 32;
  ctx.fillStyle = '#fff';
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  for (const layer of DRAW_ORDER) {
    const part = LAYERS[layer].parts[look[layer]];
    if (!part) continue;
    part.rows.forEach((row, y) => [...row].forEach((c, x) => {
      if (c === '.') return;
      ctx.fillStyle = c === '#' ? '#111' : '#fff';
      ctx.fillRect(x * s, y * s, s, s);
    }));
  }
}

function canvas(look) {
  const c = document.createElement('canvas');
  c.width = c.height = 128;
  draw(c, look);
  return c;
}

// What a style puts on, in words: its named parts, gifts marked.
const describe = st => st.parts.map((p, i) => (p <= 0 ? null : LAYERS[i].parts[p].name + (rarity(i, p) ? '*' : '')))
  .filter(Boolean).join(', ');

// ---- Styles

for (const [i, st] of STYLES.entries()) {
  const b = document.createElement('button');
  b.className = 'pet';
  b.title = 'Try it on';
  b.appendChild(canvas(withStyle(DEFAULT, st)));
  b.insertAdjacentHTML('beforeend', `<b>${esc(st.name)}</b><small>${esc(describe(st))}</small>`);
  b.onclick = () => { look = withStyle(look, st); update(); $('tryPet').scrollIntoView({ behavior: 'smooth', block: 'center' }); };
  $('styles').appendChild(b);
  const chip = document.createElement('button');
  chip.className = 'secondary';
  chip.textContent = st.name;
  chip.onclick = () => { look = withStyle(look, STYLES[i]); update(); };
  $('tryStyles').appendChild(chip);
}

// ---- Try a look

let look = [...DEFAULT];
const pickers = LAYERS.map((layer, i) => {
  const label = document.createElement('label');
  label.textContent = layer.name;
  const sel = document.createElement('select');
  layer.parts.forEach((p, k) => sel.add(new Option(p.name + (rarity(i, k) ? ` (${RARITIES[rarity(i, k)]} gift)` : ''), k)));
  sel.onchange = () => { look[i] = +sel.value; update(); };
  label.appendChild(sel);
  $('pickers').appendChild(label);
  return sel;
});
function update() {
  draw($('tryPet'), look);
  pickers.forEach((sel, i) => (sel.value = look[i]));
}
$('random').onclick = () => { look = LAYERS.map(l => Math.floor(Math.random() * l.parts.length)); update(); };
$('reset').onclick = () => { look = [...DEFAULT]; update(); };
update();

// ---- The wardrobe: every part on the plain blob, by layer (the new layers first).

const ORDER = [[5, 'Hair'], [6, 'Outfits'], [3, 'Hats'], [4, 'Extras'], [1, 'Eyes'], [2, 'Mouths'], [0, 'Bodies']];
for (const [layer, title] of ORDER) {
  $('wardrobe').insertAdjacentHTML('beforeend', `<h3>${title} <span class="note">${LAYERS[layer].parts.length}</span></h3>`);
  const grid = document.createElement('div');
  grid.className = 'wardrobe';
  LAYERS[layer].parts.forEach((p, k) => {
    const l = [...DEFAULT];
    l[4] = 0;  // no blush, so the part stands out
    l[layer] = k;
    const cell = document.createElement('div');
    cell.className = 'pet';
    cell.appendChild(canvas(l));
    cell.insertAdjacentHTML('beforeend', `<b>${esc(p.name)}</b>${rarity(layer, k) ? `<span class="gift r${rarity(layer, k)}">${RARITIES[rarity(layer, k)]}</span>` : ''}`);
    grid.appendChild(cell);
  });
  $('wardrobe').appendChild(grid);
}
