#!/usr/bin/env python3
"""Writes the license notices for the fonts in a folder, from the fonts' own metadata.

The SIL Open Font License and the DejaVu license both require their notice to travel with every copy of the fonts.
For the fonts in FONT_DIR this writes:

- OFL.txt: each Open Font License font's copyright line, followed by the license (OFL_TEXT);
- LICENSE-DejaVu.txt: the DejaVu license, which each DejaVu font embeds in full.

A font under any other license stops the build, so a new font cannot ship without its notice.

Usage: write_font_notices.py FONT_DIR OFL_TEXT
"""

import struct
import sys
from pathlib import Path

NAME_COPYRIGHT = 0
NAME_LICENSE = 13
NAME_LICENSE_URL = 14


def font_names(path: Path) -> dict[int, str]:
    """The copyright and license strings of a TrueType or OpenType font's name table (Windows, US English)."""
    data = path.read_bytes()
    table_count = struct.unpack(">H", data[4:6])[0]
    for index in range(table_count):
        tag, _, offset, length = struct.unpack(">4sIII", data[12 + 16 * index:28 + 16 * index])
        if tag == b"name":
            table = data[offset:offset + length]
            break
    else:
        raise ValueError(f"{path.name} has no name table")
    _, count, strings = struct.unpack(">HHH", table[:6])
    names: dict[int, str] = {}
    for index in range(count):
        platform, _, language, name_id, length, offset = struct.unpack(">HHHHHH", table[6 + 12 * index:18 + 12 * index])
        if platform == 3 and language == 0x409 and name_id in (NAME_COPYRIGHT, NAME_LICENSE, NAME_LICENSE_URL):
            names[name_id] = table[strings + offset:strings + offset + length].decode("utf-16-be")
    return names


def main() -> None:
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    font_dir = Path(sys.argv[1])
    ofl_text = Path(sys.argv[2]).read_text(encoding="utf-8").strip()
    ofl_fonts: list[tuple[str, str]] = []
    dejavu: dict[str, str] = {}
    for font in sorted(font_dir.glob("*.ttf")):
        names = font_names(font)
        license_text = names.get(NAME_LICENSE, "")
        if license_text.startswith("Fonts are (c) Bitstream"):
            dejavu[font.name] = license_text
        elif "Open Font License" in license_text or "OFL" in names.get(NAME_LICENSE_URL, ""):
            ofl_fonts.append((font.name, " ".join(names.get(NAME_COPYRIGHT, "").split())))
        else:
            sys.exit(f"{font}: no known font license in its metadata; add its notice before shipping it.")

    for stale in ("OFL.txt", "LICENSE-DejaVu.txt"):
        (font_dir / stale).unlink(missing_ok=True)
    if ofl_fonts:
        copyrights = "\n".join(f"{name}: {copyright}" for name, copyright in ofl_fonts)
        (font_dir / "OFL.txt").write_text(
            f"{copyrights}\n\n"
            "These fonts are licensed under the SIL Open Font License, Version 1.1.\n"
            "This license is copied below, and is also available with a FAQ at:\n"
            "https://openfontlicense.org\n\n"
            "-----------------------------------------------------------\n\n"
            f"{ofl_text}\n",
            encoding="utf-8",
        )
    if dejavu:
        texts = set(dejavu.values())
        if len(texts) != 1:
            sys.exit(f"{font_dir}: the DejaVu fonts embed different license texts.")
        (font_dir / "LICENSE-DejaVu.txt").write_text(
            f"{', '.join(sorted(dejavu))}:\n\n{texts.pop().strip()}\n",
            encoding="utf-8",
        )


if __name__ == "__main__":
    main()
