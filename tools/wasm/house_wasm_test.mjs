// Checks house.wasm the way the web app uses it, from the repo root:
//   node tools/wasm/house_wasm_test.mjs [file]   (default site/x4/house.wasm)
import { readFileSync } from 'node:fs';

let failures = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'}  ${what}`); failures += !ok; };

const { instance } = await WebAssembly.instantiate(readFileSync(process.argv[2] || 'site/x4/house.wasm'));
const w = instance.exports;
const mem = () => new Uint8Array(w.memory.buffer);
const put = s => { const b = new TextEncoder().encode(s + '\0'); mem().set(b, w.io()); };
const get = n => new TextDecoder().decode(mem().subarray(w.io(), w.io() + n));
const state = (today = 20000) => JSON.parse(get(w.state(today, 2, 1)));

// A household as the X4 would dump it: Pip (this device) and Biscuit.
const PIP = 100, BISCUIT = 200, HOUSE = 77;
w.load_begin(HOUSE, PIP, 10, 1);
// Members made by hand: kind 1, flags 0, seq, origin, ver, editor, born, a..d, text.
function recordHex(kind, seq, origin, ver, text, a = 0) {
  const b = new Uint8Array(61), dv = new DataView(b.buffer);
  b[0] = kind; dv.setUint16(2, seq, true);
  [origin, ver, origin, ver, a, 0, 0, 0].forEach((v, i) => dv.setUint32(4 + 4 * i, v, true));
  const t = new TextEncoder().encode(text); b[36] = t.length; b.set(t, 37);
  return [...b].map(x => x.toString(16).padStart(2, '0')).join('');
}
put(recordHex(1, 1, PIP, 1, 'Pip')); check(w.load_record() === 1, 'a record loads');
put(recordHex(1, 1, BISCUIT, 2, 'Biscuit')); w.load_record();
put('nonsense'); check(w.load_record() === 0, 'a bad record is refused');
let s = state();
check(s.members.length === 2 && s.members[0].name === 'Pip' && s.me === PIP, 'members come out in joining order');

put('Take out trash'); check(w.add_chore(7, 20000) === 1, 'add a chore with any name');
put('Oat milk'); w.add_item();
put('Pizza night'); check(w.add_expense(2400) === 1, 'add an expense');
put('$pipcash'); check(w.set_paytag() === 1, 'set this Pet\'s cashtag');
put('Request $15 for foods https://cash.app/pay/link/mu3w9zc7'); check(w.add_request(0, 0) === 1, 'paste a request');
put('not a link'); check(w.add_request(0, 0) === 0, 'not a request link');
s = state();
const chore = s.records.find(r => r.kind === 2);
check(chore && chore.text === 'Take out trash' && chore.overdue === false, 'the chore is there, due today');
check(state(20003).records.find(r => r.kind === 2).overdue === true, 'and overdue three days later');
check(s.members[0].tag === 'pipcash', 'the cashtag shows on the member');
const req = s.records.find(r => r.kind === 9);
check(req && req.url === 'https://cash.app/pay/link/mu3w9zc7' && req.a === 1500, 'the request, with $15 read from the text');
check(s.debts.length === 1 && s.debts[0].from === BISCUIT && s.debts[0].to === PIP && s.debts[0].pence === 1200,
  'Biscuit owes Pip half the pizza');
check(w.chore_done(chore.origin, chore.seq, 20001) === 1 && state().records.find(r => r.kind === 2).c === PIP,
  'mark the chore done');

const n = w.changes(), lines = get(n).trim().split('\n');
check(lines.at(-1) === 'H SAVE' && lines.length === 1 + w.changed_count(), 'changes come out as H PUT lines and H SAVE');
check(lines.slice(0, -1).every(l => /^H PUT [0-9a-f]{122}$/.test(l)), 'each a whole record');

// The X4's side of it: those records loaded into a fresh household give the same state.
const before = state();
w.load_begin(HOUSE, PIP, 10, 1);
put(recordHex(1, 1, PIP, 1, 'Pip')); w.load_record();
put(recordHex(1, 1, BISCUIT, 2, 'Biscuit')); w.load_record();
for (const l of lines.slice(0, -1)) { put(l.slice(6)); w.load_record(); }
const after = state();
check(JSON.stringify(after.debts) === JSON.stringify(before.debts) && after.records.length === before.records.length,
  'the records round-trip');

// The virtual household (plan_logic.h, world_logic.h): the template, a room through PLAN SET hex, the scene.
check(w.plan_template() === 6, 'the template: four shared rooms and a bedroom each');
const hexLen = w.plan_room_hex(4), roomHex = get(hexLen);
check(hexLen === 348, 'a room as PLAN SET hex');
w.plan_clear(); put(roomHex);
check(w.plan_put() === 1 && w.plan_template() === 6, 'a room loads back');
put('00'.repeat(174)); check(w.plan_put() === 0, 'not a room: refused');
w.home_clear(); w.home_add(PIP); w.home_add(BISCUIT);
const sc = JSON.parse(get(w.scene(20000, 600, PIP, 0)));
check(sc.rooms.length === 6 && sc.rooms[4].owner === PIP && sc.rooms[4].type === 4 && sc.pets.length === 2 &&
  sc.health >= 0 && sc.health <= 100, 'the scene: rooms, Pets, health');
check(JSON.parse(get(w.scene(20000, 600, PIP, 1))).awayRoom === 4, 'away: this Pet\'s bedroom');

// Badges.
const img = new Uint8Array(w.memory.buffer, w.image(), 200 * 200 * 4);
for (let i = 0; i < 200 * 200; i++) { const v = (i % 200) * 255 / 199; img.set([v, v, v, 255], i * 4); }
const len = w.badge(0, 0, 0);
const bmp = new Uint8Array(w.memory.buffer, w.bmp(), len);
check(len === 5662 && bmp[0] === 0x42 && bmp[1] === 0x4d, 'a badge is a 5662-byte BMP');
let white = 0;
for (let i = 62; i < len; i++) for (let b = 0; b < 8; b++) white += (bmp[i] >> b) & 1;
check(white > 17000 && white < 23000, 'a grey ramp dithers to about half white');

console.log(failures ? `${failures} FAILED` : 'all passed');
process.exit(failures ? 1 : 0);
