//Runs the real HealthWatch/src/HeartRate.cpp over a recorded timestamp_us,red,ir trace
//and writes its internal signals, so the figures in CODE_GUIDE.md show what the
//firmware code actually does. Private members are opened up with a macro, which is
//fine for a study tool and keeps the firmware source untouched.
//
//usage: dump_heartrate trace.csv out_prefix
//writes out_prefix_samples.csv (one row per sample) and out_prefix_scans.csv
//(one row per once-a-second scan, with the correlation score of every lag 39..151)
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#define private public
#include "../../HealthWatch/src/HeartRate.h"
#undef private

int main(int argc, char** argv) {
  if (argc != 3) { std::cerr << "usage: dump_heartrate trace.csv out_prefix\n"; return 2; }
  std::ifstream in(argv[1]);
  if (!in) { std::cerr << "cannot open trace\n"; return 2; }
  const std::string prefix = argv[2];
  FILE* samples = fopen((prefix + "_samples.csv").c_str(), "w");
  FILE* scans = fopen((prefix + "_scans.csv").c_str(), "w");
  if (!samples || !scans) { std::cerr << "cannot write output\n"; return 2; }

  fprintf(samples, "i,ir,dc,x0,x1,x2,ac,amplitude,contact,valid,bpm,filled\n");
  fprintf(scans, "i,valid,bpm,lastLag,stableScans,scansSinceValid");
  for (int lag = 39; lag <= 151; lag++) fprintf(scans, ",s%d", lag);
  fprintf(scans, "\n");

  HeartRate heart;
  std::string line;
  long i = 0;
  while (std::getline(in, line)) {
    std::stringstream row(line);
    std::string ts, red, ir;
    if (!std::getline(row, ts, ',') || !std::getline(row, red, ',') || !std::getline(row, ir, ',')) continue;
    if (ts == "timestamp_us") continue;
    const uint32_t irValue = std::stoul(ir);

    const int sinceBefore = heart.sinceScan;
    heart.update(irValue);

    //The intermediate filter outputs are locals inside update(); rebuild them from
    //the filter states, which hold exactly the values update() just computed
    const double x0 = (double)irValue - heart.dc;
    const double x1 = x0 - heart.hp1;
    const double x2 = x1 - heart.hp2;
    fprintf(samples, "%ld,%u,%.3f,%.3f,%.3f,%.3f,%.4f,%.3f,%d,%d,%.3f,%d\n", i, irValue, heart.dc,
            x0, x1, x2, heart.ac, heart.amplitude, heart.contact, heart.valid, heart.bpm, heart.filled);

    //A scan just ran if sinceScan went back to 0 while history is long enough
    if (heart.sinceScan == 0 && sinceBefore > 0 && heart.filled >= 350) {
      fprintf(scans, "%ld,%d,%.3f,%d,%d,%d", i, heart.valid, heart.bpm, heart.lastLag,
              heart.stableScans, heart.scansSinceValid);
      for (int lag = 39; lag <= 151; lag++) fprintf(scans, ",%.4f", heart.correlation(lag));
      fprintf(scans, "\n");
    }
    i++;
  }
  fclose(samples);
  fclose(scans);
  std::cerr << "samples=" << i << "\n";
  return 0;
}
