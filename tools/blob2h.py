#!/usr/bin/env python3
"""Turn the assembled hook (hook.bin) into a C header (hook_blob.h)."""
import sys
data = open(sys.argv[1], 'rb').read()
with open(sys.argv[2], 'w') as f:
    f.write('static const unsigned char HOOK_BLOB[] = {' + ','.join(hex(b) for b in data) + '};\n')
