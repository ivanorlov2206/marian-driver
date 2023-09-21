#!/bin/bash

arecord --buffer-size 256000 -D hw:CARD=M2,DEV=0 -c 128 -I -r $1 -f S32_LE --duration=$2 data/rec.wav
