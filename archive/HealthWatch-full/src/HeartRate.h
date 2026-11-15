#ifndef HEARTRATE_H
#define HEARTRATE_H

#include <stdint.h>
#include "Types.h"

//Everything the rest of the firmware reads from the optical processor
struct HeartRateState {
  float heartRateBpm;                        //Current estimate; valid only with heartRateValid
  float ratioOfRatios;                       //Optical diagnostic; never converted to SpO2 here
  uint32_t lastValidHrUs;                    //Timestamp of the last accepted estimate
  SignalQuality quality;                     //Signal class sent to the phone
  bool heartRateValid;                       //heartRateBpm is current
  bool spo2Valid;                            //Always false in this version
  bool contactDetected;                      //Optical level says skin is present
  float irDc;                                //IR baseline in counts
  float irRms;                               //IR pulse amplitude in counts
  float relativeAmplitude;                   //irRms divided by irDc
  uint32_t timingGaps;                       //Sample spacing outside 5-50 ms
  uint32_t invalidRawSamples;                //Zero or saturated samples
  uint32_t motionRejectedSamples;            //Samples dropped for wrist rotation
  uint32_t acceptedIntervals;                //Beat intervals kept as diagnostics
  uint32_t opticalRepositions;               //Large baseline steps, placement changes
  uint32_t weakSignalResets;                 //Episodes below the pulse floor
  uint32_t longIntervalResets;               //Beat intervals outside 0.33-1.5 s
  uint32_t earlyPeaksIgnored;                //Peaks inside the refractory interval
  uint32_t unstableBeatWindows;              //Five-beat windows with inconsistent timing
  float periodicity;                         //Best waveform correlation this scan
  float dcDrift;                             //Two-second fractional baseline change
  float halfPeriodicity;                     //Correlation at half the chosen period
  uint8_t periodLag;                         //Chosen period in 10 ms samples
  uint8_t intervalCount;                     //Beat intervals currently held
};

//Heart rate from red and IR waveform periodicity, with beat timing as diagnostics
class HeartRate{
  private:
    static const uint16_t HISTORY_SIZE = 400;   //4 s of filtered samples at 100 Hz

    HeartRateState state;                    //Public view of the processor
    float irDc, redDc;                       //Slow baselines
    float irAc, redAc;                       //Band-passed waveforms
    float irHp1, irHp2, redHp1, redHp2;      //Extra ~0.7 Hz high-pass stages against wrist drift
    float irEnergy, redEnergy;               //Mean squared AC amplitude
    float previous, previous2;               //Previous two IR samples for peak detection
    uint32_t previousUs;                     //Timestamp of the previous sample
    uint32_t lastPeakUs;                     //Timestamp of the last accepted peak
    uint16_t settled;                        //Samples since the filters were reset, capped
    float ibis[5];                           //Recent beat intervals in seconds
    uint8_t ibiCount, ibiIndex;              //Fill level and write position of ibis
    bool weakGateActive;                     //Currently below the pulse floor
    float irHistory[HISTORY_SIZE];           //Filtered IR ring buffer
    float redHistory[HISTORY_SIZE];          //Filtered red ring buffer
    float irDcHistory[HISTORY_SIZE];         //IR baseline ring buffer
    uint16_t historyHead, historyCount;      //Ring buffer write position and fill level
    uint16_t samplesSinceScan;               //Samples since the last period scan
    uint8_t previousPeriodLag;               //Chosen lag at the previous usable scan
    uint8_t stablePeriodScans;               //Consecutive scans agreeing on the lag
    bool periodicValid;                      //A period estimate is currently accepted
    float periodicBpm;                       //Smoothed accepted estimate
    uint32_t periodicTimestampUs;            //Timestamp of the last accepted scan

    float calculatePeriodicity(float intervalSeconds) const;
    float calculateRedIrAgreement() const;
    void updatePeriodicEstimate(uint32_t timestampUs, float relativeAmplitude);
    void resetPeriodicEstimate();

  public:
    HeartRate();

    void update(const PpgSample& sample, float gyroDps);
    void expire(uint32_t nowUs);
    void invalidateStream();

    const HeartRateState& getState() const;
};

#endif
