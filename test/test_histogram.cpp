#include "renderlib/Histogram.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <numeric>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

struct DirectScanHistogram
{
  uint16_t dataMin = 0;
  uint16_t dataMax = 0;
  size_t dataMinIdx = 0;
  size_t dataMaxIdx = 0;
  uint16_t filteredMin = 0;
  uint16_t filteredMax = 0;
  size_t modalDisplayBin = 0;
  std::vector<uint32_t> bins = std::vector<uint32_t>(4096, 0);
  std::vector<uint32_t> displayBins = std::vector<uint32_t>(512, 0);
};

// Reference implementation of the former three-pass construction. Keep it in
// the test so the optimized constructor can be checked at bin boundaries.
DirectScanHistogram
makeDirectScanHistogram(const std::vector<uint16_t>& data)
{
  DirectScanHistogram result;
  result.dataMin = data.front();
  result.dataMax = data.front();
  for (size_t i = 0; i < data.size(); ++i) {
    if (data[i] > result.dataMax) {
      result.dataMax = data[i];
      result.dataMaxIdx = i;
    } else if (data[i] < result.dataMin) {
      result.dataMin = data[i];
      result.dataMinIdx = i;
    }
  }

  const float rangeMin = static_cast<float>(result.dataMin);
  float range = static_cast<float>(result.dataMax - result.dataMin);
  if (range == 0.0f) {
    range = 1.0f;
  }
  const float invRange = 1.0f / range;
  for (const uint16_t value : data) {
    int64_t bin = static_cast<int64_t>(
      (static_cast<float>(value - rangeMin) * invRange * static_cast<float>(result.bins.size() - 1)) + 0.5f);
    bin = std::clamp<int64_t>(bin, 0, static_cast<int64_t>(result.bins.size() - 1));
    ++result.bins[static_cast<size_t>(bin)];
  }

  const uint64_t total = std::accumulate(result.bins.begin(), result.bins.end(), uint64_t(0));
  const uint64_t targetLo = static_cast<uint64_t>(total * 0.001 + 0.5);
  const uint64_t targetHi = static_cast<uint64_t>(total * 0.999 + 0.5);
  uint64_t cumulative = 0;
  size_t loBin = 0;
  size_t hiBin = result.bins.size() - 1;
  bool foundLo = false;
  for (size_t i = 0; i < result.bins.size(); ++i) {
    cumulative += result.bins[i];
    if (cumulative > targetLo && !foundLo) {
      loBin = i;
      foundLo = true;
    }
    if (cumulative >= targetHi) {
      hiBin = i;
      break;
    }
  }
  if (hiBin - loBin < 3) {
    result.filteredMin = result.dataMin;
    result.filteredMax = result.dataMax;
  } else {
    result.filteredMin = static_cast<uint16_t>(rangeMin + loBin * (range / (result.bins.size() - 1)) + 0.5f);
    result.filteredMax = static_cast<uint16_t>(rangeMin + hiBin * (range / (result.bins.size() - 1)) + 0.5f);
  }

  float filteredRange = result.filteredMax - result.filteredMin;
  if (filteredRange == 0.0f) {
    filteredRange = 1.0f;
  }
  const float displayMax = static_cast<float>(result.displayBins.size() - 1);
  for (const uint16_t value : data) {
    int64_t bin =
      static_cast<int64_t>((static_cast<float>(value - result.filteredMin) / filteredRange * displayMax) + 0.5f);
    if (bin < 0) {
      bin = 0;
    } else if (bin >= static_cast<int64_t>(result.displayBins.size())) {
      bin = result.displayBins.size() - 1;
    }
    ++result.displayBins[static_cast<size_t>(bin)];
  }
  for (size_t i = 1; i < result.displayBins.size(); ++i) {
    if (result.displayBins[i] > result.displayBins[result.modalDisplayBin]) {
      result.modalDisplayBin = i;
    }
  }
  return result;
}

} // namespace

