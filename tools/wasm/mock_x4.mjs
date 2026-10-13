// A pretend X4 for trying the web app (site/x4) in a browser without a device:
//   node tools/wasm/mock_x4.mjs [port]   then open http://localhost:<port>/?device
// It serves site/x4 and answers /api like the X4's hotspot: H (household: records kept and merged by key, newest
// wins), G, P, L/B/D/BX (badges), W (timetable), T and ?. Starts with a household of two Pets.
import { createServer } from 'node:http';
import { crc32 } from 'node:zlib';
import { readFileSync } from 'node:fs';
import { extname, join } from 'node:path';

const PORT = +(process.argv[2] || 8044), ROOT = 'site/x4';
const PIP = 100, BISCUIT = 200;
const house = { id: 77, me: PIP, clock: 2, nextSeq: 1, records: new Map() };
const settings = { sleep: 10, invert: 0, time: 0, updates: 1 };
let pet = { look: '00010000', name: 'Pip' }, badges = ['hello.bmp'], timetable = ['Mon,09:00,10:00,Maths,B12'];
let upload = null, body = null, exp = 0, dry = 0;
const owned = new Set();  // gifts found, 'layer:part'
// The Pet's parts and which are gifts: the web app's own copy of the firmware's (tools/pet/export.py).
const { LAYERS: ART, DROPS } = await import(new URL('../../site/x4/pet-art.js', import.meta.url));
// Things to hold and wear: the firmware's (items.js, from catalog/items.txt) and those added over USB or the hotspot ("I ADD").
const { ITEMS } = await import(new URL('../../site/x4/items.js', import.meta.url));
const addedItems = [];
// As items_logic.h's idOf: the CRC-32 of the name, then kind, shape, top and rows, then the picture, kept to 24 bits.
const itemId = (name, kind, shape, top, rows, data) =>
  (crc32(data, crc32(Buffer.from([kind, shape, top, rows]), crc32(Buffer.from(name)))) & 0xffffff) || 1;
const allItems = () => [...ITEMS.map(i => ({ ...i, added: 0 })), ...addedItems];
const rarity = (layer, part) => (DROPS.find(d => d.layer === layer && d.part === part) || { rarity: 0 }).rarity;
const owns = (layer, part) => !rarity(layer, part) || owned.has(layer + ':' + part);
// A chore's gift, roughly as the X4 rolls it (pet_logic.h's rollDrop): a 50% chance here, sure on the 5th try.
function choreGift() {
  const left = DROPS.filter(d => !owned.has(d.layer + ':' + d.part));
  if (!left.length || (Math.random() >= 0.5 && ++dry < 5)) return;
  const d = left[Math.floor(Math.random() * left.length)];
  owned.add(d.layer + ':' + d.part);
  dry = 0;
}

// Pronouns in a member card's b and c, as pronouns.h packs them: 5 bits a character, 6 to a number.
const PR_CHARS = "abcdefghijklmnopqrstuvwxyz/- '";
const packPronouns = p => [...p].slice(0, 12).reduce((v, ch, i) => { v[Math.floor(i / 6)] += (PR_CHARS.indexOf(ch) + 1) * 32 ** (i % 6); return v; }, [0, 0]);
function recordHex(kind, seq, origin, ver, text, a = 0, b = 0, c = 0, d = 0) {
  const r = Buffer.alloc(61);
  r[0] = kind; r.writeUInt16LE(seq, 2);
  [origin, ver, origin, ver, a, b, c, d].forEach((v, i) => r.writeUInt32LE(v >>> 0, 4 + 4 * i));
  r[36] = text.length; r.write(text, 37);
  return r.toString('hex');
}
const memberHex = (seq, origin, ver, text, pronouns = '', look = 0x10000, flag = 0, held = 0) =>
  recordHex(1, seq, origin, ver, text, look, ...packPronouns(pronouns), flag + held * 256);
