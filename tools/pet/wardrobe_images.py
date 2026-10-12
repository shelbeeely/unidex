#!/usr/bin/env python3
"""The RoomiePet images in the README (docs/roomiepet-pets.png, docs/roomiepet-wardrobe.png), drawn from the same
parts as the device (site/x4/pet-art.js, written by tools/pet/export.py). Run after changing the parts or styles:

  python3 tools/pet/export.py && python3 tools/pet/wardrobe_images.py
"""
import json, os, re
from PIL import Image, ImageDraw, ImageFont

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
art = open(os.path.join(root, "site/x4/pet-art.js")).read()
def const(name): return json.loads(re.search(rf"export const {name} = (.*?);\n", art).group(1))
LAYERS, ORDER, EXAMPLES, DROPS, RARITIES = (const("LAYERS"), const("DRAW_ORDER"), const("EXAMPLES"), const("DROPS"),
                                            const("RARITIES"))
GLYPHS, CHARS = const("PIN_GLYPHS"), const("PIN_CHARS")
FLAGS, CHEVRON, RING = const("FLAGS"), const("FLAG_CHEVRON"), const("FLAG_RING")

def flag_colour(f, x, y, w, h):
    """flags.h's colourAt."""
    fl = FLAGS[f]
    if fl["shape"] == "intersex":
        dx, dy = 2 * x + 1 - w, 2 * y + 1 - h
        r, t = h * 2 // 3, max(1, h // 6)
        return RING if (r - t) ** 2 <= dx * dx + dy * dy <= (r + t) ** 2 else fl["colours"][0]
    if fl["shape"] == "progress":
        v = (h - abs(2 * y + 1 - h)) - 2 * x
        if v > 0:
            return CHEVRON[max(0, 4 - v * 5 // (h + 1))]
    return fl["colours"][min(len(fl["colours"]) - 1, y * len(fl["colours"]) // h)]

def flag(img, f, x0, y0, w, h, gw=52, gh=32):
    """A flag in colour, w x h pixels at (x0, y0), with a dark edge."""
    d = ImageDraw.Draw(img)
    for gy in range(gh):
        for gx in range(gw):
            d.rectangle([x0 + gx * w / gw, y0 + gy * h / gh, x0 + (gx + 1) * w / gw - 1, y0 + (gy + 1) * h / gh - 1],
                        fill=flag_colour(f, gx, gy, gw, gh))
    d.rectangle([x0, y0, x0 + w - 1, y0 + h - 1], outline=INK, width=max(1, round(w / 40)))
DEFAULT = [0, 0, 0, 0, 1, 0, 0, 0]  # the seven layers, then what it holds
font_path = os.path.join(root, "tools/fonts/LiberationSans-Bold.ttf")
font = lambda size: ImageFont.truetype(font_path, size)
INK, PAPER, MUTED = (17, 17, 17), (255, 255, 255), (110, 110, 110)

def pet(look, scale, only=None):
    """The Pet (or one layer of it, on the plain blob) as an image."""
    img = Image.new("RGB", (32 * scale, 32 * scale), PAPER)
    d = ImageDraw.Draw(img)
    for layer in ORDER:
        if only is not None and layer not in (0, only):
            continue
        rows = LAYERS[layer]["parts"][look[layer]]["rows"]
        for y, row in enumerate(rows):
            for x, c in enumerate(row):
                if c != ".":
                    d.rectangle([x * scale, y * scale, x * scale + scale - 1, y * scale + scale - 1],
                                fill=INK if c == "#" else PAPER)
    return img

def pin(img, pronouns, scale):
    """The pronoun pin, as src/apps/pet/pronouns.h draws it: 3x5 capitals on a badge, at half a Pet pixel."""
    d, text, half = ImageDraw.Draw(img), pronouns.upper(), scale / 2
    n = len(text)
    w, h = n * 4 - 1 + 6, 11
    x0, y0 = (64 - w) // 2, 49
    for y in range(h):
        for x in range(w):
            if x in (0, w - 1) and y in (0, h - 1):
                continue
            edge = x in (0, w - 1) or y in (0, h - 1)
            ink, tx, ty = edge, x - 3, y - 3
            if not edge and tx >= 0 and 0 <= ty < 5 and tx < n * 4 - 1 and tx % 4 != 3:
                ink = GLYPHS[CHARS.index(text[tx // 4].lower())][ty * 3 + tx % 4] == "#"
            d.rectangle([(x0 + x) * half, (y0 + y) * half, (x0 + x + 1) * half - 1, (y0 + y + 1) * half - 1],
                        fill=INK if ink else PAPER)

# Example Pets in a row, some wearing their pronoun pins, each labelled with its pronouns.
scale, pad, label = 6, 24, 34
tile = 32 * scale
w = len(EXAMPLES) * (tile + pad) + pad
img = Image.new("RGB", (w, tile + label + 2 * pad), PAPER)
d = ImageDraw.Draw(img)
for i, ex in enumerate(EXAMPLES):
    x = pad + i * (tile + pad)
    p = pet(ex["parts"], scale).convert("RGB")
    if ex["pin"] == "pronouns":
        pin(p, ex["pronouns"], scale)
    elif ex["pin"] == "flag":
        flag(p, ex["flag"], (64 - 26) // 2 * scale // 2, 48 * scale // 2, 26 * scale // 2, 14 * scale // 2)
    img.paste(p, (x, pad))
    t = ex["pronouns"] + " · " + FLAGS[ex["flag"]]["name"]
    tw = d.textlength(t, font=font(22))
    d.text((x + (tile - tw) / 2, pad + tile + 8), t, fill=INK, font=font(22))
img.save(os.path.join(root, "docs/roomiepet-pets.png"), optimize=True)

# The wardrobe: every hair, outfit, hat, extra, eyes and mouth, each worn by the plain blob, gifts marked by rarity.
rarity = {(g["layer"], g["part"]): g["rarity"] for g in DROPS}
rows_of = [(6, "Outfits"), (5, "Hair"), (3, "Hats"), (4, "Accessories"), (7, "Things to hold"), (1, "Eyes"), (2, "Mouths"),
           (0, "Bodies")]
scale, pad, cell, name_h, head = 3, 12, 96, 40, 34
cols = 16
lines = lambda layer: -(-len(LAYERS[layer]["parts"]) // cols)
height = sum(head + lines(layer) * (cell + name_h + pad) for layer, _ in rows_of) + pad
img = Image.new("RGB", (pad + cols * (cell + pad), height), PAPER)
d = ImageDraw.Draw(img)
y = pad
for layer, title in rows_of:
    d.text((pad, y), title, fill=INK, font=font(24))
    y += head
    for i, part in enumerate(LAYERS[layer]["parts"]):
        look = list(DEFAULT)
        look[4] = 0  # no blush, so the part stands out
        look[layer] = i
        x, top = pad + i % cols * (cell + pad), y + i // cols * (cell + name_h + pad)
        img.paste(pet(look, scale), (x, top))
        name = part["name"]
        tw = d.textlength(name, font=font(14))
        d.text((x + (cell - tw) / 2, top + cell + 4), name, fill=INK, font=font(14))
        if (layer, i) in rarity:
            e = RARITIES[rarity[(layer, i)]] + " gift"
            tw = d.textlength(e, font=font(12))
            d.text((x + (cell - tw) / 2, top + cell + 21), e, fill=MUTED, font=font(12))
    y += lines(layer) * (cell + name_h + pad)
img.save(os.path.join(root, "docs/roomiepet-wardrobe.png"), optimize=True)

# The pride flags, each named.
fw, fh, pad, label = 156, 96, 18, 30
cols = 5
rows = -(-(len(FLAGS) - 1) // cols)
img = Image.new("RGB", (pad + cols * (fw + pad), pad + rows * (fh + label + pad)), PAPER)
d = ImageDraw.Draw(img)
for i, fl in enumerate(FLAGS[1:]):
    x, y = pad + i % cols * (fw + pad), pad + i // cols * (fh + label + pad)
    flag(img, i + 1, x, y, fw, fh)
    tw = d.textlength(fl["name"], font=font(18))
    d.text((x + (fw - tw) / 2, y + fh + 5), fl["name"], fill=INK, font=font(18))
img.save(os.path.join(root, "docs/roomiepet-flags.png"), optimize=True)
print("wrote docs/roomiepet-pets.png, docs/roomiepet-wardrobe.png and docs/roomiepet-flags.png")
