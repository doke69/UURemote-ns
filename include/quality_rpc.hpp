#pragma once
#include "protocol.hpp"
#include <optional>
namespace uu {
struct CaptureRpcAck {
    uint64_t requestId=0;unsigned errors=0,entries=0;int errorCode=0;bool capture=false;
    // headerExtras are unknown Request/Response header fields. payloadExtras are
    // RpcResponse arms other than CaptureSettingResponse. otherFields is their sum.
    unsigned headerExtras=0,payloadExtras=0,otherFields=0;
};
struct ScreenSourceInfo {int screenId=-1;unsigned count=0;};
// Message 25 CaptureSettingSync. fps/quality are the host's applied stream values.
struct CaptureSettingSync {
    bool hasFps=false,hasQuality=false,hasWidth=false,hasHeight=false;
    int fps=0,quality=0,width=0,height=0;
};
// screen_id -1 is the official current-capture target (not a monitor switch).
constexpr int kCurrentCaptureScreen=-1;
std::optional<ScreenSourceInfo> parseScreenSources(const uint8_t *data,size_t size) noexcept;
Bytes nativeTouchMessage(uint64_t seq,int type,const Json &points);
Bytes simpleActionMessage(uint64_t seq,unsigned action,std::optional<bool> enabled=std::nullopt);
Bytes captureSettingsRpc(uint64_t requestId,int width,int height,int fps,int quality,int screenId);
std::optional<CaptureRpcAck> parseCaptureRpcAck(const uint8_t *data,size_t size) noexcept;
std::optional<CaptureSettingSync> parseCaptureSettingSync(const uint8_t *data,size_t size) noexcept;
// A clean CaptureSetting response keeps the peer. Error records or a different
// RPC arm do not. Extra header fields alone are not a refusal.
bool captureAckRefused(const CaptureRpcAck &ack) noexcept;

enum class QualityApply : int { Save=0, Local=1, Capture=2, Reconnect=3 };
enum class QualityApplyReason : int {
    SaveUntilConnect=0, LocalDisplay, LocalBitrate, CaptureRpc,
    ReconnectNotStreaming, ReconnectCodec, ReconnectBitrate, ReconnectChannel
};
struct QualityApplyInput {
    bool hasPeer=false,streaming=false,captureChanged=false,hevcChanged=false,capChanged=false;
    int cap=0;bool rembAvailable=false,channelReady=false;
};
struct QualityApplyPlan {QualityApply action=QualityApply::Save;QualityApplyReason reason=QualityApplyReason::SaveUntilConnect;};
QualityApplyPlan planQualityApply(const QualityApplyInput &input) noexcept;
const char *qualityApplyReasonText(QualityApplyReason reason) noexcept;
}
