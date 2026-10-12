// RoomiePet manager: the web app for a RoomiePet (an Xteink X4). The same files run from the website (talking to the X4 over
// USB with Web Serial) and from the X4's own hotspot (House > Manage on phone, talking to its /api). Both send the
// X4's ordinary text commands (src/core/usbsync.cpp, src/apps/house/usb.h); only the transport differs.
//
// The household is edited with house.wasm: the X4's own rules (src/apps/house/house_logic.h) compiled to
// WebAssembly. Every change re-reads the household from the X4 first, makes the change as the X4 itself would, and
// sends back only the records that changed, which the X4 merges as at a Sync.

import { LAYERS as ART, DRAW_ORDER, BITS, PRONOUNS, FLAGS } from './pet-art.js';
import { drawPin, drawFlag, cleanPronouns, ON_PIN, flagIndex } from './pin.js';

const $ = id => document.getElementById(id);
const esc = s => String(s ?? '').replace(/[&<>"']/g, c => '&#' + c.charCodeAt(0) + ';');
const money = p => (p < 0 ? '-$' : '$') + (Math.abs(p) / 100).toFixed(2);
const enc = new TextEncoder(), dec = new TextDecoder();
const toHex = bytes => [...bytes].map(b => b.toString(16).padStart(2, '0')).join('');
const fromHex = h => dec.decode(new Uint8Array((h.match(/../g) || []).map(x => parseInt(x, 16))));

function toast(text, ms = 2600) {
  const t = $('toast');
  t.textContent = text;
  t.style.display = 'block';
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => (t.style.display = 'none'), ms);
}

// zlib crc32, as the X4's esp_rom_crc32_le.
const CRC = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();
function crc32(bytes, c = 0) {
  c = ~c >>> 0;
  for (const b of bytes) c = CRC[(c ^ b) & 255] ^ (c >>> 8);
  return ~c >>> 0;
}

// ---- Transports. run(lines) sends command lines and resolves with every reply line. A "?" goes last: its answer
// ("roomiepet 1") says the X4 has done everything before it. Batches stay small for the X4's line buffer and URL size.

const onHotspot = location.hostname === '192.168.4.1' || new URLSearchParams(location.search).has('device');

function batches(lines) {
  const out = [];
  let cur = [], size = 0;
  for (const l of lines) {
    if (cur.length && (cur.length >= 18 || size + l.length > 2400)) out.push(cur), (cur = []), (size = 0);
    cur.push(l);
    size += l.length + 1;
  }
  if (cur.length) out.push(cur);
  return out;
}

const httpTransport = {
  name: 'the X4\'s WiFi',
  async run(lines) {
    const replies = [];
    for (const b of batches(lines)) {
      const r = await fetch('/api?c=' + encodeURIComponent(b.join('\n')), { cache: 'no-store' });
      if (!r.ok) throw new Error('The X4 didn\'t answer (' + r.status + ')');
      replies.push(...(await r.text()).split('\n').filter(Boolean));
    }
    return replies;
  },
};

function serialTransport(port) {
  let reader, writer, buf = '', closed = false;
  const lines = [];
  const pump = async () => {
    try {
      for (;;) {
        const { value, done } = await reader.read();
        if (done) break;
        buf += dec.decode(value, { stream: true });
        let nl;
        while ((nl = buf.indexOf('\n')) >= 0) lines.push(buf.slice(0, nl).replace(/\r$/, '')), (buf = buf.slice(nl + 1));
      }
    } catch (e) { /* closed */ }
    closed = true;
  };
  const waitFor = async (pred, ms) => {
    const end = Date.now() + ms;
    for (;;) {
      const i = lines.findIndex(pred);
      if (i >= 0) return lines.splice(0, i + 1);
      if (closed) throw new Error('The USB connection closed');
      if (Date.now() > end) throw new Error('The X4 didn\'t answer. Is it awake?');
      await new Promise(r => setTimeout(r, 10));
    }
  };
  return {
    name: 'USB',
    async open() {
      await port.open({ baudRate: 115200 });
      await port.setSignals({ requestToSend: false });
      await port.setSignals({ dataTerminalReady: false });
      reader = port.readable.getReader();
      writer = port.writable.getWriter();
      pump();
    },
    async run(cmds) {
      const replies = [];
      for (const b of batches(cmds)) {
        lines.length = 0;
        await writer.write(enc.encode(b.join('\n') + '\n?\n'));
        const got = await waitFor(l => l === 'roomiepet 1' || l === 'unidex 1', 15000);
        if (got[got.length - 1] === 'unidex 1') throw new Error('That device runs unidex, not RoomiePet: this app only manages a RoomiePet');
        replies.push(...got.slice(0, -1));
      }
      return replies;
    },
  };
}

let device = null;
async function run(...cmds) {
  const replies = await device.run(cmds.flat());
  return replies;
}
const reply = (replies, prefix) => replies.find(l => l.startsWith(prefix));
function ok(replies, prefix) {
  if (replies.some(l => l === 'ERR' || l.startsWith('ERR '))) throw new Error(replies.find(l => l.startsWith('ERR')));
  if (prefix && !reply(replies, prefix)) throw new Error('No answer from the X4');
  return replies;
}

// ---- The household in WebAssembly

let wasm, S = null, info = { settings: {}, pet: {}, badges: [], timetable: [] };
async function loadWasm() {
  const r = await fetch('house.wasm');
  if (!r.ok) throw new Error('house.wasm didn\'t load');
  ({ instance: { exports: wasm } } = await WebAssembly.instantiate(await r.arrayBuffer(), {}));
}
const putText = s => {
  const b = enc.encode(String(s).slice(0, 4000) + '\0');
  new Uint8Array(wasm.memory.buffer).set(b, wasm.io());
};
const getText = n => dec.decode(new Uint8Array(wasm.memory.buffer, wasm.io(), n));

// Local days since 1970, and today's weekday and day of the month.
function today() {
  const d = new Date();
  return {
    day: Math.floor((Date.UTC(d.getFullYear(), d.getMonth(), d.getDate())) / 86400000),
    wday: d.getDay(), mday: d.getDate(),
  };
}

async function readHouse() {
  const r = ok(await run('H DUMP'), 'OK H DUMP');
  const [, id, me, clock, next] = reply(r, 'HH ').split(' ').map(Number);
  wasm.load_begin(id, me, clock, next);
  for (const l of r) if (l.startsWith('HR ')) putText(l.slice(3)), wasm.load_record();
  const t = today();
  S = JSON.parse(getText(wasm.state(t.day, t.wday, t.mday)));
}

// A change: read the household fresh, make it in WebAssembly as the X4 would, send what changed.
async function change(fn, done) {
  try {
    await readHouse();
    if (!fn()) return toast('That didn\'t work');
    const n = wasm.changes();
    const cmds = getText(n).trim().split('\n').filter(Boolean);
    ok(await run(cmds), 'OK H SAVE');
    wasm.sent();
    await readHouse();
    if (done) toast(done);
    render();
  } catch (e) {
    toast(e.message || String(e), 4000);
  }
}

const memberName = id => (S.members.find(m => m.id === id) || {}).name || '?';
const who = id => (id === S.me ? 'you' : memberName(id));
const recs = kind => S.records.filter(r => r.kind === kind);
const KIND = { CHORE: 2, ITEM: 3, EXPENSE: 4, SETTLE: 5, REMINDER: 6, MESSAGE: 7, PAYLINK: 9 };
const WEEKDAYS = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];
const ordinal = n => n + (n % 10 === 1 && n !== 11 ? 'st' : n % 10 === 2 && n !== 12 ? 'nd' : n % 10 === 3 && n !== 13 ? 'rd' : 'th');

