#include "Histogram.h"

#include "GradientData.h"
#include "Logging.h"
#include "MathUtil.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <math.h>
#include <numeric>

static constexpr size_t HIGH_RES_BINS = 4096;
// for display:
static constexpr size_t FILTERED_BINS = 512;

template<class T>
static T
clamp(const T& v, const T& lo, const T& hi)
{
  assert(hi > lo);
  return (v < lo) ? lo : (v > hi ? hi : v);
}

size_t
Histogram::getBinOfIntensity(uint16_t intensity) const
{
  if (intensity <= _dataMin) {
    return 0;
  } else if (intensity >= _dataMax) {
    return _bins.size() - 1;
  } else {
    static constexpr float ROUNDING_OFFSET = 0.5f;
    size_t whichbin =
      (size_t)((float)(intensity - _dataMin) / (_dataMax - _dataMin) * (_bins.size() - 1) + ROUNDING_OFFSET);
    return whichbin;
  }
}

Histogram::Histogram(uint16_t* data, size_t length)
  : _bins(HIGH_RES_BINS)
  , _filteredBins(FILTERED_BINS)
  , _ccounts(HIGH_RES_BINS)
  , _dataMin(0)
  , _dataMax(0)
  , _dataMinIdx(0)
  , _dataMaxIdx(0)
  , _filteredMin(0)
  , _filteredMax(0)
  , _pixelCount(0)
{
  std::fill(_bins.begin(), _bins.end(), 0);
  std::fill(_filteredBins.begin(), _filteredBins.end(), 0);
  std::fill(_ccounts.begin(), _ccounts.end(), 0);

  if (!data || length == 0) {
    // empty histogram, just return with all zeros.
    return;
  }

  _pixelCount = length;
  _dataMin = data[0];
  _dataMax = data[0];
  _filteredMax = data[0];
  _filteredMin = data[0];

  // Record exact uint16 intensities while finding the extrema. The display
  // histograms can then be derived from this small table instead of scanning
  // the entire volume two more times. This keeps histogram construction
  // independent of how the image was loaded or converted.
  constexpr size_t intensityCount = static_cast<size_t>(std::numeric_limits<uint16_t>::max()) + 1;
  std::vector<uint32_t> intensityCounts(intensityCount, 0);
  uint16_t val;
  for (size_t i = 0; i < length; ++i) {
    val = data[i];
    ++intensityCounts[val];
    if (val > _dataMax) {
      _dataMax = val;
      _dataMaxIdx = i;
    } else if (val < _dataMin) {
      _dataMin = val;
      _dataMinIdx = i;
    }
  }
  _filteredMax = _dataMax;
  _filteredMin = _dataMin;

  //	float fval;
  float rangeMin = (float)_dataMin;
  float rangeMax = (float)_dataMax;
  float range = (float)(rangeMax - rangeMin);
  if (range == 0.0f) {
    range = 1.0f;
  }
  float invRange = 1.0f / range;

  // compute a high resolution histogram first
  // we will use this for percentile outlier filtering, for display histogram.
  for (uint32_t intensity = _dataMin; intensity <= _dataMax; ++intensity) {
    const uint32_t count = intensityCounts[intensity];
    if (count == 0) {
      continue;
    }
    int64_t whichbin = (int64_t)((float)(intensity - rangeMin) * invRange * (float)(HIGH_RES_BINS - 1) + 0.5f);
    if (whichbin >= HIGH_RES_BINS) {
      whichbin = HIGH_RES_BINS - 1;
    } else if (whichbin < 0) {
      whichbin = 0;
    }
    _bins[whichbin] += count;
  }

  // now we will do outlier filtering based on percentiles.
  uint64_t total = std::accumulate(_bins.begin(), _bins.end(), uint64_t(0));
  assert(total == length);
  uint64_t targetLo = (uint64_t)(total * 0.001 + 0.5); // P0.1
  uint64_t targetHi = (uint64_t)(total * 0.999 + 0.5); // P99.9

  uint64_t cumulativeSum = 0;
  size_t loBin = 0;
  size_t hiBin = HIGH_RES_BINS - 1;
  bool foundLo = false;
  for (size_t i = 0; i < HIGH_RES_BINS; ++i) {
    cumulativeSum += _bins[i];
    if (cumulativeSum > targetLo && !foundLo) {
      loBin = i;
      foundLo = true;
    }
    if (cumulativeSum >= targetHi) {
      hiBin = i;
      break;
    }
  }
  // add cumulative counts into the ccounts array
  std::partial_sum(_bins.begin(), _bins.end(), _ccounts.begin(), std::plus<uint32_t>());

  if (abs((int64_t)(hiBin - loBin)) < 3) {
    // if the number of bins separating the percentiles is too small,
    // just don't filter anything.
    // This can happen if the data is very low contrast (e.g. few intensity values close together),
    // or if there are a lot of outliers.
    loBin = 0;
    hiBin = HIGH_RES_BINS - 1;
    _filteredMin = _dataMin;
    _filteredMax = _dataMax;
  } else {
    float loVal = rangeMin + loBin * (range / (float)(HIGH_RES_BINS - 1));
    float hiVal = rangeMin + hiBin * (range / (float)(HIGH_RES_BINS - 1));
    _filteredMin = (uint16_t)(loVal + 0.5f);
    _filteredMax = (uint16_t)(hiVal + 0.5f);
  }

  float filteredRange = _filteredMax - _filteredMin;
  if (filteredRange == 0.0f) {
    filteredRange = 1.0f;
  }

  float binmax = (float)(FILTERED_BINS - 1);
  for (uint32_t intensity = _dataMin; intensity <= _dataMax; ++intensity) {
    const uint32_t count = intensityCounts[intensity];
    if (count == 0) {
      continue;
    }
    int64_t whichbin =
      (int64_t)((float)(static_cast<int32_t>(intensity) - _filteredMin) / filteredRange * binmax + 0.5f);
    if (whichbin >= FILTERED_BINS) {
      whichbin = FILTERED_BINS - 1;
    } else if (whichbin < 0) {
      whichbin = 0;
    }
    _filteredBins[whichbin] += count;
  }

  // get the bin with the most frequently occurring value
  _maxFilteredBin = 0;
  uint32_t curmax = _filteredBins[0];
  for (size_t i = 1; i < _filteredBins.size(); i++) {
    if (_filteredBins[i] > curmax) {
      _maxFilteredBin = i;
      curmax = _filteredBins[i];
    }
  }

  // last ccount bin should have total number of intensities.
  assert(_pixelCount == _ccounts[_ccounts.size() - 1]);
}

