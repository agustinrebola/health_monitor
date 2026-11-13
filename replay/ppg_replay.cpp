#include "../HealthWatch/src/HeartRate.h"
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static bool parse(const std::string& line, PpgSample& s) {
  std::stringstream in(line);
  std::string field;
  uint32_t values[3];
  for (int i=0;i<3;++i) {
    if (!std::getline(in, field, ',')) return false;
    try {
      size_t used=0;
      const unsigned long long value=std::stoull(field, &used);
      if (used!=field.size() || value>UINT32_MAX) return false;
      values[i]=static_cast<uint32_t>(value);
    } catch (...) { return false; }
  }
  if (std::getline(in, field, ',')) return false;
  s={values[0],values[1],values[2],true};
  return true;
}

int main(int argc, char** argv) {
  if (argc<2 || argc>3) { std::cerr << "usage: ppg_replay raw_ppg.csv [print_every_n_samples]\n"; return 2; }
  const unsigned every = argc==3 ? unsigned(std::atoi(argv[2])) : 500u;
  if (!every) { std::cerr << "print interval must be positive\n"; return 2; }
  std::ifstream file(argv[1]);
  if (!file) { std::cerr << "cannot open file\n"; return 2; }
  HeartRate processor;
  std::string line;
  uint32_t previous=0;
  uint64_t rows=0, validRows=0;
  double validBpmSum=0;
  float validBpmMin=1000, validBpmMax=0;
  bool header=false;
  std::cout << "timestamp_us,ir_dc,ir_rms,rel,ibi,period,lag,half_corr,dc_drift,hr_bpm,valid,quality,weak,long,unstable\n";
  while (std::getline(file,line)) {
    if (line.empty()) continue;
    PpgSample sample;
    if (!parse(line,sample)) {
      if (!header && rows==0 && line=="timestamp_us,red,ir") { header=true; continue; }
      std::cerr << "invalid row after " << rows << " samples\n"; return 1;
    }
    if (rows && sample.timestampUs<=previous) { std::cerr << "timestamps must increase\n"; return 1; }
    previous=sample.timestampUs;
    processor.update(sample,0); // raw trace has no IMU stream; evaluate optical path only
    const auto& p=processor.getState();
    if (p.heartRateValid) {
      ++validRows;
      validBpmSum+=p.heartRateBpm;
      if (p.heartRateBpm<validBpmMin) validBpmMin=p.heartRateBpm;
      if (p.heartRateBpm>validBpmMax) validBpmMax=p.heartRateBpm;
    }
    if (++rows%every==0) {
      std::cout << sample.timestampUs << ',' << p.irDc << ',' << p.irRms << ',' << p.relativeAmplitude
                << ',' << unsigned(p.intervalCount) << ',' << p.periodicity << ','
                << unsigned(p.periodLag) << ',' << p.halfPeriodicity << ',' << p.dcDrift << ','
                << (p.heartRateValid?p.heartRateBpm:0) << ',' << p.heartRateValid << ','
                << unsigned(p.quality) << ',' << p.weakSignalResets << ',' << p.longIntervalResets
                << ',' << p.unstableBeatWindows << '\n';
    }
  }
  std::cerr << "samples=" << rows << " valid_fraction=" << (rows?double(validRows)/rows:0)
            << " timing_gaps=" << processor.getState().timingGaps;
  if (validRows) std::cerr << " valid_bpm_min=" << validBpmMin
                           << " valid_bpm_mean=" << validBpmSum/validRows
                           << " valid_bpm_max=" << validBpmMax;
  std::cerr << '\n';
  return rows?0:1;
}
