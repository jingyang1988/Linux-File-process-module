#!/usr/bin/env python
import sys
import string

if len(sys.argv) != 2:
    print('usage: python genfile.py 10 (unit MiB: 2^20)')
    exit(1)

f = open("%sm" % (sys.argv[1],), "w")

for i in range(string.atoi(sys.argv[1]) * 1024 * 1024):
    f.write('a')
f.close()
