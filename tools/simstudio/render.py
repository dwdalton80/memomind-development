#!/usr/bin/env python3
"""Render simstudio display lists as GRAY_4 frames.

The C harness records geometry and styles but has no font, so text metrics
and therefore label layout are resolved here. Output is quantised to the 16
grey levels the panel actually has, so the images show roughly what the
wearer would see rather than a prettier version of it.

Usage: render.py <frame.json> [...] [--scale N] [--contact-sheet out.png]
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

# LVGL align constants, from gm_plugin_lvgl_api.h.
TOP_LEFT, TOP_MID, TOP_RIGHT = 1, 2, 3
BOTTOM_LEFT, BOTTOM_MID, BOTTOM_RIGHT = 4, 5, 6
LEFT_MID, RIGHT_MID, CENTER = 7, 8, 9

FLAG_HIDDEN = 1

FONT_CANDIDATES = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
]


def load_font(size: int) -> ImageFont.FreeTypeFont:
    for path in FONT_CANDIDATES:
        if Path(path).is_file():
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def quantise(image: Image.Image) -> Image.Image:
    """Crush to the panel's 16 grey levels."""
    return image.point(lambda value: (value >> 4) * 17)


def text_size(draw: ImageDraw.ImageDraw, text: str,
              font: ImageFont.FreeTypeFont) -> tuple[int, int]:
    if not text:
        return (0, 0)
    left, top, right, bottom = draw.multiline_textbbox((0, 0), text, font=font)
    return (int(right - left), int(bottom - top))


def resolve(objects: dict, index: int, sizes: dict) -> tuple[int, int]:
    """Absolute top-left of one object, resolving its parent chain."""
    node = objects[index]
    width, height = sizes[index]
    parent = node["parent"]

    if parent < 0:
        return (0, 0)

    parent_x, parent_y = resolve(objects, parent, sizes)
    parent_w, parent_h = sizes[parent]

    if "pos" in node:
        return (parent_x + node["pos"][0], parent_y + node["pos"][1])

    align = node.get("align")
    dx, dy = node.get("offset", (0, 0))
    if align is None:
        return (parent_x + dx, parent_y + dy)

    if align in (TOP_LEFT, LEFT_MID, BOTTOM_LEFT):
        x = parent_x
    elif align in (TOP_MID, CENTER, BOTTOM_MID):
        x = parent_x + (parent_w - width) // 2
    else:
        x = parent_x + parent_w - width

    if align in (TOP_LEFT, TOP_MID, TOP_RIGHT):
        y = parent_y
    elif align in (LEFT_MID, CENTER, RIGHT_MID):
        y = parent_y + (parent_h - height) // 2
    else:
        y = parent_y + parent_h - height

    return (x + dx, y + dy)


def render(frame: dict, font_size: int = 20) -> Image.Image:
    width = frame["display"]["width"]
    height = frame["display"]["height"]
    image = Image.new("L", (width, height), 0)
    draw = ImageDraw.Draw(image)
    font = load_font(font_size)

    objects = {node["i"]: node for node in frame["objects"]}

    # Labels size to their content; boxes use the size they were given.
    sizes: dict[int, tuple[int, int]] = {}
    for index, node in objects.items():
        w, h = node["w"], node["h"]
        if node["type"] == "label":
            tw, th = text_size(draw, node.get("text", ""), font)
            w = tw if w < 0 else w
            h = th if h < 0 else h
        else:
            w = width if w < 0 else w
            h = height if h < 0 else h
        sizes[index] = (w, h)

    def hidden(index: int) -> bool:
        while index >= 0:
            if objects[index]["flags"] & FLAG_HIDDEN:
                return True
            index = objects[index]["parent"]
        return False

    # Parents before children, so later siblings paint on top.
    for index in sorted(objects):
        node = objects[index]
        if hidden(index):
            continue
        x, y = resolve(objects, index, sizes)
        w, h = sizes[index]

        if node["type"] == "box":
            bg_color, bg_opa = node["bg"]
            border_color, border_opa, border_width = node["border"]
            radius = min(node["radius"], min(w, h) // 2)
            box = [x, y, x + w - 1, y + h - 1]
            fill = bg_color if bg_opa > 0 and bg_color >= 0 else None
            outline = (border_color if border_opa != 0 and border_width > 0
                       and border_color >= 0 else None)
            if fill is None and outline is None:
                continue
            if radius > 0:
                draw.rounded_rectangle(box, radius=radius, fill=fill,
                                       outline=outline, width=max(border_width, 0))
            else:
                draw.rectangle(box, fill=fill, outline=outline,
                               width=max(border_width, 0))
        else:
            text = node.get("text", "")
            if not text:
                continue
            shade = node["text_color"]
            anchor_x = x + w // 2
            draw.multiline_text((anchor_x, y), text,
                                fill=shade if shade >= 0 else 255,
                                font=font, anchor="ma", align="center")

    return quantise(image)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("frames", nargs="+", type=Path)
    parser.add_argument("--scale", type=int, default=1)
    parser.add_argument("--font-size", type=int, default=20)
    parser.add_argument("--contact-sheet", type=Path)
    arguments = parser.parse_args()

    rendered = []
    for path in sorted(arguments.frames):
        frame = json.loads(path.read_text())
        image = render(frame, arguments.font_size)
        if arguments.scale > 1:
            image = image.resize(
                (image.width * arguments.scale, image.height * arguments.scale),
                Image.NEAREST)
        output = path.with_suffix(".png")
        image.save(output)
        rendered.append((frame.get("label", path.stem), image))
        print(f"  {output}")

    if arguments.contact_sheet and rendered:
        label_font = load_font(16)
        pad, caption = 12, 24
        cell_w = max(i.width for _, i in rendered)
        cell_h = max(i.height for _, i in rendered)
        sheet = Image.new("L", (cell_w + pad * 2,
                                (cell_h + caption + pad) * len(rendered) + pad), 32)
        pen = ImageDraw.Draw(sheet)
        for row, (label, image) in enumerate(rendered):
            top = pad + row * (cell_h + caption + pad)
            pen.text((pad, top), label, fill=200, font=label_font)
            sheet.paste(image, (pad, top + caption))
        arguments.contact_sheet.parent.mkdir(parents=True, exist_ok=True)
        sheet.save(arguments.contact_sheet)
        print(f"  {arguments.contact_sheet}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
