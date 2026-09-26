"""Draws the Massgate banner the game and launcher show (share/www-root/massgatebutton).

Two battle-scarred Cold War flags (US and Soviet), drawn here, fade into each other over a strip
of a World in Conflict wallpaper (wic_wallpaper_strip.png, cropped from Massive Entertainment's
2006 wallpaper; the game and its art are (c) Massive Entertainment AB). Everything is tinted into
massgate.org's palette, with a tactical grid and the text on top. Deterministic (fixed seed), so
the output only changes when this script or the strip does.

    python tools/banner/make_banner.py                    # writes the TGAs and banner.png
    python tools/banner/make_banner.py --no-background    # flags only, no wallpaper

Needs Pillow and numpy, and two fonts under the SIL Open Font License, looked up in
tools/banner/fonts and the Windows font folders:
    Orbitron Black      (Orbitron-Black.ttf, https://fonts.google.com/specimen/Orbitron)
    Share Tech Mono     (ShareTechMono-Regular.ttf, https://fonts.google.com/specimen/Share+Tech+Mono)
"""
import argparse
import math
import os
import struct

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT_DIR = os.path.join(REPO, 'share', 'www-root', 'massgatebutton')
LANGUAGES = ('V0', 'EN')

WIDTH, HEIGHT = 956, 100	# what the game shows
SCALE = 2					# drawn at twice the size, then downsampled

# massgate.org's palette (styles/styles.css)
ORANGE = (255, 127, 0)		# headings and links
GREY = (184, 202, 204)		# body text
TEAL_BLACK = (0, 23, 26)	# page and borders
PANEL = (50, 71, 71)		# box headers

HEADLINE = 'MASSGATE-OFFLINE'
SIDE_LINES = (('RANKED-BOT MATCHES', GREY), ('WIC FOREVER!', ORANGE))
HEADLINE_FONT = 'Orbitron-Black.ttf'
SIDE_FONT = 'ShareTechMono-Regular.ttf'
WALLPAPER_STRIP = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'wic_wallpaper_strip.png')


def find_font(name):
	folders = [os.path.join(os.path.dirname(__file__), 'fonts')]
	if os.environ.get('LOCALAPPDATA'):
		folders.append(os.path.join(os.environ['LOCALAPPDATA'], 'Microsoft', 'Windows', 'Fonts'))
	folders.append(os.path.join(os.environ.get('WINDIR', r'C:\Windows'), 'Fonts'))
	for folder in folders:
		path = os.path.join(folder, name)
		if os.path.exists(path):
			return path
	raise SystemExit(f'Font {name} not found in {", ".join(folders)} (see the top of this script).')


def star(cx, cy, outer, inner, rotation=-90.0):
	points = []
	for i in range(10):
		r = outer if i % 2 == 0 else inner
		a = math.radians(rotation + i * 36)
		points.append((cx + r * math.cos(a), cy + r * math.sin(a)))
	return points


def us_flag(h):
	w = int(h * 1.9)
	img = Image.new('RGB', (w, h))
	d = ImageDraw.Draw(img)
	stripe = h / 13
	for i in range(13):
		d.rectangle([0, i * stripe, w, (i + 1) * stripe], fill=(178, 34, 52) if i % 2 == 0 else (238, 236, 228))
	cw, ch = w * 0.4, stripe * 7
	d.rectangle([0, 0, cw, ch], fill=(50, 52, 104))
	for row in range(9):
		count = 6 if row % 2 == 0 else 5
		for col in range(count):
			x = cw * ((col * 2 + (1 if row % 2 == 0 else 2)) / 12)
			y = ch * ((row + 1) / 10)
			d.polygon(star(x, y, ch * 0.031, ch * 0.012), fill=(238, 236, 228))
	return img


