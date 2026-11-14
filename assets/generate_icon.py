"""Generate the native blue application icon at exact shell sizes (developer-only Pillow dependency)."""
from pathlib import Path
from io import BytesIO
import struct
from PIL import Image, ImageDraw

sizes = (16, 20, 24, 32, 40, 48, 64, 128, 256)
images = []
for size in sizes:
    scale = 4
    im = Image.new('RGBA', (size * scale, size * scale))
    d = ImageDraw.Draw(im)
    edge = size * scale
    d.rounded_rectangle((0, 0, edge - 1, edge - 1), radius=edge * .18, fill='#2878f0')
    stroke = max(scale, round(edge * .08))
    d.line([(edge * .22, edge * .28), (edge * .43, edge * .48), (edge * .22, edge * .68)],
           fill='white', width=stroke, joint='curve')
    d.line([(edge * .51, edge * .70), (edge * .77, edge * .70)], fill='white', width=stroke)
    im = im.resize((size, size), Image.Resampling.LANCZOS)
    stream = BytesIO()
    im.save(stream, format='PNG')
    images.append(stream.getvalue())
# Store each handcrafted resolution directly; do not derive all sizes from one 256px bitmap.
header = struct.pack('<HHH', 0, 1, len(sizes))
offset = 6 + 16 * len(sizes)
entries = []
for size, data in zip(sizes, images):
    entries.append(struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0, 1, 32, len(data), offset))
    offset += len(data)
Path(__file__).with_name('app.ico').write_bytes(header + b''.join(entries) + b''.join(images))
