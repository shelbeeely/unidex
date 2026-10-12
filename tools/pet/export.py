#!/usr/bin/env python3
"""The Pet's parts for the websites: src/apps/pet/parts.h (and the layer names and default look in pet_logic.h)
-> site/pet-parts.js (the Tools page: the first five layers, as it has always drawn them) and site/x4/pet-art.js
(the X4 web app and the RoomiePet page: every layer and part, the order they're drawn in, the bits a layer takes
in a saved look, the gifts, and the pronoun pin's letters and the pronoun choices from pronouns.h), so both draw exactly what the device draws. Run after
changing the parts:

  python3 tools/pet/export.py          writes both
  python3 tools/pet/export.py --check  fails if either is out of date
"""
import json, os, re, sys

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
parts_h = open(os.path.join(root, "src/apps/pet/parts.h")).read()
logic_h = open(os.path.join(root, "src/apps/pet/pet_logic.h")).read()
out_path = os.path.join(root, "site/pet-parts.js")
x4_path = os.path.join(root, "site/x4/pet-art.js")


def parts(block):
    """[{name, rows}] from the C initialiser of one array or single part."""
    found = []
    for name, body in re.findall(r'\{\s*"([^"]+)",\s*\{((?:\s*"[#o.]{32}",?)+)\}\s*\}', block, re.S):
        rows = re.findall(r'"([#o.]{32})"', body)
        assert len(rows) == 32, f"{name}: {len(rows)} rows"
        found.append({"name": name, "rows": rows})
    return found


def array(name):
    m = re.search(rf"static const PetPart {name}\[\] = \{{(.*?)\n\}};", parts_h, re.S)
    assert m, name
    return parts(m.group(1))


def build(text, x4):
    """pet_logic.h's `UNIDEX_X4 ? a : b`, as one build or the other sees it."""
    return re.sub(r"UNIDEX_X4 \? ([^:,{}]+?) : ([^,}]+)", lambda m: m.group(1 if x4 else 2), text)


names_c = re.search(r"LAYER_NAMES\[LAYERS\] = \{(.*?)\};", logic_h, re.S).group(1)
layer_names = re.findall(r'"([A-Za-z]+)"', build(names_c, False))
x4_names = re.findall(r'"([A-Za-z]+)"', build(names_c, True))
x4_bits = [int(b) for b in build(re.search(r"LAYER_BITS\[LAYERS\] = \{(.*?)\};", logic_h, re.S).group(1), True).split(",")]
waveshare_parts = [int(n) for n in re.search(r"WAVESHARE_PARTS\[LAYERS\] = \{(.*?)\};", logic_h).group(1).split(",")]
pronouns_h = open(os.path.join(root, "src/apps/pet/pronouns.h")).read()
presets = re.findall(r'"([^"]*)"', re.search(r"PRESETS\[\] = \{(.*?)\};", pronouns_h, re.S).group(1))
glyphs = re.findall(r'"([#.]{15})"', re.search(r"GLYPHS\[\] = \{.*?\n(.*?)\};", pronouns_h, re.S).group(1))
chars = re.search(r'CHARS\[\] = "([^"]*)"', pronouns_h).group(1)
assert len(glyphs) == len(chars), (len(glyphs), len(chars))
flags_h = open(os.path.join(root, "src/apps/pet/flags.h")).read()
flag_list = [{"name": n, "shape": shape.lower(), "colours": ["#" + c[2:].upper() for c in re.findall(r"0x[0-9A-Fa-f]{6}", cs)]}
             for n, shape, cs in re.findall(r'\{"([^"]+)", (STRIPES|PROGRESS|INTERSEX), \d+, \{([^}]*)\}\}', flags_h)]
chevron = ["#" + c[2:].upper() for c in re.findall(r"0x[0-9A-Fa-f]{6}", re.search(r"PROGRESS_CHEVRON\[5\] = \{(.*?)\}", flags_h).group(1))]
ring = "#" + re.search(r"INTERSEX_RING = 0x([0-9A-Fa-f]{6})", flags_h).group(1).upper()
default = [int(x) for x in re.search(r"DEFAULT_LOOK = \{\{([\d,\s]+)\}", logic_h).group(1).split(",")]
happy = int(re.search(r"HAPPY_EYES = (\d+)", logic_h).group(1))
closed = parts(re.search(r"EYES_CLOSED = (\{.*?\}\});", parts_h, re.S).group(1))[0]
# Example Pets (the RoomiePet page's cards and the README picture), by part name: body, eyes, mouth, hat, accessory,
# hair, outfit; then pronouns, the pride flag, what the pin shows ("pronouns", "flag" or None), and what it holds (a
# name from catalog/items.txt, or None).
EXAMPLES = [
    (["Blob", "Lashes", "Cat", "Cat ears", "Choker", "Fluffy bob", "Skirt"], "he/they", "Bi", "flag", "Broom"),
    (["Blob", "Eyeliner", "Fangs", "Clips", "Hearts", "Split dye", "Mesh top"], "she/her", "Trans", "pronouns", "Wand"),
    (["Blob", "Dots", "Lip ring", "None", "Safety pin", "Liberty", "Patch vest"], "they/them", "Nonbinary", "pronouns", "Game pad"),
    (["Blob", "Smudged", "Small o", "None", "Wristband", "Emo fringe", "Zip hoodie"], "he/him", "Gay", "flag", None),
    (["Blob", "Eyeliner", "Lipstick", "None", "Cross", "Ringlets", "Cape"], "she/they", "Ace", None, "Candle"),
    (["Blob", "Dots", "Smirk", "None", "Dog tags", "Slick back", "Leather"], "he/him", "Progress", "flag", "Wrench"),
    (["Blob", "Lashes", "Lipstick", "Sun hat", "Earrings", "Long", "Sundress"], "she/her", "Lesbian", "pronouns", "Watering can"),
    (["Bunny", "Dots", "Smile", "Top hat", "Bow tie", "None", "Waistcoat"], "any", "Rainbow", "flag", "Carrot"),
]
ARRAYS = ["BODIES", "EYES", "MOUTHS", "HATS", "EXTRAS", "HAIRS", "OUTFITS"]
all_layers = [{"name": n, "parts": array(a)} for n, a in zip(x4_names, ARRAYS)]
names_of = [[p["name"] for p in layer["parts"]] for layer in all_layers]
flag_names = [f["name"] for f in flag_list]
examples = [{"parts": [names_of[i].index(n) for i, n in enumerate(names)], "pronouns": pr,
             "flag": flag_names.index(flag), "pin": pin, "held": held} for names, pr, flag, pin, held in EXAMPLES]