def soviet_flag(h):
	w = int(h * 2)
	img = Image.new('RGB', (w, h), (204, 8, 8))
	d = ImageDraw.Draw(img)
	gold = (250, 205, 40)
	# Star outline above the hammer and sickle, in the upper hoist.
	sx, sy = h * 0.36, h * 0.12
	d.polygon(star(sx, sy, h * 0.075, h * 0.03), outline=gold, width=max(2, int(h * 0.012)))
	# Sickle: a crescent (a disc minus an offset disc) with a short handle.
	cx, cy, r = h * 0.36, h * 0.34, h * 0.14
	d.pieslice([cx - r, cy - r, cx + r, cy + r], 150, 390, fill=gold)
	r2 = r * 0.8
	d.ellipse([cx - r2 + r * 0.12, cy - r2 - r * 0.12, cx + r2 + r * 0.12, cy + r2 - r * 0.12], fill=(204, 8, 8))
	d.line([(cx - r * 0.72, cy + r * 0.72), (cx - r * 1.05, cy + r * 1.05)], fill=gold, width=int(h * 0.035))
	# Hammer: a diagonal handle and a head across its top.
	d.line([(cx - r * 0.9, cy + r * 0.95), (cx + r * 0.55, cy - r * 0.5)], fill=gold, width=int(h * 0.03))
	hx, hy = cx + r * 0.55, cy - r * 0.5
	a, b = h * 0.075, h * 0.03
	d.polygon([(hx - a * 0.7 + b, hy - a * 0.7 - b), (hx + a * 0.7 + b, hy + a * 0.7 - b),
		(hx + a * 0.7 - b, hy + a * 0.7 + b), (hx - a * 0.7 - b, hy - a * 0.7 + b)], fill=gold)
	return img


