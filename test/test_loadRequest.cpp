#include "renderlib/io/BlockingFileReader.h"
#include "renderlib/io/LoadRequest.h"

#include "renderlib/ImageXYZC.h"
#include "renderlib/VolumeDimensions.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

using namespace std::chrono_literals;

namespace {

std::shared_ptr<ImageXYZC>
makeImage(uint32_t x = 2, uint32_t y = 2, uint32_t z = 2, uint32_t c = 1)
{
  const std::uint64_t bytes = static_cast<std::uint64_t>(x) * y * z * c * (ImageXYZC::IN_MEMORY_BPP / 8);
  auto* data = new uint8_t[bytes];
  std::memset(data, 0, bytes);
  return std::make_shared<ImageXYZC>(
    x, y, z, c, static_cast<uint32_t>(ImageXYZC::IN_MEMORY_BPP), data, 1.0f, 1.0f, 1.0f, "units");
}

LoadSpec
makeSpec(uint32_t time = 0)
{
  LoadSpec s;
  s.filepath = "fake.tif";
  s.time = time;
  return s;
}

// A reader that does no I/O, so the async plumbing can be tested deterministically.
//
// By default loadVolumeBlocking returns immediately. Set `gated` to make it park
// until release() is called, which gives the test a window in which a load is
// reliably in flight -- necessary to exercise cancellation without racing.
class FakeReader : public BlockingFileReader
{
public:
  bool supportChunkedLoading() const override { return false; }
  uint32_t loadNumScenes(const std::string&) override { return 1; }
  VolumeDimensions loadDimensions(const std::string&, uint32_t) override { return VolumeDimensions(); }
  std::vector<MultiscaleDims> loadMultiscaleDims(const std::string&, uint32_t) override { return {}; }

  std::shared_ptr<ImageXYZC> loadVolumeBlocking(const LoadSpec& loadSpec, LoadProgress& progress) override
  {
    m_callCount.fetch_add(1);
    m_started.store(true);

    if (m_gated) {
      // Poll for either release or cancellation, the same shape as a reader
      // checking progress.isCancelled() between channels or planes.
      while (!m_released.load()) {
        if (progress.isCancelled()) {
          m_observedCancel.store(true);
          return {};
        }
        std::this_thread::sleep_for(1ms);
      }
    }

    if (progress.isCancelled()) {
      m_observedCancel.store(true);
      return {};
    }

    progress.setProgress(1.0f);
    m_lastSpecTime.store(loadSpec.time);
    return makeImage();
  }

  // setMaxConcurrentLoads is protected on the base; expose it for the test.
  void setMaxConcurrent(uint32_t n) { setMaxConcurrentLoads(n); }

  void setGated(bool gated) { m_gated = gated; }
  void release() { m_released.store(true); }
  bool started() const { return m_started.load(); }
  bool observedCancel() const { return m_observedCancel.load(); }
  int callCount() const { return m_callCount.load(); }
  uint32_t lastSpecTime() const { return m_lastSpecTime.load(); }

private:
  bool m_gated = false;
  std::atomic<bool> m_released{ false };
  std::atomic<bool> m_started{ false };
  std::atomic<bool> m_observedCancel{ false };
  std::atomic<int> m_callCount{ 0 };
  std::atomic<uint32_t> m_lastSpecTime{ 0 };
};

// Spin until `pred` holds or the timeout elapses. Returns whether it held.
template<class Pred>
bool
waitFor(Pred pred, std::chrono::milliseconds timeout = 5000ms)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred()) {
      return true;
    }
    std::this_thread::sleep_for(1ms);
  }
  return pred();
}

} // namespace

TEST_CASE("submitLoad returns a request that completes", "[loadRequest]")
{
  FakeReader reader;
  auto request = reader.submitLoad(makeSpec(7));
  REQUIRE(request);

  auto image = request->take();
  REQUIRE(image);
  CHECK(reader.callCount() == 1);
  CHECK(reader.lastSpecTime() == 7);
  CHECK(request->isReady());
  CHECK(request->progress() == 1.0f);
  CHECK_FALSE(request->isCancelled());
  CHECK(request->spec().time == 7);
}

