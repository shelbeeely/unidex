// The RoomiePet page: example Pets, dress-up with pronouns and the pin, and the whole wardrobe, drawn from the same
// parts as the X4 (x4/pet-art.js, generated from the firmware by tools/pet/export.py; x4/pin.js draws pins and flags).
import { LAYERS, DRAW_ORDER, EXAMPLES, PRONOUNS, FLAGS, DROPS, RARITIES } from './x4/pet-art.js';
import { drawPin, drawFlag, drawFlagInk, ON_PIN } from './x4/pin.js';
import { drawPetWorn, itemOf, KIND_OF_LAYER, HAND } from './x4/held.js';
import { ITEMS as LIST } from './x4/items.js';

const $ = id => document.getElementById(id);
const esc = s => String(s).replace(/[&<>"']/g, c => '&#' + c.charCodeAt(0) + ';');
const DEFAULT = [0, 0, 0, 0, 1, 0, 0];
const ROWS = [6, 5, 3, 4, 0, 1, 2];  // as on the X4: clothes first
const rarity = (layer, part) => (DROPS.find(d => d.layer === layer && d.part === part) || {}).rarity || 0;  // 0: a starter

// The catalog the X4's firmware brings (catalog/items.txt): things to hold, and clothes worn instead of the wardrobe's.
const ITEMS = LIST.map(itemOf);
const find = id => ITEMS.find(i => i.id === id);
const idOf = name => (ITEMS.find(i => i.name === name) || {}).id || 0;

// worn: catalog ids [held, hat, accessory, hair, outfit] (x4/held.js).
function draw(canvas, look, pronouns = '', pin = false, flag = 0, worn = []) {
  const s = canvas.width / 32;
  drawPetWorn(canvas.getContext('2d'), LAYERS, DRAW_ORDER, look, s, worn, find);
  if (pin) drawPin(canvas.getContext('2d'), pronouns, s, flag);  // over what it holds, as on the X4
}

function canvas(look, pronouns, pin, flag, worn) {
  const c = document.createElement('canvas');
  c.width = c.height = 128;
  draw(c, look, pronouns, pin, flag, worn);
  return c;
}

// ---- Example Pets.

for (const ex of EXAMPLES) {
  const b = document.createElement('button');
  b.className = 'pet';
  b.title = 'Try it on';
  const flagByte = ex.flag | (ex.pin === 'flag' ? ON_PIN : 0);
  b.appendChild(canvas(ex.parts, ex.pronouns, !!ex.pin, flagByte, [idOf(ex.held)]));
  const names = ROWS.slice(0, 4).map(l => ex.parts[l] ? LAYERS[l].parts[ex.parts[l]].name : null).filter(Boolean);
  b.insertAdjacentHTML('beforeend', `<b>${esc(ex.pronouns)} · ${esc(FLAGS[ex.flag].name)}</b><small>${esc(names.join(', '))}</small>`);
  b.onclick = () => {
    look = [...ex.parts];
    pronouns = ex.pronouns;
    flag = ex.flag;
    worn = [idOf(ex.held), 0, 0, 0, 0];
    $('tryPin').value = ex.pin || 'off';
    update();
    $('tryPet').scrollIntoView({ behavior: 'smooth', block: 'center' });
  };
  $('examples').appendChild(b);
}

// ---- Dress up: a picker a layer, the pronouns and the pin.

let look = [...DEFAULT], pronouns = 'they/them', flag = 1, worn = [idOf('Broom'), 0, 0, 0, 0];
const pickers = ROWS.map(layer => {
  const label = document.createElement('label');
  label.textContent = LAYERS[layer].name;
  const sel = document.createElement('select'), k = KIND_OF_LAYER[layer];
  LAYERS[layer].parts.forEach((p, i) => sel.add(new Option(p.name + (rarity(layer, i) ? ` (${RARITIES[rarity(layer, i)]} gift)` : ''), i)));
  for (const it of ITEMS.filter(i => k !== undefined && i.kind === k)) sel.add(new Option(it.name + ' (catalog)', 'c' + it.id));
  sel.onchange = () => {
    if (sel.value[0] === 'c') worn[k] = +sel.value.slice(1);
    else look[layer] = +sel.value, k !== undefined && (worn[k] = 0);
    update();
  };
  label.appendChild(sel);
  $('pickers').appendChild(label);
  return [layer, sel];
});
for (const p of ['', ...PRONOUNS]) $('tryPronouns').add(new Option(p || 'None', p));
$('tryPronouns').onchange = () => { pronouns = $('tryPronouns').value; update(); };
FLAGS.forEach((f, i) => $('tryFlag').add(new Option(f.name, i)));
$('tryFlag').onchange = () => { flag = +$('tryFlag').value; update(); };
$('tryPin').onchange = update;
for (const it of [{ id: 0, name: 'Nothing' }, ...ITEMS.filter(i => i.kind === HAND)]) $('tryHeld').add(new Option(it.name, it.id));
$('tryHeld').onchange = () => { worn[HAND] = +$('tryHeld').value; update(); };
function update() {
  const pin = $('tryPin').value;
  draw($('tryPet'), look, pronouns, pin !== 'off', flag | (pin === 'flag' ? ON_PIN : 0), worn);
  $('tryHeld').value = worn[HAND];
  for (const [layer, sel] of pickers) sel.value = worn[KIND_OF_LAYER[layer]] ? 'c' + worn[KIND_OF_LAYER[layer]] : look[layer];
  $('tryPronouns').value = pronouns;
  $('tryFlag').value = flag;
}
$('random').onclick = () => { look = LAYERS.map(l => Math.floor(Math.random() * l.parts.length)); worn = [worn[HAND], 0, 0, 0, 0]; update(); };
$('reset').onclick = () => { look = [...DEFAULT]; worn = [0, 0, 0, 0, 0]; update(); };
update();

// ---- Pride flags: each in colour, and as the X4 draws it.

FLAGS.forEach((f, i) => {
  if (!i) return;
  const card = document.createElement('div');
  card.className = 'card';
  const colour = document.createElement('canvas'), ink = document.createElement('canvas');
  colour.width = 104, colour.height = 64, ink.width = 26 * 4, ink.height = 14 * 4;
  drawFlag(colour.getContext('2d'), i, 0, 0, 104, 64);
  drawFlagInk(ink.getContext('2d'), i, 0, 0, 26, 14, 4);
  card.append(colour, ink);
  card.insertAdjacentHTML('beforeend', `<b>${esc(f.name)}</b>`);
  $('flags').appendChild(card);
});

// ---- Things to hold and wear: the X4's catalog (catalog/items.txt), each held or worn by a plain Pet, by kind.

['Things to hold', 'Hats', 'Accessories', 'Hair', 'Outfits'].forEach((title, k) => {
  const list = ITEMS.filter(i => i.kind === k);
  if (!list.length) return;
  $('items').insertAdjacentHTML('beforeend', `<h3>${title} <span class="note">${list.length}</span></h3>`);
  const grid = document.createElement('div');
  grid.className = 'wardrobe';
  for (const it of list) {
    const cell = document.createElement('div'), w = [0, 0, 0, 0, 0], l = [...DEFAULT];
    w[k] = it.id;
    if (k) l[4] = 0;  // no blush under a necklace, so it stands out
    cell.className = 'pet';
    cell.appendChild(canvas(l, '', false, 0, w));
    cell.insertAdjacentHTML('beforeend', `<b>${esc(it.name)}</b>`);
    grid.appendChild(cell);
  }
  $('items').appendChild(grid);
});

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
