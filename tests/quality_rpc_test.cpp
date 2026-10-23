#include "quality_rpc.hpp"
#include <cstdint>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
using namespace uu;

void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}

struct Field {
    unsigned key = 0, wire = 0;
    uint64_t value = 0;
    std::vector<uint8_t> bytes;
};
std::vector<Field> decode(const uint8_t *data, size_t size) {
    std::vector<Field> out;
    const uint8_t *p = data, *end = data + size;
    auto varint = [&](uint64_t &value) {
        value = 0;
        for (unsigned i = 0; i < 10; ++i) {
            if (p == end)
                throw std::runtime_error("truncated varint");
            const auto byte = *p++;
            if (i == 9 && byte > 1)
                throw std::runtime_error("varint overflow");
            value |= uint64_t(byte & 127) << (i * 7);
            if (!(byte & 128))
                return;
        }
        throw std::runtime_error("varint overflow");
    };
    while (p != end) {
        uint64_t tag = 0;
        varint(tag);
        Field f;
        f.key = unsigned(tag >> 3);
        f.wire = unsigned(tag & 7);
        if (f.wire == 0)
            varint(f.value);
        else if (f.wire == 1 || f.wire == 2 || f.wire == 5) {
            uint64_t length = f.wire == 1 ? 8 : f.wire == 5 ? 4 : 0;
            if (f.wire == 2)
                varint(length);
            if (length > uint64_t(end - p))
                throw std::runtime_error("truncated field");
            f.bytes.assign(p, p + length);
            p += length;
        } else
            throw std::runtime_error("unsupported wire");
        out.push_back(std::move(f));
    }
    return out;
}
const Field *find(const std::vector<Field> &fields, unsigned key) {
    for (const auto &f : fields)
        if (f.key == key)
            return &f;
    return nullptr;
}
std::map<unsigned, uint64_t> numbers(const std::vector<Field> &fields) {
    std::map<unsigned, uint64_t> out;
    for (const auto &f : fields) {
        if (f.wire != 0 || out.count(f.key))
            throw std::runtime_error("expected one varint");
        out[f.key] = f.value;
    }
    return out;
}
std::vector<Field> child(const std::vector<Field> &fields, unsigned key) {
    const auto *f = find(fields, key);
    check(f && f->wire == 2, "missing nested message");
    return decode(f->bytes.data(), f->bytes.size());
}
std::vector<Field> nested(const Bytes &message, unsigned key) {
    return child(decode(message.data(), message.size()), key);
}

Bytes message(unsigned key, const Bytes &value) {
    Bytes out;
    auto var = [&](uint64_t n) {
        while (n > 127) {
            out.push_back(uint8_t(n) | 128);
            n >>= 7;
        }
        out.push_back(uint8_t(n));
    };
    var(key * 8 + 2);
    var(value.size());
    out.insert(out.end(), value.begin(), value.end());
    return out;
}
Bytes varintField(unsigned key, uint64_t value) {
    Bytes out;
    auto var = [&](uint64_t n) {
        while (n > 127) {
            out.push_back(uint8_t(n) | 128);
            n >>= 7;
        }
        out.push_back(uint8_t(n));
    };
    var(key * 8);
    var(value);
    return out;
}
Bytes concat(std::initializer_list<Bytes> parts) {
    Bytes out;
    for (const auto &p : parts)
        out.insert(out.end(), p.begin(), p.end());
    return out;
}