// ---- Views

const TABS = [['home', 'Home'], ['pet', 'Pet'], ['chores', 'Chores'], ['shopping', 'Shopping'], ['money', 'Money'],
  ['reminders', 'Reminders'], ['messages', 'Messages'], ['roommates', 'Roommates'], ['badges', 'Badges'],
  ['timetable', 'Timetable'], ['settings', 'Settings']];
let tab = 'home';

function render() {
  $('tabs').innerHTML = TABS.map(([k, n]) => `<button data-tab="${k}" class="${k === tab ? 'on' : ''}">${n}</button>`).join('');
  const view = VIEWS[tab];
  $('main').innerHTML = S && !S.id && !['pet', 'badges', 'timetable', 'settings'].includes(tab) ? noHouse() : view();
  paintPets();
}

function noHouse() {
  return `<h2>No household yet</h2><p class="note">Start one here, or join a roommate's: on the X4, House, hold B
    to join, held near their X4.</p><button class="b p" data-act="start">Start a household</button>`;
}

const VIEWS = {
  pet: () => designer(),
  home() {
    const t = today().day, mine = recs(KIND.CHORE).filter(c => c.turn === S.me && c.b <= t);
    const late = recs(KIND.CHORE).filter(c => c.overdue);
    const debts = S.debts.filter(d => d.from === S.me || d.to === S.me);
    const reminders = recs(KIND.REMINDER).filter(r => r.today);
    return `<h2>Today</h2>
      ${reminders.map(r => `<div class="card"><b>${esc(r.text)}</b> today</div>`).join('')}
      <div class="card"><div class="title">Your chores</div>${mine.length ? mine.map(c =>
        `<div class="row"><span class="grow">${esc(c.text)} ${c.overdue ? '<span class="late">late</span>' : ''}</span>
        <button class="b" data-act="done" data-o="${c.origin}" data-s="${c.seq}">Done</button></div>`).join('') :
        '<p class="note good">Nothing due. Nice.</p>'}</div>
      <div class="card"><div class="title">House health</div><p class="sub">${late.length ? late.length +
        ' chore' + (late.length > 1 ? 's' : '') + ' late: ' + late.map(c => esc(c.text) + ' (' + esc(who(c.turn)) + ')').join(', ')
        : 'Everything is on time.'}</p></div>
      <div class="card"><div class="title">Money</div>${debts.length ? debts.map(d =>
        `<p class="sub">${d.from === S.me ? 'You owe ' + esc(memberName(d.to)) : esc(memberName(d.from)) + ' owes you'} ${money(d.pence)}</p>`).join('')
        : '<p class="sub">Everyone is even.</p>'}</div>
      <div class="card"><div class="title">Shopping</div><p class="sub">${recs(KIND.ITEM).filter(i => !i.a).map(i => esc(i.text)).join(', ') || 'Nothing to buy.'}</p></div>`;
  },
  chores() {
    const t = today().day, list = recs(KIND.CHORE).sort((a, b) => a.b - b.b);
    return `<h2>Chores</h2>
      ${list.map(c => {
        const due = c.b < t ? `<span class="late">${t - c.b}d late</span>` : c.b === t ? 'today' : `in ${c.b - t}d`;
        return `<div class="card"><div class="row"><div class="grow"><div class="title">${esc(c.text)}</div>
          <div class="sub">${esc(who(c.turn))}'s turn, ${due}, every ${c.a} day${c.a > 1 ? 's' : ''}${c.c ? ', last done by ' + esc(who(c.c)) : ''}</div></div>
          <button class="b p" data-act="done" data-o="${c.origin}" data-s="${c.seq}">Done</button>
          <button class="b" data-act="remove" data-o="${c.origin}" data-s="${c.seq}">Remove</button></div></div>`;
      }).join('') || '<p class="note">No chores yet.</p>'}
      <h3>Add a chore</h3><form class="add" data-form="chore">
        <label>Chore<input type="text" name="text" maxlength="24" required placeholder="Wipe counters"></label>
        <label>Every<select name="every"><option value="1">day</option><option value="2">2 days</option><option value="3">3 days</option>
          <option value="7" selected>week</option><option value="14">2 weeks</option><option value="30">month</option></select></label>
        <label>First due<input type="date" name="due" required value="${new Date().toISOString().slice(0, 10)}"></label>
        <button class="b p">Add</button></form>`;
  },
  shopping() {
    const items = recs(KIND.ITEM).sort((a, b) => a.a - b.a);
    return `<h2>Shopping</h2>
      ${items.map(i => `<div class="card"><div class="row"><span class="grow ${i.a ? 'sub' : 'title'}">${i.a ? '<s>' + esc(i.text) + '</s>' : esc(i.text)}</span>
        <button class="b ${i.a ? '' : 'p'}" data-act="toggle" data-o="${i.origin}" data-s="${i.seq}">${i.a ? 'Not got' : 'Got it'}</button>
        <button class="b" data-act="remove" data-o="${i.origin}" data-s="${i.seq}">Remove</button></div></div>`).join('') || '<p class="note">The list is empty.</p>'}
      <form class="add" data-form="item"><label>Add<input type="text" name="text" maxlength="24" required placeholder="Oat milk"></label><button class="b p">Add</button></form>
      ${items.some(i => i.a) ? '<button class="b" data-act="clearBought">Clear what\'s been got</button>' : ''}`;
  },
  money() {
    return `<h2>Money</h2>
      ${S.debts.map((d, i) => {
        const to = S.members.find(m => m.id === d.to) || {}, link = payLink(to, d.from === S.me ? d.pence : 0, d.from);
        return `<div class="card"><div class="row"><div class="grow"><div class="title">${esc(who(d.from))} ${d.from === S.me ? 'owe' : 'owes'} ${esc(who(d.to))} ${money(d.pence)}</div>
          <div class="sub">${link ? (link.request ? 'Cash App request' : '$' + esc(to.tag)) : esc(to.name) + ' has no $cashtag yet'}</div></div>
          ${link && d.from === S.me ? `<a class="b cash" href="${esc(link.url)}">Pay in Cash App</a>` : ''}
          ${link ? `<button class="b" data-act="share" data-i="${i}">Share</button>` : ''}
          <button class="b" data-act="settle" data-i="${i}">Settled</button></div></div>`;
      }).join('') || '<p class="note good">Everyone is even.</p>'}
      <h3>I paid for something</h3><p class="note">Shared by everyone who lived here at the time.</p>
      <form class="add" data-form="expense"><label>What for<input type="text" name="text" maxlength="24" required placeholder="Groceries"></label>
        <label>Amount<input class="n" name="amount" inputmode="decimal" required placeholder="24.00"></label><button class="b p">Add</button></form>
      <h3>Paste a Cash App request</h3><p class="note">In Cash App: Request, the amount, Share link, then copy it here (the link, or
        everything Cash App shared). Roommates' X4s show it as the code to pay you.</p>
      <form class="add" data-form="request"><label class="grow">Request<input type="text" name="text" required placeholder="https://cash.app/pay/link/..."></label>
        <label>Amount<input class="n" name="amount" inputmode="decimal" placeholder="optional"></label>
        <label>For<select name="for"><option value="0">Anyone</option>${S.members.filter(m => m.id !== S.me).map(m => `<option value="${m.id}">${esc(m.name)}</option>`).join('')}</select></label>
        <button class="b p">Save</button></form>
      ${recs(KIND.PAYLINK).map(r => `<div class="card"><div class="row"><span class="grow">${r.a ? money(r.a) : 'Request'} for ${r.c ? esc(memberName(r.c)) : 'anyone'}<div class="sub">${esc(r.url)}</div></span>
        <button class="b" data-act="remove" data-o="${r.origin}" data-s="${r.seq}">Remove</button></div></div>`).join('')}
      <h3>History</h3>${[...recs(KIND.EXPENSE), ...recs(KIND.SETTLE)].sort((a, b) => b.born - a.born).slice(0, 30).map(r =>
        `<div class="row sub"><span class="grow">${r.kind === KIND.EXPENSE ? esc(memberName(r.a)) + ' paid ' + money(r.b) + ' for ' + esc(r.text)
          : esc(memberName(r.a)) + ' paid ' + esc(memberName(r.c)) + ' ' + money(r.b)}</span>
          <button class="b" data-act="remove" data-o="${r.origin}" data-s="${r.seq}">Remove</button></div>`).join('') || '<p class="note">Nothing yet.</p>'}`;
  },
  reminders() {
    return `<h2>Reminders</h2>
      ${recs(KIND.REMINDER).map(r => `<div class="card"><div class="row"><span class="grow"><b>${esc(r.text)}</b>
        <span class="sub">every ${r.a ? ordinal(r.b) + ' of the month' : WEEKDAYS[r.b % 7]}${r.today ? ' (today)' : ''}</span></span>
        <button class="b" data-act="remove" data-o="${r.origin}" data-s="${r.seq}">Remove</button></div></div>`).join('') || '<p class="note">None yet.</p>'}
      <form class="add" data-form="reminder"><label>What<input type="text" name="text" maxlength="24" required placeholder="Bin day"></label>
        <label>When<select name="when">${WEEKDAYS.map((d, i) => `<option value="w${i}">Every ${d}</option>`).join('')}
          ${Array.from({ length: 28 }, (_, i) => `<option value="m${i + 1}">Every ${ordinal(i + 1)} of the month</option>`).join('')}</select></label>
        <button class="b p">Add</button></form>`;
  },
  messages() {
    return `<h2>Pet messages</h2><p class="note">Your Pet passes these on to your roommates' Pets at the next Sync.</p>
      ${recs(KIND.MESSAGE).sort((a, b) => b.born - a.born).map(r => `<div class="card"><div class="row"><span class="grow"><b>${esc(memberName(r.origin))}:</b> ${esc(r.text)}</span>
        <button class="b" data-act="remove" data-o="${r.origin}" data-s="${r.seq}">Remove</button></div></div>`).join('') || '<p class="note">No messages.</p>'}
      <form class="add" data-form="message"><label class="grow">Your Pet says<input type="text" name="text" maxlength="24" required placeholder="Pizza night?"></label><button class="b p">Send</button></form>`;
  },
  roommates() {
    const me = S.members.find(m => m.id === S.me) || {};
    return `<h2>Roommates</h2>
      ${S.members.map(m => {
        const link = m.id !== S.me && payLink(m, 0, S.me);
        return `<div class="card"><div class="row"><canvas class="mini" data-look="${m.look}" data-pronouns="${esc(m.pronouns || '')}" data-flag-byte="${m.flag || 0}" width="96" height="96"></canvas>
          <div class="grow"><div class="title">${withPronouns(m.name, m.pronouns, m.flag)}${m.id === S.me ? ' · you' : ''}</div>
          <div class="sub">${m.tag ? '$' + esc(m.tag) : 'No $cashtag'} · ${m.balance > 0 ? 'owed ' + money(m.balance) : m.balance < 0 ? 'owes ' + money(-m.balance) : 'even'}</div></div>
          ${link ? `<a class="b cash" href="${esc(link.url)}">Cash App</a><button class="b" data-act="shareMember" data-id="${m.id}">Share</button>` : ''}</div></div>`;
      }).join('')}
      <h3>Your $cashtag</h3><p class="note">So roommates can pay you. Only your own X4 can set it.</p>
      <form class="add" data-form="cashtag"><label>$cashtag<input type="text" name="tag" maxlength="21" value="${esc(me.tag || '')}" placeholder="yourname"></label>
        <button class="b p">Save</button></form>`;
  },
  badges() {
    return `<h2>Badges</h2><p class="note">Full-screen pictures for the Badge app (and the Pets send them to friends).
      Photos are turned into e-ink dots in your browser with WebAssembly.</p>
      <div class="badges">${info.badges.map(b => `<div class="card"><div class="title" style="word-break:break-all">${esc(b)}</div>
        <button class="b" data-act="badgeDelete" data-name="${esc(b)}">Delete</button></div>`).join('') || '<p class="note">No badges on the card.</p>'}</div>
      <h3>Add a badge</h3><form class="add" data-form="badge"><label>Picture<input type="file" name="file" accept="image/*" required></label>
        <label>Style<select name="style"><option value="0">Auto</option><option value="1">Line art</option><option value="2">Photo</option></select></label>
        <label>Light<input type="range" name="light" min="-80" max="80" value="0"></label>
        <label>Name<input type="text" name="name" maxlength="24" placeholder="my-badge"></label>
        <button class="b p">Send to the X4</button></form><canvas class="prev" id="prev" width="200" height="200"></canvas>`;
  },
  timetable() {
    const rows = info.timetable.length ? info.timetable : [];
    const days = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'];
    return `<h2>Timetable</h2><p class="note">Weekly classes or shifts on the X4's Timetable (timetable.csv on the card).</p>
      <table><thead><tr><th>Day</th><th>Start</th><th>End</th><th>What</th><th>Where</th><th></th></tr></thead><tbody id="tt">
      ${rows.map((r, i) => `<tr data-i="${i}"><td><select>${days.map(d => `<option${d === r[0] ? ' selected' : ''}>${d}</option>`).join('')}</select></td>
        <td><input type="time" value="${esc(r[1])}"></td><td><input type="time" value="${esc(r[2])}"></td>
        <td><input value="${esc(r[3])}" maxlength="40"></td><td><input value="${esc(r[4])}" maxlength="30"></td>
        <td><button class="b" data-act="ttRemove" data-i="${i}">✕</button></td></tr>`).join('')}</tbody></table>
      <p><button class="b" data-act="ttAdd">Add a row</button> <button class="b p" data-act="ttSave">Save to the X4</button></p>`;
  },
  settings() {
    const s = info.settings;
    return `<h2>Settings</h2>
      <div class="card"><div class="title">The X4</div>
        <form class="add" data-form="settings"><label>Sleep after<select name="sleep">${[10, 20, 30, 60].map(v =>
          `<option value="${v}"${+s.sleep === v ? ' selected' : ''}>${v} s</option>`).join('')}</select></label>
          <label>Screen<select name="invert"><option value="0">Black on white</option><option value="1"${s.invert === '1' ? ' selected' : ''}>White on black</option></select></label>
          <button class="b p">Save</button></form>
        <p class="sub">Clock: ${+s.time ? new Date(s.time * 1000).toLocaleString() : 'not set'}
          <button class="b" data-act="setTime">Set it from this ${onHotspot ? 'phone' : 'computer'}</button></p>
        <p class="sub">Firmware ${esc(s.version || '?')} · connected over ${device.name}</p></div>
      ${S && S.id ? `<div class="card"><div class="title">Household</div><p class="note">Leaving forgets the household on this X4 only; the others keep it.</p>
        <button class="b" data-act="leave">Leave the household</button></div>` : ''}`;
  },
};

