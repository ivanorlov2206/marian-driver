#!/bin/bash

rm -rf data/rec.wav.*
echo "Recording test sample..."
./record_ni.sh $1 $2 &
./play_ni.sh $1 &
wait
