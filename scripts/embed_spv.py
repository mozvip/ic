#!/usr/bin/env python3
"""Embed SPIR-V binary files into a single C header.

Usage: embed_spv.py <out.h> <input.spv>...

Each input filename becomes a symbol name. Example:
    invert.frag.spv  ->  ic_spv_invert_frag (Uint8 array)
                        ic_spv_invert_frag_len (size_t)
"""

import os
import re
import sys


def sym_name(path: str) -> str:
    base = os.path.basename(path)
    base = re.sub(r"\.spv$", "", base)
    base = re.sub(r"[^A-Za-z0-9]", "_", base)
    return "ic_spv_" + base


def main(argv):
    if len(argv) < 3:
        sys.stderr.write(__doc__)
        return 2

    out_path = argv[1]
    inputs = argv[2:]

    parts = ["/* AUTO-GENERATED. DO NOT EDIT. */",
             "#ifndef IC_SPV_EMBED_H",
             "#define IC_SPV_EMBED_H",
             "#include <stddef.h>",
             "#include <stdint.h>",
             ""]

    for path in inputs:
        with open(path, "rb") as fp:
            data = fp.read()
        name = sym_name(path)
        parts.append(f"static const uint8_t {name}[] = {{")
        for i in range(0, len(data), 16):
            chunk = data[i:i + 16]
            parts.append("    " + ",".join(f"0x{b:02x}" for b in chunk) + ",")
        parts.append("};")
        parts.append(f"static const size_t {name}_len = sizeof({name});")
        parts.append("")

    parts.append("#endif /* IC_SPV_EMBED_H */")
    parts.append("")

    with open(out_path, "w") as fp:
        fp.write("\n".join(parts))

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