// ---- The Pet designer (the Pet tab): the website's Tools page designer, for the X4. One row per layer with
// previous and next, a preview drawn from the same art as the X4 (pet-art.js, generated from the firmware's parts),
// a strip of every piece in the layer you're on, the name, the pronouns, a pride flag, the pin (pronouns or flag), and
// Wear it. Gifts not found yet (they drop after chores) are skipped, and shown locked in the strip.
const LAYERS = ART.map(l => l.name);
const ROWS = [6, 5, 3, 4, 0, 1, 2];  // as on the X4: clothes first (Outfit, Hair, Hat, Accessory, Body, Eyes, Mouth)
const SHIFT = BITS.map((_, i) => BITS.slice(0, i).reduce((a, b) => a + b, 0));
const PIN = 2 ** 31;  // the top bit of a saved look: it wears its pronoun pin
const lookParts = hex => { const v = parseInt(hex || '0', 16) || 0; return BITS.map((b, i) => Math.floor(v / 2 ** SHIFT[i]) % 2 ** b); };
const lookPin = hex => (parseInt(hex || '0', 16) || 0) >= PIN;
const lookHex = (parts, pin) => (parts.reduce((v, p, i) => v + p * 2 ** SHIFT[i], 0) + (pin ? PIN : 0)).toString(16).padStart(8, '0');
const RARITY = ['starter', 'uncommon', 'rare', 'legendary'];
const partInfo = (layer, index) => info.parts.find(x => x.layer === layer && x.index === index);
const owned = (layer, index) => { const p = partInfo(layer, index); return !p || p.owned; };
const offered = layer => ART[layer].parts.map((_, i) => i).filter(i => owned(layer, i));
// What's being designed: the parts, the pin, the name, the pronouns and the layer shown in the strip. Starts as what
// the X4 wears; Load puts that back.
const design = { parts: null, pin: false, name: '', pronouns: '', flag: 0, strip: 6 };  // flag: the flag byte
function loadDesign() {
  design.parts = lookParts(info.pet.look).map((p, i) => (p < ART[i].parts.length ? p : 0));
  design.pin = lookPin(info.pet.look);
  design.name = info.pet.name || '';
  design.pronouns = info.pet.pronouns || '';
  design.flag = info.pet.flag || 0;
}
const pinShows = () => (!design.pin ? 'off' : design.flag & ON_PIN ? 'flag' : 'pronouns');

