"""Draws the README banner, light and dark, in English and Turkish, into docs/media.

The left side names Osdrop and what it does; the right side shows it: a MacBook and a PC monitor
side by side, their facing edges lit, and the cursor carrying a file from the Mac across the edge
onto the PC. The app icon comes from scripts/windows/make_icons.py, so the two stay in step.

Usage, with the static Inter fonts from https://github.com/rsms/inter/releases (SIL Open Font
License; the ttf files are in extras/ttf):
    uvx --with pillow python scripts/readme/make_banner.py <folder with Inter-*.ttf>
"""

import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/windows"))
import make_icons  # noqa: E402  (the app icon's drawing)

OUT = ROOT / "docs/media"
WIDTH, HEIGHT = 960, 320  # layout units; the file is twice that for sharp text on HiDPI screens
OUTPUT_SCALE = 2
SUPERSAMPLE = 2  # drawn larger again, then scaled down, so lines stay smooth
SCALE = OUTPUT_SCALE * SUPERSAMPLE

THEMES = {
    "light": {
        "background": (244, 247, 245),
        "border": (225, 232, 228),
        "text": (20, 33, 27),
        "secondary": (88, 104, 96),
        "accent": (30, 142, 90),
        "device": (128, 142, 135),
        "screen": (255, 255, 255),
        "cursor": (20, 24, 22),
        "cursor_edge": (255, 255, 255),
    },
    "dark": {
        "background": (21, 27, 24),
        "border": (38, 48, 43),
        "text": (238, 243, 240),
        "secondary": (155, 170, 162),
        "accent": (60, 196, 135),
        "device": (100, 114, 107),
        "screen": (14, 20, 17),
        "cursor": (245, 248, 246),
        "cursor_edge": (14, 20, 17),
    },
}

TEXT = {
    "en": {
        "tagline": ["One keyboard and mouse", "for your Macs and PCs."],
        "features": [
            ("cursor", "Cursor across screens"),
            ("file", "Drag files and folders"),
            ("clipboard", "Shared clipboard"),
            ("swap", "Mac and Windows"),
            ("network", "Local network only"),
            ("lock", "End-to-end encrypted"),
        ],
    },
    "tr": {
        "tagline": ["Mac’lerin ve PC’lerin için", "tek klavye ve fare."],
        "features": [
            ("cursor", "Ekrandan ekrana imleç"),
            ("file", "Dosya ve klasör sürükle"),
            ("clipboard", "Ortak pano"),
            ("swap", "Mac ve Windows"),
            ("network", "Yalnız yerel ağ"),
            ("lock", "Uçtan uca şifreli"),
        ],
    },
}


def s(value: float) -> int:
    return round(value * SCALE)


def box(x0: float, y0: float, x1: float, y1: float) -> tuple[int, int, int, int]:
    return s(x0), s(y0), s(x1), s(y1)


