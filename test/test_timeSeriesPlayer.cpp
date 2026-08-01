#include "renderlib/io/TimeSeriesPlayer.h"

#include <catch2/catch_test_macros.hpp>

#include <set>

namespace {

// Everything is ready. The common case once the cache is warm.
const TimeSeriesPlayer::ReadyPredicate kAllReady = [](uint32_t) { return true; };

// Nothing is ready. Models a cold cache.
const TimeSeriesPlayer::ReadyPredicate kNoneReady = [](uint32_t) { return false; };

TimeSeriesPlayer::Config
configFor(TimeSeriesPlayer::Mode mode, float fps = 10.0f, bool loop = true)
{
  TimeSeriesPlayer::Config c;
  c.mode = mode;
  c.fps = fps;
  c.loop = loop;
  return c;
}

} // namespace

TEST_CASE("TimeSeriesPlayer does nothing until played", "[timeSeriesPlayer]")
{
  TimeSeriesPlayer player;
  player.setRange(0, 9);
  player.setConfig(configFor(TimeSeriesPlayer::Mode::ShowEveryFrame));

  CHECK(player.state() == TimeSeriesPlayer::State::Stopped);
  CHECK_FALSE(player.isPlaying());
  CHECK_FALSE(player.advance(1000, 0, kAllReady).has_value());
}

TEST_CASE("TimeSeriesPlayer advances at the configured rate", "[timeSeriesPlayer]")
{
  TimeSeriesPlayer player;
  player.setRange(0, 9);
  player.setConfig(configFor(TimeSeriesPlayer::Mode::ShowEveryFrame, 10.0f)); // 100ms per frame
  player.play(0, 0);

  // Too early: no frame yet.
  CHECK_FALSE(player.advance(50, 0, kAllReady).has_value());
  CHECK_FALSE(player.advance(99, 0, kAllReady).has_value());

  // The slot has elapsed.
  auto next = player.advance(100, 0, kAllReady);
  REQUIRE(next.has_value());
  CHECK(*next == 1);

  // And again a slot later.
  CHECK_FALSE(player.advance(150, 1, kAllReady).has_value());
  next = player.advance(200, 1, kAllReady);
  REQUIRE(next.has_value());
  CHECK(*next == 2);
}

TEST_CASE("TimeSeriesPlayer pause and resume", "[timeSeriesPlayer]")
{
  TimeSeriesPlayer player;
  player.setRange(0, 9);
  player.setConfig(configFor(TimeSeriesPlayer::Mode::ShowEveryFrame));
  player.play(0, 0);

  player.pause();
  CHECK(player.state() == TimeSeriesPlayer::State::Paused);
  CHECK_FALSE(player.advance(1000, 0, kAllReady).has_value());

  // Resuming means playing again from wherever we are.
  player.play(3, 1000);
  CHECK(player.isPlaying());
  auto next = player.advance(1100, 3, kAllReady);
  REQUIRE(next.has_value());
  CHECK(*next == 4);
}

TEST_CASE("TimeSeriesPlayer stop returns the frame playback started from", "[timeSeriesPlayer]")
{
  TimeSeriesPlayer player;
  player.setRange(0, 20);
  player.setConfig(configFor(TimeSeriesPlayer::Mode::ShowEveryFrame));

  player.play(7, 0);
  // Run forward a few frames.
  CHECK(player.advance(100, 7, kAllReady).value() == 8);
  CHECK(player.advance(200, 8, kAllReady).value() == 9);

  auto origin = player.stop();
  REQUIRE(origin.has_value());
  CHECK(*origin == 7);
  CHECK(player.state() == TimeSeriesPlayer::State::Stopped);

  // Stopping again has nothing to restore.
  CHECK_FALSE(player.stop().has_value());
}

TEST_CASE("TimeSeriesPlayer loops at the end of the range", "[timeSeriesPlayer]")
{
  TimeSeriesPlayer player;
  player.setRange(5, 7);
  player.setConfig(configFor(TimeSeriesPlayer::Mode::ShowEveryFrame, 10.0f, /*loop=*/true));
  player.play(5, 0);

  CHECK(player.advance(100, 5, kAllReady).value() == 6);
  CHECK(player.advance(200, 6, kAllReady).value() == 7);
  // Wraps back to the start of the range, not to zero.
  CHECK(player.advance(300, 7, kAllReady).value() == 5);
  CHECK(player.isPlaying());
}

TEST_CASE("TimeSeriesPlayer stops at the end when looping is off", "[timeSeriesPlayer]")
{
  TimeSeriesPlayer player;
  player.setRange(0, 2);
  player.setConfig(configFor(TimeSeriesPlayer::Mode::ShowEveryFrame, 10.0f, /*loop=*/false));
  player.play(0, 0);

  CHECK(player.advance(100, 0, kAllReady).value() == 1);
  CHECK(player.advance(200, 1, kAllReady).value() == 2);

  // At the last frame it stops instead of wrapping.
  CHECK_FALSE(player.advance(300, 2, kAllReady).has_value());
  CHECK(player.state() == TimeSeriesPlayer::State::Stopped);
}