TEST_CASE("Histogram edge cases are stable", "[histogram]")
{
  SECTION("Histogram of single value")
  {
    static const uint16_t VALUE = 257;
    uint16_t data[] = { VALUE, VALUE, VALUE };
    int COUNT = sizeof(data) / sizeof(data[0]);
    Histogram h(data, COUNT);

    REQUIRE(h.getPixelCount() == COUNT);
    REQUIRE(h.getDataMax() == VALUE);
    REQUIRE(h.getDataMin() == VALUE);

    // all data in first bin
    REQUIRE(h.getDisplayBinCount(0) == h.getPixelCount());
  }

  SECTION("Histogram of binary segmentation")
  {
    static const uint16_t VALUE = 257;
    uint16_t data[] = { 0, 0, VALUE, VALUE };
    int COUNT = sizeof(data) / sizeof(data[0]);
    Histogram h(data, COUNT);

    REQUIRE(h.getPixelCount() == COUNT);
    REQUIRE(h.getDataMax() == VALUE);
    REQUIRE(h.getDataMin() == 0);

    // only 2 bins with data
    REQUIRE(h.getDisplayBinCount(0) == 2);
    REQUIRE(h.getDisplayBinCount(h.getNumDisplayBins() - 1) == 2);
  }

  SECTION("Histogram binning accuracy is good")
  {
    uint16_t data[] = { 0, 0, 1, 1, 2, 2, 510, 510, 511, 511, 512, 512 };
    int COUNT = sizeof(data) / sizeof(data[0]);
    Histogram h(data, COUNT);

    REQUIRE(h.getPixelCount() == COUNT);
    REQUIRE(h.getDataMax() == 512);
    REQUIRE(h.getDataMin() == 0);

    REQUIRE(h.getDisplayBinCount(0) == 2);
    REQUIRE(h.getDisplayBinCount(1) == 2);
    REQUIRE(h.getDisplayBinCount(2) == 2);
    REQUIRE(h.getDisplayBinCount(h.getNumDisplayBins() / 2 - 1) == 0);
    REQUIRE(h.getDisplayBinCount(h.getNumDisplayBins() - 2) == 2);
    REQUIRE(h.getDisplayBinCount(h.getNumDisplayBins() - 1) == 2);
  }
}

