#include <switch.h>
#include "membership.inc"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>

static FILE *report;
static void line(const char *fmt,...) {
    va_list ap;va_start(ap,fmt);vprintf(fmt,ap);va_end(ap);puts("");
    if(report){va_start(ap,fmt);vfprintf(report,fmt,ap);va_end(ap);fputc('\n',report);fflush(report);}
    consoleUpdate(NULL);
}
static int name_ok(const unsigned char *p) {
    for(unsigned i=0;i<16;i++){
        unsigned c=p[i];if(!c)return i!=0;
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return 0;
    }return 0;
}
static void query(void) {
    if(hosversionGet()!=MAKEHOSVERSION(23,0,1)){line("Unsupported HOS; stopped before service access.");return;}
    Service creator={0},driver={0};
    Result rc=smGetService(&creator,"wlan:nd");
    line("creator result=0x%08x",rc);if(R_FAILED(rc))return;
    rc=serviceDispatch(&creator,0,.out_num_objects=1,.out_objects=&driver);
    line("CreateDriverService result=0x%08x",rc);
    if(R_FAILED(rc)){serviceClose(&creator);return;}
    enum { STRIDE=0xb0,CAPACITY=16 };
    unsigned char records[STRIDE*CAPACITY]={0};u32 count=0;
    rc=serviceDispatchOut(&driver,129,count,
        .buffer_attrs={SfBufferAttr_Out|SfBufferAttr_HipcAutoSelect},
        .buffers={{records,sizeof(records)}});
    line("GetNetworkInterfaceList result=0x%08x count=%u capacity=%u stride=%u",rc,count,CAPACITY,STRIDE);
    if(R_SUCCEEDED(rc)&&count<=CAPACITY){
        for(u32 i=0;i<count;i++){
            const unsigned char *p=records+i*STRIDE;
            if(!name_ok(p)){line("record=%u unexpected name encoding; not interpreted",i);continue;}
            unsigned groups=0,invalid=0,zero_seen=0;
            for(unsigned j=0;j<16;j++){
                const unsigned char *mac=p+0x48+j*6;unsigned nonzero=0;
                for(unsigned k=0;k<6;k++)nonzero|=mac[k];
                if(!nonzero){zero_seen=1;continue;}
                if(zero_seen||!(mac[0]&1)){invalid++;continue;}
                groups++;
            }
            line("interface=%s cached_groups_inferred=%u invalid_entries=%u",(const char*)p,groups,invalid);
            if(!invalid)for(unsigned j=0;j<groups;j++){
                const unsigned char *mac=p+0x48+j*6;
                line("group%u=%02x:%02x:%02x:%02x:%02x:%02x",j,mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
            }
        }
    }else if(R_SUCCEEDED(rc))line("Unexpected count; no records interpreted.");
    serviceClose(&driver);serviceClose(&creator);
    line("Service sessions closed. No interface opened, no setter or packet sender called.");
}
int main(int argc,char **argv) {
    (void)argc;(void)argv;consoleInit(NULL);
    padConfigureInput(1,HidNpadStyleSet_NpadStandard);PadState pad;padInitializeDefault(&pad);
    mkdir("sdmc:/switch/UU-IPv6-Inspector",0777);
    char path[160];snprintf(path,sizeof path,"sdmc:/switch/UU-IPv6-Inspector/report-%llu.txt",(unsigned long long)armGetSystemTick());
    report=fopen(path,"w");
    line("inspector_version=0.16.0 READ-ONLY; active experiment withdrawn");u32 h=hosversionGet();
    line("hos=%u.%u.%u",HOSVER_MAJOR(h),HOSVER_MINOR(h),HOSVER_MICRO(h));
    line("A: query interface list once (read-only). +: exit.");
    line("Filter writes unavailable: shared update/recovery contract not established.");
    line("No OpenNetworkInterface, multicast setter, IPv6 TX or queue access.");
    int done=0;
    while(appletMainLoop()){
        padUpdate(&pad);u64 down=padGetButtonsDown(&pad);
        if(down&HidNpadButton_Plus)break;
        if(!done&&(down&HidNpadButton_A)){done=1;query();line("Finished. + exits; reconnect DBI MTP.");}
        consoleUpdate(NULL);
    }
    if(report)fclose(report);
    consoleExit(NULL);
    return 0;
}
