#!/bin/sh
set -eu
cd "$(dirname "$0")"
c++ -std=c++17 -O2 -Wall -Wextra -pedantic replay.cpp ../HealthWatch/src/Steps.cpp -o replay
c++ -std=c++17 -O2 -Wall -Wextra -pedantic test_motion.cpp ../HealthWatch/src/Steps.cpp -o test_motion
./test_motion
c++ -std=c++17 -O2 -Wall -Wextra -pedantic test_physiology.cpp ../HealthWatch/src/HeartRate.cpp -o test_physiology
./test_physiology
c++ -std=c++17 -O2 -Wall -Wextra -pedantic test_ppg_clock.cpp ../HealthWatch/src/PPGClock.cpp -o test_ppg_clock
./test_ppg_clock
c++ -std=c++17 -O2 -Wall -Wextra -pedantic ppg_replay.cpp ../HealthWatch/src/HeartRate.cpp -o ppg_replay