function drawPet(canvas, parts, pin, pronouns, flag = 0) {
  const ctx = canvas.getContext('2d'), s = canvas.width / 32;
  ctx.fillStyle = '#fff';
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  for (const layer of DRAW_ORDER) {
    const part = ART[layer].parts[parts[layer]] || ART[layer].parts[0];
    part.rows.forEach((row, y) => [...row].forEach((c, x) => {
      if (c === '.') return;
      ctx.fillStyle = c === '#' ? '#111' : '#fff';
      ctx.fillRect(x * s, y * s, s, s);
    }));
  }
  if (pin) drawPin(ctx, pronouns, s, flag);
}
// Name with the pride flag before it and pronouns after it, as the X4 shows them: "[flag] Pip (she/her)".
const withPronouns = (name, pronouns, flag) => (flagIndex(flag) ? `<canvas class="flag" data-flag="${flag}" width="48" height="30"></canvas> ` : '') +
  esc(name) + (pronouns ? ` <span class="pr">(${esc(pronouns)})</span>` : '');

function designer() {
  if (!design.parts) loadDesign();
  const d = design, level = 1 + Math.floor(info.exp / 50);
  const rows = ROWS.map(layer => {
    const list = offered(layer), at = list.indexOf(d.parts[layer]), p = ART[layer].parts[d.parts[layer]];
    const r = partInfo(layer, d.parts[layer])?.rarity;
    return `<div class="petRow${layer === d.strip ? ' on' : ''}"><button class="b lab" data-act="pStrip" data-l="${layer}">${LAYERS[layer]}</button>
      <button class="b" data-act="pStep" data-l="${layer}" data-s="-1" aria-label="Previous ${LAYERS[layer]}">◀</button>
      <span>${esc(p.name)}${r ? ` <i class="rar r${r}">${RARITY[r]}</i>` : ''}<small>${at + 1}/${list.length}</small></span>
      <button class="b" data-act="pStep" data-l="${layer}" data-s="1" aria-label="Next ${LAYERS[layer]}">▶</button></div>`;
  }).join('');
  const strip = ART[d.strip].parts.map((p, i) => {
    const r = partInfo(d.strip, i)?.rarity, has = owned(d.strip, i);
    return `<button class="tile${i === d.parts[d.strip] ? ' on' : ''}${has ? '' : ' locked'}" data-act="pPick" data-l="${d.strip}" data-i="${i}"
      ${has ? '' : 'disabled'} title="${esc(p.name)}${r ? ' (' + RARITY[r] + (has ? '' : ', not found yet') + ')' : ''}"><canvas data-tile="${i}" width="64" height="64"></canvas>
      <span>${esc(p.name)}</span>${r ? `<i class="rar r${r}">${has ? RARITY[r] : 'not found yet'}</i>` : ''}</button>`;
  }).join('');
  const preset = PRONOUNS.includes(d.pronouns) || !d.pronouns;
  return `<h2>Your Pet</h2>
    <p class="note">Dress it up and send it to the X4. Level ${level} · ${info.gifts} of ${info.giftsAll} gifts
      found${info.gifts < info.giftsAll ? ': more drop after chores, like loot' : ''}.</p>
    <div class="designer">
      <div class="stage"><div class="frame"><canvas id="petPreview" width="256" height="256" role="img" aria-label="Pet preview"></canvas></div>
        <label class="nm">Name<input id="petName" type="text" maxlength="12" placeholder="No name" value="${esc(d.name)}" autocomplete="off"></label>
        <label class="nm">Pronouns<select id="petPronouns"><option value="">None</option>${PRONOUNS.map(p =>
          `<option${p === d.pronouns ? ' selected' : ''}>${esc(p)}</option>`).join('')}<option value="*"${preset ? '' : ' selected'}>Other...</option></select></label>
        <input id="petPronounsOther" type="text" maxlength="12" placeholder="Your pronouns" value="${preset ? '' : esc(d.pronouns)}"
          autocomplete="off" ${preset ? 'hidden' : ''} aria-label="Your pronouns">
        <label class="nm">Pride flag<select id="petFlag">${FLAGS.map((f, i) => `<option value="${i}"${i === flagIndex(d.flag) ? ' selected' : ''}>${esc(f.name)}</option>`).join('')}</select></label>
        <label class="nm">Pin<select id="petPin">${[['off', 'No pin'], ['pronouns', 'Pronoun pin'], ['flag', 'Flag pin']].map(([v, n]) =>
          `<option value="${v}"${v === pinShows() ? ' selected' : ''}>${n}</option>`).join('')}</select></label>
        <button class="b p" data-act="pWear">Wear it on the X4</button>
        <button class="b" data-act="pRandom">Random look</button>
        <button class="b" data-act="pLoad">Load from the X4</button></div>
      <div class="rows">${rows}</div>
    </div>
    <p class="note">Your pronouns and flag show beside your Pet's name on the X4, to Pets you meet and in their friends
      lists, and on your household's Roommates; a pin shows one of them on your Pet too. The X4 draws flags in black and
      white, each colour a texture.</p>
    <h3>${LAYERS[d.strip]}</h3>
    <div class="strip">${strip}</div>`;
}

