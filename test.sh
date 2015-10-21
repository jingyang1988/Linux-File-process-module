#!/bin/sh
set -x
make
#make C=1
rmmod sys_xjob
insmod sys_xjob.ko
