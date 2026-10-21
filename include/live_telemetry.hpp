#pragma once
#include "stream_stats.hpp"
#include <memory>
namespace uu {
// Diagnostic-only UDP output. No inbound socket commands or protocol payloads.
class LiveTelemetry {
    struct Impl;
    std::unique_ptr<Impl> impl_;
  public:
    LiveTelemetry() noexcept;
    ~LiveTelemetry();
    bool observe(uint32_t ticks, bool streaming, bool submitted, double uploadUs) noexcept;
    void publish(const StreamStatsSnapshot &, bool vsync, bool nv12,
                 double refreshMs, unsigned stableSwaps, bool nativeDisplay=false, uint64_t directFrames=0, uint64_t mapHits=0, uint64_t mapImports=0, uint64_t mapCached=0) noexcept;
};
}
