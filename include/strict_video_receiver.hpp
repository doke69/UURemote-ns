#pragma once
#include <atomic>
#include <functional>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <rtc/rtc.hpp>

namespace uu {
// Independent bounded H.264 packet receiver. Only structurally complete access
// units reach the decoder. tick() is driven by Client, not packet arrivals.
class StrictVideoReceiver final : public rtc::MediaHandler {
  public:
    struct Stats {
        uint64_t accepted = 0, dropped = 0, waitingIdr = 0, fastForwards = 0;
        uint64_t nackMessages = 0, rtxRecovered = 0;
        double meanWaitMs = 0;
        int recentWaitMs = 0, maxWaitMs = 0;
        int pendingFrames = 0, blockedMs = 0, nackRetryMs = 45;
        double repairRttMs = 0;
        uint64_t repeatedRequests = 0, pliRequests = 0;
    };
    using Output = std::function<void(const uint8_t *, size_t, uint32_t)>;
    StrictVideoReceiver(bool rtx, int deadlineMs, Output output, bool hevc = false);
    ~StrictVideoReceiver() override;
    void incoming(rtc::message_vector &, const rtc::message_callback &) override;
    void tick();
    void shutdown();
    void requestRecovery();
    void setRtt(int milliseconds);
    Stats snapshot();
    void setSources(uint32_t primary, uint32_t retransmission);
    uint64_t nackCount() const { return nacks_; }
    uint64_t rtxCount() const { return recovered_; }

  private:
    struct Packet { std::vector<uint8_t> payload; bool marker = false; };
    struct Frame {
        uint32_t timestamp = 0;
        int64_t created = 0, marker = -1;
        std::map<int64_t, Packet> packets;
        size_t bytes = 0;
    };
    void process(int64_t now);
    bool skipToReadyIdr(int64_t now);
    void feedback(int64_t now);
    int retryDelayMs() const;
    void pli(int64_t now);
    void retire(std::map<int64_t, Frame>::iterator, const char *reason);
    bool assemble(const Frame &, int64_t first, std::vector<uint8_t> &, bool &idr,
                  std::vector<uint8_t> &sps, std::vector<uint8_t> &pps, std::vector<uint8_t> &vps) const;
    bool assembleHevc(const Frame &, int64_t, std::vector<uint8_t> &, bool &,
                      std::vector<uint8_t> &, std::vector<uint8_t> &, std::vector<uint8_t> &) const;
    void flush();
    std::mutex mutex_;
    const bool hevc_;
    bool rtx_, stopped_ = false, haveSeq_ = false, haveTs_ = false;
    bool waitingIdr_ = true, haveClosed_ = false, haveBoundary_ = false;
    int deadlineMs_;
    int64_t highestSeq_ = 0, highestTs_ = 0, closedTs_ = 0, nextSeq_ = 0;
    int64_t lastNack_ = 0, lastPli_ = 0;
    uint32_t primary_ = 0, rtxSource_ = 0;
    std::map<int64_t, Frame> frames_;
    struct Request { int64_t sentAt = 0; unsigned attempts = 0; };
    std::map<int64_t, Request> requested_;
    int fallbackRttMs_ = 45;
    double repairRttMs_ = 0;
    uint64_t repairSamples_ = 0, repeatedRequests_ = 0, pliRequests_ = 0;
    std::vector<uint8_t> sps_, pps_, vps_;
    size_t buffered_ = 0;
    Output output_;
    rtc::message_callback send_;
    std::atomic<uint64_t> nacks_{0}, recovered_{0};
    uint64_t accepted_ = 0, dropped_ = 0, invalid_ = 0, stale_ = 0, duplicates_ = 0;
    uint64_t waiting_ = 0, recoveries_ = 0;
    uint64_t fastForwards_ = 0, waitTotalMs_ = 0;
    int recentWaitMs_ = 0, maxWaitMs_ = 0;
    std::deque<std::string> trace_;
};
} // namespace uu