// Every Pet on the page: the designer's preview and strip, and the roommates' Pets (canvas[data-look]).
function paintPets() {
  const c = $('petPreview');
  if (c) drawPet(c, design.parts, design.pin, design.pronouns, design.flag);
  document.querySelectorAll('canvas[data-tile]').forEach(t => {
    const parts = [...design.parts];
    parts[design.strip] = +t.dataset.tile;
    drawPet(t, parts, false, '');
  });
  document.querySelectorAll('canvas[data-look]').forEach(t => {
    const hex = (+t.dataset.look >>> 0).toString(16);
    drawPet(t, lookParts(hex), lookPin(hex), t.dataset.pronouns || '', +t.dataset.flagByte || 0);
  });
  document.querySelectorAll('canvas[data-flag]').forEach(t => drawFlag(t.getContext('2d'), +t.dataset.flag, 0, 0, t.width, t.height));
}
document.addEventListener('input', e => {
  if (e.target.id === 'petName') design.name = e.target.value;
  if (e.target.id === 'petPronounsOther') { design.pronouns = cleanPronouns(e.target.value); paintPets(); }
});
document.addEventListener('change', e => {
  if (e.target.id === 'petPin') {
    design.pin = e.target.value !== 'off';
    design.flag = (design.flag & 0x7f) | (e.target.value === 'flag' ? ON_PIN : 0);
    paintPets();
  }
  if (e.target.id === 'petFlag') { design.flag = +e.target.value | (design.flag & ON_PIN); paintPets(); }
  if (e.target.id === 'petPronouns') {
    const other = e.target.value === '*';
    $('petPronounsOther').hidden = !other;
    design.pronouns = other ? cleanPronouns($('petPronounsOther').value) : e.target.value;
    if (other) $('petPronounsOther').focus();
    paintPets();
  }
});

