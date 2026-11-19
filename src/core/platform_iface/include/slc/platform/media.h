#pragma once

namespace slc::platform {

/**
 * One-time per-thread setup for threads that decode images.
 * On Windows this initialises COM (COINIT_MULTITHREADED) so WIC codecs work
 * off the main thread; other platforms are no-ops. Implemented next to
 * paths_*.cpp so src/core stays free of platform #ifdefs.
 */
void initImageDecodeThread();

} // namespace slc::platform
