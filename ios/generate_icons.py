import os
from PIL import Image
import json

# Input files
icons = {
    "light": "icon_gold_backfill_1024.png",
    "dark": "dark.png",
    "tinted": "tinted.png"
}

output_dir = "IconGold.appiconset"
os.makedirs(output_dir, exist_ok=True)

# Sizes and scales required by iOS
icon_sizes = [
    (20, [1, 2, 3]),
    (29, [1, 2, 3]),
    (40, [1, 2, 3]),
    (60, [2, 3]),
    (76, [1, 2]),
    (83.5, [2]),
    (1024, [1]),
]

# Appearance tags for Contents.json, none for light
appearance_tags = {
    "light": None,
    "dark": [{"appearance": "luminosity", "value": "dark"}],
    "tinted": [{"appearance": "luminosity", "value": "tinted"}]
}

def save_icon(image, size_pt, scale, appearance):
    px = int(size_pt * scale)
    # file name like: icon_20x20@2x_light.png
    suffix = appearance
    filename = f"icon_{int(size_pt)}x{int(size_pt)}@{scale}x_{suffix}.png"
    filepath = os.path.join(output_dir, filename)
    resized = image.resize((px, px), Image.LANCZOS)
    resized.save(filepath)
    return filename

def generate_images_for_appearance(img_path, appearance):
    image = Image.open(img_path)
    images = []
    for size, scales in icon_sizes:
        for scale in scales:
            filename = save_icon(image, size, scale, appearance)
            entry = {
                "idiom": "universal",
                "size": f"{size}x{size}",
                "scale": f"{scale}x",
                "filename": filename,
                "platform": "ios",
            }
            if appearance_tags[appearance]:
                entry["appearances"] = appearance_tags[appearance]
            images.append(entry)
    return images

# Generate all images and JSON entries
all_images = []
for appearance, filepath in icons.items():
    all_images.extend(generate_images_for_appearance(filepath, appearance))

contents = {
    "images": all_images,
    "info": {
        "version": 1,
        "author": "xcode"
    }
}

with open(os.path.join(output_dir, "Contents.json"), "w") as f:
    json.dump(contents, f, indent=4)

print("✅ IconGold asset catalog with light, dark and tinted appearances generated.")