const PET_ACTIONS = {
  pStep: d => {
    const layer = +d.l, list = offered(layer);
    const at = list.indexOf(design.parts[layer]);
    design.parts[layer] = list[((at < 0 ? 0 : at + +d.s) + list.length) % list.length];
    design.strip = layer;
  },
  pPick: d => { design.parts[+d.l] = +d.i; },
  pStrip: d => { design.strip = +d.l; },
  pRandom: () => { design.parts = design.parts.map((_, layer) => { const l = offered(layer); return l[Math.floor(Math.random() * l.length)]; }); },
  pLoad: async () => { await readExtras(); loadDesign(); toast('Loaded what your Pet is wearing'); },
  pWear: async () => {
    const name = design.name.replace(/[^\x20-\x7e]/g, '').trim().slice(0, 12), pr = cleanPronouns(design.pronouns);
    const hex = t => (t ? toHex(enc.encode(t)) : '-');
    const flag = (flagIndex(design.flag) ? design.flag : 0).toString(16).padStart(2, '0');
    ok(await run(`P SET ${lookHex(design.parts, design.pin)} ${hex(name)} ${hex(pr)} ${flag}`), 'OK P SET');
    await readExtras();
    await readHouse();  // its member card (name, look, pronouns), as roommates will see it
    loadDesign();
    toast(name ? `${name} is wearing it (on the X4's Pet screen)` : 'Your Pet is wearing it (on the X4\'s Pet screen)');
  },
};
for (const [k, fn] of Object.entries(PET_ACTIONS)) PET_ACTIONS[k] = async d => { await fn(d); render(); };

// The way to pay member `m`: their request for `payer` (pasted on their phone or here) or their $cashtag link.
function payLink(m, pence, payer) {
  if (!m) return null;
  if (m.request && payer === S.me) return { url: m.request.url, request: true };
  if (m.id === S.me) {  // you're owed: your own request for the payer, if you pasted one
    const debtor = S.members.find(x => x.id === payer);
    if (debtor && debtor.myRequest) return { url: debtor.myRequest.url, request: true };
  }
  if (m.tag) return { url: 'https://cash.app/$' + m.tag + (pence ? '/' + (pence / 100).toFixed(2) : ''), request: false };
  return null;
}

