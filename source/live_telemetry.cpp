#include "live_telemetry.hpp"
#include "protocol.hpp"
#include "diagnostics.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <fstream>
#include <map>
#ifdef __SWITCH__
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
namespace uu {
struct LiveTelemetry::Impl {
    int fd=-1;
#ifdef __SWITCH__
    sockaddr_in target{};
#endif
    std::string pair,session;
    uint64_t sequence=0,sent=0,failed=0;
    uint32_t window=0,now=0,lastFrame=0;
    bool started=false,streaming=false;
    uint64_t loops=0,uploads=0,gapSamples=0,totalUploads=0;
    double uploadUs=0;
    unsigned maxGap=0;
    std::array<uint32_t,129> histogram{};
    Impl() {
#ifdef __SWITCH__
        std::ifstream file("sdmc:/switch/UURemote-ns/telemetry.ini");
        std::map<std::string,std::string> settings;
        std::string line;
        while(std::getline(file,line)) {
            if(!line.empty() && line.back()=='\r') line.pop_back();
            const auto eq=line.find('=');
            if(eq!=line.npos && line.size()<256) settings[line.substr(0,eq)]=line.substr(eq+1);
        }
        if(settings["enabled"]!="1") return;
        const auto &host=settings["host"];const auto &port=settings["port"];pair=settings["pair"];
        if(pair.size()!=48 || !std::all_of(pair.begin(),pair.end(),[](unsigned char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');})) return;
        if(port.empty() || port.size()>5 || !std::all_of(port.begin(),port.end(),[](unsigned char c){return c>='0'&&c<='9';})) return;
        const auto number=std::stoul(port);
        if(number<1024 || number>65535 || inet_pton(AF_INET,host.c_str(),&target.sin_addr)!=1) return;
        const auto ip=ntohl(target.sin_addr.s_addr);
        if((ip>>24)!=10 && (ip>>20)!=0xac1 && (ip>>16)!=0xc0a8) return;
        target.sin_len=sizeof target;target.sin_family=AF_INET;target.sin_port=htons(uint16_t(number));
        session=uuid().substr(0,12);
        fd=socket(AF_INET,SOCK_DGRAM,0);
        if(fd<0) return;
        const int flags=fcntl(fd,F_GETFL,0);
        if(flags<0 || fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0) {close(fd);fd=-1;return;}
        diagnostic("live-telemetry-enabled");
#endif
    }
    ~Impl() {
#ifdef __SWITCH__
        if(fd>=0) close(fd);
#endif
    }
    void clearWindow() {
        window=now;loops=uploads=gapSamples=0;uploadUs=0;maxGap=0;histogram.fill(0);
    }
};
LiveTelemetry::LiveTelemetry() noexcept {try{impl_=std::make_unique<Impl>();}catch(...){diagnostic("live-telemetry-init-unavailable");}}
LiveTelemetry::~LiveTelemetry()=default;
bool LiveTelemetry::observe(uint32_t ticks,bool streaming,bool submitted,double uploadUs) noexcept {
    if(!impl_ || impl_->fd<0) return false;
    auto &p=*impl_;p.now=ticks;
    if(!p.started || p.streaming!=streaming) {
        p.started=true;p.streaming=streaming;p.lastFrame=0;p.clearWindow();
    }
    ++p.loops;
    if(streaming && submitted) {
        if(p.lastFrame) {
            const auto gap=uint32_t(ticks-p.lastFrame);
            ++p.histogram[std::min(gap,128u)];++p.gapSamples;p.maxGap=std::max(p.maxGap,gap);
        }
        p.lastFrame=ticks;++p.uploads;++p.totalUploads;p.uploadUs+=uploadUs;
    }
    return uint32_t(ticks-p.window)>=1000;
}
void LiveTelemetry::publish(const StreamStatsSnapshot &s,bool vsync,bool nv12,double refreshMs,unsigned stableSwaps, bool nativeDisplay, uint64_t directFrames, uint64_t mapHits, uint64_t mapImports, uint64_t mapCached) noexcept {
    if(!impl_ || impl_->fd<0) return;
    auto &p=*impl_;
    try {
        unsigned p95=0;uint64_t count=0;
        const auto threshold=(p.gapSamples*95+99)/100;
        if(threshold) for(unsigned bin=0;bin<p.histogram.size();++bin) {
            count+=p.histogram[bin];if(count>=threshold) {p95=bin;break;}
        }
        const auto elapsed=std::max(uint32_t(p.now-p.window),1u);
        auto r=[](double value){return std::round(value*1000)/1000;};
        // Explicit allowlist only: never serialize Snapshot, SDP, config or log text.
        const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        Json data={{"schema",10},{"pair",p.pair},{"session",p.session},{"seq",++p.sequence},
            {"version",UU_VERSION},{"ticks_ms",p.now},{"streaming",p.streaming},
            {"path",s.transport.substr(0,120)},{"local",s.localEndpoint.substr(0,100)},{"remote",s.remoteEndpoint.substr(0,100)},
            {"rtt_ms",s.rttMs},{"mbps",r(s.video.bitrateMbps)},{"initial_gap_pct",r(s.video.lossPercent)},
            {"jitter_ms",r(s.video.jitterMs)},{"decode_fps",r(s.decodeFps)},{"decode_ms",r(s.decodeMs)},
            {"display_fps",r(p.uploads*1000.0/elapsed)},{"render_hz",r(p.loops*1000.0/elapsed)},
            {"upload_ms",r(p.uploads?p.uploadUs/p.uploads/1000:0)},
            {"gap_p95_ms",p95},{"gap_max_ms",p.maxGap},
            {"frame_age_ms",p.streaming&&p.lastFrame?uint32_t(p.now-p.lastFrame):0},
            {"decode_errors",s.decodeErrors},{"decode_drop",s.droppedFrames},{"display_skip",s.presentationSkipped},
            {"accepted",s.receiverAccepted},{"rx_drop",s.receiverDropped},{"wait_idr",s.receiverWaitingIdr},
            {"nack",s.nackSent},{"rtx",s.rtxRecovered},{"wait_ms",s.receiverRecentWaitMs},
            {"ready",s.readyFrames},{"buffer_ms",s.playoutDelayMs},{"vsync",vsync},{"nv12",nv12},
            {"refresh_ms",r(refreshMs)},{"stable_swaps",stableSwaps},
            {"width",s.width},{"height",s.height},{"requested_fps",s.requestedFps},
            {"rx_pending",s.receiverPending},{"rx_block_ms",s.receiverBlockedMs},
            {"repair_ms",r(s.repairRttMs)},{"nack_retry_ms",s.nackRetryMs},
            {"nack_repeat",s.nackRepeated},{"pli",s.receiverPli},{"kf_requests",s.keyframeRequests},
            {"cap_mbps",s.maxBitrateMbps},{"cap_feedback",s.bitrateFeedbackSent?1:0},
            {"hevc",s.codec.rfind("H.265",0)==0?1:0},
            {"dec_total",s.decodedTotal},{"pub_total",s.publishedTotal},{"epoch",s.mediaEpoch},
            {"q_in",s.inputQueued},{"hw_frame",s.hardwareFrame?1:0},
            {"stats_ms",s.sampledMs},{"snap_age",s.sampledMs?std::max<int64_t>(0,now-s.sampledMs):-1},
            {"present_total",p.totalUploads},{"sup_due",s.skipDue},{"q_bound",s.skipBound},{"sel_late",s.selectedLateMs},
            {"cpu_xfer",s.cpuTransfers},{"gpu_pub",s.nativePublished},
            {"native_display",nativeDisplay?1:0},{"gpu_present",directFrames},
            {"map_hit",mapHits},{"map_new",mapImports},{"map_live",mapCached},
            {"touch_packets",s.touchPackets},{"touch_fail",s.touchFailures},{"touch_points",s.touchPoints},
            {"quality_req",s.qualitySent},{"quality_ack",s.qualityAcked},{"quality_fail",s.qualityFailed},{"quality_pending",s.qualityPending?1:0},
            {"screen_id",s.activeScreenId},{"quality_code",s.qualityCode},{"quality_errors",s.qualityErrors},
            {"sent",p.sent},{"send_drop",p.failed}};
        const auto payload=data.dump();
#ifdef __SWITCH__
        if(payload.size()<=1400 && sendto(p.fd,payload.data(),payload.size(),0,
            reinterpret_cast<const sockaddr *>(&p.target),sizeof p.target)==ssize_t(payload.size())) ++p.sent;
        else ++p.failed;
#endif
    } catch(...) {++p.failed;}
    p.clearWindow();
}
}
