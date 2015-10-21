#!/bin/sh
set -x
lsmod
rmmod sys_xjob
insmod sys_xjob.ko
lsmod
