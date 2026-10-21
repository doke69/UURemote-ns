// Finite passive observation on this process's own standard BPF buffer.
#include <switch.h>
#include <switch/services/bsd.h>
#include <net/if.h>
#include <net/bpf.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
static FILE *report;
static void logline(const char *fmt,...) {
    va_list a,b;va_start(a,fmt);va_copy(b,a);vprintf(fmt,a);printf("\n");
    if(report){vfprintf(report,fmt,b);fprintf(report,"\n");fflush(report);}va_end(b);va_end(a);
}
static int dual_ioctl(int fd,u32 request,void *data,size_t size,const char *label) {
    if(!size || size>8192 || size!=IOCPARM_LEN(request))return -1;
    void *input=calloc(1,size),*output=calloc(1,size);
    if(!input||!output){free(input);free(output);return -1;}
    memcpy(input,data,size);
    const struct {s32 fd;u32 request;s32 bufcount;} in={fd,request,1};
    struct {s32 ret,error;} out={-1,-1};
    Result rc=serviceDispatchInOut(bsdGetServiceSession(),19,in,out,
        .buffer_attrs={
            SfBufferAttr_In|SfBufferAttr_HipcAutoSelect,SfBufferAttr_In|SfBufferAttr_HipcAutoSelect,
            SfBufferAttr_In|SfBufferAttr_HipcAutoSelect,SfBufferAttr_In|SfBufferAttr_HipcAutoSelect,
            SfBufferAttr_Out|SfBufferAttr_HipcAutoSelect,SfBufferAttr_Out|SfBufferAttr_HipcAutoSelect,
            SfBufferAttr_Out|SfBufferAttr_HipcAutoSelect,SfBufferAttr_Out|SfBufferAttr_HipcAutoSelect},
        .buffers={{input,size},{NULL,0},{NULL,0},{NULL,0},{output,size},{NULL,0},{NULL,0},{NULL,0}});
    logline("%s request=0x%08x bytes=%zu ipc=0x%08x ret=%d raw_errno=%d",label,request,size,rc,out.ret,out.error);
    int ok=R_SUCCEEDED(rc)&&out.ret==0;
    if(ok)memcpy(data,output,size);
    free(input);free(output);return ok?0:-1;
}
struct counters {u64 batches,bytes,records,ipv4,ipv6,vlan,icmp6,ra,rs,ns,na,udp6,other,malformed,diag,probe4,probe6,multicast6,unicast6;};
static unsigned be16(const unsigned char *p){return (unsigned)p[0]*256+p[1];}
static void count_frame(const unsigned char *p,size_t n,struct counters *c) {
    if(n<14){c->malformed++;return;}
    unsigned type=be16(p+12);size_t off=14;
    for(unsigned tag=0;tag<2&&(type==0x8100||type==0x88a8);tag++){
        c->vlan++;if(n<off+4){c->malformed++;return;}type=be16(p+off+2);off+=4;
    }
    if(type==0x0800){
        // Exclude this tool's IPv4 UDP diagnostics and acknowledgements.
        if(n>=off+20&&(p[off]>>4)==4&&p[off+9]==17){size_t ihl=(p[off]&15)*4;
            if(ihl>=20&&n>=off+ihl+4&&(be16(p+off+ihl)==26791||be16(p+off+ihl+2)==26791)){c->diag++;return;}}
        c->ipv4++;
        if(n>=off+20 && (p[off]>>4)==4 && p[off+9]==17){size_t ihl=(p[off]&15)*4;
            const char marker[]="UU6-PROBE-v1";
            if(ihl>=20 && n>=off+ihl+8+sizeof(marker)-1 && be16(p+off+ihl+2)==18770 && !memcmp(p+off+ihl+8,marker,sizeof(marker)-1))c->probe4++;}
        return;}if(type!=0x86dd){c->other++;return;}
    c->ipv6++;if(p[0]&1)c->multicast6++;else c->unicast6++;if(n<off+40||(p[off]>>4)!=6){c->malformed++;return;}
    if(p[off+6]==17){
        c->udp6++;const char marker[]="UU6-PROBE-v1";
        if(n>=off+40+8+sizeof(marker)-1 && be16(p+off+42)==18770 && !memcmp(p+off+48,marker,sizeof(marker)-1))c->probe6++;
    }
    if(p[off+6]==58&&n>off+40){c->icmp6++;switch(p[off+40]){
        case 133:c->rs++;break;case 134:c->ra++;break;case 135:c->ns++;break;case 136:c->na++;break;}}
}
static void count_buffer(const unsigned char *p,size_t n,struct counters *c) {
    size_t off=0;const size_t minimum=offsetof(struct bpf_hdr,bh_hdrlen)+sizeof(((struct bpf_hdr*)0)->bh_hdrlen);
    while(n-off>=minimum){
        struct bpf_hdr h={0};size_t copy=n-off<sizeof h?n-off:sizeof h;memcpy(&h,p+off,copy);
        if(h.bh_hdrlen<minimum||h.bh_hdrlen>n-off||h.bh_caplen>n-off-h.bh_hdrlen||h.bh_caplen>h.bh_datalen){c->malformed++;return;}
        c->records++;count_frame(p+off+h.bh_hdrlen,h.bh_caplen,c);
        size_t used=(size_t)h.bh_hdrlen+h.bh_caplen,step=BPF_WORDALIGN(used);
        if(step>n-off){if(used!=n-off)c->malformed++;return;}if(!step){c->malformed++;return;}off+=step;
    }
    if(off<n)c->malformed++;
}

