"""Draws the app icons (Windows and Mac) and the Windows tray icons from the menu bar icon's geometry.

The glyph is macos/Osdrop/Resources/Assets.xcassets/MenuBarIcon.imageset/menubar.svg: a screen
edge with a half arrow crossing it each way, on a 22-unit grid. This script draws the same lines
(keep the two in step) into:

  Osdrop.ico                 the app icon: the glyph in white on the accent colour
  Tray-black.ico, Tray-white.ico
                             the tray icon for light and dark taskbars
  Tray-controlled-black.ico, Tray-controlled-white.ico
                             the tray icon while another computer controls this PC: the glyph
                             cut out of a filled square
  AppIcon.appiconset         the Mac app icon: the same glyph and colours in the macOS icon
                             shape (a rounded square inside the 1024-point grid's margins)

The Android icons are vector drawables with the same lines, written by hand:
android/app/src/main/res/drawable/ic_launcher_foreground.xml and ic_notification.xml.

Run from the repository root:  uvx --with pillow python scripts/windows/make_icons.py
"""

import json
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "windows/app/Osdrop.App/Assets"
MAC_ICONSET = ROOT / "macos/Osdrop/Resources/Assets.xcassets/AppIcon.appiconset"
# The macOS icon grid: the rounded square spans 824 of 1024 points with this corner radius.
MAC_BODY = 824 / 1024
MAC_CORNER = 185 / 824
MAC_SIZES = [(16, 1), (16, 2), (32, 1), (32, 2), (128, 1), (128, 2), (256, 1), (256, 2), (512, 1), (512, 2)]

GRID = 22.0
STROKE = 1.5
# menubar.svg, in grid units: the edge, then each half arrow as a polyline.
LINES = [
    [(11, 2.5), (11, 19.5)],
    [(3, 8.5), (9, 8.5)],
    [(13, 8.5), (19, 8.5), (15, 4.5)],
    [(19, 13.5), (13, 13.5)],
    [(9, 13.5), (3, 13.5), (7, 17.5)],
]

ACCENT = (0x1E, 0x8E, 0x5A, 255)  # Theme/Tokens.xaml Osdrop.Color.Accent
WHITE = (255, 255, 255, 255)
BLACK = (0, 0, 0, 255)
CLEAR = (0, 0, 0, 0)

APP_SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]
TRAY_SIZES = [16, 20, 24, 32, 40, 48, 64]
SUPERSAMPLE = 8


def draw_glyph(draw: ImageDraw.ImageDraw, size: int, scale: float, offset: float, colour) -> None:
    """The glyph's lines with round caps and joins, `scale` pixels per grid unit."""
    width = STROKE * scale
    radius = width / 2

    def point(x: float, y: float) -> tuple[float, float]:
        return offset + x * scale, offset + y * scale

    for line in LINES:
        points = [point(x, y) for x, y in line]
        draw.line(points, fill=colour, width=max(1, round(width)), joint="curve")
        for x, y in points:
            draw.ellipse((x - radius, y - radius, x + radius, y + radius), fill=colour)


def render(size: int, background, glyph, glyph_fraction: float, corner_fraction: float) -> Image.Image:
    """One size, drawn large and scaled down so thin lines stay smooth."""
    big = size * SUPERSAMPLE
    image = Image.new("RGBA", (big, big), CLEAR)
    draw = ImageDraw.Draw(image)
    if background is not None:
        draw.rounded_rectangle((0, 0, big - 1, big - 1), radius=big * corner_fraction, fill=background)
    glyph_size = big * glyph_fraction
    scale = glyph_size / GRID
    offset = (big - glyph_size) / 2
    if glyph == CLEAR:
        # Cut the glyph out of the background.
        mask = Image.new("L", (big, big), 0)
        draw_glyph(ImageDraw.Draw(mask), big, scale, offset, 255)
        alpha = image.getchannel("A")
        alpha.paste(0, mask=mask)
        image.putalpha(alpha)
    else:
        draw_glyph(draw, big, scale, offset, glyph)
    return image.resize((size, size), Image.LANCZOS)


def save(name: str, sizes: list[int], background, glyph, glyph_fraction: float, corner_fraction: float) -> None:
    images = [render(size, background, glyph, glyph_fraction, corner_fraction) for size in sizes]
    largest = images[-1]
    largest.save(ASSETS / name, format="ICO", sizes=[(s, s) for s in sizes], append_images=images[:-1])
    print(f"wrote {name}")


def render_mac(pixels: int) -> Image.Image:
    """The app icon in the macOS shape: transparent margins around the rounded square."""
    body = max(1, round(pixels * MAC_BODY))
    inner = render(body, ACCENT, WHITE, glyph_fraction=0.78, corner_fraction=MAC_CORNER)
    image = Image.new("RGBA", (pixels, pixels), CLEAR)
    image.paste(inner, ((pixels - body) // 2, (pixels - body) // 2))
    return image


def save_mac_iconset() -> None:
    MAC_ICONSET.mkdir(parents=True, exist_ok=True)
    images = []
    for points, scale in MAC_SIZES:
        name = f"icon_{points}x{points}{'@2x' if scale == 2 else ''}.png"
        render_mac(points * scale).save(MAC_ICONSET / name)
        images.append({"idiom": "mac", "size": f"{points}x{points}", "scale": f"{scale}x", "filename": name})
    contents = {"images": images, "info": {"author": "xcode", "version": 1}}
    (MAC_ICONSET / "Contents.json").write_text(json.dumps(contents, indent=2) + "\n", encoding="utf-8")
    print("wrote AppIcon.appiconset")


def main() -> None:
    ASSETS.mkdir(parents=True, exist_ok=True)
    save("Osdrop.ico", APP_SIZES, ACCENT, WHITE, glyph_fraction=0.78, corner_fraction=0.22)
    # Tray glyphs fill the icon like the system's own tray icons.
    save("Tray-black.ico", TRAY_SIZES, None, BLACK, glyph_fraction=1.0, corner_fraction=0)
    save("Tray-white.ico", TRAY_SIZES, None, WHITE, glyph_fraction=1.0, corner_fraction=0)
    save("Tray-controlled-black.ico", TRAY_SIZES, BLACK, CLEAR, glyph_fraction=0.9, corner_fraction=0.2)
    save("Tray-controlled-white.ico", TRAY_SIZES, WHITE, CLEAR, glyph_fraction=0.9, corner_fraction=0.2)
    save_mac_iconset()


if __name__ == "__main__":
    main()