TEST_CASE("ShowEveryFrame waits for a frame that is not ready", "[timeSeriesPlayer]")
{
  TimeSeriesPlayer player;
  player.setRange(0, 9);
  player.setConfig(configFor(TimeSeriesPlayer::Mode::ShowEveryFrame, 10.0f));
  player.play(0, 0);

  // t=1 is not ready, so no frame is produced even though the slot has elapsed.
  auto notReady = [](uint32_t t) { return t != 1; };
  CHECK_FALSE(player.advance(100, 0, notReady).has_value());
  CHECK_FALSE(player.advance(500, 0, notReady).has_value());
  // Nothing was skipped.
  CHECK(player.droppedFrames() == 0);

  // Once it is ready it is shown immediately, and never skipped.
  auto next = player.advance(600, 0, kAllReady);
  REQUIRE(next.has_value());
  CHECK(*next == 1);
}

TEST_CASE("ShowEveryFrame does not burst after a slow frame", "[timeSeriesPlayer]")
{
  TimeSeriesPlayer player;
  player.setRange(0, 9);
  player.setConfig(configFor(TimeSeriesPlayer::Mode::ShowEveryFrame, 10.0f)); // 100ms
  player.play(0, 0);

  // Stalled well past several frame intervals.
  auto notReady = [](uint32_t t) { return t != 1; };
  CHECK_FALSE(player.advance(1000, 0, notReady).has_value());

  // The frame becomes available and is shown.
  REQUIRE(player.advance(1000, 0, kAllReady).value() == 1);

  // The interval is measured from when the frame was actually shown, so the next
  // one is not immediately due. Without that, a long stall would be followed by
  // a burst of catch-up frames.
  CHECK_FALSE(player.advance(1050, 1, kAllReady).has_value());
  CHECK(player.advance(1100, 1, kAllReady).value() == 2);
}

TEST_CASE("RealTime skips frames that are not ready", "[timeSeriesPlayer]")
{
  TimeSeriesPlayer player;
  player.setRange(0, 9);
  player.setConfig(configFor(TimeSeriesPlayer::Mode::RealTime, 10.0f));
  player.play(0, 0);

  // Only t=3 onwards is loaded, so 1 and 2 get skipped to hold the rate.
  auto readyFromThree = [](uint32_t t) { return t >= 3; };
  auto next = player.advance(100, 0, readyFromThree);
  REQUIRE(next.has_value());
  CHECK(*next == 3);
  CHECK(player.droppedFrames() == 2);
}

TEST_CASE("RealTime holds the current frame when nothing is ready", "[timeSeriesPlayer]")
{
  TimeSeriesPlayer player;
  player.setRange(0, 4);
  player.setConfig(configFor(TimeSeriesPlayer::Mode::RealTime, 10.0f));
  player.play(0, 0);

  // A completely cold cache must not spin or loop forever inside one tick.
  CHECK_FALSE(player.advance(100, 0, kNoneReady).has_value());
  CHECK(player.isPlaying());

  // The elapsed slot was consumed, so it does not immediately rescan.
  CHECK_FALSE(player.advance(120, 0, kNoneReady).has_value());
}

TEST_CASE("RealTime with looping off stops rather than skipping past the end", "[timeSeriesPlayer]")
{
  TimeSeriesPlayer player;
  player.setRange(0, 3);
  player.setConfig(configFor(TimeSeriesPlayer::Mode::RealTime, 10.0f, /*loop=*/false));
  player.play(0, 0);

  // Nothing after t=0 is ready, so it walks to the end, finds nothing, and stops.
  auto onlyZero = [](uint32_t t) { return t == 0; };
  CHECK_FALSE(player.advance(100, 0, onlyZero).has_value());
  CHECK(player.state() == TimeSeriesPlayer::State::Stopped);
}

TEST_CASE("TimeSeriesPlayer handles a single-timepoint range", "[timeSeriesPlayer]")
{
  TimeSeriesPlayer player;
  player.setRange(4, 4);
  player.setConfig(configFor(TimeSeriesPlayer::Mode::ShowEveryFrame, 10.0f, /*loop=*/true));
  player.play(4, 0);

  // Even with looping on there is nowhere to go, so it stops instead of
  // re-displaying the same frame forever.
  CHECK_FALSE(player.advance(100, 4, kAllReady).has_value());
  CHECK(player.state() == TimeSeriesPlayer::State::Stopped);
}

