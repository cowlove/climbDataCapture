#pragma once

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <string>
#include <vector>

static const float FEET_PER_METER_CDC = 3.280839895f;

struct TimedValue {
  float value = 0;
  uint32_t updatedMs = 0;
  bool valid = false;

  void update(float newValue, uint32_t nowMs) {
    value = newValue;
    updatedMs = nowMs;
    valid = true;
  }

  bool fresh(uint32_t nowMs, uint32_t maximumAgeMs) const {
    return valid && (uint32_t)(nowMs - updatedMs) <= maximumAgeMs;
  }
};

struct G5Sample {
  double timeSec = 0;
  float pitch = 0;
  float pressureAltFt = 0;
  float iasKt = 0;
  float tasKt = 0;
};

class G5Data {
public:
  TimedValue pitch;
  TimedValue pressureAlt;
  TimedValue ias;
  TimedValue tas;
  uint32_t packetCount = 0;
  uint32_t recognizedTokenCount = 0;
  uint32_t malformedTokenCount = 0;
  uint32_t lastPacketMs = 0;

  int parsePayload(const std::string &payload, uint32_t nowMs) {
    packetCount++;
    lastPacketMs = nowMs;
    int recognized = 0;
    size_t begin = 0;

    while (begin < payload.size()) {
      begin = payload.find_first_not_of(" \t\r\n", begin);
      if (begin == std::string::npos) break;
      size_t end = payload.find_first_of(" \t\r\n", begin);
      std::string token = payload.substr(begin, end - begin);
      begin = end == std::string::npos ? payload.size() : end;

      size_t equals = token.find('=');
      if (equals == std::string::npos || equals == 0 || equals + 1 >= token.size()) {
        malformedTokenCount++;
        continue;
      }

      std::string key = token.substr(0, equals);
      const char *valueText = token.c_str() + equals + 1;
      char *valueEnd = NULL;
      float value = strtof(valueText, &valueEnd);
      if (valueEnd == valueText || *valueEnd != '\0') {
        // Other ad-hoc tokens (for example NMEA strings) are not errors for
        // this application; they simply are not numeric fields we consume.
        continue;
      }

      if (key == "P") {
        pitch.update(value, nowMs);
      } else if (key == "PALT") {
        pressureAlt.update(value * FEET_PER_METER_CDC, nowMs);
      } else if (key == "IAS") {
        ias.update(value, nowMs);
      } else if (key == "TAS") {
        tas.update(value, nowMs);
      } else {
        continue;
      }
      recognized++;
      recognizedTokenCount++;
    }
    return recognized;
  }

  bool ready(uint32_t nowMs, uint32_t maximumAgeMs = 1000) const {
    return pitch.fresh(nowMs, maximumAgeMs) &&
           pressureAlt.fresh(nowMs, maximumAgeMs) &&
           ias.fresh(nowMs, maximumAgeMs) && tas.fresh(nowMs, maximumAgeMs);
  }

  G5Sample sample(double nowSec) const {
    G5Sample result;
    result.timeSec = nowSec;
    result.pitch = pitch.value;
    result.pressureAltFt = pressureAlt.value;
    result.iasKt = ias.value;
    result.tasKt = tas.value;
    return result;
  }
};

struct ValueStats {
  int count = 0;
  double mean = 0;
  double stddev = 0;
  double minimum = 0;
  double maximum = 0;
};

struct LinearStats {
  double slope = 0;
  double intercept = 0;
  double residualRmse = 0;
};

struct StabilitySnapshot {
  int sampleCount = 0;
  double windowSec = 0;
  ValueStats ias;
  ValueStats tas;
  ValueStats pitch;
  LinearStats iasTrend;
  LinearStats pitchTrend;
  LinearStats altitudeTrend;
  bool stable = false;
};

class StabilityWindow {
public:
  static const int capacity = 100;
  static constexpr double requiredWindowSec = 9.0;
  static constexpr double maximumIasStddevKt = 1.0;
  static constexpr double maximumIasTrendKtPerSec = 0.10;
  static constexpr double maximumPitchStddevDeg = 0.50;
  static constexpr double maximumPitchTrendDegPerSec = 0.05;
  static constexpr double maximumAltitudeFitRmseFt = 10.0;