float*
Histogram::generate_fullRange(size_t length) const
{
  float window = 1.0;
  float level = 0.5;
  return generate_windowLevel(window, level, length);
}

float*
Histogram::generate_dataRange(size_t length) const
{
  float window = 1.0;
  float level = 0.5;
  return generate_windowLevel(window, level, length);
}

float*
Histogram::generate_bestFit(size_t length) const
{
  size_t pixcount = _pixelCount;
  size_t limit = pixcount / 10;

  size_t i = 0;
  size_t count = 0;
  for (i = 0; i < _bins.size(); ++i) {
    count += _bins[i];
    if (count > limit) {
      break;
    }
  }
  size_t hmin = i;

  count = 0;
  for (i = _bins.size() - 1; i >= 0; --i) {
    count += _bins[i];
    if (count > limit) {
      break;
    }
  }
  size_t hmax = i;

  size_t range = hmax - hmin;
  if (range < 1) {
    range = _bins.size() - 1;
  }

  float window = (float)(range) / (float)(_bins.size() - 1);
  float level = ((float)hmin + (float)range * 0.5f) / (float)(_bins.size() - 1);
  return generate_windowLevel(window, level, length);
}

// attempt to redo imagej's Auto
float*
Histogram::generate_auto2(size_t length) const
{

  size_t AUTO_THRESHOLD = 10000;
  size_t pixcount = _pixelCount; // -_bins[0];
  //  const pixcount = this.imgData.data.length;
  size_t limit = pixcount / 10;
  size_t threshold = pixcount / AUTO_THRESHOLD;

  size_t nbins = _bins.size();

#if 0
	size_t i = -1;
	bool found = false;
	int count;
	do {
		i++;
		count = _bins[i];
		if (count>limit) count = 0;
		found = count> threshold;
	} while (!found && i<(nbins-1));
	size_t hmin = i;
	i = nbins;
	do {
		i--;
		count = _bins[i];
		if (count>limit) count = 0;
		found = count > threshold;
	} while (!found && i > 0);
	size_t hmax = i;
#endif

  // #if 0
  //  this will skip the "zero" bin which contains pixels of zero intensity.
  size_t hmin = nbins - 1;
  size_t hmax = 0;
  for (size_t i = 0; i < nbins; ++i) {
    if (_bins[i] > threshold && _bins[i] <= limit) {
      hmin = i;
      break;
    }
  }
  // need a signed loop control variable here to test for >= 0.
  for (int i = (int)nbins - 1; i >= 0; --i) {
    if (_bins[i] > threshold && _bins[i] <= limit) {
      hmax = i;
      break;
    }
  }
  // #endif

  if (hmax < hmin) {
    // just reset to whole range in this case.
    return generate_fullRange(length);
  } else {
    // LOG_DEBUG << "auto2 range: " << hmin << "..." << hmax;
    float range = (float)hmax - (float)hmin;
    float window = (range) / (float)(nbins - 1);
    float level = ((float)hmin + range * 0.5f) / (float)(nbins - 1);
    // LOG_DEBUG << "auto2 window/level: " << window << " / " << level;
    return generate_windowLevel(window, level, length);
  }
}