TEST_CASE("Histogram LUT generation is working", "[histogram]")
{
  SECTION("Simple linear gradient is working")
  {
    Histogram h(nullptr, 0);

    // simple straight line
    std::vector<LutControlPoint> pts = { { 0.0f, 0.0f }, { 1.0f, 1.0f } };
    float* lut = h.generate_controlPoints(pts);

    REQUIRE(lut[0] == 0.0);
    REQUIRE(lut[127] < 0.5);
    REQUIRE(lut[128] > 0.5);
    REQUIRE(lut[255] == 1.0);
  }

  SECTION("Step function control points generate a proper Lut")
  {
    Histogram h(nullptr, 0);

    // discontinuity at 0.5
    std::vector<LutControlPoint> pts = { { 0.0f, 0.0f }, { 0.5f, 0.0f }, { 0.5f, 1.0f }, { 1.0f, 1.0f } };
    float* lut = h.generate_controlPoints(pts);

    REQUIRE(lut[0] == 0.0);
    REQUIRE(lut[127] == 0.0);
    REQUIRE(lut[128] == 1.0);
    REQUIRE(lut[255] == 1.0);
  }

  SECTION("Non-degenerate Step function control points generate a proper Lut")
  {
    Histogram h(nullptr, 0);

    // discontinuity at 0.5
    std::vector<LutControlPoint> pts = { { 0.0f, 0.0f }, { 0.49999f, 0.0f }, { 0.500001f, 1.0f }, { 1.0f, 1.0f } };
    float* lut = h.generate_controlPoints(pts);

    REQUIRE(lut[0] == 0.0);
    REQUIRE(lut[127] == 0.0);
    REQUIRE(lut[128] == 1.0);
    REQUIRE(lut[255] == 1.0);
  }

  SECTION("Control points starting at non-zero will fill in")
  {
    Histogram h(nullptr, 0);

    // discontinuity at 0.5
    std::vector<LutControlPoint> pts = { { 0.5f, 0.5f }, { 1.0f, 1.0f } };
    float* lut = h.generate_controlPoints(pts);

    REQUIRE(lut[0] == 0.0);
    REQUIRE(lut[1] == 0.0);
    REQUIRE(lut[127] == 0.0);
    REQUIRE(lut[128] > 0.5);
    REQUIRE(lut[255] == 1.0);
  }

  SECTION("Control points aligned with lut array values are exact")
  {
    Histogram h(nullptr, 0);

    // value at 16/255, and at 197/255
    std::vector<LutControlPoint> pts = {
      { 0.0f, 0.0f }, { 16.0f / 255.0f, 0.5f }, { 197.0f / 255.0f, 0.75f }, { 1.0f, 1.0f }
    };
    float* lut = h.generate_controlPoints(pts);

    REQUIRE(lut[16] == 0.5);
    REQUIRE(lut[197] == 0.75);
  }
}
TEST_CASE("Histogram outlier filtering works correctly", "[histogram]")
{
  SECTION("Outlier filtering excludes extreme values in continuous data")
  {
    // Create data with extreme outliers: mostly values 100-200, but with extreme min/max
    std::vector<uint16_t> data;
    data.push_back(0); // extreme low outlier (single pixel)
    for (int i = 0; i < 1000; ++i) {
      data.push_back(100 + (i % 101)); // main data range 100-200
    }
    data.push_back(65535); // extreme high outlier (single pixel)

    Histogram hFiltered(data.data(), data.size());

    // Unfiltered should use full range
    REQUIRE(hFiltered.getDataMin() == 0);
    REQUIRE(hFiltered.getDataMax() == 65535);

    // Filtered should exclude the outliers
    // Allow some discretization error from binning (bin width ~16 for this data range)
    REQUIRE(hFiltered.getFilteredMin() > 50); // Well above 0, excluding low outlier
    REQUIRE(hFiltered.getFilteredMax() < 65535);
    REQUIRE(hFiltered.getFilteredMin() < 210); // Within or near main data range
    REQUIRE(hFiltered.getFilteredMax() <= 210);
  }

  SECTION("Outlier filtering preserves label data when disabled")
  {
    // Label data: 0, 1, 2, 3, 4 uniformly distributed
    std::vector<uint16_t> data;
    for (int label = 0; label <= 4; ++label) {
      for (int count = 0; count < 200; ++count) {
        data.push_back(label);
      }
    }

    // Without outlier filtering (for label data)
    Histogram h(data.data(), data.size());

    REQUIRE(h.getDataMin() == 0);
    REQUIRE(h.getDataMax() == 4);
    REQUIRE(h.getPixelCount() == 1000);
  }

  SECTION("Outlier filtering handles gaussian-like distribution")
  {
    // Create data with normal-like distribution centered at 1000 with a few extreme outliers
    // Need enough samples for 0.1% to be meaningful (at least 1000 samples)
    std::vector<uint16_t> data;
    data.push_back(0); // extreme outlier

    // Main distribution around 1000 (5000 samples)
    for (int i = 0; i < 5000; ++i) {
      data.push_back(950 + (i % 101)); // 950-1050
    }

    data.push_back(10000); // extreme outlier

    Histogram hFiltered(data.data(), data.size());

    // Unfiltered includes outliers
    REQUIRE(hFiltered.getDataMin() == 0);
    REQUIRE(hFiltered.getDataMax() == 10000);

    // Filtered excludes outliers
    REQUIRE(hFiltered.getFilteredMin() >= 950);
    REQUIRE(hFiltered.getFilteredMax() <= 1050);
  }

  SECTION("Outlier filtering handles uniform distribution correctly")
  {
    // Uniform distribution from 1000 to 2000
    std::vector<uint16_t> data;
    for (int i = 1000; i <= 2000; ++i) {
      data.push_back(i);
    }

    Histogram h(data.data(), data.size());

    // Should preserve most of the range (maybe trim very edges)
    REQUIRE(h.getFilteredMin() > 1000);
    REQUIRE(h.getFilteredMin() < 1010);
    REQUIRE(h.getFilteredMax() > 1990);
    REQUIRE(h.getFilteredMax() < 2000);
  }

  SECTION("Outlier filtering is stable with all identical values")
  {
    uint16_t data[] = { 500, 500, 500, 500, 500 };
    int COUNT = sizeof(data) / sizeof(data[0]);

    Histogram hFiltered(data, COUNT);
    Histogram hUnfiltered(data, COUNT);

    // Both should handle identical values the same way
    REQUIRE(hFiltered.getDataMin() == 500);
    REQUIRE(hFiltered.getDataMax() == 500);
    REQUIRE(hUnfiltered.getDataMin() == 500);
    REQUIRE(hUnfiltered.getDataMax() == 500);
  }

  SECTION("Outlier filtering with very sparse outliers")
  {
    // 99.8% of data in narrow range, 0.1% each extreme outliers
    std::vector<uint16_t> data;

    // Add low outliers (0.1%)
    for (int i = 0; i < 50; ++i) {
      data.push_back(10);
    }

    // Main data (99.8%)
    for (int i = 0; i < 49900; ++i) {
      data.push_back(1000 + (i % 100)); // 1000-1099
    }

    // Add high outliers (0.1%)
    for (int i = 0; i < 50; ++i) {
      data.push_back(50000);
    }

    Histogram hFiltered(data.data(), data.size());

    // Should exclude the extreme outliers at 10 and 50000
    // Allow some discretization error from binning (bin width ~12 for this data range)
    REQUIRE(hFiltered.getFilteredMin() > 100);  // Well above 10, excluding outliers
    REQUIRE(hFiltered.getFilteredMin() < 1100); // Within main data range
    REQUIRE(hFiltered.getFilteredMax() < 50000);
  }

  SECTION("Outlier filtering maintains correct bin counts")
  {
    // Data with outliers
    std::vector<uint16_t> data;
    data.push_back(0); // outlier
    for (int i = 0; i < 1000; ++i) {
      data.push_back(1000 + (i % 100)); // main range: 1000-1099
    }
    data.push_back(20000); // outlier

    Histogram h(data.data(), data.size());

    // Total pixel count should still be correct
    REQUIRE(h.getPixelCount() == 1002);

    // Outliers should be clamped to boundary bins
    REQUIRE(h.getDisplayBinCount(0) > 0);                         // Low outlier clamped to first bin
    REQUIRE(h.getDisplayBinCount(h.getNumDisplayBins() - 1) > 0); // High outlier clamped to last bin
  }
}