  void add(const G5Sample &sample) {
    samples[next] = sample;
    next = (next + 1) % capacity;
    if (count < capacity) count++;
  }

  StabilitySnapshot calculate() const {
    StabilitySnapshot result;
    result.sampleCount = count;
    if (count == 0) return result;

    result.windowSec = orderedSample(count - 1).timeSec -
                       orderedSample(0).timeSec;
    result.ias = calculateValueStats(&G5Sample::iasKt);
    result.tas = calculateValueStats(&G5Sample::tasKt);
    result.pitch = calculateValueStats(&G5Sample::pitch);
    result.iasTrend = calculateLinearStats(&G5Sample::iasKt);
    result.pitchTrend = calculateLinearStats(&G5Sample::pitch);
    result.altitudeTrend = calculateLinearStats(&G5Sample::pressureAltFt);
    result.stable =
        result.windowSec >= requiredWindowSec &&
        result.ias.stddev <= maximumIasStddevKt &&
        fabs(result.iasTrend.slope) <= maximumIasTrendKtPerSec &&
        result.pitch.stddev <= maximumPitchStddevDeg &&
        fabs(result.pitchTrend.slope) <= maximumPitchTrendDegPerSec &&
        result.altitudeTrend.residualRmse <= maximumAltitudeFitRmseFt;
    return result;
  }

private:
  G5Sample samples[capacity];
  int count = 0;
  int next = 0;

  const G5Sample &orderedSample(int index) const {
    int oldest = count == capacity ? next : 0;
    return samples[(oldest + index) % capacity];
  }

  ValueStats calculateValueStats(float G5Sample::*field) const {
    ValueStats result;
    result.count = count;
    if (count == 0) return result;
    double sum = 0;
    double sumSquares = 0;
    result.minimum = result.maximum = orderedSample(0).*field;
    for (int i = 0; i < count; i++) {
      double value = orderedSample(i).*field;
      sum += value;
      sumSquares += value * value;
      result.minimum = std::min(result.minimum, value);
      result.maximum = std::max(result.maximum, value);
    }
    result.mean = sum / count;
    double variance = sumSquares / count - result.mean * result.mean;
    result.stddev = sqrt(std::max(0.0, variance));
    return result;
  }

  LinearStats calculateLinearStats(float G5Sample::*field) const {
    LinearStats result;
    if (count < 2) return result;
    double time0 = orderedSample(0).timeSec;
    double value0 = orderedSample(0).*field;
    double sumX = 0, sumY = 0, sumXX = 0, sumXY = 0;
    for (int i = 0; i < count; i++) {
      double x = orderedSample(i).timeSec - time0;
      double y = orderedSample(i).*field - value0;
      sumX += x;
      sumY += y;
      sumXX += x * x;
      sumXY += x * y;
    }
    double denominator = count * sumXX - sumX * sumX;
    if (fabs(denominator) < 1e-9) return result;
    result.slope = (count * sumXY - sumX * sumY) / denominator;
    result.intercept = (sumY - result.slope * sumX) / count;
    double residualSquares = 0;
    for (int i = 0; i < count; i++) {
      double x = orderedSample(i).timeSec - time0;
      double y = orderedSample(i).*field - value0;
      double residual = y - (result.intercept + result.slope * x);
      residualSquares += residual * residual;
    }
    result.residualRmse = sqrt(residualSquares / count);
    return result;
  }
};

class OnlineValueStats {
public:
  void clear() {
    count = 0;
    sum = sumSquares = 0;
    minimum = maximum = 0;
  }

  void add(double value) {
    if (count == 0) minimum = maximum = value;
    minimum = std::min(minimum, value);
    maximum = std::max(maximum, value);
    sum += value;
    sumSquares += value * value;
    count++;
  }