float*
Histogram::generate_auto(size_t length) const
{

  // simple linear mapping cutting elements with small appearance
  // get 10% threshold
  float PERCENTAGE = 0.1f;
  // get a count of pixels in the most frequent filtered bin
  float th = std::floor(_filteredBins[_maxFilteredBin] * PERCENTAGE);
  // we will use the value from the filtered bins, which should be higher
  // than if unfiltered bins were used,
  // and then walk the unfiltered bins to find the first and last bins that exceed this threshold.
  size_t b = 0;
  size_t e = _bins.size() - 1;
  for (size_t x = 0; x < _bins.size(); ++x) {
    if (_bins[x] > th) {
      b = x;
      break;
    }
  }
  for (size_t x = _bins.size() - 1; x >= 0; --x) {
    if (_bins[x] > th) {
      e = x;
      break;
    }
  }

  size_t range = e - b;
  if (range < 1) {
    range = _bins.size() - 1;
    b = 0;
  }
  // if range == e-b, then
  // b+range/2 === e-range/2
  //
  float window = (float)range / (float)(_bins.size() - 1);
  float level = ((float)b + (float)range * 0.5f) / (float)(_bins.size() - 1);
  return generate_windowLevel(window, level, length);
}

/**
 * Generate a piecewise linear lookup table that ramps up from 0 to 1 over the b to e domain
 *  |
 * 1|               +---------+-----
 *  |              /
 *  |             /
 *  |            /
 *  |           /
 *  |          /
 * 0+=========+---------------+-----
 *  0         b    e          1
 * window = e-b      width of range e,b
 * level = 0.5*(e+b) midpoint of e,b
 */
