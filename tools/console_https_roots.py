"""Writes port/console/https/console_https_roots.c: the root certificates the
consoles' HTTPS (port/console/https/console_https.c) checks
raw.githubusercontent.com's chain against, as DER in C arrays, each named and
its SHA-256 fingerprint in a comment.

Each root is given as NAME=FILE (PEM or DER). The fingerprints printed must
be checked against the certificate authority's own published ones before the
file is committed (docs/console-https.md, "Rotating keys"). Usage:

  python3 tools/console_https_roots.py "ISRG Root X1=isrg-root-x1.pem" ... \
      > port/console/https/console_https_roots.c
"""

import base64
import hashlib
import re
import sys

HEADER = """/*
CONSOLE_HTTPS_ROOTS.C

The roots raw.githubusercontent.com's certificate chain must lead to
(console_https_trust.c), as DER. Made by tools/console_https_roots.py; each
root's SHA-256 fingerprint is beside it. docs/console-https.md says when and
how the set changes.
*/

#include "console_https_trust.h"
"""


def der_of(path):
    data = open(path, "rb").read()
    match = re.search(rb"-----BEGIN CERTIFICATE-----(.*?)-----END CERTIFICATE-----", data, re.S)
    if match:
        return base64.b64decode(b"".join(match.group(1).split()))
    return data


def identifier(name):
    return "root_" + re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")


def main(arguments):
    if not arguments:
        print(__doc__, file=sys.stderr)
        return 2
    out = [HEADER]
    names = []
    for argument in arguments:
        name, _, path = argument.partition("=")
        der = der_of(path)
        fingerprint = hashlib.sha256(der).hexdigest().upper()
        print(f"{name}: SHA-256 {':'.join(fingerprint[i:i + 2] for i in range(0, 64, 2))}", file=sys.stderr)
        variable = identifier(name)
        names.append((name, variable))
        out.append(f"\n/* {name}, SHA-256 {fingerprint} */")
        out.append(f"static const unsigned char {variable}[{len(der)}] =\n{{")
        for start in range(0, len(der), 16):
            out.append("\t" + ", ".join(f"0x{byte:02x}" for byte in der[start:start + 16]) + ",")
        out.append("};")
    out.append("\nconst struct console_https_root console_https_roots[] =\n{")
    for name, variable in names:
        out.append(f'\t{{ "{name}", {variable}, sizeof({variable}) }},')
    out.append("};")
    out.append("const int console_https_root_count = sizeof(console_https_roots) / sizeof(*console_https_roots);")
    sys.stdout.write("\n".join(out) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