TEST_CASE("take is idempotent and does not re-run the load", "[loadRequest]")
{
  FakeReader reader;
  auto request = reader.submitLoad(makeSpec());

  auto first = request->take();
  auto second = request->take();
  REQUIRE(first);
  CHECK(first == second);
  CHECK(reader.callCount() == 1);
}

TEST_CASE("loadFromFile still works as a blocking convenience", "[loadRequest]")
{
  FakeReader reader;
  // Exercised through the base-class pointer, the way existing call sites do.
  IFileReader& asInterface = reader;
  auto image = asInterface.loadFromFile(makeSpec(3));
  REQUIRE(image);
  CHECK(reader.callCount() == 1);
  CHECK(reader.lastSpecTime() == 3);
}

TEST_CASE("cancelling an in-flight load makes it return null", "[loadRequest]")
{
  FakeReader reader;
  reader.setGated(true);

  auto request = reader.submitLoad(makeSpec());
  REQUIRE(request);
  REQUIRE(waitFor([&] { return reader.started(); }));

  request->cancel();
  CHECK(request->isCancelled());

  // The reader observes the cancellation and abandons the load, so take()
  // returns null rather than an image.
  auto image = request->take();
  CHECK_FALSE(image);
  CHECK(reader.observedCancel());
}

TEST_CASE("cancelling before the worker starts skips the load entirely", "[loadRequest]")
{
  FakeReader reader;
  reader.setGated(true);

  auto request = reader.submitLoad(makeSpec());
  REQUIRE(request);
  request->cancel();
  reader.release();

  CHECK_FALSE(request->take());
}

TEST_CASE("destroying an in-flight request cancels instead of hanging", "[loadRequest]")
{
  FakeReader reader;
  reader.setGated(true);

  {
    auto request = reader.submitLoad(makeSpec());
    REQUIRE(request);
    REQUIRE(waitFor([&] { return reader.started(); }));
    // Never released, never taken. The destructor must cancel and then join; if
    // it only joined, this scope would block forever.
  }

  CHECK(reader.observedCancel());
}

TEST_CASE("maxConcurrentLoads defaults to 1 and is configurable", "[loadRequest]")
{
  FakeReader reader;
  CHECK(reader.maxConcurrentLoads() == 1);

  reader.setMaxConcurrent(4);
  CHECK(reader.maxConcurrentLoads() == 4);

  // 0 would let a caller compute a zero-sized window; clamp it.
  reader.setMaxConcurrent(0);
  CHECK(reader.maxConcurrentLoads() == 1);
}

TEST_CASE("several loads can be in flight at once", "[loadRequest]")
{
  FakeReader reader;
  reader.setGated(true);

  auto a = reader.submitLoad(makeSpec(0));
  auto b = reader.submitLoad(makeSpec(1));
  auto c = reader.submitLoad(makeSpec(2));

  REQUIRE(waitFor([&] { return reader.callCount() == 3; }));
  CHECK_FALSE(a->isReady());

  reader.release();

  REQUIRE(a->take());
  REQUIRE(b->take());
  REQUIRE(c->take());
}

TEST_CASE("LoadProgress reports fractional progress", "[loadRequest]")
{
  LoadProgress progress;
  CHECK(progress.progress() == 0.0f);
  CHECK_FALSE(progress.isCancelled());

  progress.setProgress(1u, 4u);
  CHECK(progress.progress() == 0.25f);

  progress.setProgress(4u, 4u);
  CHECK(progress.progress() == 1.0f);

  // A zero total means "no progress information available", not a divide by zero.
  progress.setProgress(0u, 0u);
  CHECK(progress.progress() == 0.0f);

  progress.cancel();
  CHECK(progress.isCancelled());
}