class Banner:
    def __init__(self, fonts: Path, theme: dict, text: dict) -> None:
        self.theme = theme
        self.text = text
        self.image = Image.new("RGBA", (s(WIDTH), s(HEIGHT)), (0, 0, 0, 0))
        self.draw = ImageDraw.Draw(self.image)
        self.bold = ImageFont.truetype(str(fonts / "Inter-Bold.ttf"), s(54))
        self.tagline = ImageFont.truetype(str(fonts / "Inter-SemiBold.ttf"), s(25))
        self.feature = ImageFont.truetype(str(fonts / "Inter-Regular.ttf"), s(15.5))
        self.label = ImageFont.truetype(str(fonts / "Inter-Medium.ttf"), s(14))

    def render(self) -> Image.Image:
        t = self.theme
        self.draw.rounded_rectangle(
            box(0, 0, WIDTH - 1, HEIGHT - 1), radius=s(28), fill=t["background"], outline=t["border"],
            width=s(1.5))
        self.identity()
        self.features()
        self.scene()
        return self.image.resize((WIDTH * OUTPUT_SCALE, HEIGHT * OUTPUT_SCALE), Image.LANCZOS)

    # --- Left: name, tagline, features --------------------------------------------------------

    def identity(self) -> None:
        icon = make_icons.render(s(64), make_icons.ACCENT, make_icons.WHITE, glyph_fraction=0.78,
                                 corner_fraction=0.22)
        self.image.alpha_composite(icon, (s(56), s(50)))
        self.draw.text((s(138), s(82)), "Osdrop", font=self.bold, fill=self.theme["text"], anchor="lm")
        for index, line in enumerate(self.text["tagline"]):
            self.draw.text((s(56), s(148 + index * 32)), line, font=self.tagline, fill=self.theme["text"],
                           anchor="lm")

    def features(self) -> None:
        """Two columns of three, each column as wide as its longest label."""
        features = self.text["features"]
        first = max(self.draw.textlength(label, font=self.feature) for _, label in features[0::2]) / SCALE
        columns = [56, 56 + 25 + first + 30]
        rows = [228, 256, 284]
        for index, (icon, label) in enumerate(features):
            x, y = columns[index % 2], rows[index // 2]
            self.feature_icon(icon, x, y)
            self.draw.text((s(x + 25), s(y)), label, font=self.feature, fill=self.theme["secondary"],
                           anchor="lm")

    def feature_icon(self, kind: str, x: float, y: float) -> None:
        """A 16-unit line icon centred on (x + 8, y)."""
        colour = self.theme["secondary"]
        width = s(1.6)
        d = self.draw
        top = y - 8
        if kind == "cursor":
            self.cursor(x + 3, top, 0.78, colour, None)
        elif kind == "file":
            d.line([(s(x + 3), s(top)), (s(x + 10), s(top)), (s(x + 14), s(top + 4)), (s(x + 14), s(top + 16)),
                    (s(x + 3), s(top + 16)), (s(x + 3), s(top))], fill=colour, width=width, joint="curve")
            d.line([(s(x + 10), s(top)), (s(x + 10), s(top + 4)), (s(x + 14), s(top + 4))], fill=colour,
                   width=width, joint="curve")
        elif kind == "clipboard":
            d.rounded_rectangle(box(x + 2, top + 2, x + 14, top + 16), radius=s(2), outline=colour, width=width)
            d.rounded_rectangle(box(x + 5, top, x + 11, top + 4), radius=s(1.2), outline=colour, width=width,
                                fill=self.theme["background"])
            for line_y in (top + 8, top + 11.5):
                d.line([(s(x + 5), s(line_y)), (s(x + 11), s(line_y))], fill=colour, width=width)
        elif kind == "swap":
            d.line([(s(x + 2), s(top + 5)), (s(x + 14), s(top + 5))], fill=colour, width=width)
            d.line([(s(x + 10), s(top + 1.5)), (s(x + 14), s(top + 5)), (s(x + 10), s(top + 8.5))], fill=colour,
                   width=width, joint="curve")
            d.line([(s(x + 14), s(top + 12)), (s(x + 2), s(top + 12))], fill=colour, width=width)
            d.line([(s(x + 6), s(top + 8.5)), (s(x + 2), s(top + 12)), (s(x + 6), s(top + 15.5))], fill=colour,
                   width=width, joint="curve")
        elif kind == "network":
            centres = [(x + 8, top + 3), (x + 3, top + 13), (x + 13, top + 13)]
            d.line([(s(cx), s(cy)) for cx, cy in centres + centres[:1]], fill=colour, width=width)
            for cx, cy in centres:
                d.ellipse(box(cx - 2.6, cy - 2.6, cx + 2.6, cy + 2.6), fill=self.theme["background"], outline=colour,
                          width=width)
        elif kind == "lock":
            d.rounded_rectangle(box(x + 2.5, top + 7, x + 13.5, top + 16), radius=s(2), outline=colour, width=width)
            d.arc(box(x + 4.5, top + 1, x + 11.5, top + 10), start=180, end=360, fill=colour, width=width)
            d.line([(s(x + 4.5), s(top + 5.5)), (s(x + 4.5), s(top + 7))], fill=colour, width=width)
            d.line([(s(x + 11.5), s(top + 5.5)), (s(x + 11.5), s(top + 7))], fill=colour, width=width)

    # --- Right: a Mac and a PC, a file crossing the edge between them -------------------------

    def scene(self) -> None:
        t = self.theme
        d = self.draw
        stroke = s(2.5)
        mac = (612, 118, 772, 222)  # the MacBook's screen
        pc = (790, 78, 922, 190)  # the PC monitor's screen

        # MacBook: screen and base.
        d.rounded_rectangle(box(*mac), radius=s(9), fill=t["screen"], outline=t["device"], width=stroke)
        d.polygon([(s(596), s(226)), (s(788), s(226)), (s(780), s(236)), (s(604), s(236))], fill=t["device"])
        d.rounded_rectangle(box(596, 222, 788, 228), radius=s(2), fill=t["device"])
        # PC monitor: screen, stand and foot.
        d.rounded_rectangle(box(*pc), radius=s(8), fill=t["screen"], outline=t["device"], width=stroke)
        d.rounded_rectangle(box(850, 190, 862, 220), radius=s(2), fill=t["device"])
        d.rounded_rectangle(box(822, 218, 890, 224), radius=s(3), fill=t["device"])

        # The facing edges, where the cursor crosses.
        top, bottom = 128, 182
        d.rounded_rectangle(box(mac[2] - 2.5, top, mac[2] + 1.5, bottom), radius=s(2), fill=t["accent"])
        d.rounded_rectangle(box(pc[0] - 1.5, top, pc[0] + 2.5, bottom), radius=s(2), fill=t["accent"])

        # Where the file was: a dashed outline on the Mac.
        self.file_card(650, 154, t["device"], dashed=True)
        # The way it went: dots arching through the lit edges.
        self.dotted_arc((664, 152), (770, 138), (829, 136), t["accent"])
        # The file on its way, under the cursor on the PC.
        self.file_card(840, 146, t["accent"], dashed=False)
        self.cursor(830, 134, 1.25, t["cursor"], t["cursor_edge"])

        for (x0, _, x1, _), y, name in ((mac, 254, "Mac"), (pc, 254, "PC")):
            d.text((s((x0 + x1) / 2), s(y)), name, font=self.label, fill=t["secondary"], anchor="mm")

    def file_card(self, x: float, y: float, colour, dashed: bool) -> None:
        """A document 24 by 30 units, its corner folded, top left at (x, y)."""
        w, h, fold = 24, 30, 7
        outline = [(x, y), (x + w - fold, y), (x + w, y + fold), (x + w, y + h), (x, y + h)]
        d = self.draw
        if dashed:
            points = outline + outline[:1]
            for (ax, ay), (bx, by) in zip(points, points[1:]):
                length = math.hypot(bx - ax, by - ay)
                steps = max(1, int(length // 4))
                for i in range(0, steps, 2):
                    t0, t1 = i / steps, min(1, (i + 1) / steps)
                    d.line([(s(ax + (bx - ax) * t0), s(ay + (by - ay) * t0)),
                            (s(ax + (bx - ax) * t1), s(ay + (by - ay) * t1))], fill=colour, width=s(1.6))
            return
        d.polygon([(s(px), s(py)) for px, py in outline], fill=colour)
        d.polygon([(s(x + w - fold), s(y)), (s(x + w - fold), s(y + fold)), (s(x + w), s(y + fold))],
                  fill=self.theme["screen"])
        for line_y, line_w in ((y + 14, 14), (y + 19, 14), (y + 24, 9)):
            d.rounded_rectangle(box(x + 5, line_y - 1, x + 5 + line_w, line_y + 1), radius=s(1),
                                fill=self.theme["screen"])

    def dotted_arc(self, start, control, end, colour) -> None:
        """Dots along a quadratic curve, evenly spaced by length."""
        samples = []
        for i in range(201):
            t = i / 200
            x = (1 - t) ** 2 * start[0] + 2 * (1 - t) * t * control[0] + t * t * end[0]
            y = (1 - t) ** 2 * start[1] + 2 * (1 - t) * t * control[1] + t * t * end[1]
            samples.append((x, y))
        spacing, travelled, next_dot = 7.0, 0.0, 0.0
        for (ax, ay), (bx, by) in zip(samples, samples[1:]):
            travelled += math.hypot(bx - ax, by - ay)
            if travelled >= next_dot:
                self.draw.ellipse(box(bx - 1.6, by - 1.6, bx + 1.6, by + 1.6), fill=colour)
                next_dot += spacing

    def cursor(self, x: float, y: float, size: float, fill, edge) -> None:
        """The arrow cursor, its tip at (x, y), `size` times 17 units tall."""
        shape = [(0, 0), (0, 17), (4.2, 13.2), (7.2, 20.0), (10.2, 18.7), (7.3, 12.0), (12.8, 11.8)]
        points = [(s(x + px * size), s(y + py * size)) for px, py in shape]
        if edge is None:
            self.draw.line(points + points[:1], fill=fill, width=s(1.5), joint="curve")
            return
        self.draw.polygon(points, fill=fill)
        self.draw.line(points + points[:1], fill=edge, width=s(1.6), joint="curve")


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    fonts = Path(sys.argv[1])
    OUT.mkdir(parents=True, exist_ok=True)
    for language, text in TEXT.items():
        for name, theme in THEMES.items():
            suffix = "" if language == "en" else f".{language}"
            path = OUT / f"banner-{name}{suffix}.png"
            Banner(fonts, theme, text).render().save(path, optimize=True)
            print(f"wrote {path.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