TEST_CASE("TimeSeriesPlayer clamps a play position outside the range", "[timeSeriesPlayer]")
{
  TimeSeriesPlayer player;
  player.setRange(2, 8);
  player.setConfig(configFor(TimeSeriesPlayer::Mode::ShowEveryFrame));

  player.play(100, 0);
  auto origin = player.stop();
  REQUIRE(origin.has_value());
  CHECK(*origin == 8);
}

TEST_CASE("TimeSeriesPlayer guards against a non-positive frame rate", "[timeSeriesPlayer]")
{
  TimeSeriesPlayer player;
  player.setRange(0, 9);
  player.setConfig(configFor(TimeSeriesPlayer::Mode::ShowEveryFrame, 0.0f));
  // Clamped to something usable rather than dividing by zero.
  CHECK(player.config().fps > 0.0f);

  player.play(0, 0);
  // Still advances, just at the clamped rate.
  bool advanced = false;
  for (uint64_t t = 0; t <= 5000 && !advanced; t += 100) {
    advanced = player.advance(t, 0, kAllReady).has_value();
  }
  CHECK(advanced);
}

TEST_CASE("TimeSeriesPlayer tolerates a clock that goes backwards", "[timeSeriesPlayer]")
{
  TimeSeriesPlayer player;
  player.setRange(0, 9);
  player.setConfig(configFor(TimeSeriesPlayer::Mode::ShowEveryFrame, 10.0f));
  player.play(0, 1000);

  // A timestamp before the last frame must not leave the player permanently
  // waiting for a moment that already passed.
  CHECK_FALSE(player.advance(500, 0, kAllReady).has_value());
  CHECK(player.advance(600, 0, kAllReady).value() == 1);
}

TEST_CASE("TimeSeriesPlayer counts dropped frames and can reset them", "[timeSeriesPlayer]")
{
  TimeSeriesPlayer player;
  player.setRange(0, 9);
  player.setConfig(configFor(TimeSeriesPlayer::Mode::RealTime, 10.0f));
  player.play(0, 0);

  auto readyFromThree = [](uint32_t t) { return t >= 3; };
  player.advance(100, 0, readyFromThree);
  CHECK(player.droppedFrames() == 2);

  player.resetDroppedFrames();
  CHECK(player.droppedFrames() == 0);
}

TEST_CASE("TimeSeriesPlayer peeks the next timepoint without advancing", "[timeSeriesPlayer]")
{
  // advance() is passive: it asks whether a frame is ready and never makes it
  // ready. The caller needs to know which frame is being waited on so it can
  // fetch it -- otherwise, with prefetch off, ShowEveryFrame holds for ever and
  // the play button appears dead.
  TimeSeriesPlayer player;
  player.setRange(0, 4);

  SECTION("Returns nothing while stopped or paused")
  {
    CHECK_FALSE(player.peekNextTime(0).has_value());
    player.play(0, 0);
    player.pause();
    CHECK_FALSE(player.peekNextTime(0).has_value());
  }

  SECTION("Reports the frame advance would move to, and does not move it")
  {
    player.play(0, 0);
    REQUIRE(player.peekNextTime(0) == 1u);
    // Peeking is side-effect free: still due, still the same answer.
    CHECK(player.peekNextTime(0) == 1u);
    // And it agrees with what advance() actually does once the frame is ready.
    TimeSeriesPlayer::Config cfg;
    cfg.mode = TimeSeriesPlayer::Mode::ShowEveryFrame;
    cfg.fps = 10.0f;
    player.setConfig(cfg);
    CHECK(player.advance(1000, 0, [](uint32_t) { return true; }) == 1u);
  }

  SECTION("Wraps at the end when looping, and stops when not")
  {
    TimeSeriesPlayer::Config cfg;
    cfg.loop = true;
    player.setConfig(cfg);
    player.play(4, 0);
    CHECK(player.peekNextTime(4) == 0u);

    cfg.loop = false;
    player.setConfig(cfg);
    CHECK_FALSE(player.peekNextTime(4).has_value());
  }

  SECTION("Keeps reporting the same frame while it is not ready")
  {
    // The case that matters: holding on an unready frame, tick after tick, must
    // keep naming that frame so the caller can fetch it.
    TimeSeriesPlayer::Config cfg;
    cfg.mode = TimeSeriesPlayer::Mode::ShowEveryFrame;
    cfg.fps = 10.0f;
    player.setConfig(cfg);
    player.play(0, 0);

    auto neverReady = [](uint32_t) { return false; };
    for (uint64_t now = 1000; now < 1500; now += 100) {
      CHECK_FALSE(player.advance(now, 0, neverReady).has_value());
      CHECK(player.peekNextTime(0) == 1u);
    }
    CHECK(player.isPlaying());
  }
}
