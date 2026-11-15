#ifndef HEARTRATE_H
#define HEARTRATE_H

#include <stdint.h>

//Heart rate from how often the IR pulse waveform repeats
class HeartRate{
  private:
    static const int HISTORY = 400;          //4 s of filtered samples at 100 Hz

    float dc;                                //Slow baseline of the IR signal
    float hp1, hp2;                          //Two high-pass stages against wrist drift
    float ac;                                //Filtered pulse waveform
    float energy;                            //Average of ac squared
    float history[HISTORY];                  //Ring buffer of ac
    int head, filled;                        //Write position and fill level
    int sinceScan;                           //Samples since the last period scan
    int lastLag;                             //Period found by the previous scan
    int stableScans;                         //Consecutive scans agreeing on the period
    int scansSinceValid;                     //Scans since a period was accepted

    float correlation(int lag);

  public:
    float bpm;                               //Heart rate; only meaningful when valid
    bool valid;                              //A rate is currently accepted
    bool contact;                            //Sensor is against skin
    float amplitude;                         //Pulse size in ADC counts

    HeartRate();

    void reset();
    void update(uint32_t ir);
};

#endif
