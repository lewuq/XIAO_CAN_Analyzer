"""Rasterize the v5 host-renderer previews as GitHub README PNGs.

Run the C host tests from the repository root first. Install Pillow to compose the
PPM frames. All screen pixels come directly from the firmware renderer.
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

DOCS = Path(__file__).resolve().parents[1] / "docs"
NAVY = "#101b2d"
PANEL = "#19283e"
YELLOW = "#f5dc4d"
INK = "#faf7ef"
MUTED = "#bdcbe1"


def font(size):
    for name in ("DejaVuSans.ttf", "segoeui.ttf", "Arial.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default(size=size)


def sheet(output, title, cards):
    canvas = Image.new("RGB", (1056, 698), NAVY)
    draw = ImageDraw.Draw(canvas)
    draw.rectangle((0, 0, 1055, 4), fill=YELLOW)
    draw.text((24, 18), title, fill=INK, font=font(28))
    draw.text((25, 56), "V5 FIRMWARE UI  /  SIMULATED TRAFFIC  /  240 x 120 DISPLAY",
              fill=MUTED, font=font(15))
    for i, (name, label) in enumerate(cards):
        x = 24 + (i % 2) * 512
        y = 88 + (i // 2) * 298
        draw.rounded_rectangle((x, y, x + 495, y + 282), radius=9, fill=PANEL)
        draw.text((x + 8, y + 8), label, fill=YELLOW, font=font(17))
        with Image.open(DOCS / f"{name}.ppm") as source:
            assert source.size == (240, 120)
            canvas.paste(source.resize((480, 240), Image.Resampling.NEAREST), (x + 8, y + 33))
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
