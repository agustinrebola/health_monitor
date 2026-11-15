//Feeds a recorded timestamp_us,red,ir CSV through the firmware's HeartRate
//module and reports how often a rate was valid and its range.
//Build: ./build.sh    Run: ./ppg_replay trace.csv
#include "../HealthWatch/src/HeartRate.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

int main(int argc, char** argv) {
  if (argc != 2) { std::cerr << "usage: ppg_replay trace.csv\n"; return 2; }
  std::ifstream file(argv[1]);
  if (!file) { std::cerr << "cannot open file\n"; return 2; }

  HeartRate heart;
  std::string line;
  long samples = 0, validSamples = 0;
  float minBpm = 1000, maxBpm = 0;
  double sumBpm = 0;

  while (std::getline(file, line)) {
    std::stringstream in(line);
    std::string ts, red, ir;
    if (!std::getline(in, ts, ',') || !std::getline(in, red, ',') || !std::getline(in, ir, ',')) continue;
    if (ts == "timestamp_us") continue;     //Header
    heart.update(std::stoul(ir));
    samples++;
    if (heart.valid) {
      validSamples++;
      sumBpm += heart.bpm;
      if (heart.bpm < minBpm) minBpm = heart.bpm;
      if (heart.bpm > maxBpm) maxBpm = heart.bpm;
    }
  }

  std::cout << "samples=" << samples << " valid_fraction=" << (samples ? double(validSamples)/samples : 0);
  if (validSamples) std::cout << " bpm_min=" << minBpm << " bpm_mean=" << sumBpm/validSamples << " bpm_max=" << maxBpm;
  std::cout << "\n";
  return 0;
}
