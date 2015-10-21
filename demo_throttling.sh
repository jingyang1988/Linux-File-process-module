#!/bin/bash
set -x
# put large file here to make it slow
f=300m
for i in {1..10}; do
./xhw3 -C -o $f.sha1 -a sha1 -w -n $f
done