// window and level are percentages of full range 0..1
float*
Histogram::generate_windowLevel(float window, float level, size_t length) const
{
  // return a LUT with new values(?)
  // data type of lut values is out_phys_range (uint8)
  // length of lut is number of histogram bins (represents the input data range)
  float* lut = new float[length];

  float a = (level - window * 0.5f);
  float b = (level + window * 0.5f);
  // b-a should be equal to window!
  assert(fabs(b - a - window) < 0.0001);
  float range = window;
  // float range = b - a;

  for (size_t x = 0; x < length; ++x) {
    float v = ((float)x / (float)(length - 1) - a) / range;
    lut[x] = clamp(v, 0.0f, 1.0f);
  }

  return lut;
}

void
Histogram::computeWindowLevelFromPercentiles(float pct_low, float pct_high, float& window, float& level) const
{
  // e.g. 0.50, 0.983 starts from 50th percentile bucket and ends at 98.3 percentile bucket.
  if (pct_low > pct_high) {
    std::swap(pct_high, pct_low);
  }

  size_t length = _bins.size();

  size_t lowlimit = size_t(_pixelCount * pct_low);
  size_t hilimit = size_t(_pixelCount * pct_high);

  // TODO use _ccounts in these loops!!

  size_t i = 0;
  size_t count = 0;
  for (i = 0; i < _bins.size(); ++i) {
    count += _bins[i];
    if (count > lowlimit) {
      break;
    }
  }
  size_t hmin = i;

  count = 0;
  for (i = 0; i < _bins.size(); ++i) {
    count += _bins[i];
    if (count > hilimit) {
      break;
    }
  }
  size_t hmax = i;

  // calculate a window and level that are percentages of the full range (0 .. length-1)
  window = (float)(hmax - hmin) / (float)(length - 1);
  level = (float)(hmin + hmax) * 0.5f / (float)(length - 1);
}

float*
Histogram::generate_percentiles(float lo, float hi, size_t length) const
{
  float window, level;
  computeWindowLevelFromPercentiles(lo, hi, window, level);
  return generate_windowLevel(window, level, length);
}

float*
Histogram::generate_controlPoints(std::vector<LutControlPoint> pts, size_t length) const
{
  // pts is piecewise linear from first to last control point.
  // pts is in order of increasing x value (the first element of the pair)
  // pts[0].first === 0
  // pts[pts.size()-1].first === 1

  float* lut = new float[length]{ 0 };

  for (size_t x = 0; x < length; ++x) {
    float fx = (float)x / (float)(length - 1);
    // find the interval of control points that contains fx.
    for (size_t i = 0; i < pts.size() - 1; ++i) {
      // am i in between?
      if ((fx >= pts[i].first) && (fx <= pts[i + 1].first)) {
        // what fraction of this interval in x?
        float fxi = (fx - pts[i].first) / (pts[i + 1].first - pts[i].first);
        // use that fraction against y range
        lut[x] = pts[i].second + fxi * (pts[i + 1].second - pts[i].second);
        break;
      }
    }
  }
  return lut;
}

void
Histogram::binRange(uint32_t nbins,
                    uint16_t dataMin,
                    uint16_t dataMax,
                    float& firstBinCenter,
                    float& lastBinCenter,
                    float& binSize)
{
  uint16_t dmin = dataMin;
  uint16_t dmax = dataMax;
  float fbc, lbc, bsize;
  if (nbins > 1) {
    if (dmax > dmin) {
      fbc = dmin;
      lbc = dmax;
    } else {
      fbc = dmin - 1.0f;
      lbc = dmax + 1.0f;
    }
    bsize = (lbc - fbc) / (nbins - 1);
  } else {
    fbc = lbc = 0.5f * (dmax + dmin);
    bsize = 2.0f * (dmax - dmin);
    if (bsize <= 0) {
      bsize = 2;
    }
  }
  firstBinCenter = fbc;
  lastBinCenter = lbc;
  binSize = bsize;
}