// The catalog clothes a member's Pet wears (house_logic.h's WEAR): hat, accessory, hair, outfit.
const wearHex = (origin, ver, w) => recordHex(10, 2, origin, ver, '', ...w);
const byName = n => ITEMS.find(i => i.name === n)?.id || 0;
const key = hex => { const b = Buffer.from(hex, 'hex'); return b.readUInt32LE(4) + ':' + b.readUInt16LE(2); };
const verOf = hex => { const b = Buffer.from(hex, 'hex'); return [b.readUInt32LE(8), b.readUInt32LE(12)]; };
for (const h of [memberHex(1, PIP, 1, 'Pip'), memberHex(1, BISCUIT, 2, 'Biscuit', 'they/them', 0x80010000, 0x83, ITEMS[0].id),
  wearHex(BISCUIT, 2, [byName('Crown'), 0, 0, byName('Heart jumper')])]) house.records.set(key(h), h);

function handle(line, out) {
  if (body) {  // W lines
    body.lines.push(line);
    if (body.lines.length === body.count) { timetable = body.lines; body = null; out.push('OK W 0'); }
    return;
  }
  if (line === '?') return out.push('roomiepet 1');
  if (line === 'H DUMP') {
    out.push(`HH ${house.id} ${house.me} ${house.clock} ${house.nextSeq} ${house.records.size}`);
    for (const h of house.records.values()) out.push('HR ' + h);
    return out.push('OK H DUMP');
  }
  if (line.startsWith('H PUT ')) {
    const hex = line.slice(6), k = key(hex), old = house.records.get(k), [v, e] = verOf(hex);
    house.clock = Math.max(house.clock, v) + 1;
    const b = Buffer.from(hex, 'hex');
    if (b.readUInt32LE(4) === house.me) house.nextSeq = Math.max(house.nextSeq, b.readUInt16LE(2));
    // A chore this Pet did: EXP for its level and maybe a gift, as the X4 does (house/store.cpp).
    if (b[0] === 2 && b.readUInt32LE(28) === house.me && b.readUInt32LE(12) === house.me) {
      const before = old && Buffer.from(old, 'hex');
      if (!before || before.readUInt32LE(28) !== house.me || before.readUInt32LE(32) !== b.readUInt32LE(32))
        exp += before && b.readUInt32LE(32) <= before.readUInt32LE(24) ? 15 : 10, choreGift();
    }
    if (!old) { house.records.set(k, hex); return out.push('OK H PUT 1'); }
    const [ov, oe] = verOf(old);
    if (v > ov || (v === ov && e > oe)) { house.records.set(k, hex); return out.push('OK H PUT 2'); }
    return out.push('OK H PUT 3');
  }
  if (line === 'H SAVE') return out.push('OK H SAVE');
  if (line === 'H START') { house.id = 78; return out.push('OK H START'); }
  if (line === 'H LEAVE') { house.id = 0; house.records.clear(); return out.push('OK H LEAVE'); }
  if (line === 'G') return out.push(`OK G sleep=${settings.sleep} invert=${settings.invert} time=${settings.time} version=mock updates=${settings.updates}`);
  if (line.startsWith('G SET ')) { const [, , k, v] = line.split(' '); settings[k] = +v; return out.push('OK G SET'); }
  if (line === 'P GET') return out.push(`OK P ${pet.look} ${Buffer.from(pet.name).toString('hex')} ${Buffer.from(pet.pronouns || '').toString('hex')} ${(pet.flag || 0).toString(16).padStart(2, '0')} ${(pet.worn || [0, 0, 0, 0, 0]).map(id => id.toString(16).padStart(6, '0')).join(' ')}`);
  if (line === 'P PARTS') {
    ART.forEach((l, layer) => l.parts.forEach((p, i) =>
      out.push(`PP ${layer} ${i} ${rarity(layer, i)} ${+owns(layer, i)} ${p.looks} ${Buffer.from(p.name).toString('hex')}`)));
    return out.push(`OK P PARTS ${exp} ${owned.size} ${DROPS.length}`);
  }
  if (line.startsWith('P SET ')) {
    const [, , look, name, pronouns, flag, ...worn] = line.split(' '), text = h => Buffer.from(!h || h === '-' ? '' : h, 'hex').toString();
    const was = pet.worn || [0, 0, 0, 0, 0];
    pet = { look, name: text(name), pronouns: pronouns === undefined ? pet.pronouns : text(pronouns),
      flag: flag === undefined ? pet.flag : parseInt(flag, 16) || 0,
      worn: was.map((w, k) => (worn[k] === undefined ? w : (allItems().find(i => i.id === parseInt(worn[k], 16))?.kind === k ? parseInt(worn[k], 16) : 0))) };
    // As the X4 does when the House next opens (houseUpdateMe): its member card takes the name, look and pronouns,
    // and its WEAR record the catalog clothes.
    const mine = memberHex(1, PIP, ++house.clock, pet.name || 'Pet', pet.pronouns, parseInt(look, 16) >>> 0, pet.flag || 0, pet.worn[0]);
    const wear = wearHex(PIP, house.clock, pet.worn.slice(1));
    house.records.set(key(mine), mine);
    house.records.set(key(wear), wear);
    return out.push('OK P SET');
  }
  if (line === 'I LIST') {
    const all = allItems();
    all.forEach(i => out.push(`IT ${i.id.toString(16).padStart(6, '0')} ${Buffer.from(i.name).toString('hex')} ${i.kind} ${i.shape} ${i.top} ${i.rows} ${i.data} ${i.added}`));
    return out.push(`OK I LIST ${all.length} 12345678`);
  }
  if (line.startsWith('I ADD ')) {
    const [, , nameHex, ...f] = line.split(' '), name = Buffer.from(nameHex || '', 'hex').toString();
    const [kind, shape, top, rows] = f.slice(0, 4).map(Number), data = f[4] || '';
    const icon = shape === 0, okShape = icon ? kind <= 2 : shape === 1 && kind >= 1 && kind <= 4 && rows >= 1 && rows <= 27 && top + rows <= 32;
    const t = icon ? 0 : top, r = icon ? 16 : rows;
    if (!name || name.length > 12 || !okShape || !new RegExp(`^[0-9a-f]{${icon ? 64 : rows * 16}}$`, 'i').test(data)) return out.push('ERR');
    const id = itemId(name, kind, shape, t, r, Buffer.from(data, 'hex'));
    if (!allItems().some(i => i.id === id)) addedItems.push({ id, name, kind, shape, top: t, rows: r, data: data.toLowerCase(), added: 1 });
    return out.push('OK I ADD ' + id.toString(16).padStart(6, '0'));
  }
  if (line.startsWith('I DEL ')) {
    const k = addedItems.findIndex(i => i.id === parseInt(line.slice(6), 16));
    if (k < 0) return out.push('ERR');
    addedItems.splice(k, 1);
    return out.push('OK I DEL');
  }
  if (line === 'L') { badges.forEach(b => out.push('F ' + b)); return out.push('OK L'); }
  if (line.startsWith('B ')) { const [, name, bytes] = line.split(' '); upload = { name, left: +bytes }; return out.push('OK B'); }
  if (line.startsWith('D ') && upload) {
    upload.left -= (line.length - 2) / 2; out.push('K');
    if (upload.left <= 0) { badges.push(upload.name); out.push('OK F ' + upload.name); upload = null; }
    return;
  }
  if (line.startsWith('BX ')) { badges = badges.filter(b => b !== line.slice(3)); return out.push('OK BX'); }
  if (line === 'W GET') { timetable.forEach(l => out.push('WL ' + l)); return out.push('OK W GET'); }
  if (line.startsWith('W ')) { body = { count: +line.split(' ')[1], lines: [] }; return; }
  if (line.startsWith('T ')) { settings.time = +line.slice(2); return out.push('OK T'); }
  out.push('ERR');
}

const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.png': 'image/png' };
createServer((req, res) => {
  const url = new URL(req.url, 'http://x');
  if (url.pathname === '/api') {
    const out = [];
    for (const l of (url.searchParams.get('c') || '').split('\n')) if (l) handle(l, out);
    res.writeHead(200, { 'Content-Type': 'text/plain' });
    return res.end(out.join('\n') + '\n');
  }
  const file = join(ROOT, url.pathname === '/' ? 'index.html' : url.pathname);
  try {
    const data = readFileSync(file);
    res.writeHead(200, { 'Content-Type': TYPES[extname(file)] || 'application/octet-stream' });
    res.end(data);
  } catch (e) { res.writeHead(404); res.end(); }
}).listen(PORT, () => console.log(`mock X4 on http://localhost:${PORT}/?device`));
