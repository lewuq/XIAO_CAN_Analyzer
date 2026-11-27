"""Rasterize the V5.1 host-renderer previews as GitHub README PNGs.

Run the C host tests from the repository root first. Install Pillow to compose the
PPM frames. All screen pixels come directly from the firmware renderer.
"""
import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

DOCS = Path(__file__).resolve().parents[1] / "docs"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("font", type=Path, nargs="?", help="Montserrat-Bold.ttf for preview captions")
FONT = parser.parse_args().font
NAVY = "#101b2d"
PANEL = "#19283e"
YELLOW = "#f5dc4d"
INK = "#faf7ef"
MUTED = "#bdcbe1"
SCALE = 2
SCREEN_WIDTH = 240
SCREEN_HEIGHT = 120
MARGIN = 24
CARD_WIDTH = SCREEN_WIDTH * SCALE + 16
CARD_HEIGHT = SCREEN_HEIGHT * SCALE + 43
GAP = 16


def font(size):
    if FONT is not None:
        return ImageFont.truetype(FONT, size)
    return ImageFont.load_default(size=size)


def sheet(output, title, cards):
    width = 2 * MARGIN + 2 * CARD_WIDTH + GAP
    height = 88 + 2 * CARD_HEIGHT + GAP + MARGIN
    canvas = Image.new("RGB", (width, height), NAVY)
    draw = ImageDraw.Draw(canvas)
    draw.rectangle((0, 0, width - 1, 4), fill=YELLOW)
    draw.text((24, 18), title, fill=INK, font=font(28))
    draw.text((25, 56), "V5.1 FIRMWARE UI  /  SIMULATED TRAFFIC  /  240 × 120 DISPLAY",
              fill=MUTED, font=font(15))
    for i, (name, label) in enumerate(cards):
        x = MARGIN + (i % 2) * (CARD_WIDTH + GAP)
        y = 88 + (i // 2) * (CARD_HEIGHT + GAP)
        draw.rounded_rectangle((x, y, x + CARD_WIDTH - 1, y + CARD_HEIGHT - 1), radius=9, fill=PANEL)
        draw.text((x + 8, y + 8), label, fill=YELLOW, font=font(17))
        with Image.open(DOCS / f"{name}.ppm") as source:
            assert source.size == (SCREEN_WIDTH, SCREEN_HEIGHT)
            # Integer nearest-neighbor scaling preserves the exact OLED pixels.
            panel = source.resize((SCREEN_WIDTH * SCALE, SCREEN_HEIGHT * SCALE), Image.Resampling.NEAREST)
            canvas.paste(panel, (x + 8, y + 33))
    canvas.save(DOCS / output, optimize=True)


sheet("v5-rate-preview.png", "Bitrate selection and detection", (
    ("preview_scan", "Automatic scan"),
    ("preview_classic", "Classic traffic: nominal rate confirmed"),
    ("preview_fd", "CAN FD + BRS: both rates confirmed"),
    ("preview_manual", "Manual preset"),
))

sheet("v5-capture-preview.png", "Capture and inspection", (
    ("preview_ids", "Received frame IDs"),
    ("preview_data", "64-byte CAN FD payload"),
    ("preview_storage", "External Flash recording"),
    ("preview_idle", "Idle bus: last lock retained"),
))

# Export the photographed pages separately so image viewers can show the pixels at 100%.
for source, output in (
    ("preview_data", "v5-data-pixels.png"),
    ("preview_rate_locked", "v5-rate-pixels.png"),
):
    with Image.open(DOCS / f"{source}.ppm") as frame:
        frame.resize((SCREEN_WIDTH * SCALE, SCREEN_HEIGHT * SCALE),
                     Image.Resampling.NEAREST).save(DOCS / output, optimize=True)
