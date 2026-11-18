#!/usr/bin/env python3
# Draws the port's HOME Menu art from scratch: the 48x48 icon, the 256x128
# banner and a short banner jingle. None of it comes from the game, so it can
# live in the repository and ship with public builds.
#
# Usage: python3 make_art.py [out_dir]   (default: the folder of this script)

import math
import struct
import sys
import wave
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

RED = (178, 28, 36)
DARK = (18, 20, 24)
STEEL = (120, 128, 140)
WHITE = (236, 236, 236)
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"


def font(size):
    try:
        return ImageFont.truetype(FONT, size)
    except OSError:
        return ImageFont.load_default()


def stripes(draw, w, h, step, width):
    for x in range(-h, w + h, step):
        draw.polygon([(x, h), (x + width, h), (x + width + h, 0), (x + h, 0)], fill=(28, 31, 37))


def icon(path):
    im = Image.new("RGB", (48, 48), DARK)
    d = ImageDraw.Draw(im)
    stripes(d, 48, 48, 8, 3)
    d.rectangle([0, 0, 47, 47], outline=STEEL)
    d.polygon([(24, 6), (40, 24), (24, 42), (8, 24)], fill=RED, outline=WHITE)
    d.text((24, 24), "C", font=font(18), fill=WHITE, anchor="mm")
    im.save(path)


def banner(path):
    # The panel is drawn on its own, so the stripes stay inside it and the
    # banner around it stays transparent.
    panel = Image.new("RGBA", (240, 96), DARK + (255,))
    d = ImageDraw.Draw(panel)
    stripes(d, 240, 96, 12, 4)
    d.rectangle([0, 66, 239, 74], fill=RED)
    d.rectangle([0, 0, 239, 95], outline=STEEL, width=2)
    d.text((120, 34), "CRUSADER", font=font(30), fill=WHITE, anchor="mm")
    d.text((120, 85), "3DS PORT", font=font(12), fill=STEEL, anchor="mm")
    im = Image.new("RGBA", (256, 128), (0, 0, 0, 0))
    im.paste(panel, (8, 16))
    im.save(path)


def jingle(path, rate=22050):
    # Two short falling tones with a soft decay: under the 3 s the banner allows.
    notes = [(330.0, 0.18), (247.0, 0.18), (165.0, 0.5)]
    frames = bytearray()
    for freq, seconds in notes:
        n = int(rate * seconds)
        for i in range(n):
            env = math.exp(-4.0 * i / n)
            s = 0.5 * math.sin(2 * math.pi * freq * i / rate) + 0.2 * math.sin(4 * math.pi * freq * i / rate)
            frames += struct.pack("<h", int(32767 * 0.5 * env * s))
    with wave.open(str(path), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(rate)
        out.writeframes(bytes(frames))


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parent
    out.mkdir(parents=True, exist_ok=True)
    icon(out / "icon.png")
    banner(out / "banner.png")
    jingle(out / "banner.wav")


if __name__ == "__main__":
    main()