# The Tools page's designer (the Waveshare board) has rows for the first five, with the parts it always had.
layers = [{"name": n, "parts": array(a)[:k]} for n, a, k in zip(layer_names, ARRAYS, waveshare_parts)][:5]
order_names = re.search(r"DRAW_ORDER\[LAYERS\] = \{(.*?)\};", logic_h).group(1).replace(" ", "").split(",")
index_of = {"BODY": 0, "EYES_LAYER": 1, "MOUTH": 2, "HAT": 3, "EXTRA": 4, "HAIR": 5, "OUTFIT": 6}
draw_order = [index_of[n] for n in order_names]
rarity_of = {"UNCOMMON": 1, "RARE": 2, "LEGENDARY": 3}
drops = [{"layer": index_of[l], "part": int(p), "rarity": rarity_of[r], "what": w}
         for l, p, r, w in re.findall(r'\{(BODY|EYES_LAYER|MOUTH|HAT|EXTRA|HAIR|OUTFIT), (\d+), (UNCOMMON|RARE|LEGENDARY), "([^"]+)"\}', logic_h)]
assert drops, "no DROPS found in pet_logic.h"

js = ("// Generated by tools/pet/export.py from src/apps/pet/parts.h and pet_logic.h: don't edit, run the script.\n"
      "// Each part is 32 rows of 32 characters: '#' ink, 'o' paper (covers what is under it), '.' see-through.\n"
      f"export const LAYERS = {json.dumps(layers, separators=(',', ':'))};\n"
      f"export const EYES_CLOSED = {json.dumps(closed, separators=(',', ':'))};\n"
      f"export const DEFAULT_LOOK = {json.dumps(default)};\n"
      f"export const HAPPY_EYES = {happy};\n")
x4_js = ("// Generated by tools/pet/export.py from src/apps/pet/parts.h and pet_logic.h: don't edit, run the script.\n"
         "// Every layer (parts by index, as the X4 saves them), the order they're drawn in, the bits each layer takes in a\n"
         "// saved look (the top bit: the pronoun pin), the gifts that drop after chores (rarity 1 uncommon, 2 rare,\n"
         "// 3 legendary; every other part is a starter), the pin's 3x5 letters and the pronoun choices (pronouns.h).\n"
         f"export const LAYERS = {json.dumps(all_layers, separators=(',', ':'))};\n"
         f"export const DRAW_ORDER = {json.dumps(draw_order)};\n"
         f"export const BITS = {json.dumps(x4_bits)};\n"
         f"export const PIN_CHARS = {json.dumps(chars)};\n"
         f"export const PIN_GLYPHS = {json.dumps(glyphs)};\n"
         f"export const PRONOUNS = {json.dumps(presets[1:])};\n"
         f"export const FLAGS = {json.dumps(flag_list, separators=(',', ':'))};\n"
         f"export const FLAG_CHEVRON = {json.dumps(chevron)};\n"
         f"export const FLAG_RING = {json.dumps(ring)};\n"
         f"export const EXAMPLES = {json.dumps(examples, separators=(',', ':'))};\n"
         f"export const RARITIES = {json.dumps(['starter', 'uncommon', 'rare', 'legendary'])};\n"
         f"export const DROPS = {json.dumps(drops, separators=(',', ':'))};\n")
if "--check" in sys.argv:
    for path, text in [(out_path, js), (x4_path, x4_js)]:
        current = open(path).read() if os.path.exists(path) else ""
        if current != text:
            sys.exit(f"{os.path.relpath(path, root)} is out of date: run python3 tools/pet/export.py")
    print("site/pet-parts.js and site/x4/pet-art.js are up to date")
else:
    open(out_path, "w").write(js)
    open(x4_path, "w").write(x4_js)
    print(f"wrote site/pet-parts.js: {', '.join(f'{l['name']} {len(l['parts'])}' for l in layers)}")
