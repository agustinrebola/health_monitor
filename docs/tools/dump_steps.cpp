//Runs the real HealthWatch/src/Steps.cpp over a CSV of t_ms,ax,ay,az and prints its
//internal state after every sample. Private members are opened with a macro.
//
//usage: dump_steps input.csv > output.csv
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#define private public
#include "../../HealthWatch/src/Steps.h"
#undef private

int main(int argc, char** argv) {
  if (argc != 2) { std::cerr << "usage: dump_steps input.csv\n"; return 2; }
  std::ifstream in(argv[1]);
  if (!in) { std::cerr << "cannot open input\n"; return 2; }
  printf("t_ms,mag,baseline,filtered,threshold,peak,count,walking,cadence,candidates,lastPeakMs\n");

  Steps steps;
  std::string line;
  while (std::getline(in, line)) {
    std::stringstream row(line);
    std::string a, b, c, d;
    if (!std::getline(row, a, ',') || !std::getline(row, b, ',') || !std::getline(row, c, ',') || !std::getline(row, d, ',')) continue;
    if (a == "t_ms") continue;
    const uint32_t t = std::stoul(a);
    const float ax = std::stof(b), ay = std::stof(c), az = std::stof(d);

    //Values update() will compare for its peak test, taken before it shifts them
    const float prevPrev = steps.prevPrevFiltered, prev = steps.prevFiltered;
    steps.update(ax, ay, az, t);
    const bool peak = prev > prevPrev && prev >= steps.filtered && prev > steps.threshold;

    printf("%u,%.4f,%.4f,%.4f,%.4f,%d,%u,%d,%.2f,%u,%u\n", t, sqrtf(ax*ax + ay*ay + az*az),
           steps.baseline, steps.filtered, steps.threshold, peak, steps.count, steps.walking,
           steps.cadence, steps.candidateCount, steps.lastPeakMs);
  }
  return 0;
}
