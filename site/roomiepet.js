// The RoomiePet page: example Pets, dress-up with pronouns and the pin, and the whole wardrobe, drawn from the same
// parts as the X4 (x4/pet-art.js, generated from the firmware by tools/pet/export.py; x4/pin.js draws the pin).
import { LAYERS, DRAW_ORDER, EXAMPLES, PRONOUNS, DROPS, RARITIES } from './x4/pet-art.js';
import { drawPin } from './x4/pin.js';

const $ = id => document.getElementById(id);
const esc = s => String(s).replace(/[&<>"']/g, c => '&#' + c.charCodeAt(0) + ';');
const DEFAULT = [0, 0, 0, 0, 1, 0, 0];
const ROWS = [6, 5, 3, 4, 0, 1, 2];  // as on the X4: clothes first
const rarity = (layer, part) => (DROPS.find(d => d.layer === layer && d.part === part) || {}).rarity || 0;  // 0: a starter

function draw(canvas, look, pronouns = '', pin = false) {
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
  if (pin) drawPin(ctx, pronouns, s);
}

function canvas(look, pronouns, pin) {
  const c = document.createElement('canvas');
  c.width = c.height = 128;
  draw(c, look, pronouns, pin);
  return c;
}

// ---- Example Pets.

for (const ex of EXAMPLES) {
  const b = document.createElement('button');
  b.className = 'pet';
  b.title = 'Try it on';
  b.appendChild(canvas(ex.parts, ex.pronouns, ex.pin));
  const names = ROWS.slice(0, 4).map(l => ex.parts[l] ? LAYERS[l].parts[ex.parts[l]].name : null).filter(Boolean);
  b.insertAdjacentHTML('beforeend', `<b>${esc(ex.pronouns)}</b><small>${esc(names.join(', '))}</small>`);
  b.onclick = () => {
    look = [...ex.parts];
    pronouns = ex.pronouns;
    $('tryPin').checked = ex.pin;
    update();
    $('tryPet').scrollIntoView({ behavior: 'smooth', block: 'center' });
  };
  $('examples').appendChild(b);
}

// ---- Dress up: a picker a layer, the pronouns and the pin.

let look = [...DEFAULT], pronouns = 'they/them';
const pickers = ROWS.map(layer => {
  const label = document.createElement('label');
  label.textContent = LAYERS[layer].name;
  const sel = document.createElement('select');
  LAYERS[layer].parts.forEach((p, i) => sel.add(new Option(p.name + (rarity(layer, i) ? ` (${RARITIES[rarity(layer, i)]} gift)` : ''), i)));
  sel.onchange = () => { look[layer] = +sel.value; update(); };
  label.appendChild(sel);
  $('pickers').appendChild(label);
  return [layer, sel];
});
for (const p of ['', ...PRONOUNS]) $('tryPronouns').add(new Option(p || 'None', p));
$('tryPronouns').onchange = () => { pronouns = $('tryPronouns').value; update(); };
$('tryPin').onchange = update;
function update() {
  draw($('tryPet'), look, pronouns, $('tryPin').checked);
  for (const [layer, sel] of pickers) sel.value = look[layer];
  $('tryPronouns').value = pronouns;
}
$('random').onclick = () => { look = LAYERS.map(l => Math.floor(Math.random() * l.parts.length)); update(); };
$('reset').onclick = () => { look = [...DEFAULT]; update(); };
update();

// ---- The wardrobe: every part on the plain blob, by layer.

const ORDER = [[6, 'Outfits'], [5, 'Hair'], [3, 'Hats'], [4, 'Accessories'], [1, 'Eyes'], [2, 'Mouths'], [0, 'Bodies']];
for (const [layer, title] of ORDER) {
  $('wardrobe').insertAdjacentHTML('beforeend', `<h3>${title} <span class="note">${LAYERS[layer].parts.length}</span></h3>`);
  const grid = document.createElement('div');
  grid.className = 'wardrobe';
  LAYERS[layer].parts.forEach((p, i) => {
    const l = [...DEFAULT];
    l[4] = 0;  // no blush, so the part stands out
    l[layer] = i;
    const cell = document.createElement('div');
    cell.className = 'pet';
    cell.appendChild(canvas(l));
    const r = rarity(layer, i);
    cell.insertAdjacentHTML('beforeend', `<b>${esc(p.name)}</b>${r ? `<span class="gift r${r}">${RARITIES[r]}</span>` : ''}`);
    grid.appendChild(cell);
  });
  $('wardrobe').appendChild(grid);
}
