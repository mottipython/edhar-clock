#!/usr/bin/env python3
"""Turn the drawings in assets/src into the pictures the screensaver builds in,
under assets/art.

    tools/make_sprites.py

Objects are cut out of their flat magenta background and saved as PNG with
transparency. The chain was drawn on pale grey, so it is cut out by darkness, and
trimmed only at the sides so that it still repeats end to end. The chancel is a
full picture and is saved as JPEG. Needs ImageMagick and numpy.
"""
import subprocess
import numpy as np

SRC, OUT = "assets/src", "assets/art"
OBJECTS = {  # name: height in pixels, or ("w", width)
    "meteorite": 380, "cube": 320, "octahedron": 340, "dodecahedron": 330, "icosahedron": 340, "sphere": 300,
    "dial": 760, "hub": ("w", 620), "push1": 440, "push2": 440, "push_toward": 420, "push_away": 440,
    "banner": ("w", 900),
}

def load(path):
    w, h = map(int, subprocess.check_output(["magick", "identify", "-format", "%w %h", path]).split())
    raw = subprocess.check_output(["magick", path, "-depth", "8", "RGB:-"])
    return np.frombuffer(raw, np.uint8).reshape(h, w, 3).astype(np.float32)

def key(img):
    """Magenta background to alpha, and take its colour cast off the edges."""
    r, g, b = img[..., 0].copy(), img[..., 1].copy(), img[..., 2].copy()
    spill = np.minimum(r, b) - g
    cut = np.clip(spill, 0, None) * 0.92
    r, b = r - cut, b - cut
    alpha = np.clip(1.0 - (spill - 24) / 70.0, 0, 1)
    return np.dstack([r, g, b, alpha * 255]).clip(0, 255).astype(np.uint8)

def crop(a, rows_too=True):
    solid = a[..., 3] > 128
    cols = np.where(solid.sum(axis=0) > 3)[0]
    if not rows_too: return a[:, cols.min():cols.max() + 1]
    rows = np.where(solid.sum(axis=1) > 3)[0]
    return a[rows.min():rows.max() + 1, cols.min():cols.max() + 1]

def save(rgba, w, h, path):
    subprocess.run(["magick", "-size", f"{rgba.shape[1]}x{rgba.shape[0]}", "-depth", "8", "RGBA:-",
                    "-filter", "Lanczos", "-resize", f"{w}x{h}!", "-define", "png:compression-level=9", path],
                   input=rgba.tobytes(), check=True)

def main():
    subprocess.run(["mkdir", "-p", OUT], check=True)
    for n, size in OBJECTS.items():
        a = crop(key(load(f"{SRC}/{n}.jpeg")))
        f = size[1] / a.shape[1] if isinstance(size, tuple) else size / a.shape[0]
        w, h = max(1, round(a.shape[1] * f)), max(1, round(a.shape[0] * f))
        save(a, w, h, f"{OUT}/{n}.png")
        print(n, w, h)
    # the chain: dark iron on pale grey
    img = load(f"{SRC}/chain.jpeg")
    lum = img.mean(axis=2)
    alpha = np.clip((176 - lum) / 46.0, 0, 1)
    a = crop(np.dstack([img, alpha * 255]).clip(0, 255).astype(np.uint8), rows_too=False)
    w = 84; h = round(a.shape[0] * w / a.shape[1])
    save(a, w, h, f"{OUT}/chain.png")
    print("chain", w, h)
    subprocess.run(["magick", f"{SRC}/chancel.jpeg", "-resize", "1920x1072!", "-quality", "90", f"{OUT}/chancel.jpg"], check=True)
    print("chancel 1920x1072")

if __name__ == "__main__":
    main()
