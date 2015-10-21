#!/bin/bash
set -x
dd if=/dev/zero of=$1m bs=1 count=0 seek=$1M