def fractal_noise(rng, w, h, octaves=5, base=6):
	# Sum of upsampled random grids: smooth, cloudy noise in 0..1.
	field = np.zeros((h, w), np.float32)
	amplitude, total = 1.0, 0.0
	for octave in range(octaves):
		cells = base * 2 ** octave
		grid = rng.random((max(2, cells * h // w), cells)).astype(np.float32)
		layer = np.asarray(Image.fromarray(grid).resize((w, h), Image.BICUBIC), np.float32)
		field += layer * amplitude
		total += amplitude
		amplitude *= 0.5
	field /= total
	field -= field.min()
	return field / max(field.max(), 1e-6)


def cloth(img, rng, amplitude, period):
	# Folds: shift each column up or down along a wave, and shade the slopes.
	a = np.asarray(img, np.float32)
	h, w, _ = a.shape
	x = np.arange(w)
	phase = rng.random() * math.tau
	shift = (amplitude * np.sin(x / period * math.tau + phase) + amplitude * 0.5 * np.sin(x / (period * 0.37) * math.tau)).astype(int)
	out = np.empty_like(a)
	for col in range(w):
		out[:, col] = np.roll(a[:, col], shift[col], axis=0)
	shade = 0.72 + 0.28 * np.cos(x / period * math.tau + phase)
	out *= shade[None, :, None]
	return out


def battle_damage(a, rng):
	# Burn holes with charred edges and a faint ember glow, soot, and bullet holes. Returns the
	# colours and an opacity mask: the holes are see-through.
	h, w, _ = a.shape
	burn = fractal_noise(rng, w, h, octaves=6, base=5)
	soot = fractal_noise(rng, w, h, octaves=4, base=3)
	char = np.clip((burn - 0.55) / 0.15, 0, 1)
	ember = np.exp(-((burn - 0.69) / 0.012) ** 2)
	a = a * (1 - 0.85 * char[..., None])
	a = a * (1 - 0.55 * soot[..., None])
	a += ember[..., None] * np.array([120, 50, 0], np.float32) * 0.6
	alpha = np.clip((0.71 - burn) / 0.01, 0, 1)
	img = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))
	mask = Image.fromarray((alpha * 255).astype(np.uint8))
	d, m = ImageDraw.Draw(img), ImageDraw.Draw(mask)
	for _ in range(int(w * h / 9000)):
		x, y = rng.random() * w, rng.random() * h
		r = 1.5 + rng.random() * 3.5
		d.ellipse([x - r * 1.8, y - r * 1.8, x + r * 1.8, y + r * 1.8], fill=(40, 30, 25))
		m.ellipse([x - r, y - r, x + r, y + r], fill=0)
	return np.asarray(img, np.float32), np.asarray(mask, np.float32) / 255


def place(layer, alpha, w, h, x_offset, y_offset):
	# Puts a flag (colours and opacity) onto a banner-sized canvas; uncovered parts stay clear.
	rgb = np.zeros((h, w, 3), np.float32)
	opacity = np.zeros((h, w), np.float32)
	src_x = max(0, -x_offset)
	dst_x = max(0, x_offset)
	span = min(w - dst_x, layer.shape[1] - src_x)
	rgb[:, dst_x:dst_x + span] = layer[y_offset:y_offset + h, src_x:src_x + span]
	opacity[:, dst_x:dst_x + span] = alpha[y_offset:y_offset + h, src_x:src_x + span]
	return rgb, opacity


def wallpaper(path, box, w, h):
	# The optional picture behind the flags, cropped (box in source pixels, or the middle strip)
	# and scaled to cover the banner.
	src = Image.open(path).convert('RGB')
	if box:
		src = src.crop(box)
	scale = max(w / src.width, h / src.height)
	src = src.resize((max(w, round(src.width * scale)), max(h, round(src.height * scale))), Image.LANCZOS)
	left, top = (src.width - w) // 2, (src.height - h) // 2
	return np.asarray(src.crop((left, top, left + w, top + h)).filter(ImageFilter.GaussianBlur(SCALE)), np.float32)


def background(rng, w, h, backdrop=None):
	# US flag behind the headline (left), Soviet flag on the right with its hammer and sickle in
	# the gap before the side text. Each fades out towards the middle along a smoky edge, so they
	# blend instead of meeting in a line. Burn holes and the fade show the backdrop: a picture
	# (--background) or massgate.org's near-black teal.
	us_height = int(h * 3.4)
	us, us_alpha = battle_damage(cloth(us_flag(us_height), rng, h * 0.05, us_height * 0.5), rng)
	su_height = int(h * 2.0)
	su, su_alpha = battle_damage(cloth(soviet_flag(su_height), rng, h * 0.05, su_height * 0.6), rng)
	# Across the bottom of the US canton (stars and stripes); the top of the Soviet flag (emblem).
	left, left_alpha = place(us, us_alpha, w, h, 0, int(us_height * 0.36))
	emblem_x = int(su_height * 0.36)
	right, right_alpha = place(su, su_alpha, w, h, int(w * 0.70) - emblem_x, int(h * 0.1))

	x = np.linspace(0, 1, w)[None, :]
	smoke = fractal_noise(rng, w, h, octaves=5, base=4)
	edge = (smoke - 0.5) * 0.16
	fade_out = np.clip((0.64 - x + edge) / 0.12, 0, 1)		# US: gone by about 64%
	fade_in = np.clip((x - 0.56 + edge) / 0.10, 0, 1)		# Soviet: from about 56%
	def soften(mask):
		img = Image.fromarray((np.clip(mask, 0, 1) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(4 * SCALE))
		return np.asarray(img, np.float32) / 255
	left_alpha = soften(left_alpha * fade_out)
	right_alpha = soften(right_alpha * fade_in)

	def blur(a, radius):
		return np.asarray(Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(radius)), np.float32)
	left, right = blur(left, 3 * SCALE), blur(right, 3 * SCALE)

	if backdrop is not None:
		base = backdrop
		flag_opacity, brightness, band_depth = 0.45, 0.95, 0.2
	else:
		base = np.zeros((h, w, 3), np.float32)
		base[:] = TEAL_BLACK
		flag_opacity, brightness, band_depth = 1.0, 0.82, 0.25
	f = base
	f = f * (1 - left_alpha[..., None] * flag_opacity) + left * left_alpha[..., None] * flag_opacity
	f = f * (1 - right_alpha[..., None] * flag_opacity) + right * right_alpha[..., None] * flag_opacity

	# Muted and pulled towards massgate.org's colours.
	grey = f.mean(axis=2, keepdims=True)
	f = grey + (f - grey) * 0.8
	f = f * brightness
	f = f * 0.85 + np.array(TEAL_BLACK, np.float32) * 0.15
	f *= (0.8 + 0.2 * smoke)[..., None]
	# Vignette, plus a darker band behind the text so it reads.
	y = np.linspace(-1, 1, h)[:, None]
	vignette = 1 - 0.3 * y ** 2
	band = 1 - band_depth * np.exp(-((x - 0.3) / 0.28) ** 4) - (band_depth + 0.05) * np.exp(-((x - 0.93) / 0.1) ** 4)
	f *= (vignette * band)[..., None]
	return f


def grid(img):
	d = ImageDraw.Draw(img, 'RGBA')
	w, h = img.size
	step = 24 * SCALE
	for x in range(0, w, step):
		d.line([(x, 0), (x, h)], fill=PANEL + (70,), width=1)
	for y in range(0, h, step):
		d.line([(0, y), (w, y)], fill=PANEL + (70,), width=1)
	for y in range(0, h, 2 * SCALE):
		d.line([(0, y), (w, y)], fill=(0, 0, 0, 22), width=SCALE)
	# Frame and corner brackets.
	d.rectangle([0, 0, w - 1, h - 1], outline=PANEL + (255,), width=SCALE)
	c, t = 14 * SCALE, 2 * SCALE
	for (x, y, sx, sy) in ((0, 0, 1, 1), (w - 1, 0, -1, 1), (0, h - 1, 1, -1), (w - 1, h - 1, -1, -1)):
		d.line([(x, y), (x + sx * c, y)], fill=ORANGE + (230,), width=t)
		d.line([(x, y), (x, y + sy * c)], fill=ORANGE + (230,), width=t)


def text_with_shadow(img, position, text, font, fill, anchor, glow=None):
	shadow = Image.new('RGBA', img.size, (0, 0, 0, 0))
	sd = ImageDraw.Draw(shadow)
	sd.text((position[0] + 2 * SCALE, position[1] + 2 * SCALE), text, font=font, fill=(0, 0, 0, 230), anchor=anchor)
	img.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(3 * SCALE)))
	if glow:
		halo = Image.new('RGBA', img.size, (0, 0, 0, 0))
		ImageDraw.Draw(halo).text(position, text, font=font, fill=glow, anchor=anchor)
		img.alpha_composite(halo.filter(ImageFilter.GaussianBlur(6 * SCALE)))
	ImageDraw.Draw(img).text(position, text, font=font, fill=fill + (255,), anchor=anchor)


