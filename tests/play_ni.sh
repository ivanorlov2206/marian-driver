#!/bin/bash

aplay -D hw:CARD=M2,DEV=0 -c 128 -I -r 48000 -f S32_LE data/out.wav