void testCaptureRequest() {
    auto body = child(nested(captureSettingsRpc(9, 1920, 1080, 60, 4, kCurrentCaptureScreen), 21), 2);
    auto values = numbers(body);
    // Observed viewer capture body is option 2, quality, 1920x1080, fps_count 60.
    // Hot apply adds cursor_capture and current-screen -1; it does not invent a monitor id.
    check(values.size() == 7, "unexpected capture fields");
    check(values[1] == 2 && values[2] == 4 && values[3] == 1, "fps option, quality, cursor");
    check(values[4] == uint64_t(-1), "screen_id -1 is the current capture");
    check(values[5] == 1920 && values[6] == 1080 && values[18] == 60, "resolution and fps_count");
    auto slow = numbers(child(nested(captureSettingsRpc(3, 1280, 720, 30, 1, 0), 21), 2));
    check(slow[1] == 1 && slow[4] == 0 && slow[18] == 30, "30 fps option and explicit screen 0");
    bool rejected = false;
    try {
        captureSettingsRpc(1, 1280, 720, 60, 3, -2);
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    check(rejected, "screen id below -1 is not current-capture");
}

void testAckAndSync() {
    const auto header = message(1, varintField(1, 77));
    auto headerOnly = parseCaptureRpcAck(message(22, header).data(), message(22, header).size());
    check(headerOnly && headerOnly->requestId == 77 && !headerOnly->capture && !captureAckRefused(*headerOnly),
          "header-only ack is not a refusal");
    auto extraHeader = message(22, concat({message(1, concat({varintField(1, 77), varintField(2, 1)})), message(2, {})}));
    auto extra = parseCaptureRpcAck(extraHeader.data(), extraHeader.size());
    check(extra && extra->headerExtras == 1 && extra->capture && !captureAckRefused(*extra),
          "unknown response-header fields are not a refusal");
    auto refused = message(22, concat({header, message(2, message(1, varintField(1, 5)))}));
    auto bad = parseCaptureRpcAck(refused.data(), refused.size());
    check(bad && bad->errors == 1 && bad->errorCode == 5 && captureAckRefused(*bad), "nonzero error refuses");
    auto otherRpc = message(22, concat({header, message(4, varintField(1, 1))}));
    auto other = parseCaptureRpcAck(otherRpc.data(), otherRpc.size());
    check(other && other->payloadExtras == 1 && !other->capture && captureAckRefused(*other),
          "a different RPC arm is not a capture ack");
    auto sync = concat({varintField(1, 60), varintField(2, 3), varintField(9, 1280), varintField(10, 720)});
    auto parsed = parseCaptureSettingSync(message(25, sync).data(), message(25, sync).size());
    check(parsed && parsed->hasFps && parsed->fps == 60 && parsed->quality == 3 && parsed->width == 1280 &&
              parsed->height == 720,
          "sync reports applied fps, quality and size");
    check(!parseCaptureSettingSync(message(22, header).data(), message(22, header).size()), "ack is not a sync");
}

void testScreens() {
    // Screen.dpr is a fixed64 double. 1.0 must not make the inventory unreadable.
    const Bytes dpr{0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x3f};
    auto screen = concat({varintField(1, 4), dpr});
    auto oneScreen = message(7, message(1, screen));
    auto parsed = parseScreenSources(oneScreen.data(), oneScreen.size());
    check(parsed && parsed->count == 1 && parsed->screenId == 4, "one screen without current id");
    auto two = message(7, concat({message(1, varintField(1, 1)), message(1, varintField(1, 2))}));
    auto many = parseScreenSources(two.data(), two.size());
    check(many && many->count == 2 && many->screenId == -1, "do not guess among screens");
    auto selected = message(7, concat({message(1, varintField(1, 1)), message(1, varintField(1, 2)), varintField(2, 2)}));
    auto current = parseScreenSources(selected.data(), selected.size());
    check(current && current->screenId == 2, "explicit current screen");
    auto missed = message(7, concat({message(1, screen), varintField(2, 9)}));
    auto miss = parseScreenSources(missed.data(), missed.size());
    check(miss && miss->count == 1 && miss->screenId == -1, "explicit miss is not rewritten");
}

void testPlan() {
    auto plan = [](QualityApplyInput in) { return planQualityApply(in); };
    QualityApplyInput streaming;
    streaming.hasPeer = true;
    streaming.streaming = true;
    streaming.captureChanged = true;
    streaming.channelReady = true;
    auto hot = plan(streaming);
    check(hot.action == QualityApply::Capture && hot.reason == QualityApplyReason::CaptureRpc, "fps/quality stays on the peer");
    check(std::string(qualityApplyReasonText(hot.reason)) == "capture-rpc", "capture reason text");
    streaming.channelReady = false;
    check(plan(streaming).reason == QualityApplyReason::ReconnectChannel, "closed channel still reconnects");
    streaming.channelReady = true;
    streaming.hevcChanged = true;
    check(plan(streaming).reason == QualityApplyReason::ReconnectCodec, "codec change needs a new offer");
    streaming.hevcChanged = false;
    streaming.capChanged = true;
    streaming.cap = 0;
    check(plan(streaming).reason == QualityApplyReason::ReconnectBitrate, "clearing the cap reconnects");
    streaming.captureChanged = false;
    streaming.cap = 8;
    streaming.rembAvailable = true;
    check(plan(streaming).action == QualityApply::Local && plan(streaming).reason == QualityApplyReason::LocalBitrate,
          "remb cap stays local");
    streaming.capChanged = false;
    check(plan(streaming).reason == QualityApplyReason::LocalDisplay, "vsync/delay stays local");
    QualityApplyInput early;
    early.hasPeer = true;
    early.captureChanged = true;
    early.channelReady = true;
    check(plan(early).reason == QualityApplyReason::ReconnectNotStreaming, "before streaming, connect options apply");
    check(plan({}).action == QualityApply::Save, "offline changes are saved");
}

int main() {
    try {
        testCaptureRequest();
        testAckAndSync();
        testScreens();
        testPlan();
        std::cout << "Quality RPC tests passed (hot-apply request, ack, sync, screen id, reconnect policy).\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
