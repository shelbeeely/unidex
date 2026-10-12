#!/usr/bin/env python3
"""The RoomiePet images in the README (docs/roomiepet-styles.png, docs/roomiepet-wardrobe.png), drawn from the same
parts as the device (site/x4/pet-art.js, written by tools/pet/export.py). Run after changing the parts or styles:

  python3 tools/pet/export.py && python3 tools/pet/wardrobe_images.py
"""
import json, os, re
from PIL import Image, ImageDraw, ImageFont

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
art = open(os.path.join(root, "site/x4/pet-art.js")).read()
def const(name): return json.loads(re.search(rf"export const {name} = (.*?);\n", art).group(1))
LAYERS, ORDER, STYLES, DROPS, RARITIES = (const("LAYERS"), const("DRAW_ORDER"), const("STYLES"), const("DROPS"),
                                          const("RARITIES"))
DEFAULT = [0, 0, 0, 0, 1, 0, 0]
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

def styled(style):
    look = [d if p < 0 else p for d, p in zip(DEFAULT, style["parts"])]
    return look

# All the styles in a row, named.
scale, pad, label = 6, 24, 34
tile = 32 * scale
w = len(STYLES) * (tile + pad) + pad
img = Image.new("RGB", (w, tile + label + 2 * pad), PAPER)
d = ImageDraw.Draw(img)
for i, st in enumerate(STYLES):
    x = pad + i * (tile + pad)
    img.paste(pet(styled(st), scale), (x, pad))
    t = st["name"]
    tw = d.textlength(t, font=font(22))
    d.text((x + (tile - tw) / 2, pad + tile + 8), t, fill=INK, font=font(22))
img.save(os.path.join(root, "docs/roomiepet-styles.png"), optimize=True)

# The wardrobe: every hair, outfit, hat, extra, eyes and mouth, each worn by the plain blob, gifts marked by rarity.
rarity = {(g["layer"], g["part"]): g["rarity"] for g in DROPS}
rows_of = [(5, "Hair"), (6, "Outfits"), (3, "Hats"), (4, "Extras"), (1, "Eyes"), (2, "Mouths"), (0, "Bodies")]
scale, pad, cell, name_h, head = 3, 12, 96, 40, 34
cols = 16
height = sum(head + cell + name_h + pad for _ in rows_of) + pad
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
        x = pad + i * (cell + pad)
        img.paste(pet(look, scale), (x, y))
        name = part["name"]
        tw = d.textlength(name, font=font(14))
        d.text((x + (cell - tw) / 2, y + cell + 4), name, fill=INK, font=font(14))
        if (layer, i) in rarity:
            e = RARITIES[rarity[(layer, i)]] + " gift"
            tw = d.textlength(e, font=font(12))
            d.text((x + (cell - tw) / 2, y + cell + 21), e, fill=MUTED, font=font(12))
    y += cell + name_h + pad
img.save(os.path.join(root, "docs/roomiepet-wardrobe.png"), optimize=True)
print("wrote docs/roomiepet-styles.png and docs/roomiepet-wardrobe.png")
