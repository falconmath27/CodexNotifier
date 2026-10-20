from __future__ import annotations

from collections import deque
from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "PET_DESIGN"
OUTPUT = ROOT / "app" / "assets" / "pets" / "idle"
CANVAS_SIZE = (232, 200)

COLOR_SHEETS = {
    "yellow": "smiling-yellow-blob.png",
    "pink": "smiling-pink-blob-1.png",
    "green": "smiling-green-blob-1.png",
    "blue": "smiling-blue-blob.png",
    "purple": "smiling-purple-blob-1.png",
    "white": "smiling-white-blob.png",
    "blue-green": "smiling-blue-green-blob-1.png",
    "grey": "smiling-grey-blob.png",
    "golden": "smiling-golden-blob-1.png",
}


def remove_edge_connected_black(image: Image.Image, threshold: int = 8) -> Image.Image:
    rgba = image.convert("RGBA")
    pixels = rgba.load()
    width, height = rgba.size
    visited = bytearray(width * height)
    queue: deque[tuple[int, int]] = deque()

    def add(x: int, y: int) -> None:
        offset = y * width + x
        if visited[offset]:
            return
        visited[offset] = 1
        red, green, blue, _ = pixels[x, y]
        if max(red, green, blue) <= threshold:
            queue.append((x, y))

    for x in range(width):
        add(x, 0)
        add(x, height - 1)
    for y in range(height):
        add(0, y)
        add(width - 1, y)

    while queue:
        x, y = queue.popleft()
        pixels[x, y] = (0, 0, 0, 0)
        if x > 0:
            add(x - 1, y)
        if x + 1 < width:
            add(x + 1, y)
        if y > 0:
            add(x, y - 1)
        if y + 1 < height:
            add(x, y + 1)
    return rgba


def fit_to_canvas(image: Image.Image) -> Image.Image:
    image.thumbnail(CANVAS_SIZE, Image.Resampling.NEAREST)
    canvas = Image.new("RGBA", CANVAS_SIZE, (0, 0, 0, 0))
    x = (CANVAS_SIZE[0] - image.width) // 2
    y = CANVAS_SIZE[1] - image.height
    canvas.alpha_composite(image, (x, y))
    return canvas


def export_two_frame_sheet(color: str, filename: str) -> None:
    source_path = SOURCE / filename
    if not source_path.exists():
        raise FileNotFoundError(f"Missing {color} sprite sheet: {source_path}")

    with Image.open(source_path) as sheet:
        sheet = sheet.convert("RGBA")
        split = sheet.width // 2
        bounds = ((0, 0, split, sheet.height), (split, 0, sheet.width, sheet.height))
        destination = OUTPUT / color
        destination.mkdir(parents=True, exist_ok=True)
        for index, box in enumerate(bounds, start=1):
            frame = sheet.crop(box)
            frame.thumbnail(CANVAS_SIZE, Image.Resampling.NEAREST)
            frame = remove_edge_connected_black(frame)
            frame = fit_to_canvas(frame)
            output_path = destination / f"frame_{index}.png"
            frame.save(output_path)
            print(output_path)


def export_orange_frames() -> None:
    source_directory = SOURCE / "smiling-orange-blob"
    source_files = sorted(source_directory.glob("smiling-orange-blob-*.png"))
    if len(source_files) != 4:
        raise RuntimeError(f"Expected four Orange frames in {source_directory}, found {len(source_files)}")

    destination = OUTPUT / "orange"
    destination.mkdir(parents=True, exist_ok=True)
    for index, source_path in enumerate(source_files, start=1):
        with Image.open(source_path) as source:
            output_path = destination / f"frame_{index}.png"
            fit_to_canvas(source.convert("RGBA")).save(output_path)
            print(output_path)


def export_preview() -> None:
    colors = ["orange", *COLOR_SHEETS.keys()]
    tile_width, tile_height = 280, 240
    preview = Image.new("RGB", (tile_width * 5, tile_height * 2), "#eef1f6")
    draw = ImageDraw.Draw(preview)
    for index, color in enumerate(colors):
        frame_number = 3 if color == "orange" else 2
        with Image.open(OUTPUT / color / f"frame_{frame_number}.png") as frame:
            x = (index % 5) * tile_width + (tile_width - frame.width) // 2
            y = (index // 5) * tile_height + 8
            preview.paste(frame, (x, y), frame)
        label = color.replace("-", " ").title()
        draw.text(((index % 5) * tile_width + 12, (index // 5 + 1) * tile_height - 24), label, fill="#18243a")
    preview_path = ROOT / "build" / "pet-color-preview.png"
    preview_path.parent.mkdir(parents=True, exist_ok=True)
    preview.save(preview_path)
    print(preview_path)


def main() -> None:
    export_orange_frames()
    for color, filename in COLOR_SHEETS.items():
        export_two_frame_sheet(color, filename)
    export_preview()


if __name__ == "__main__":
    main()