  ValueStats result() const {
    ValueStats value;
    value.count = count;
    if (count == 0) return value;
    value.mean = sum / count;
    double variance = sumSquares / count - value.mean * value.mean;
    value.stddev = sqrt(std::max(0.0, variance));
    value.minimum = minimum;
    value.maximum = maximum;
    return value;
  }

private:
  int count = 0;
  double sum = 0;
  double sumSquares = 0;
  double minimum = 0;
  double maximum = 0;
};

class OnlineLinearStats {
public:
  void clear() {
    count = 0;
    sumX = sumY = sumXX = sumXY = sumYY = 0;
  }

  void add(double x, double y) {
    sumX += x;
    sumY += y;
    sumXX += x * x;
    sumXY += x * y;
    sumYY += y * y;
    count++;
  }

  LinearStats result() const {
    LinearStats value;
    if (count < 2) return value;
    double denominator = count * sumXX - sumX * sumX;
    if (fabs(denominator) < 1e-9) return value;
    value.slope = (count * sumXY - sumX * sumY) / denominator;
    value.intercept = (sumY - value.slope * sumX) / count;
    double residualSquares = sumYY + count * value.intercept * value.intercept +
                             value.slope * value.slope * sumXX -
                             2 * value.intercept * sumY -
                             2 * value.slope * sumXY +
                             2 * value.intercept * value.slope * sumX;
    value.residualRmse = sqrt(std::max(0.0, residualSquares / count));
    return value;
  }

private:
  int count = 0;
  double sumX = 0, sumY = 0, sumXX = 0, sumXY = 0, sumYY = 0;
};

struct RunSummary {
  int runNumber = 0;
  int sampleCount = 0;
  double durationSec = 0;
  ValueStats ias;
  ValueStats tas;
  ValueStats pitch;
  double startPressureAltFt = 0;
  double endPressureAltFt = 0;
  double verticalSpeedFpm = 0;
  double altitudeFitRmseFt = 0;
};

class RunAccumulator {
public:
  bool active = false;

  void start(const G5Sample &sample) {
    active = true;
    sampleCount = 0;
    startTimeSec = sample.timeSec;
    lastTimeSec = sample.timeSec;
    startPressureAltFt = sample.pressureAltFt;
    endPressureAltFt = sample.pressureAltFt;
    ias.clear();
    tas.clear();
    pitch.clear();
    altitude.clear();
  }

  void add(const G5Sample &sample) {
    if (!active) return;
    double time = sample.timeSec - startTimeSec;
    double relativeAltitude = sample.pressureAltFt - startPressureAltFt;
    ias.add(sample.iasKt);
    tas.add(sample.tasKt);
    pitch.add(sample.pitch);
    altitude.add(time, relativeAltitude);
    lastTimeSec = sample.timeSec;
    endPressureAltFt = sample.pressureAltFt;
    sampleCount++;
  }

  bool stop(int runNumber, RunSummary &summary) {
    if (!active) return false;
    active = false;
    if (sampleCount < 2) return false;
    LinearStats altitudeResult = altitude.result();
    summary.runNumber = runNumber;
    summary.sampleCount = sampleCount;
    summary.durationSec = lastTimeSec - startTimeSec;
    summary.ias = ias.result();
    summary.tas = tas.result();
    summary.pitch = pitch.result();
    summary.startPressureAltFt = startPressureAltFt;
    summary.endPressureAltFt = endPressureAltFt;
    summary.verticalSpeedFpm = altitudeResult.slope * 60.0;
    summary.altitudeFitRmseFt = altitudeResult.residualRmse;
    return true;
  }

  int samples() const { return sampleCount; }
  double duration(double nowSec) const {
    return active ? nowSec - startTimeSec : 0;
  }

private:
  int sampleCount = 0;
  double startTimeSec = 0;
  double lastTimeSec = 0;
  double startPressureAltFt = 0;
  double endPressureAltFt = 0;
  OnlineValueStats ias;
  OnlineValueStats tas;
  OnlineValueStats pitch;
  OnlineLinearStats altitude;
};
