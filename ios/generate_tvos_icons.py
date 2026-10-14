# Generates the tvOS app icon and Top Shelf images in tvOS-assets.xcassets from the iOS icon art.
# Run from the ios directory: python3 generate_tvos_icons.py
#
# tvOS icons are layered for the parallax effect when focused: the logo floats over the background.

import json
import os
import shutil

from PIL import Image, ImageDraw, ImageFilter, ImageFont

LOGO = "assets.xcassets/AppIcon.appiconset/dark.png"  # The logo on transparency.
FONT = "../assets/Roboto_Condensed-Bold.ttf"
OUTPUT = "tvOS-assets.xcassets"
BRAND = "AppIcon.brandassets"

# The background of the iOS icon, lightened a bit toward the top.
BG_TOP = (61, 92, 110)
BG_BOTTOM = (45, 69, 83)
TEXT = (226, 240, 246)

INFO = {"author": "xcode", "version": 1}


def write_json(path, data):
	with open(os.path.join(path, "Contents.json"), "w", newline="\n") as f:
		json.dump(data, f, indent=2)
		f.write("\n")


def background(w, h):
	img = Image.new("RGB", (w, h))
	draw = ImageDraw.Draw(img)
	for y in range(h):
		t = y / max(1, h - 1)
		color = tuple(round(a + (b - a) * t) for a, b in zip(BG_TOP, BG_BOTTOM))
		draw.line([(0, y), (w, y)], fill=color)
	return img


def logo(size):
	src = Image.open(LOGO).convert("RGBA")
	src = src.crop(src.getchannel("A").getbbox())
	return src.resize((size, size), Image.LANCZOS)


def with_shadow(layer, radius):
	# A soft shadow under whatever is opaque in the layer, slightly offset down.
	alpha = layer.getchannel("A")
	shadow = Image.new("RGBA", layer.size, (0, 0, 0, 0))
	shadow.putalpha(alpha.point(lambda a: a * 0.55))
	shadow = shadow.filter(ImageFilter.GaussianBlur(radius))
	out = Image.new("RGBA", layer.size, (0, 0, 0, 0))
	out.alpha_composite(shadow, (0, int(radius * 0.6)))
	out.alpha_composite(layer)
	return out


def front_layer(w, h):
	# tvOS crops the edges as the focused icon moves, so keep the logo well inside.
	size = round(h * 0.72)
	layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
	layer.alpha_composite(logo(size), ((w - size) // 2, (h - size) // 2))
	return with_shadow(layer, h / 60)


def top_shelf(w, h):
	img = background(w, h).convert("RGBA")
	size = round(h * 0.6)
	font = ImageFont.truetype(FONT, round(h * 0.3))
	text = "PPSSPP"
	draw = ImageDraw.Draw(img)
	left, top, right, bottom = draw.textbbox((0, 0), text, font=font)
	gap = round(h * 0.08)
	total = size + gap + (right - left)
	x = (w - total) // 2
	fg = Image.new("RGBA", (w, h), (0, 0, 0, 0))
	fg.alpha_composite(logo(size), (x, (h - size) // 2))
	ImageDraw.Draw(fg).text((x + size + gap - left, (h - (bottom - top)) // 2 - top), text, font=font, fill=TEXT)
	img.alpha_composite(with_shadow(fg, h / 80))
	return img.convert("RGB")


def image_stack(name, w, h, scales):
	stack = os.path.join(OUTPUT, BRAND, name + ".imagestack")
	layers = [("Front", lambda sw, sh: front_layer(sw, sh)), ("Back", lambda sw, sh: background(sw, sh))]
	for layer_name, make in layers:
		layer_dir = os.path.join(stack, layer_name + ".imagestacklayer")
		content = os.path.join(layer_dir, "Content.imageset")
		os.makedirs(content)
		images = []
		for scale in scales:
			filename = "%s%s.png" % (layer_name.lower(), "" if scale == 1 else "@%dx" % scale)
			make(w * scale, h * scale).save(os.path.join(content, filename))
			images.append({"filename": filename, "idiom": "tv", "scale": "%dx" % scale})
		write_json(content, {"images": images, "info": INFO})
		write_json(layer_dir, {"info": INFO})
	write_json(stack, {"info": INFO, "layers": [{"filename": n + ".imagestacklayer"} for n, _ in layers]})


def image_set(name, w, h):
	path = os.path.join(OUTPUT, BRAND, name + ".imageset")
	os.makedirs(path)
	images = []
	for scale in (1, 2):
		filename = "%s%s.png" % (name.replace(" ", "_").lower(), "" if scale == 1 else "@2x")
		top_shelf(w * scale, h * scale).save(os.path.join(path, filename))
		images.append({"filename": filename, "idiom": "tv", "scale": "%dx" % scale})
	write_json(path, {"images": images, "info": INFO})


shutil.rmtree(OUTPUT, ignore_errors=True)
os.makedirs(os.path.join(OUTPUT, BRAND))
write_json(OUTPUT, {"info": INFO})

image_stack("App Icon", 400, 240, (1, 2))
image_stack("App Icon - App Store", 1280, 768, (1,))
image_set("Top Shelf Image", 1920, 720)
image_set("Top Shelf Image Wide", 2320, 720)

write_json(os.path.join(OUTPUT, BRAND), {
	"assets": [
		{"filename": "App Icon - App Store.imagestack", "idiom": "tv", "role": "primary-app-icon", "size": "1280x768"},
		{"filename": "App Icon.imagestack", "idiom": "tv", "role": "primary-app-icon", "size": "400x240"},
		{"filename": "Top Shelf Image Wide.imageset", "idiom": "tv", "role": "top-shelf-image-wide", "size": "2320x720"},
		{"filename": "Top Shelf Image.imageset", "idiom": "tv", "role": "top-shelf-image", "size": "1920x720"},
	],
	"info": INFO,
})
print("Wrote", OUTPUT)
