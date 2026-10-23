#include "quality_rpc.hpp"
#include <limits>
#include <cstring>
#include <cmath>
#include <set>
#include <stdexcept>
namespace uu {
namespace {
void var(Bytes &b,uint64_t value) {while(value>127){b.push_back(uint8_t(value)|128);value>>=7;}b.push_back(uint8_t(value));}
void integer(Bytes &b,unsigned key,uint64_t value){var(b,key*8);var(b,value);}
void message(Bytes &b,unsigned key,const Bytes &value){var(b,key*8+2);var(b,value.size());b.insert(b.end(),value.begin(),value.end());}
struct Field {unsigned key=0,wire=0;uint64_t value=0;const uint8_t *data=nullptr;size_t size=0;};
class Reader {
    const uint8_t *p,*end;unsigned fields=0;bool valid=true;
    bool varint(uint64_t &value){value=0;for(unsigned i=0;i<10;++i){if(p==end)return valid=false;const auto byte=*p++;if(i==9 && byte>1)return valid=false;value|=uint64_t(byte&127)<<(i*7);if(!(byte&128))return true;}return valid=false;}
  public:
    Reader(const uint8_t *data,size_t size):p(data),end(data+size){}
    bool next(Field &f){
        if(!valid || p==end)return false;
        if(++fields>128)return valid=false;
        uint64_t tag=0;if(!varint(tag) || !(tag>>3) || (tag>>3)>0x1fffffff)return valid=false;
        f={};f.key=unsigned(tag>>3);f.wire=unsigned(tag&7);
        if(f.wire==0)return varint(f.value);
        uint64_t length=0;
        if(f.wire==2){if(!varint(length))return false;}else if(f.wire==1)length=8;else if(f.wire==5)length=4;else return valid=false;
        if(length>uint64_t(end-p))return valid=false;
        f.data=p;f.size=size_t(length);p+=length;return true;
    }
    bool good()const{return valid && p==end;}
};
}
// APK Main.Message field 3 / General.SimpleAction field 1; enum values
// recovered from cg.d1. These are one-way commands, not acknowledged RPCs.
// Verified mobile Wire adapters K3/C3/X6/b7: Main 20 -> Input 1 -> Touch.
Bytes nativeTouchMessage(uint64_t seq,int type,const Json &points){
    if(!seq || type<1 || type>4 || !points.is_array() || points.size()>10 || ((type<=2)==points.empty()))
        throw std::invalid_argument("Invalid native touch snapshot");
    Bytes touch,input,out;integer(touch,1,type);std::set<int> ids;
    auto real=[&](Bytes &dst,unsigned tag,float value){uint32_t bits;std::memcpy(&bits,&value,4);var(dst,tag*8+5);for(int i=0;i<4;++i)dst.push_back(uint8_t(bits>>(i*8)));};
    for(const auto &p:points){
        const int id=p.at("id").get<int>();const float x=p.at("x").get<float>(),y=p.at("y").get<float>();
        if(id<0 || id>9 || !ids.insert(id).second || !std::isfinite(x)||!std::isfinite(y)||x<0||x>1||y<0||y>1)
            throw std::invalid_argument("Invalid native touch point");
        Bytes point;integer(point,1,id);real(point,2,x);real(point,3,y);real(point,4,2.f);real(point,5,2.f);
        message(touch,2,point);
    }
    message(input,1,touch);integer(out,1,seq);integer(out,2,nowMs());message(out,20,input);return out;
}
Bytes simpleActionMessage(uint64_t seq,unsigned action,std::optional<bool> enabled) {
    if(!seq || !(action==2 || action==9 || action==10 || action==12 || action==13 ||
        action==16 || action==17 || action==18 || action==19) || (action==19)!=enabled.has_value())
        throw std::invalid_argument("Invalid system action");
    Bytes body,out;integer(body,1,action);
    if(enabled)integer(body,5,*enabled?1:0);
    integer(out,1,seq);integer(out,2,nowMs());message(out,3,body);return out;
}
Bytes captureSettingsRpc(uint64_t id,int w,int h,int fps,int quality,int screenId){
    if(!id || screenId<-1 || !((w==960&&h==540)||(w==1280&&h==720)||(w==1920&&h==1080)) ||
       (fps!=30&&fps!=60) || quality<1 || quality>5)throw std::invalid_argument("Invalid capture settings");
    Bytes capture,header,rpc,out;
    // rpc_main CaptureSettingRequest: 1 fps option (1=30, 2=60), 2 quality,
    // 3 cursor_capture, 4 screen_id, 5/6 resolution, 18 fps_count (the rate).
    // Field 1 alone is an index. Hosts that read fps_count ignore a missing 18,
    // which left the encoder on the connect-time rate. -1 is the current
    // capture (phone hot-apply); a listed id switches monitors.
    integer(capture,1,fps==30?1:2);integer(capture,2,quality);integer(capture,3,1);
    integer(capture,4,uint64_t(int64_t(screenId)));
    integer(capture,5,unsigned(w));integer(capture,6,unsigned(h));
    integer(capture,18,unsigned(fps));
    // Do not guess private custom-bitrate units or change chroma settings.
    integer(header,1,id);message(rpc,1,header);message(rpc,2,capture);
    integer(out,1,id);integer(out,2,nowMs());message(out,21,rpc);return out;
}

std::optional<ScreenSourceInfo> parseScreenSources(const uint8_t *data,size_t size)noexcept{
    if(!data || !size || size>65536)return {};
    bool found=false;ScreenSourceInfo result;
    Reader outer(data,size);Field f;
    while(outer.next(f))if(f.key==7){
        if(found || f.wire!=2)return {};found=true;
        std::array<int,32> ids{};unsigned count=0;
        int current=0;bool currentPresent=false;
        Reader sources(f.data,f.size);Field source;
        while(sources.next(source)){
            if(source.key==1){
                if(source.wire!=2 || count==ids.size())return {};
                int id=0;bool idPresent=false;Reader screen(source.data,source.size);Field prop;
                while(screen.next(prop))if(prop.key==1){
                    if(idPresent || prop.wire!=0 || prop.value>uint64_t(INT32_MAX))return {};
                    idPresent=true;id=int(prop.value);
                }
                if(!screen.good())return {};
                for(unsigned i=0;i<count;++i)if(ids[i]==id)return {};
                ids[count++]=id;
            }else if(source.key==2){
                if(currentPresent || source.wire!=0)return {};
                currentPresent=true;
                current=source.value<=uint64_t(INT32_MAX)?int(source.value):-1;
            }
        }
        if(!sources.good())return {};
        result.count=count;
        // Protobuf's omitted int32 ID is zero; it is accepted only when the
        // complete advertised list contains that same ID. A single listed
        // screen with no current id is that capture target. Never guess among
        // several screens, and never substitute when an explicit id misses.
        if(currentPresent){
            for(unsigned i=0;i<count;++i)if(current>=0 && ids[i]==current)result.screenId=current;
        }else{
            for(unsigned i=0;i<count;++i)if(ids[i]==0)result.screenId=0;
            if(result.screenId<0 && count==1)result.screenId=ids[0];
        }
    }
    if(!outer.good() || !found)return {};
    return result;
}

std::optional<CaptureRpcAck> parseCaptureRpcAck(const uint8_t *data,size_t size)noexcept{
    if(!data || !size || size>65536)return {};
    CaptureRpcAck result;bool response=false,header=false,id=false;
    Reader outer(data,size);Field f;
    while(outer.next(f))if(f.key==22){
        if(response || f.wire!=2)return {};response=true;
        Reader rpc(f.data,f.size);Field r;
        while(rpc.next(r)){
            if(r.key==1){
                if(header || r.wire!=2)return {};header=true;
                Reader head(r.data,r.size);Field h;
                while(head.next(h)){if(h.key==1){if(id || h.wire!=0)return {};id=true;result.requestId=h.value;}else ++result.headerExtras;}
                if(!head.good())return {};
            }else if(r.key==2){
                if(result.capture || r.wire!=2)return {};result.capture=true;
                Reader capture(r.data,r.size);Field e;
                while(capture.next(e))if(e.key==1){
                    if(e.wire!=2 || ++result.entries>64)return {};
                    Reader error(e.data,e.size);Field v;bool code=false;int entryCode=0;
                    while(error.next(v))if(v.key==1){
                        if(code || v.wire!=0)return {};code=true;
                        entryCode=int(int32_t(uint32_t(v.value)));
                    }
                    if(!error.good())return {};
                    // The repeated Error container also carries code-zero
                    // result records. Check every record, not container size
                    // or only the first record. Proto3's omitted int32 is zero.
                    if(entryCode!=0){
                        if(!result.errors)result.errorCode=entryCode;
                        ++result.errors;
                    }
                }
                if(!capture.good())return {};
            }else ++result.payloadExtras;
        }
        if(!rpc.good())return {};
    }
    if(!outer.good() || !response || !header || !id || !result.requestId)return {};
    result.otherFields=result.headerExtras+result.payloadExtras;
    return result;
}

std::optional<CaptureSettingSync> parseCaptureSettingSync(const uint8_t *data,size_t size)noexcept{
    if(!data || !size || size>65536)return {};
    CaptureSettingSync result;bool found=false;
    Reader outer(data,size);Field f;
    while(outer.next(f))if(f.key==25){
        if(found || f.wire!=2)return {};
        found=true;
        Reader sync(f.data,f.size);Field s;
        auto assign=[&](bool &present,int &dest)->bool{
            if(s.wire!=0 || s.value>uint64_t(INT32_MAX) || present)return false;
            present=true;dest=int(s.value);return true;
        };
        while(sync.next(s)){
            if(s.key==1){if(!assign(result.hasFps,result.fps))return {};}
            else if(s.key==2){if(!assign(result.hasQuality,result.quality))return {};}
            else if(s.key==5){if(!assign(result.hasWidth,result.width))return {};}
            else if(s.key==6){if(!assign(result.hasHeight,result.height))return {};}
            else if(s.key==9 && s.wire==0 && !result.hasWidth && s.value<=uint64_t(INT32_MAX)){result.hasWidth=true;result.width=int(s.value);}
            else if(s.key==10 && s.wire==0 && !result.hasHeight && s.value<=uint64_t(INT32_MAX)){result.hasHeight=true;result.height=int(s.value);}
        }
        if(!sync.good())return {};
    }
    if(!outer.good() || !found || (!result.hasFps && !result.hasQuality))return {};
    return result;
}

bool captureAckRefused(const CaptureRpcAck &ack)noexcept{
    return !ack.requestId || ack.errors || ack.payloadExtras;
}

QualityApplyPlan planQualityApply(const QualityApplyInput &in)noexcept{
    const bool streamChanged=in.captureChanged || in.capChanged || in.hevcChanged;
    if(!in.hasPeer || (!in.streaming && !streamChanged))
        return {QualityApply::Save,QualityApplyReason::SaveUntilConnect};
    // No live session yet: the next connect carries ConnectOptions. That is not
    // a hot apply, and there is no peer connection to preserve.
    if(!in.streaming) return {QualityApply::Reconnect,QualityApplyReason::ReconnectNotStreaming};
    // Codec is chosen in SDP. Clearing the cap, or a cap the REMB path cannot
    // send, cannot be expressed on the open peer.
    if(in.hevcChanged) return {QualityApply::Reconnect,QualityApplyReason::ReconnectCodec};
    if(in.capChanged && (in.cap<=0 || !in.rembAvailable))
        return {QualityApply::Reconnect,QualityApplyReason::ReconnectBitrate};
    if(!in.captureChanged)
        return {QualityApply::Local,in.capChanged?QualityApplyReason::LocalBitrate:QualityApplyReason::LocalDisplay};
    if(!in.channelReady) return {QualityApply::Reconnect,QualityApplyReason::ReconnectChannel};
    return {QualityApply::Capture,QualityApplyReason::CaptureRpc};
}

const char *qualityApplyReasonText(QualityApplyReason reason)noexcept{
    switch(reason){
    case QualityApplyReason::SaveUntilConnect:return "save-until-connect";
    case QualityApplyReason::LocalDisplay:return "local-display";
    case QualityApplyReason::LocalBitrate:return "local-bitrate";
    case QualityApplyReason::CaptureRpc:return "capture-rpc";
    case QualityApplyReason::ReconnectNotStreaming:return "reconnect-not-streaming";
    case QualityApplyReason::ReconnectCodec:return "reconnect-codec";
    case QualityApplyReason::ReconnectBitrate:return "reconnect-bitrate";
    case QualityApplyReason::ReconnectChannel:return "reconnect-channel";
    }
    return "quality-apply-unknown";
}
}
