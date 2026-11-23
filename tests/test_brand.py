"""The icons are what Windows and Impossible OS can use."""
import re
import struct
import sys
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import brand


class Brand(unittest.TestCase):
    def test_the_icons_use_only_what_the_os_renders(self):
        """The system SVG profile of Impossible OS: no mask, filter, text or CSS, and every gradient is there."""
        allowed = {"svg", "defs", "linearGradient", "stop", "path", "rect", "circle"}
        icons = sorted((brand.BRAND / "icon").glob("*.svg"))
        self.assertEqual(len(icons), 2 + len(brand.SMALL))
        for path in icons:
            text = path.read_text(encoding="utf-8")
            self.assertEqual({e.tag.split("}")[1] for e in ET.fromstring(text).iter()} - allowed, set(), path.name)
            self.assertEqual(set(re.findall(r"url\(#([^)]+)\)", text)) - set(re.findall(r'id="([^"]+)"', text)), set(), path.name)
            self.assertNotIn("style=", text, path.name)

    def test_the_icon_file_has_every_size(self):
        data = (brand.BRAND / "icon.ico").read_bytes()
        count = struct.unpack_from("<H", data, 4)[0]
        self.assertEqual(sorted(data[6 + 16 * i] or 256 for i in range(count)), [16, 20, 24, 32, 48, 64, 256])


if __name__ == "__main__":
    unittest.main()
