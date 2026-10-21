#include "video_assembler.hpp"
#include "diagnostics.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <rtc/rtp.hpp>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

namespace uu {
namespace {
int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
const char *diagDirectory() {
#ifdef __SWITCH__
    return "sdmc:/switch/UURemote-ns/diag";
#else
    return "diag";
#endif
}
void appendStartCode(rtc::binary &out) {
    out.push_back(std::byte{0});
    out.push_back(std::byte{0});
    out.push_back(std::byte{0});
    out.push_back(std::byte{1});
}
void appendBytes(rtc::binary &out, const uint8_t *data, size_t size) {
    out.reserve(out.size() + size);
    for (size_t i = 0; i < size; ++i)
        out.push_back(std::byte(data[i]));
}
// Returns true when the frame contains an IDR slice or SPS, i.e. a keyframe.
bool containsKeyframeNal(const rtc::message_ptr &message, size_t headerSize, size_t size) {
    const auto *payload = reinterpret_cast<const uint8_t *>(message->data()) + headerSize;
    const auto payloadSize = size - headerSize;
    if (payloadSize == 0)
        return false;
    const uint8_t nal = payload[0] & 0x1f;
    switch (nal) {
    case 5: // IDR slice
    case 7: // SPS
    case 8: // PPS
        return true;
    case 24: { // STAP-A
        size_t pos = 1;
        while (pos + 2 <= payloadSize) {
            const auto length = size_t(payload[pos] << 8 | payload[pos + 1]);
            pos += 2;
            if (pos + length > payloadSize || length == 0)
                break;
            const uint8_t inner = payload[pos] & 0x1f;
            if (inner == 5 || inner == 7 || inner == 8)
                return true;
            pos += length;
        }
        return false;
    }
    default:
        return false;
    }
}
} // namespace

VideoAssembler::VideoAssembler(bool rtx, int jitterMs) : rtx_(rtx), jitterMs_(jitterMs) {}
VideoAssembler::~VideoAssembler() { flushDiagnostics(); }

void VideoAssembler::flushDiagnostics() {
#ifdef _WIN32
    _mkdir(diagDirectory());
#else
    mkdir(diagDirectory(), 0777);
#endif
    std::ofstream file(std::string(diagDirectory()) + "/video_assembler.txt");
    if (!file)
        return;
    file << "nacks=" << nacks_.load() << "\n";
    file << "rtx_packets=" << rtxPackets_.load() << "\n";
    file << "frames=" << emitted_.load() << "\n";
    file << "damaged_frames=" << damaged_.load() << "\n";
    file << "damaged_keyframes=" << damagedKeyframes_.load() << "\n";
    file << "dropped_stale=" << droppedStale_.load() << "\n";
    file << "jitter_ms=" << jitterMs_ << "\n";
    file << "rtx=" << (rtx_ ? 1 : 0) << "\n";
}

bool VideoAssembler::parse(const rtc::message_ptr &message, uint8_t &payloadType,
                           uint32_t &timestamp, uint16_t &sequence) const {
    if (!message || message->type != rtc::Message::Binary)
        return false;
    if (message->size() < sizeof(rtc::RtpHeader))
        return false;
    const auto *header = reinterpret_cast<const rtc::RtpHeader *>(message->data());
    if (header->version() != 2)
        return false;
    payloadType = header->payloadType();
    timestamp = header->timestamp();
    sequence = header->seqNumber();
    return true;
}

rtc::message_ptr VideoAssembler::normalizeRtx(const rtc::message_ptr &message, uint32_t &timestamp,
                                              uint16_t &sequence) const {
    const auto *data = reinterpret_cast<const uint8_t *>(message->data());
    const auto size = message->size();
    const auto *header = reinterpret_cast<const rtc::RtpHeader *>(data);
    const auto headerSize = size_t(reinterpret_cast<const uint8_t *>(header->getBody()) - data);
    if (size < headerSize + 2)
        return nullptr;
    const auto originalSequence =
        uint16_t((uint16_t(data[headerSize]) << 8) | uint16_t(data[headerSize + 1]));
    const auto payloadSize = size - headerSize - 2;
    auto replacement = rtc::make_message(headerSize + payloadSize, rtc::Message::Binary);
    auto *out = reinterpret_cast<uint8_t *>(replacement->data());
    std::memcpy(out, data, headerSize);
    std::memcpy(out + headerSize, data + headerSize + 2, payloadSize);
    auto *newHeader = reinterpret_cast<rtc::RtpHeader *>(out);
    newHeader->setSeqNumber(originalSequence);
    newHeader->setPayloadType(videoPayloadType_);
    timestamp = newHeader->timestamp();
    sequence = originalSequence;
    return replacement;
}

void VideoAssembler::addPacket(const rtc::message_ptr &message, uint32_t timestamp,
                               uint16_t sequence) {
    // Drop retransmissions belonging to frames that were already handed to the
    // decoder: re-injecting them would rebuild a stale access unit out of order.
    if (haveEmitted_ && int32_t(timestamp - lastEmittedTs_) < 0) {
        ++droppedStale_;
        return;
    }
    if (!haveHighest_) {
        haveHighest_ = true;
        highest_ = int64_t(sequence);
    }
    const auto diff = int16_t(uint16_t(sequence - uint16_t(highest_)));
    const auto ext = highest_ + int64_t(diff);
    if (ext < highest_ - 4096)
        return; // far too old
    if (ext > highest_)
        highest_ = ext;

    receivedExts_.insert(ext);
    if (receivedExts_.size() > 8192) {
        const auto threshold = highest_ - 4096;
        for (auto it = receivedExts_.begin(); it != receivedExts_.end() && *it < threshold;)
            it = receivedExts_.erase(it);
    }

    int64_t key;
    if (auto it = byTimestamp_.find(timestamp); it != byTimestamp_.end()) {
        key = it->second;
    } else {
        key = nextFrameKey_++;
        byTimestamp_.emplace(timestamp, key);
        Frame frame;
        frame.timestamp = timestamp;
        frame.firstMs = nowMs();
        frame.lastMs = frame.firstMs;
        frame.firstExt = ext;
        frame.lastExt = ext;
        frames_.emplace(key, std::move(frame));
    }
    auto &frame = frames_.at(key);
    if (frame.packets.count(ext))
        return; // duplicate
    const auto *data = reinterpret_cast<const uint8_t *>(message->data());
    const auto *header = reinterpret_cast<const rtc::RtpHeader *>(data);
    const auto size = message->size();
    const auto headerSize = size_t(reinterpret_cast<const uint8_t *>(header->getBody()) - data);
    if (header->marker()) {
        frame.haveMarker = true;
        frame.markerExt = ext;
    }
    if (size > headerSize && containsKeyframeNal(message, headerSize, size))
        frame.keyframe = true;
    frame.packets.emplace(ext, message);
    frame.lastMs = nowMs();
    frame.lastExt = std::max(frame.lastExt, ext);
    frame.firstExt = std::min(frame.firstExt, ext);
}

void VideoAssembler::emitFrame(Frame &frame, bool complete, rtc::message_vector &messages) {
    const auto expected = frame.haveMarker ? frame.markerExt - frame.firstExt + 1 : 0;
    const auto got = int64_t(frame.packets.size());
    // A badly damaged keyframe is worse than no frame: it poisons the reference
    // picture until the next IDR. Drop it and ask the encoder for a fresh IDR.
    if (!complete && frame.keyframe && expected > 0 && got * 10 < expected * 9) {
        ++damaged_;
        ++damagedKeyframes_;
        requestKeyframe();
        haveEmitted_ = true;
        lastEmittedTs_ = frame.timestamp;
        return;
    }
    rtc::binary accessUnit;
    for (const auto &[ext, packet] : frame.packets) {
        const auto *data = reinterpret_cast<const uint8_t *>(packet->data());
        const auto size = packet->size();
        const auto *header = reinterpret_cast<const rtc::RtpHeader *>(data);
        const auto headerSize = size_t(reinterpret_cast<const uint8_t *>(header->getBody()) - data);
        if (size <= headerSize)
            continue;
        const auto *payload = data + headerSize;
        const auto payloadSize = size - headerSize;
        const uint8_t nal = payload[0] & 0x1f;
        if (nal == 24) { // STAP-A
            size_t pos = 1;
            while (pos + 2 <= payloadSize) {
                const auto length = size_t(payload[pos] << 8 | payload[pos + 1]);
                pos += 2;
                if (length == 0 || pos + length > payloadSize)
                    break;
                appendStartCode(accessUnit);
                appendBytes(accessUnit, payload + pos, length);
                pos += length;
            }
        } else if (nal == 28) { // FU-A
            if (payloadSize < 2)
                continue;
            if ((payload[1] & 0x80) != 0) {
                appendStartCode(accessUnit);
                accessUnit.push_back(std::byte(uint8_t((payload[0] & 0xe0) | (payload[1] & 0x1f))));
            }
            appendBytes(accessUnit, payload + 2, payloadSize - 2);
        } else {
            appendStartCode(accessUnit);
            appendBytes(accessUnit, payload, payloadSize);
        }
    }
    if (!accessUnit.empty()) {
        auto info = std::make_shared<rtc::FrameInfo>(frame.timestamp);
        info->payloadType = videoPayloadType_;
        auto message = rtc::make_message(std::move(accessUnit), info);
        messages.push_back(std::move(message));
    }
    haveEmitted_ = true;
    lastEmittedTs_ = frame.timestamp;
    ++emitted_;
    if (!complete) {
        ++damaged_;
        if (frame.keyframe) {
            ++damagedKeyframes_;
            requestKeyframe();
        }
    }
}

void VideoAssembler::requestKeyframe() {
    if (!send_)
        return;
    const auto now = nowMs();
    if (now - lastPliMs_ < 400)
        return;
    auto message = rtc::make_message(rtc::RtcpPli::Size(), rtc::Message::Control);
    auto *pli = reinterpret_cast<rtc::RtcpPli *>(message->data());
    pli->preparePacket(videoSsrc_);
    send_(message);
    lastPliMs_ = now;
}

void VideoAssembler::requestMissing(const rtc::message_callback &send) {
    if (!rtx_ || !send)
        return;
    const auto now = nowMs();
    if (now - lastNackMs_ < 15)
        return;
    int64_t oldest = highest_ - 512;
    if (!frames_.empty())
        oldest = std::max(oldest, frames_.begin()->second.firstExt);
    oldest = std::max<int64_t>(oldest, highest_ - 1024);
    std::vector<std::pair<uint16_t, uint16_t>> groups;
    bool any = false;
    for (int64_t ext = oldest; ext <= highest_ && groups.size() < 32; ++ext) {
        if (receivedExts_.count(ext))
            continue;
        if (auto it = requestedExts_.find(ext); it != requestedExts_.end() && now - it->second < 25)
            continue;
        requestedExts_[ext] = now;
        const auto sequence = uint16_t(ext);
        if (groups.empty() || sequence < groups.back().first ||
            uint16_t(sequence - groups.back().first) > 16) {
            groups.emplace_back(sequence, uint16_t(0));
        } else {
            groups.back().second |= uint16_t(1u << (sequence - groups.back().first - 1));
        }
        any = true;
    }
    for (auto it = requestedExts_.begin(); it != requestedExts_.end();) {
        if (receivedExts_.count(it->first) || it->first < highest_ - 2048)
            it = requestedExts_.erase(it);
        else
            ++it;
    }
    if (!any)
        return;
    auto message =
        rtc::make_message(rtc::RtcpNack::Size(unsigned(groups.size())), rtc::Message::Control);
    auto *nack = reinterpret_cast<rtc::RtcpNack *>(message->data());
    nack->preparePacket(videoSsrc_, unsigned(groups.size()));
    auto *parts = reinterpret_cast<rtc::RtcpNackPart *>(nack->parts);
    for (size_t i = 0; i < groups.size(); ++i) {
        parts[i].setPid(groups[i].first);
        parts[i].setBlp(groups[i].second);
    }
    send(message);
    ++nacks_;
    if (nacks_.load() % 50 == 0)
        diagnostic(("video-nack-" + std::to_string(nacks_.load())).c_str());
    lastNackMs_ = now;
}

void VideoAssembler::incoming(rtc::message_vector &messages, const rtc::message_callback &send) {
    send_ = send;
    rtc::message_vector incoming;
    incoming.swap(messages);
    for (auto &message : incoming) {
        uint8_t payloadType = 0;
        uint32_t timestamp = 0;
        uint16_t sequence = 0;
        if (!parse(message, payloadType, timestamp, sequence))
            continue;
        const auto *data = reinterpret_cast<const uint8_t *>(message->data());
        const auto *header = reinterpret_cast<const rtc::RtpHeader *>(data);
        if (payloadType == videoPayloadType_) {
            if (videoSsrc_ == 0)
                videoSsrc_ = header->ssrc();
            addPacket(message, timestamp, sequence);
        } else if (rtx_) {
            const auto original = normalizeRtx(message, timestamp, sequence);
            if (original) {
                ++rtxPackets_;
                if (rtxPackets_.load() % 100 == 0)
                    diagnostic(("video-rtx-" + std::to_string(rtxPackets_.load())).c_str());
                addPacket(original, timestamp, sequence);
            }
        }
    }
    const auto now = nowMs();
    for (auto it = frames_.begin(); it != frames_.end();) {
        auto &frame = it->second;
        const auto age = now - frame.firstMs;
        bool complete = frame.haveMarker && frame.markerExt >= frame.firstExt;
        if (complete) {
            const auto end = std::min<int64_t>(frame.markerExt, frame.firstExt + 4096);
            for (int64_t ext = frame.firstExt; ext <= end; ++ext) {
                if (!frame.packets.count(ext)) {
                    complete = false;
                    break;
                }
            }
        }
        if (complete) {
            emitFrame(frame, true, messages);
            byTimestamp_.erase(frame.timestamp);
            it = frames_.erase(it);
            continue;
        }
        if (age >= jitterMs_) {
            emitFrame(frame, false, messages);
            byTimestamp_.erase(frame.timestamp);
            it = frames_.erase(it);
            continue;
        }
        if (age > 2000) {
            byTimestamp_.erase(frame.timestamp);
            it = frames_.erase(it);
            continue;
        }
        break; // keep decoding order: wait for the oldest frame
    }
    requestMissing(send);
    while (frames_.size() > 120) {
        byTimestamp_.erase(frames_.begin()->second.timestamp);
        frames_.erase(frames_.begin());
    }
}
} // namespace uu
