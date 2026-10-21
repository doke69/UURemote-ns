#pragma once
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <set>

#include <rtc/rtc.hpp>

namespace uu {
// Reassembles H.264 RTP access units for the video track.
//
// This replaces libdatachannel's stock H264RtpDepacketizer, which cannot merge
// retransmitted packets that arrive after the frame timestamp changed. The
// assembler keeps a small reorder/jitter buffer, requests missing packets with
// RTCP NACK (when RTX is negotiated), merges retransmitted packets back into
// their frame and only then hands complete access units to the decoder.
class VideoAssembler : public rtc::MediaHandler {
  public:
    VideoAssembler(bool rtx, int jitterMs);
    ~VideoAssembler() override;
    void incoming(rtc::message_vector &messages, const rtc::message_callback &send) override;

    uint64_t nackCount() const { return nacks_.load(); }
    uint64_t rtxCount() const { return rtxPackets_.load(); }
    uint64_t frameCount() const { return emitted_.load(); }
    uint64_t damagedCount() const { return damaged_.load(); }
    uint64_t damagedKeyframeCount() const { return damagedKeyframes_.load(); }

  private:
    struct Frame {
        uint32_t timestamp = 0;
        int64_t firstMs = 0;
        int64_t lastMs = 0;
        int64_t firstExt = 0;
        int64_t lastExt = 0;
        bool haveMarker = false;
        int64_t markerExt = -1;
        bool keyframe = false;
        std::map<int64_t, rtc::message_ptr> packets; // extended seq -> RTP packet
    };

    bool parse(const rtc::message_ptr &message, uint8_t &payloadType, uint32_t &timestamp,
               uint16_t &sequence) const;
    rtc::message_ptr normalizeRtx(const rtc::message_ptr &message, uint32_t &timestamp,
                                  uint16_t &sequence) const;
    void addPacket(const rtc::message_ptr &message, uint32_t timestamp, uint16_t sequence);
    void emitFrame(Frame &frame, bool complete, rtc::message_vector &messages);
    void requestMissing(const rtc::message_callback &send);
    void requestKeyframe();
    void flushDiagnostics();

    const bool rtx_;
    const int jitterMs_;
    std::map<int64_t, Frame> frames_; // keyed by frame creation order
    std::map<uint32_t, int64_t> byTimestamp_;
    int64_t nextFrameKey_ = 0;
    bool haveHighest_ = false;
    int64_t highest_ = 0;
    uint8_t videoPayloadType_ = 96;
    uint32_t videoSsrc_ = 0;
    int64_t lastNackMs_ = 0;
    int64_t lastPliMs_ = 0;
    bool haveEmitted_ = false;
    uint32_t lastEmittedTs_ = 0;
    std::set<int64_t> receivedExts_;
    std::map<int64_t, int64_t> requestedExts_;
    rtc::message_callback send_;
    std::atomic<uint64_t> nacks_{0};
    std::atomic<uint64_t> rtxPackets_{0};
    std::atomic<uint64_t> emitted_{0};
    std::atomic<uint64_t> damaged_{0};
    std::atomic<uint64_t> damagedKeyframes_{0};
    std::atomic<uint64_t> droppedStale_{0};
};
} // namespace uu
