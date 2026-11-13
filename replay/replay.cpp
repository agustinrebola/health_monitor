#include "../HealthWatch/src/Steps.h"
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static bool parse(const std::string& line, ImuSample& s) {
  std::stringstream in(line);
  std::string field;
  double v[7];
  for (int i=0;i<7;++i) {
    if (!std::getline(in, field, ',')) return false;
    try { size_t used=0; v[i]=std::stod(field, &used); if (used != field.size() || !std::isfinite(v[i])) return false; }
    catch (...) { return false; }
  }
  if (std::getline(in, field, ',')) return false;
  if (v[0] < 0 || v[0] > 4294967295.0) return false;
  s = {static_cast<uint32_t>(v[0]), static_cast<float>(v[1]),static_cast<float>(v[2]),static_cast<float>(v[3]),
       static_cast<float>(v[4]),static_cast<float>(v[5]),static_cast<float>(v[6]), true};
  return true;
}
int main(int argc, char** argv) {
  if (argc != 2) { std::cerr << "usage: replay imu.csv\n"; return 2; }
  std::ifstream file(argv[1]);
  if (!file) { std::cerr << "cannot open file\n"; return 2; }
  Steps motion;
  std::string line;
  uint64_t rows=0, candidates=0, confirmed=0;
  uint32_t previous=0;
  bool header=false;
  while (std::getline(file, line)) {
    if (line.empty()) continue;
    ImuSample s;
    if (!parse(line, s)) {
      if (!header && rows==0 && line.rfind("timestamp_us,",0)==0) { header=true; continue; }
      std::cerr << "invalid CSV row after " << rows << " samples\n"; return 1;
    }
    if (rows && s.timestampUs <= previous) { std::cerr << "timestamps must increase\n"; return 1; }
    previous=s.timestampUs;
    const auto& state=motion.update(s);
    if (state.candidate) { ++candidates; std::cout << s.timestampUs << ",candidate," << state.filteredG << '\n'; }
    if (state.confirmed) { ++confirmed; std::cout << s.timestampUs << ",confirmed," << state.steps << '\n'; }
    ++rows;
  }
  std::cerr << "samples=" << rows << " candidates=" << candidates << " confirmed_events=" << confirmed
            << " counted_steps=" << motion.getState().steps << '\n';
  return rows ? 0 : 1;
}