// Find the data value where a specified fraction of voxels have lower value.
// Result is an approximation using binned data.
float
Histogram::rank_data_value(float fraction) const
{
  float targetcount = fraction * _ccounts[_ccounts.size() - 1];
  int b = 0;
  // assumes ccounts is monotonically increasing.
  // b is the index where one would insert targetcount in ccounts.
  for (; b < _ccounts.size(); ++b) {
    if (_ccounts[b] > targetcount) {
      break;
    }
  }
  // int b = _ccounts.searchsorted(fraction*_ccounts[_ccounts.size()-1]);
  float fbc, lbc, bsize;
  binRange((uint32_t)_bins.size(), _dataMin, _dataMax, fbc, lbc, bsize);
  float v = fbc + b * (lbc - fbc) / (float)_bins.size();
  return v;
}

float*
Histogram::initialize_thresholds(float vfrac_min /*= 0.01*/, float vfrac_max /*= 0.90*/) const
{
  float ilow = 0.0f;
  float imid = 0.8f;
  float imax = 1.0f;
  float vlow = rank_data_value(1.0f - vfrac_max);
  float vmid = rank_data_value(1.0f - vfrac_min);
  float vmax = _dataMax;
  // LOG_DEBUG << "LOW: " << vlow << " HIGH: " << vmid;

  // normalize to 0..1
  float range = (float)(_dataMax - _dataMin);
  if (range == 0.0f) {
    range = 1.0f;
  }
  vlow = (vlow - _dataMin) / range;
  vmid = (vmid - _dataMin) / range;
  vmax = 1.0f;

  if ((vlow < vmid) && (vmid < vmax)) {
    if (vlow == 0.0f) {
      return generate_controlPoints({ { vlow, ilow }, { vmid, imid }, { vmax, imax } });
    } else {
      return generate_controlPoints({ { 0.0f, 0.0f }, { vlow, ilow }, { vmid, imid }, { vmax, imax } });
    }
  } else {
    if (vlow == 0.0f) {
      return generate_controlPoints({ { vlow, ilow }, { 0.9f * vlow + 0.1f * vmax, imid }, { vmax, imax } });
    } else {
      return generate_controlPoints(
        { { 0.0f, 0.0f }, { vlow, ilow }, { 0.9f * vlow + 0.1f * vmax, imid }, { vmax, imax } });
    }
  }
}

float*
Histogram::generate_equalized(size_t length) const
{
  float* lut = new float[length];

  size_t n_bins = _bins.size();

  // Build LUT from cumulative histrogram

  // Find first non-zero bin
  int i = 0;
  while (_bins[i] == 0)
    ++i;

  if (_bins[i] == _pixelCount) {
    for (size_t x = 0; x < length; ++x) {
      float fx = (float)x / (float)(length - 1);
      lut[x] = (float)_pixelCount / (float)n_bins; // or (n_bins-1) ??
    }
    return lut;
  }

  // Compute scale
  float scale = 1.0f / (_pixelCount - _bins[i]);

  // Initialize lut
  for (size_t x = 0; x < length; ++x) {
    float fx = (float)x / (float)(length - 1);
    // select the cumulative value from histo
    uint32_t sum = _ccounts[int(fx * (_ccounts.size() - 1))];
    // the value is saturated in range [0, max_val]
    lut[x] = std::max(0.0f, std::min(sum * scale, 1.0f));
  }
  return lut;
}