def render(backdrop_path=None, backdrop_box=None):
	rng = np.random.default_rng(1983)
	w, h = WIDTH * SCALE, HEIGHT * SCALE
	backdrop = wallpaper(backdrop_path, backdrop_box, w, h) if backdrop_path else None
	img = Image.fromarray(np.clip(background(rng, w, h, backdrop), 0, 255).astype(np.uint8)).convert('RGBA')
	grid(img)

	margin = 26 * SCALE
	# Headline, left, as wide as fits beside the side text.
	size = 60 * SCALE
	font = ImageFont.truetype(find_font(HEADLINE_FONT), size)
	measure = ImageDraw.Draw(img)
	while measure.textlength(HEADLINE, font=font) > w * 0.58:
		size -= SCALE
		font = ImageFont.truetype(find_font(HEADLINE_FONT), size)
	text_with_shadow(img, (margin, h // 2), HEADLINE, font, ORANGE, 'lm', glow=ORANGE + (90,))

	# Side lines, right-aligned.
	side = ImageFont.truetype(find_font(SIDE_FONT), 19 * SCALE)
	gap = 25 * SCALE
	top = h // 2 - gap * (len(SIDE_LINES) - 1) / 2
	for i, (line, colour) in enumerate(SIDE_LINES):
		text_with_shadow(img, (w - margin, top + i * gap), line, side, colour, 'rm')

	# Fully opaque: the overlays above are drawn with alpha, which Pillow also writes into the image.
	return img.resize((WIDTH, HEIGHT), Image.LANCZOS).convert('RGB')


def write_tga(img, path):
	# Uncompressed 32-bit TGA, rows bottom-up (the classic layout old loaders expect).
	rgba = img.convert('RGBA')
	w, h = rgba.size
	header = struct.pack('<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, w, h, 32, 0x08)
	px = rgba.tobytes('raw', 'BGRA')
	rows = [px[y * w * 4:(y + 1) * w * 4] for y in range(h)]
	with open(path, 'wb') as f:
		f.write(header + b''.join(reversed(rows)))


def main():
	parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
	parser.add_argument('--preview', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), 'banner.png'),
		help='PNG preview to write (default: tools/banner/banner.png)')
	parser.add_argument('--preview-only', action='store_true', help='do not write the TGA files')
	parser.add_argument('--background', default=WALLPAPER_STRIP,
		help='picture to show behind the flags (default: the wallpaper strip next to this script)')
	parser.add_argument('--background-box', help='crop of that picture in its pixels: left,top,right,bottom')
	parser.add_argument('--no-background', action='store_true', help='flags only, over the dark teal of massgate.org')
	args = parser.parse_args()

	box = tuple(int(v) for v in args.background_box.split(',')) if args.background_box else None
	img = render(None if args.no_background else args.background, box)
	img.convert('RGB').save(args.preview)
	print('wrote', args.preview)
	if not args.preview_only:
		for lang in LANGUAGES:
			path = os.path.join(OUT_DIR, f'button_image_{lang}.tga')
			write_tga(img, path)
			print('wrote', path)


if __name__ == '__main__':
	main()
