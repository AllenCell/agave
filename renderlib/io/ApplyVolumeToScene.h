#pragma once

#include <memory>

class ImageXYZC;
class RenderSettings;
class Scene;

// Install a newly loaded volume as the scene's current volume.
//
// Remaps each channel's transfer function from the outgoing volume's histogram
// to the incoming one's, so absolute thresholding is preserved across a time
// step, then swaps in the new volume and raises the dirty flags the renderers
// watch.
//
// Extracted from the two near-identical copies that used to live in
// QTimelineWidget::OnTimeChanged and SetTimeCommand::execute, so the GUI and the
// command/websocket paths cannot drift apart again.
//
// Threading: must be called on the thread that owns the scene -- the GUI thread
// for the interactive viewer, the render thread for commands. It is cheap LUT
// work, not I/O, so it does not belong on a loader thread. It also reads the
// outgoing volume's histograms, so it must run at the point the frame is
// displayed rather than when it finishes loading; otherwise out-of-order
// prefetch completions would remap against the wrong source histograms.
//
// Invariants for time steps of a given source url or filepath:
//   * same channel count, and
//   * same channel order.
// (The channel selection lives in the LoadSpec and is fixed for the series, so
// both hold for a subset selection too.)
//
// The second invariant is what makes the remap below correct: channel i of the
// outgoing volume is paired with channel i of the incoming one purely by index,
// which is only meaningful if index i denotes the same channel in both. Count is
// checked and a mismatch is refused; order cannot be verified here, so it is
// relied upon.
//
// Returns false and leaves the scene untouched if the volume could not be
// applied: null scene, null image, or a channel-count mismatch.
bool
applyVolumeToScene(Scene* scene, const std::shared_ptr<ImageXYZC>& image, RenderSettings* renderSettings);