float*
Histogram::generateFromGradientData(const GradientData& gradientData, size_t length) const
{
  switch (gradientData.m_activeMode) {
    case GradientEditMode::WINDOW_LEVEL:
      return generate_windowLevel(gradientData.m_window, gradientData.m_level, length);
    case GradientEditMode::PERCENTILE:
      return generate_percentiles(gradientData.m_pctLow, gradientData.m_pctHigh, length);
    case GradientEditMode::MINMAX: {
      // min and max are already set in gradientData
      float lowEnd = normalizeInt(gradientData.m_minu16, _dataMin, _dataMax);
      float highEnd = normalizeInt(gradientData.m_maxu16, _dataMin, _dataMax);
      float window = highEnd - lowEnd;
      float level = (lowEnd + highEnd) * 0.5f;
      return generate_windowLevel(window, level, length);
    }
    case GradientEditMode::ISOVALUE: {
      float lowEnd = gradientData.m_isovalue - gradientData.m_isorange * 0.5f;
      float highEnd = gradientData.m_isovalue + gradientData.m_isorange * 0.5f;
      std::vector<LutControlPoint> pts;
      pts.push_back({ 0.0f, 0.0f });
      pts.push_back({ lowEnd, 0.0f });
      pts.push_back({ lowEnd, 1.0f });
      pts.push_back({ highEnd, 1.0f });
      pts.push_back({ highEnd, 0.0f });
      pts.push_back({ 1.0f, 0.0f });
      return generate_controlPoints(pts, length);
    }
    case GradientEditMode::CUSTOM:
      return generate_controlPoints(gradientData.m_customControlPoints, length);
    default:
      return generate_fullRange(length);
  }
}

void
Histogram::computePercentile(uint16_t intensity, float& percentile) const
{
  // given an intensity value, compute the percentile of pixels in the histogram less than or equal to that value.
  if (intensity <= _dataMin) {
    percentile = 0.0f;
    return;
  }
  if (intensity >= _dataMax) {
    percentile = 1.0f;
    return;
  }

  // Find the bin corresponding to the intensity value
  size_t bin = this->getBinOfIntensity(intensity);

  if (bin < _ccounts.size()) {

    // Compute the percentile based on the cumulative counts
    if (bin > 0) {
      percentile = (float)_ccounts[bin - 1] / (float)_pixelCount;
    } else {
      percentile = 0.0f;
    }
  } else {
    percentile = 1.0f;
  }
}

size_t
Histogram::getDisplayBinCount(size_t bin) const
{
  // bounds check:
  if (bin >= _filteredBins.size()) {
    LOG_WARNING << "Requested bin " << bin << " out of range (max " << (_filteredBins.size() - 1) << ")";
    return 0;
  }
  return _filteredBins[bin];
}

std::vector<uint32_t>
Histogram::computeForDisplay(float xmin, float xmax, size_t nbins) const
{
  if (nbins == 0) {
    return {};
  }

  std::vector<uint32_t> displayBins(nbins, 0);
  if (_pixelCount == 0 || _bins.empty()) {
    return displayBins;
  }

  if (xmax < xmin) {
    std::swap(xmin, xmax);
  }

  float sourceFirstCenter, sourceLastCenter, sourceBinSize;
  binRange(static_cast<uint32_t>(_bins.size()), _dataMin, _dataMax, sourceFirstCenter, sourceLastCenter, sourceBinSize);
  if (sourceBinSize <= 0.0f) {
    return displayBins;
  }

  if (xmax == xmin) {
    int64_t sourceIndex = static_cast<int64_t>(((xmin - sourceFirstCenter) / sourceBinSize) + 0.5f);
    sourceIndex = std::max<int64_t>(0, std::min<int64_t>(sourceIndex, static_cast<int64_t>(_bins.size() - 1)));
    displayBins[0] = _bins[static_cast<size_t>(sourceIndex)];
    return displayBins;
  }

  float displayRange = xmax - xmin;
  float displayBinMax = static_cast<float>(nbins - 1);
  for (size_t i = 0; i < _bins.size(); ++i) {
    uint32_t count = _bins[i];
    if (count == 0) {
      continue;
    }

    float sourceCenter = sourceFirstCenter + static_cast<float>(i) * sourceBinSize;
    if (sourceCenter < xmin || sourceCenter > xmax) {
      continue;
    }

    int64_t displayIndex = static_cast<int64_t>(((sourceCenter - xmin) / displayRange) * displayBinMax + 0.5f);
    displayIndex = std::max<int64_t>(0, std::min<int64_t>(displayIndex, static_cast<int64_t>(nbins - 1)));
    displayBins[static_cast<size_t>(displayIndex)] += count;
  }

  return displayBins;
}
