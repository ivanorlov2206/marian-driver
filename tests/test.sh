#!/bin/bash

echo "Recording test sample..."
./record_ni.sh &
./play_ni.sh &
wait
