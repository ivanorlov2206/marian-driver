#!/bin/bash

arecord -c 32 -r 400000 -f S32_LE --duration=40 data/rec3.wav &
aplay -c 32 -r 400000 -f S32_LE --duration=40 data/rec2.wav &
wait
