#include "../HealthWatch/src/Steps.h"
#include <cassert>
#include <cmath>

int main() {
  Steps detector;
  // Stationary samples must not add steps.
  for (uint32_t i=1;i<=1000;++i) {
    ImuSample s{i*10000,0,0,1,0,0,0,true};
    detector.update(s);
  }
  assert(detector.getState().steps == 0);
  assert(detector.getState().activity == Activity::Stationary);
  // One isolated impulse cannot satisfy the cadence confirmation state.
  for (uint32_t i=1001;i<=1300;++i) {
    ImuSample s{i*10000,0,0,1+(i==1050 ? 1.0f : 0.0f),0,0,0,true};
    detector.update(s);
  }
  assert(detector.getState().steps == 0);
  // A synthetic repeated vertical pulse verifies basic counting mechanics only.
  detector.reset();
  for (uint32_t i=1;i<=3000;++i) {
    float phase = float(i)*0.01f*2.0f*3.14159265f*2.0f;
    ImuSample s{i*10000,0,0,1+0.25f*std::sin(phase),0,0,20,true};
    detector.update(s);
  }
  assert(detector.getState().steps > 30);
}