// Share: the phone's share sheet where the page may use it (https or a computer), else Copy / Text / Email.
function share(title, url) {
  if (navigator.share && window.isSecureContext) return navigator.share({ title, text: title, url }).catch(() => {});
  $('shTitle').textContent = title;
  $('shLink').value = url;
  const body = encodeURIComponent(title + ': ' + url);
  $('shText').href = (/iPhone|iPad|iPod/.test(navigator.userAgent) ? 'sms:&body=' : 'sms:?body=') + body;
  $('shMail').href = 'mailto:?subject=' + encodeURIComponent(title) + '&body=' + body;
  $('shMsg').textContent = 'Or press and hold the link to share it.';
  $('sheet').style.display = 'block';
}
window.closeSheet = () => ($('sheet').style.display = 'none');
window.copyLink = async () => {
  const e = $('shLink');
  let done = false;
  try { if (navigator.clipboard && window.isSecureContext) await navigator.clipboard.writeText(e.value), (done = true); } catch (x) { /* fall back */ }
  if (!done) {
    e.focus(); e.select(); e.setSelectionRange(0, 999);
    try { done = document.execCommand('copy'); } catch (x) { /* nothing */ }
  }
  $('shMsg').textContent = done ? 'Copied' : 'Select the link and copy it';
};

// ---- Actions

const pence = v => Math.round(parseFloat(String(v).replace(/[^0-9.]/g, '')) * 100) || 0;
const dayOf = iso => Math.floor(Date.UTC(+iso.slice(0, 4), +iso.slice(5, 7) - 1, +iso.slice(8, 10)) / 86400000);

const ACTIONS = {
  start: async () => { ok(await run('H START'), 'OK H START'); await readHouse(); render(); toast('Household started'); },
  leave: async () => {
    if (!confirm('Leave the household on this X4? The others keep it.')) return;
    ok(await run('H LEAVE'), 'OK H LEAVE'); await readHouse(); render();
  },
  done: async d => {
    const had = new Set(info.parts.filter(p => p.owned).map(p => p.layer + ':' + p.index));
    await change(() => wasm.chore_done(+d.o, +d.s, today().day));
    await readExtras();
    render();
    const gift = info.parts.find(p => p.owned && !had.has(p.layer + ':' + p.index));
    toast(gift ? `Gift! ${gift.name} (${RARITY[gift.rarity]}): your Pet found it. Dress it up` : 'Done!', gift ? 4000 : 2600);
  },
  toggle: d => change(() => wasm.toggle_item(+d.o, +d.s)),
  remove: d => confirm('Remove it for everyone (at the next Sync)?') && change(() => wasm.remove_record(+d.o, +d.s), 'Removed'),
  clearBought: () => change(() => recs(KIND.ITEM).filter(i => i.a).every(i => wasm.remove_record(i.origin, i.seq)), 'Cleared'),
  settle: d => {
    const debt = S.debts[+d.i];
    if (!confirm(`${who(debt.from)} paid ${who(debt.to)} ${money(debt.pence)}?`)) return;
    return change(() => wasm.settle(debt.from, debt.to, debt.pence), 'Settled');
  },
  share: d => {
    const debt = S.debts[+d.i], to = S.members.find(m => m.id === debt.to), link = payLink(to, debt.pence, debt.from);
    if (link) share(`Pay ${to.name} ${money(debt.pence)} on Cash App`, link.url);
  },
  shareMember: d => {
    const m = S.members.find(x => x.id === +d.id), link = payLink(m, 0, S.me);
    if (link) share(`Pay ${m.name} on Cash App`, link.url);
  },
  badgeDelete: async d => {
    if (!confirm('Delete ' + d.name + '?')) return;
    ok(await run('BX ' + d.name), 'OK BX'); await readExtras(); render(); toast('Deleted');
  },
  ttAdd: () => { saveTimetableEdits(); info.timetable.push(['Mon', '09:00', '10:00', '', '']); render(); },
  ttRemove: d => { saveTimetableEdits(); info.timetable.splice(+d.i, 1); render(); },
  ttSave: async () => {
    saveTimetableEdits();
    const lines = info.timetable.filter(r => r[3]).map(r => r.map(v => String(v).replace(/[,\n\r]/g, ' ').trim()).join(','));
    if (!lines.length) return toast('Add a row first');
    let c = 0;
    for (const l of lines) c = crc32(enc.encode(l + '\n'), c);
    ok(await run('W ' + lines.length + ' ' + c, ...lines), 'OK W');
    toast('Timetable saved');
  },
  setTime: async () => { ok(await run('T ' + Math.floor(Date.now() / 1000)), 'OK T'); await readExtras(); render(); toast('Clock set'); },
};

function saveTimetableEdits() {
  document.querySelectorAll('#tt tr').forEach(tr => {
    const i = +tr.dataset.i, f = tr.querySelectorAll('select,input');
    info.timetable[i] = [...f].map(x => x.value);
  });
}