TEST_CASE("Histogram binRange calculation uses robust range", "[histogram]")
{
  SECTION("binRange uses filtered data range, not absolute range")
  {
    // Data with outliers
    std::vector<uint16_t> data;
    data.push_back(0); // outlier
    for (int i = 0; i < 1000; ++i) {
      data.push_back(1000 + i % 100); // 1000-1099
    }
    data.push_back(65535); // outlier

    Histogram hFiltered(data.data(), data.size());

    float firstBinCenter, lastBinCenter, binSize;
    hFiltered.binRange(
      512, hFiltered.getFilteredMin(), hFiltered.getFilteredMax(), firstBinCenter, lastBinCenter, binSize);

    // Bin range should correspond to filtered range, not 0-65535
    // Allow some discretization error from binning (bin width ~16 for this data range)
    REQUIRE(firstBinCenter > 900.0f); // Well above 0, excluding low outlier
    REQUIRE(firstBinCenter < 1100.0f);
    REQUIRE(lastBinCenter >= 1000.0f);
    REQUIRE(lastBinCenter < 1200.0f);

    // Bin size should be much smaller than if full range was used
    REQUIRE(binSize < 1.0f); // With full range, binSize would be ~128
  }
}

TEST_CASE("Histogram computeForDisplay supports visible range bins", "[histogram]")
{
  std::vector<uint16_t> data;
  for (uint16_t value = 0; value < 100; ++value) {
    data.push_back(value);
  }

  Histogram h(data.data(), data.size());

  SECTION("Full range preserves all counts")
  {
    auto bins = h.computeForDisplay(static_cast<float>(h.getDataMin()), static_cast<float>(h.getDataMax()), 64);
    REQUIRE(bins.size() == 64);

    uint64_t sum = 0;
    for (uint32_t count : bins) {
      sum += count;
    }
    REQUIRE(sum == h.getPixelCount());
  }

  SECTION("Narrow visible range yields subset of counts")
  {
    auto bins = h.computeForDisplay(20.0f, 29.0f, 32);
    REQUIRE(bins.size() == 32);

    uint64_t sum = 0;
    for (uint32_t count : bins) {
      sum += count;
    }

    REQUIRE(sum < h.getPixelCount());
    REQUIRE(sum > 0);
  }
}

