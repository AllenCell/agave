#pragma once

class CStatus;

// Publish CacheManager counters and tier usage into `status`, for display in the
// GUI statistics panel. Populates a "Cache" group; CacheManager already tracks
// all of this, it simply had no consumer before.
//
// Threading: CStatus notifies its observers synchronously, and in the GUI those
// observers are Qt widgets. This must therefore be called from the thread that
// owns them -- i.e. from the render/GUI thread, alongside the other statistics
// reporting -- and never from a background loader thread.
void
reportCacheStatistics(CStatus* status);
