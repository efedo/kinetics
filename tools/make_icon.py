"""Draws the Kinetics icon: a saturation curve (Michaelis-Menten) rising to a
dashed Vmax line, with a few measured points, on a rounded blue-to-teal tile.

Writes, from one set of shapes:
  web/icon.svg                     the README logo, and the favicon:
  web/shell.html                   its <link rel="icon">, the SVG inlined
  source/remake/app/kinetics.ico   the Windows program icon (16 to 256 pixels)

Run from the repository root:  python tools/make_icon.py [--preview DIR]
Needs Pillow.
"""

import argparse
import math
import pathlib

from PIL import Image, ImageDraw

SIZE = 512                      # the design's coordinate space
TOP, BOTTOM = "#3b82f6", "#0d9488"  # gradient, top left to bottom right
X0, X1, Y0, VMAX_Y = 136, 404, 392, 156  # axes: origin (X0, Y0), plateau line

def curve_y(s):
    return Y0 - 236.0 * s / (1.4 + s)

def curve_points(n=48):
    pts = []
    for i in range(n + 1):
        s = 10.0 * i / n
        pts.append((X0 + s * (X1 - X0) / 10.0, curve_y(s)))
    return pts

# Measured points: concentration and a small scatter about the curve.
DOTS = [(0.55, 20.0), (1.6, -22.0), (3.7, 20.0), (7.4, -18.0)]
DOT_R, HALO = 20, 9  # each point is ringed in the tile colour, apart from the curve
CURVE_W, AXIS_W, DASH_W = 28, 16, 11


def dot_centres():
    return [(X0 + s * (X1 - X0) / 10.0, curve_y(s) + dy) for s, dy in DOTS]


def svg():
    path = " ".join(f"{'M' if i == 0 else 'L'}{x:.1f} {y:.1f}" for i, (x, y) in enumerate(curve_points()))
    dots = "".join(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{DOT_R + HALO / 2}"/>' for x, y in dot_centres())
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {SIZE} {SIZE}">'
        f'<defs><linearGradient id="g" gradientUnits="userSpaceOnUse" x1="24" y1="24" x2="488" y2="488">'
        f'<stop offset="0" stop-color="{TOP}"/><stop offset="1" stop-color="{BOTTOM}"/></linearGradient></defs>'
        f'<rect x="24" y="24" width="464" height="464" rx="104" fill="url(#g)"/>'
        f'<path d="M{X0} 120V{Y0}H{X1}" fill="none" stroke="#fff" stroke-opacity=".45" stroke-width="{AXIS_W}" '
        f'stroke-linecap="round" stroke-linejoin="round"/>'
        f'<path d="M{X0} {VMAX_Y}H{X1}" stroke="#fff" stroke-opacity=".5" stroke-width="{DASH_W}" '
        f'stroke-dasharray="22 18"/>'
        f'<path d="{path}" fill="none" stroke="#fff" stroke-width="{CURVE_W}" stroke-linecap="round" '
        f'stroke-linejoin="round"/>'
        f'<g fill="#fff" stroke="url(#g)" stroke-width="{HALO}">{dots}</g></svg>\n'
    )


def hex_rgb(h):
    return tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))


def raster(px):
    """The icon at px pixels, drawn four times larger and reduced."""
    k = 4 * px / SIZE
    big = 4 * px
    a, b = hex_rgb(TOP), hex_rgb(BOTTOM)
    grad = Image.new("RGB", (big, big))
    gp = grad.load()
    for y in range(big):
        for x in range(big):
            t = (x + y) / (2.0 * (big - 1))
            gp[x, y] = tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))
    mask = Image.new("L", (big, big), 0)
    ImageDraw.Draw(mask).rounded_rectangle([24 * k, 24 * k, 488 * k, 488 * k], radius=104 * k, fill=255)
    img = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    img.paste(grad, (0, 0), mask)

    over = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    d = ImageDraw.Draw(over)
    faint = (255, 255, 255, 115)
    axis = [(X0 * k, 120 * k), (X0 * k, Y0 * k), (X1 * k, Y0 * k)]
    d.line(axis, fill=faint, width=round(AXIS_W * k), joint="curve")
    for x, y in (axis[0], axis[-1]):
        r = AXIS_W * k / 2
        d.ellipse([x - r, y - r, x + r, y + r], fill=faint)
    x = X0
    while x < X1:
        d.line([(x * k, VMAX_Y * k), (min(x + 22, X1) * k, VMAX_Y * k)], fill=(255, 255, 255, 128),
               width=round(DASH_W * k))
        x += 40
    img = Image.alpha_composite(img, over)

    d = ImageDraw.Draw(img)
    # The curve as a run of round stamps, which stays smooth where a thick
    # polyline would show its joints.
    r = CURVE_W * k / 2
    for x, y in curve_points(400):
        d.ellipse([x * k - r, y * k - r, x * k + r, y * k + r], fill="white")
    # Small sizes keep only the curve: the dots would blur into it.
    if px >= 32:
        for x, y in dot_centres():
            t = (x + y - 48) / (2.0 * 464)  # the tile's colour here
            ring = tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))
            rr = (DOT_R + HALO) * k
            d.ellipse([x * k - rr, y * k - rr, x * k + rr, y * k + rr], fill=ring)
            rr = DOT_R * k
            d.ellipse([x * k - rr, y * k - rr, x * k + rr, y * k + rr], fill="white")
    return img.resize((px, px), Image.LANCZOS)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", help="also write PNG previews to this folder")
    args = ap.parse_args()
    root = pathlib.Path(__file__).resolve().parent.parent
    (root / "web" / "icon.svg").write_text(svg(), encoding="utf-8", newline="\n")
    # The favicon, inlined in the page so that it needs no file of its own.
    shell = root / "web" / "shell.html"
    uri = svg().strip().replace('"', "'").replace("#", "%23").replace("<", "%3C").replace(">", "%3E")
    link = f'<link rel="icon" type="image/svg+xml" href="data:image/svg+xml,{uri}">'
    lines = shell.read_text(encoding="utf-8").splitlines()
    lines = [ln for ln in lines if not ln.startswith('<link rel="icon"')]
    at = next(i for i, ln in enumerate(lines) if ln.startswith("<title>")) + 1
    lines.insert(at, link)
    shell.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    sizes = [16, 24, 32, 48, 64, 128, 256]
    images = [raster(s) for s in sizes]
    images[-1].save(root / "source" / "remake" / "app" / "kinetics.ico", sizes=[(s, s) for s in sizes],
                    append_images=images[:-1])
    if args.preview:
        out = pathlib.Path(args.preview)
        out.mkdir(parents=True, exist_ok=True)
        for s, im in zip(sizes, images):
            im.save(out / f"icon-{s}.png")
        raster(512).save(out / "icon-512.png")
    print("Wrote web/icon.svg, the favicon in web/shell.html and source/remake/app/kinetics.ico")


if __name__ == "__main__":
    main()
