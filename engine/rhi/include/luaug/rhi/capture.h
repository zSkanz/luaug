// Reading a recorded command stream back (ADR 0005, architecture.md §9).
//
// The capture backend is the blocking render-regression gate, and it is that
// precisely because it needs no GPU: CI compares a recorded stream against a
// checked-in golden and fails on any difference. Real images are a nightly,
// non-blocking job, because a driver update should not be able to turn the
// merge queue red.
//
// This header is the seam's, not the backend's -- `app` and the test harness
// need to read a capture without linking anything that knows how one is made.
#pragma once

#include "luaug/rhi/device.h"

#include <string>

namespace luaug::rhi {

// The recorded stream: one JSON object per line, in the order the calls
// happened. Line-oriented on purpose -- a failing gate should point at the call
// that changed, which one enormous JSON document cannot do.
//
// Canonical by construction: handle ids are sequential per resource kind, and
// every float is quantized to four decimals and written from an integer, so no
// libc's float formatting can make two identical frames disagree.
[[nodiscard]] const std::string& captureStream(const IDevice& device);

// Empties the recorded stream. A scenario records one frame, compares, and
// resets.
void resetCapture(IDevice& device);

} // namespace luaug::rhi
