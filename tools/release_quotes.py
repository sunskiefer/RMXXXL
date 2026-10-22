#!/usr/bin/env python3
"""Lets mpc-vst-plugins' release tools package a plugin whose maker has a ' in it (L'Cronx).

The installer templates hold NAME='@NAME@' and SKIN='@SKIN@' in shell single quotes; a ' in the value ends the quote.
This escapes it there ('\\'') in tools/release.py and tools/catalog_check.py of the framework checkout (a submodule
here, so it is patched in CI, before packaging). Safe to run twice.

A framework that already escapes the quote itself (sunskiefer/mpc-vst-plugins lcronx-release-quoting, and upstream
once that lands there) is left as it is: the file is only checked, never patched a second time.

    python3 tools/release_quotes.py third_party/mpc-vst-plugins
"""
import os, re, sys

ESCAPE = ".replace(\"'\", \"'\\\\''\")"   # the code that writes L'\''Cronx, in whichever form the tool already has it

root = sys.argv[1] if len(sys.argv) > 1 else "third_party/mpc-vst-plugins"
for name in ("release.py", "catalog_check.py"):
    p = os.path.join(root, "tools", name)
    s = open(p).read()
    if "L'Cronx) inside" in s or ESCAPE in s:
        print("release_quotes: %s already escapes a quote" % p)
        continue
    s, n = re.subn(r"(\n([ \t]*)for k, v in sub\.items\(\):\n\2    text = text\.replace\(k, v\)\n)",
                   lambda m: "\n%sfor k in (\"@NAME@\", \"@SKIN@\"):   # a name with ' (L'Cronx) inside the script's '...' quotes\n"
                             "%s    text = text.replace(\"'%%s'\" %% k, \"'%%s'\" %% sub[k].replace(\"'\", \"'\\\\''\"))%s"
                             % (m.group(2), m.group(2), m.group(1)), s, count=1)
    if n != 1:
        sys.exit("release_quotes: %s has changed, can't patch it" % p)
    open(p, "w").write(s)
    print("release_quotes: patched", p)
