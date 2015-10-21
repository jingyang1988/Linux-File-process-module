#!/bin/bash
set -x
f=10m
for i in {1..7}; do
./xhw3 -C -o $f.sha1 -a sha1 -w -n $f
done
./xhw3 -L
./xhw3 -r 5
./xhw3 -r 7
./xhw3 -L