TEST_CASE("Histogram construction matches direct-scan bins", "[histogram]")
{
  std::vector<uint16_t> data(200003);
  uint32_t state = 0x12345678u;
  for (size_t i = 0; i < data.size(); ++i) {
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    data[i] = (i % 19u == 0u) ? static_cast<uint16_t>(state & 0xffffu)
                              : static_cast<uint16_t>(1000u + ((state >> 16u) & 1023u));
  }
  data[0] = 65535;
  data[1] = 0;

  const DirectScanHistogram expected = makeDirectScanHistogram(data);
  Histogram actual(data.data(), data.size());
  REQUIRE(expected.dataMin < expected.filteredMin);
  REQUIRE(expected.filteredMax < expected.dataMax);
  REQUIRE(actual.getDataMin() == expected.dataMin);
  REQUIRE(actual.getDataMax() == expected.dataMax);
  REQUIRE(actual.getDataMinIdx() == expected.dataMinIdx);
  REQUIRE(actual.getDataMaxIdx() == expected.dataMaxIdx);
  REQUIRE(actual.getFilteredMin() == expected.filteredMin);
  REQUIRE(actual.getFilteredMax() == expected.filteredMax);
  REQUIRE(actual.getModalDisplayBin() == expected.modalDisplayBin);
  for (size_t bin = 0; bin < expected.displayBins.size(); ++bin) {
    INFO("display bin " << bin);
    REQUIRE(actual.getDisplayBinCount(bin) == expected.displayBins[bin]);
  }

  for (uint32_t intensity = 0; intensity <= 65535u; intensity += 257u) {
    float percentile = -1.0f;
    actual.computePercentile(static_cast<uint16_t>(intensity), percentile);
    float expectedPercentile = 0.0f;
    if (intensity >= expected.dataMax) {
      expectedPercentile = 1.0f;
    } else if (intensity > expected.dataMin) {
      const size_t bin = actual.getBinOfIntensity(static_cast<uint16_t>(intensity));
      const size_t count = std::accumulate(expected.bins.begin(), expected.bins.begin() + bin, size_t(0));
      expectedPercentile = static_cast<float>(count) / static_cast<float>(data.size());
    }
    REQUIRE(percentile == expectedPercentile);
  }

  const size_t lowLimit = static_cast<size_t>(data.size() * Histogram::DEFAULT_PCT_LOW);
  const size_t highLimit = static_cast<size_t>(data.size() * Histogram::DEFAULT_PCT_HIGH);
  size_t lowBin = 0;
  size_t highBin = 0;
  size_t count = 0;
  bool foundLow = false;
  for (size_t i = 0; i < expected.bins.size(); ++i) {
    count += expected.bins[i];
    if (count > lowLimit && !foundLow) {
      lowBin = i;
      foundLow = true;
    }
    if (count > highLimit) {
      highBin = i;
      break;
    }
  }
  const float expectedWindow = static_cast<float>(highBin - lowBin) / static_cast<float>(expected.bins.size() - 1);
  const float expectedLevel =
    static_cast<float>(lowBin + highBin) * 0.5f / static_cast<float>(expected.bins.size() - 1);
  float actualWindow = 0.0f;
  float actualLevel = 0.0f;
  actual.computeWindowLevelFromPercentiles(
    Histogram::DEFAULT_PCT_LOW, Histogram::DEFAULT_PCT_HIGH, actualWindow, actualLevel);
  REQUIRE(actualWindow == expectedWindow);
  REQUIRE(actualLevel == expectedLevel);
  std::unique_ptr<float[]> lut(actual.generate_percentiles());
  for (size_t i = 0; i < 256; ++i) {
    const float normalized = static_cast<float>(i) / 255.0f;
    const float ramp = (normalized - (expectedLevel - expectedWindow * 0.5f)) / expectedWindow;
    REQUIRE(lut[i] == std::clamp(ramp, 0.0f, 1.0f));
  }
}

TEST_CASE("Histogram construction benchmark", "[.histogram-benchmark]")
{
  // A 128 MiB channel is larger than CPU caches and has a low-intensity
  // background plus sparse higher-intensity signal, like the GUI load case.
  constexpr size_t voxelCount = 1u << 26;
  std::vector<uint16_t> data(voxelCount);
  for (size_t i = 0; i < data.size(); ++i) {
    const uint32_t bits = static_cast<uint32_t>(i) * 1664525u + 1013904223u;
    const int sum = static_cast<int>(bits & 15u) + static_cast<int>((bits >> 4u) & 15u) +
                    static_cast<int>((bits >> 8u) & 15u) + static_cast<int>((bits >> 12u) & 15u);
    data[i] = (i % 14u == 0u) ? static_cast<uint16_t>(160u + ((bits >> 16u) % 96u))
                              : static_cast<uint16_t>(24 + (sum - 30) / 2);
  }

  std::vector<double> durations;
  durations.reserve(3);
  volatile size_t result = 0;
  for (int run = 0; run < 3; ++run) {
    const auto start = std::chrono::steady_clock::now();
    Histogram histogram(data.data(), data.size());
    const auto stop = std::chrono::steady_clock::now();
    durations.push_back(std::chrono::duration<double, std::milli>(stop - start).count());
    result += histogram.getDisplayBinCount(0) + histogram.getDataMax();
  }
  std::sort(durations.begin(), durations.end());
  std::cout << "Histogram 128 MiB channel median: " << durations[1] << " ms; checksum " << result << '\n';
  REQUIRE(result > 0);
}
