// A pretend X4 for trying the web app (site/x4) in a browser without a device:
//   node tools/wasm/mock_x4.mjs [port]   then open http://localhost:<port>/?device
// It serves site/x4 and answers /api like the X4's hotspot: H (household: records kept and merged by key, newest
// wins), G, P, L/B/D/BX (badges), W (timetable), T and ?. Starts with a household of two Pets.
import { createServer } from 'node:http';
import { readFileSync } from 'node:fs';
import { extname, join } from 'node:path';

const PORT = +(process.argv[2] || 8044), ROOT = 'site/x4';
const PIP = 100, BISCUIT = 200;
const house = { id: 77, me: PIP, clock: 2, nextSeq: 1, records: new Map() };
const settings = { sleep: 10, invert: 0, time: 0 };
let pet = { look: '10000', name: 'Pip' }, badges = ['hello.bmp'], timetable = ['Mon,09:00,10:00,Maths,B12'];
let upload = null, body = null, exp = 0;
// The Pet's parts and what they cost, read from the firmware's own files.
const partsSrc = readFileSync('src/apps/pet/parts.h', 'utf8'), logicSrc = readFileSync('src/apps/pet/pet_logic.h', 'utf8');
const PARTS = ['BODIES', 'EYES', 'MOUTHS', 'HATS', 'EXTRAS'].map(sec => {
  const start = partsSrc.indexOf(`PetPart ${sec}[]`), end = partsSrc.indexOf('\n};', start);
  return [...partsSrc.slice(start, end).matchAll(/\{"([^"]+)", \{/g)].map(m => m[1]);
});
const LAYER = { BODY: 0, EYES_LAYER: 1, MOUTH: 2, HAT: 3, EXTRA: 4 };
const UNLOCKS = [...logicSrc.matchAll(/\{(BODY|EYES_LAYER|MOUTH|HAT|EXTRA), (\d+), (\d+), "/g)].map(m => ({ layer: LAYER[m[1]], part: +m[2], exp: +m[3] }));
const need = (layer, part) => (UNLOCKS.find(u => u.layer === layer && u.part === part) || { exp: 0 }).exp;

function memberHex(seq, origin, ver, text) {
  const b = Buffer.alloc(61);
  b[0] = 1; b.writeUInt16LE(seq, 2);
  [origin, ver, origin, ver, 0, 0, 0, 0].forEach((v, i) => b.writeUInt32LE(v, 4 + 4 * i));
  b[36] = text.length; b.write(text, 37);
  return b.toString('hex');
}
const key = hex => { const b = Buffer.from(hex, 'hex'); return b.readUInt32LE(4) + ':' + b.readUInt16LE(2); };
const verOf = hex => { const b = Buffer.from(hex, 'hex'); return [b.readUInt32LE(8), b.readUInt32LE(12)]; };
for (const h of [memberHex(1, PIP, 1, 'Pip'), memberHex(1, BISCUIT, 2, 'Biscuit')]) house.records.set(key(h), h);

function handle(line, out) {
  if (body) {  // W lines
    body.lines.push(line);
    if (body.lines.length === body.count) { timetable = body.lines; body = null; out.push('OK W 0'); }
    return;
  }
  if (line === '?') return out.push('unidex 1');
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
    // A chore this Pet did: EXP, as the X4 does (house/store.cpp).
    if (b[0] === 2 && b.readUInt32LE(28) === house.me && b.readUInt32LE(12) === house.me) {
      const before = old && Buffer.from(old, 'hex');
      if (!before || before.readUInt32LE(28) !== house.me || before.readUInt32LE(32) !== b.readUInt32LE(32))
        exp += before && b.readUInt32LE(32) <= before.readUInt32LE(24) ? 15 : 10;
    }
    if (!old) { house.records.set(k, hex); return out.push('OK H PUT 1'); }
    const [ov, oe] = verOf(old);
    if (v > ov || (v === ov && e > oe)) { house.records.set(k, hex); return out.push('OK H PUT 2'); }
    return out.push('OK H PUT 3');
  }
  if (line === 'H SAVE') return out.push('OK H SAVE');
  if (line === 'H START') { house.id = 78; return out.push('OK H START'); }
  if (line === 'H LEAVE') { house.id = 0; house.records.clear(); return out.push('OK H LEAVE'); }
  if (line === 'G') return out.push(`OK G sleep=${settings.sleep} invert=${settings.invert} time=${settings.time} version=mock`);
  if (line.startsWith('G SET ')) { const [, , k, v] = line.split(' '); settings[k] = +v; return out.push('OK G SET'); }
  if (line === 'P GET') return out.push(`OK P ${pet.look} ${Buffer.from(pet.name).toString('hex')}`);
  if (line === 'P PARTS') {
    PARTS.forEach((names, layer) => names.forEach((n, i) => out.push(`PP ${layer} ${i} ${need(layer, i)} ${Buffer.from(n).toString('hex')}`)));
    return out.push('OK P PARTS ' + exp);
  }
  if (line.startsWith('P SET ')) { const [, , look, name] = line.split(' '); pet = { look, name: Buffer.from(name || '', 'hex').toString() }; return out.push('OK P SET'); }
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

const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm' };
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
