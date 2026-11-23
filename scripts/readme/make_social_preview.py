"""Draws docs/media/social-preview.png, the card GitHub shows for links to the repository.

It is the README banner laid out for GitHub's 2:1 card: the app icon, the name and the tagline on
the left, and the banner's own Mac and PC on the right, cut from a fresh rendering so the two stay
in step. Upload it in the repository's Settings > General > Social preview.

Usage, with the static Inter fonts (see make_banner.py):
    uvx --with pillow python scripts/readme/make_social_preview.py <folder with Inter-*.ttf>
"""

import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parent))
import make_banner  # noqa: E402  (the banner's drawing, colours and text)

WIDTH, HEIGHT = 1280, 640  # GitHub's size for social previews; the file must stay under 1 MB
MARGIN = 88
GAP = 56  # between the text and the scene
SCENE = (592, 66, 932, 270)  # the banner's Mac and PC with their labels, in its layout units
FEATURES = "Cursor  ·  Files  ·  Clipboard"


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    fonts = Path(sys.argv[1])
    theme, text = make_banner.THEMES["light"], make_banner.TEXT["en"]

    card = Image.new("RGBA", (WIDTH, HEIGHT), theme["background"] + (255,))
    draw = ImageDraw.Draw(card)
    name = ImageFont.truetype(str(fonts / "Inter-Bold.ttf"), 92)
    tagline = ImageFont.truetype(str(fonts / "Inter-SemiBold.ttf"), 44)
    features = ImageFont.truetype(str(fonts / "Inter-Regular.ttf"), 27)

    icon_size = 104
    top = 150
    icon = make_banner.make_icons.render(icon_size, make_banner.make_icons.ACCENT,
                                         make_banner.make_icons.WHITE, glyph_fraction=0.78,
                                         corner_fraction=0.22)
    card.alpha_composite(icon, (MARGIN, top))
    name_x = MARGIN + icon_size + 28
    draw.text((name_x, top + icon_size / 2), "Osdrop", font=name, fill=theme["text"], anchor="lm")
    y = top + icon_size + 70
    for line in text["tagline"]:
        draw.text((MARGIN, y), line, font=tagline, fill=theme["text"], anchor="lm")
        y += 58
    draw.text((MARGIN, y + 22), FEATURES, font=features, fill=theme["secondary"], anchor="lm")

    # The text's right edge decides how large the scene can be.
    right = max([name_x + draw.textlength("Osdrop", font=name),
                 MARGIN + draw.textlength(FEATURES, font=features)] +
                [MARGIN + draw.textlength(line, font=tagline) for line in text["tagline"]])
    banner = make_banner.Banner(fonts, theme, text).render()
    scene = banner.crop(tuple(value * make_banner.OUTPUT_SCALE for value in SCENE))
    room = WIDTH - MARGIN / 2 - (right + GAP)
    scale = min(1.0, room / scene.width)
    scene = scene.resize((round(scene.width * scale), round(scene.height * scale)), Image.LANCZOS)
    card.alpha_composite(scene, (round(WIDTH - MARGIN / 2 - scene.width),
                                 round((HEIGHT - scene.height) / 2)))

    out = make_banner.OUT / "social-preview.png"
    card.convert("RGB").save(out, optimize=True)
    print(f"wrote {out.relative_to(make_banner.ROOT)}, {out.stat().st_size // 1024} KB")


if __name__ == "__main__":
    main()