const FORMS = {
  chore: f => change(() => (putText(f.text.value.trim()), wasm.add_chore(+f.every.value, dayOf(f.due.value))), 'Chore added'),
  item: f => change(() => (putText(f.text.value.trim()), wasm.add_item()), 'On the list'),
  expense: f => {
    const p = pence(f.amount.value);
    if (!p) return toast('How much?');
    return change(() => (putText(f.text.value.trim()), wasm.add_expense(p)), 'Shared with everyone');
  },
  request: f => change(() => (putText(f.text.value), wasm.add_request(pence(f.amount.value), +f.for.value)),
    'Saved. Roommates get it at the next Sync.'),
  reminder: f => {
    const w = f.when.value;
    return change(() => (putText(f.text.value.trim()), wasm.add_reminder(w[0] === 'm' ? 1 : 0, +w.slice(1))), 'Reminder added');
  },
  message: f => change(() => (putText(f.text.value.trim()), wasm.add_message()), 'Sent at the next Sync'),
  cashtag: f => {
    const tag = f.tag.value.trim().replace(/^\$/, '');
    putText(tag);
    if (tag && !wasm.valid_cashtag()) return toast('1-20 letters, digits, _ or -, with a letter in it');
    return change(() => (putText(tag), wasm.set_paytag()), tag ? 'Saved' : 'Removed');
  },
  settings: async f => {
    ok(await run('G SET sleep ' + f.sleep.value, 'G SET invert ' + f.invert.value));
    await readExtras(); render(); toast('Saved');
  },
  badge: async f => {
    const file = f.file.files[0];
    if (!file) return;
    const bmp = await badgeFrom(file, +f.style.value, +f.light.value);
    let name = (f.name.value || file.name.replace(/\.[^.]+$/, '')).toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '').slice(0, 24) || 'badge';
    name += '.bmp';
    const lines = ['B ' + name + ' ' + bmp.length + ' ' + crc32(bmp)];
    for (let i = 0; i < bmp.length; i += 64) lines.push('D ' + toHex(bmp.subarray(i, i + 64)));
    ok(await run(lines), 'OK F');
    await readExtras(); render(); toast('Badge sent');
  },
};

// The picture fitted into 200x200 (RGBA), then turned into a 1-bit BMP by house.wasm.
async function badgeFrom(file, style, light) {
  const img = await createImageBitmap(file);
  const c = document.createElement('canvas');
  c.width = c.height = 200;
  const ctx = c.getContext('2d', { willReadFrequently: true });
  ctx.fillStyle = '#fff';
  ctx.fillRect(0, 0, 200, 200);
  const s = Math.min(200 / img.width, 200 / img.height), w = img.width * s, h = img.height * s;
  ctx.imageSmoothingQuality = 'high';
  ctx.drawImage(img, (200 - w) / 2, (200 - h) / 2, w, h);
  new Uint8Array(wasm.memory.buffer, wasm.image(), 200 * 200 * 4).set(ctx.getImageData(0, 0, 200, 200).data);
  const n = wasm.badge(light, 0, style);
  const bmp = new Uint8Array(wasm.memory.buffer, wasm.bmp(), n).slice();
  // Preview.
  const prev = $('prev');
  if (prev) {
    const p = prev.getContext('2d'), out = p.createImageData(200, 200);
    for (let y = 0; y < 200; y++) for (let x = 0; x < 200; x++) {
      const v = bmp[62 + (199 - y) * 28 + (x >> 3)] & (0x80 >> (x & 7)) ? 255 : 0;
      out.data.set([v, v, v, 255], (y * 200 + x) * 4);
    }
    p.putImageData(out, 0, 0);
  }
  return bmp;
}

async function readExtras() {
  const r = await run('G', 'P GET', 'P PARTS', 'L', 'W GET');
  info.parts = r.filter(l => l.startsWith('PP ')).map(l => {
    const [, layer, index, rarity, has, name] = l.split(' ');
    return { layer: +layer, index: +index, rarity: +rarity, owned: has === '1', name: fromHex(name) };
  });
  [info.exp, info.gifts, info.giftsAll] = (reply(r, 'OK P PARTS ') || '').split(' ').slice(3).map(Number).concat([0, 0, 0]);

  const g = reply(r, 'OK G ');
  info.settings = Object.fromEntries((g ? g.slice(5) : '').split(' ').map(kv => kv.split('=')));
  const p = (reply(r, 'OK P ') || 'OK P 0 ').split(' ');
  info.pet = { look: p[2], name: fromHex(p[3] || ''), pronouns: fromHex(p[4] || ''), flag: parseInt(p[5] || '0', 16) || 0 };
  info.badges = r.filter(l => l.startsWith('F ')).map(l => l.slice(2));
  info.timetable = r.filter(l => l.startsWith('WL ')).map(l => l.slice(3).split(',').slice(0, 5));
}

// ---- Wiring

document.addEventListener('click', async e => {
  const t = e.target.closest('[data-tab],[data-act]');
  if (!t) return;
  if (t.dataset.tab) { tab = t.dataset.tab; render(); return; }
  try { await (ACTIONS[t.dataset.act] || PET_ACTIONS[t.dataset.act])?.(t.dataset); } catch (x) { toast(x.message || String(x), 4000); }
});
document.addEventListener('submit', async e => {
  const f = e.target.closest('form[data-form]');
  if (!f) return;
  e.preventDefault();
  const button = f.querySelector('button');
  button.disabled = true;
  try { await FORMS[f.dataset.form](f); } catch (x) { toast(x.message || String(x), 4000); }
  button.disabled = false;
});

async function start(transport) {
  device = transport;
  $('status').textContent = 'Connecting...';
  await loadWasm();
  await readHouse();
  await readExtras();
  $('status').textContent = 'Connected over ' + device.name;
  $('tabs').hidden = false;
  render();
}

if (onHotspot) {
  $('usb').hidden = true;
  $('how').textContent = 'Connecting to the X4...';
  start(httpTransport).catch(e => ($('connectErr').textContent = e.message || String(e)));
} else if (!('serial' in navigator)) {
  $('usb').disabled = true;
  $('connectErr').textContent = 'This browser can\'t use USB. Use Chrome or Edge on a computer, or on a phone use the X4\'s own WiFi (House, Manage on phone).';
} else {
  $('usb').onclick = async () => {
    $('connectErr').textContent = '';
    try {
      const port = await navigator.serial.requestPort({ filters: [{ usbVendorId: 0x303a }] });
      const t = serialTransport(port);
      await t.open();
      await start(t);
    } catch (e) {
      $('connectErr').textContent = e.name === 'NotFoundError' ? 'No X4 picked. Nothing listed? It\'s asleep: press a button, then try again.' : (e.message || String(e));
    }
  };
}
