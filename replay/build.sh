#!/bin/sh
set -eu
cd "$(dirname "$0")"
c++ -std=c++17 -O2 -Wall ppg_replay.cpp ../HealthWatch/src/HeartRate.cpp -o ppg_replay
