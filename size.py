#!/usr/bin/env python3

import re
import sys

# Typical GNU ld map entry:
# .text          0x0000000000401000     0x1234 foo.o
#                                      0x5678 foo.o

ENTRY_RE = re.compile(
    r'^\s*(\S+)\s+'
    r'0x[0-9a-fA-F]+\s+'
    r'(0x[0-9a-fA-F]+)\s+'
    r'(.+)$'
)

objects = []

with open(sys.argv[1], "r", errors="replace") as f:
    for line in f:
        m = ENTRY_RE.match(line)
        if not m:
            continue

        section, size_hex, obj = m.groups()
        size = int(size_hex, 16)

        # Keep only likely object/archive entries.
        if ".o" in obj or ".a(" in obj:
            objects.append((size, section, obj.strip()))

for size, section, obj in sorted(objects, reverse=True):
    print(f"{size:10d}  {size / 1024:8.2f} KiB  {section:12}  {obj}")