static void passive(PadState *pad);
#include "membership.inc"
static void passive(PadState *pad) {
    int fd=bsdOpen("/dev/bpf0",O_RDWR);logline("bpf0 open=%d bsd_errno=%d ipc=0x%08x",fd,g_bsdErrno,g_bsdResult);
    if(fd<0)return;
    unsigned char *buffer=NULL;
    struct bpf_version version={0};if(dual_ioctl(fd,BIOCVERSION,&version,sizeof version,"version"))goto done;
    logline("bpf_version=%u.%u",version.bv_major,version.bv_minor);
    if(version.bv_major!=1)goto done;
    // BSD23 open initializes mode1 at0xffd0..0xffdc; no mode changes here.
    logline("buffer_mode=1 source=offline_open_initializer; not_runtime_queried");
    struct ifreq name={0};snprintf(name.ifr_name,sizeof name.ifr_name,"wl0");
    if(dual_ioctl(fd,BIOCSETIF,&name,sizeof name,"bind_wl0"))goto done;
    unsigned link=0;if(dual_ioctl(fd,BIOCGDLT,&link,sizeof link,"link_type"))goto done;
    logline("link=%u",link);if(link!=1)goto done;
    // GETIF is not exposed; name comes from the successful bind request.
    logline("bound_name=wl0 source=successful_bind_request");
    struct bpf_program_serialized filter={0};
    const struct bpf_insn insns[]={
        BPF_STMT(BPF_LD|BPF_H|BPF_ABS,12),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,0x86dd,3,0),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,0x0800,2,0),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,0x8100,1,0),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K,0x88a8,0,1),
        BPF_STMT(BPF_RET|BPF_K,128),BPF_STMT(BPF_RET|BPF_K,0)};
    filter.bf_len=sizeof insns/sizeof insns[0];memcpy(filter.bf_insns,insns,sizeof insns);
    if(dual_ioctl(fd,BIOCSETF_SERIALIZED,&filter,sizeof filter,"filter_snaplen128"))goto done;
    unsigned immediate=1;if(dual_ioctl(fd,BIOCIMMEDIATE,&immediate,sizeof immediate,"immediate_own_descriptor"))goto done;
    unsigned length=0;if(dual_ioctl(fd,BIOCGBLEN,&length,sizeof length,"buffer_length"))goto done;
    logline("buffer_length=%u hdr=%zu minimum=%zu align=%zu",length,sizeof(struct bpf_hdr),offsetof(struct bpf_hdr,bh_hdrlen)+sizeof(((struct bpf_hdr*)0)->bh_hdrlen),(size_t)BPF_ALIGNMENT);
    if(length<32||length>524288)goto done;
    int rc=bsdFcntl(fd,F_SETFL,O_NONBLOCK);logline("nonblock ret=%d raw_errno=%d ipc=0x%08x",rc,g_bsdErrno,g_bsdResult);if(rc<0)goto done;
    buffer=malloc(length);if(!buffer)goto done;
    if(!experiment_apply())goto done;
    struct counters c={0};u64 began=armGetSystemTick();unsigned previous=0;logline("Listening60s on own BPF copy. B/+ stops capture then attempts guarded restore. No sender.");
    while(appletMainLoop()){
        padUpdate(pad);if(padGetButtonsDown(pad)&(HidNpadButton_B|HidNpadButton_Plus))break;
        unsigned elapsed=(unsigned)(armTicksToNs(armGetSystemTick()-began)/1000000);if(elapsed>=60000)break;
        struct pollfd item={fd,POLLIN,0};rc=bsdPoll(&item,1,25);
        if(rc<0){if(g_bsdErrno==EINTR)continue;logline("poll error raw_errno=%d ipc=0x%08x",g_bsdErrno,g_bsdResult);break;}
        if(item.revents&(POLLERR|POLLHUP|POLLNVAL)){logline("poll_flags=%d",item.revents);break;}
        if(item.revents&POLLIN){ssize_t got=bsdRead(fd,buffer,length);
            if(got>0){c.batches++;c.bytes+=(u64)got;count_buffer(buffer,(size_t)got,&c);memset(buffer,0,length);}
            else if(got<0&&g_bsdErrno!=EAGAIN&&g_bsdErrno!=EWOULDBLOCK&&g_bsdErrno!=EINTR){logline("read error raw_errno=%d ipc=0x%08x",g_bsdErrno,g_bsdResult);break;}}
        if(elapsed/1000>=previous+5){previous=elapsed/1000;printf("%us v4=%llu v6=%llu RA=%llu\n",previous,(unsigned long long)c.ipv4,(unsigned long long)c.ipv6,(unsigned long long)c.ra);}
        consoleUpdate(NULL);
    }
    logline("counts batches=%llu bytes=%llu records=%llu v4=%llu v6=%llu vlan=%llu icmp6=%llu RA=%llu RS=%llu NS=%llu NA=%llu udp6=%llu other=%llu malformed=%llu",
        (unsigned long long)c.batches,(unsigned long long)c.bytes,(unsigned long long)c.records,(unsigned long long)c.ipv4,(unsigned long long)c.ipv6,(unsigned long long)c.vlan,(unsigned long long)c.icmp6,(unsigned long long)c.ra,(unsigned long long)c.rs,(unsigned long long)c.ns,(unsigned long long)c.na,(unsigned long long)c.udp6,(unsigned long long)c.other,(unsigned long long)c.malformed);
    logline("controlled_probe4=%llu controlled_probe6=%llu multicast6=%llu unicast6=%llu",(unsigned long long)c.probe4,(unsigned long long)c.probe6,(unsigned long long)c.multicast6,(unsigned long long)c.unicast6);
    logline("elapsed_ms=%llu excluded_diagnostic_frames=%llu live_ack=%u",(unsigned long long)(armTicksToNs(armGetSystemTick()-began)/1000000),(unsigned long long)c.diag,0u);
    logline("IPv6=0 in this window does not prove IPv6 unavailable. No IPv6 TX/connectivity evidence.");
done:
    if(buffer)free(buffer);
    {int closed=bsdClose(fd);logline("close ret=%d raw_errno=%d ipc=0x%08x",closed,g_bsdErrno,g_bsdResult);}
}
