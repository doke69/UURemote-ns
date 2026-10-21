// Prepared only. Deployment/execution requires a new, specific authorization.
static Service driver;
static const unsigned char baseline[6]={1,0,0x5e,0,0,1};
static const unsigned char temporary[12]={1,0,0x5e,0,0,1,0x33,0x33,0,0,0,1};
static const char *once_path="sdmc:/switch/UU-IPv6-Inspector/v016-once.flag";
static const char *pending_path="sdmc:/switch/UU-IPv6-Inspector/v016-pending.flag";
static int marker_exists(const char *path){FILE *f=fopen(path,"r");if(!f)return 0;fclose(f);return 1;}
static int mark(const char *path,const char *text){
    FILE *f=fopen(path,"wx");if(!f)return 0;
    int ok=fputs(text,f)>=0;ok=fflush(f)==0&&ok;ok=fclose(f)==0&&ok;return ok;
}
// Return inferred count, or -1 for an ambiguous/invalid snapshot.
static int snapshot(unsigned char *out_record,unsigned char *groups){
    unsigned char records[16*0xb0]={0};u32 count=0;
    Result rc=serviceDispatchOut(&driver,129,count,
        .buffer_attrs={SfBufferAttr_Out|SfBufferAttr_HipcAutoSelect},
        .buffers={{records,sizeof(records)}});
    logline("snapshot rc=0x%08x records=%u",rc,count);
    if(R_FAILED(rc)||count>16)return -1;
    unsigned matches=0;int n=-1;
    for(u32 i=0;i<count;i++){
        unsigned char *p=records+i*0xb0;
        if(memcmp(p,"wl0\0",4))continue;
        matches++;n=0;int zero=0;
        for(unsigned j=0;j<16;j++){
            unsigned char *mac=p+0x48+6*j;unsigned any=0;
            for(unsigned k=0;k<6;k++)any|=mac[k];
            if(!any){zero=1;continue;}
            if(zero||!(mac[0]&1))return -1;
            memcpy(groups+6*n,mac,6);n++;
        }
        if(out_record)memcpy(out_record,p,0xb0);
    }
    return matches==1?n:-1;
}
static int is_baseline(int n,const unsigned char *p){return n==1&&!memcmp(p,baseline,6);}
static int is_temporary(int n,const unsigned char *p){return n==2&&!memcmp(p,temporary,12);}
static Result set_groups(Service *iface,const unsigned char *groups,size_t bytes){
    return serviceDispatch(iface,260,
        .buffer_attrs={SfBufferAttr_In|SfBufferAttr_HipcAutoSelect},
        .buffers={{groups,bytes}});
}
static Service iface;
static int experiment_apply(void){
    unsigned char groups[96]={0};int n=snapshot(NULL,groups);
    if(!is_baseline(n,groups)){logline("Baseline changed; no filter write.");return 0;}
    if(!mark(once_path,"One experiment attempt; never auto-repeat.\n")){
        logline("One-shot claim unavailable; no filter write.");return 0;}
    if(!mark(pending_path,"Restore exact baseline 01:00:5e:00:00:01 only after state guard.\n")){
        logline("Recovery marker unavailable; no filter write.");return 0;}
    Result rc=set_groups(&iface,temporary,sizeof temporary);
    logline("append-one-group rc=0x%08x",rc);
    memset(groups,0,sizeof groups);n=snapshot(NULL,groups);
    if(R_FAILED(rc)||!is_temporary(n,groups)){
        logline("Temporary state not confirmed; skip capture, inspect/restore guarded state.");return 0;}
    logline("Temporary cache confirmed; IPv4 original retained. Passive60s; no packet sender.");
    return 1;
}
// One guarded restore attempt per invocation. Never clobber concurrent changes.
static void restore(void){
    if(!marker_exists(pending_path))return;
    unsigned char groups[96]={0};int n=snapshot(NULL,groups);
    if(is_baseline(n,groups)){
        logline("Baseline already present; no restore write.");
        if(remove(pending_path))logline("Recovery marker cleanup failed.");
        return;
    }
    if(!is_temporary(n,groups)){
        logline("State differs from our temporary list: NO overwrite. Recovery remains pending.");return;
    }
    Result rc=set_groups(&iface,baseline,sizeof baseline);
    logline("restore-original rc=0x%08x",rc);
    memset(groups,0,sizeof groups);n=snapshot(NULL,groups);
    if(R_SUCCEEDED(rc)&&is_baseline(n,groups)){
        logline("Original cache readback confirmed.");
        if(remove(pending_path))logline("Recovery marker cleanup failed.");
    }else logline("RESTORE NOT VERIFIED; pending marker preserved. No automatic retry.");
}
static void run(PadState *pad,int recovery){
    Service creator={0};unsigned char record[0xb0]={0},groups[96]={0};
    Result rc=smGetService(&creator,"wlan:nd");logline("creator rc=0x%08x",rc);
    if(R_FAILED(rc))return;
    rc=serviceDispatch(&creator,0,.out_num_objects=1,.out_objects=&driver);
    logline("driver rc=0x%08x",rc);if(R_FAILED(rc))goto done;
    int n=snapshot(record,groups);
    if((!recovery&&!is_baseline(n,groups))||(recovery&&!is_baseline(n,groups)&&!is_temporary(n,groups))){
        logline("Unexpected baseline; stopped without opening interface.");goto done;
    }
    if(recovery&&is_baseline(n,groups)){restore();goto done;}
    rc=serviceDispatch(&driver,0,
        .buffer_attrs={SfBufferAttr_In|SfBufferAttr_HipcAutoSelect},
        .buffers={{record,sizeof record}},.out_num_objects=1,.out_objects=&iface);
    logline("OpenNetworkInterface rc=0x%08x",rc);if(R_FAILED(rc))goto done;
    if(recovery){restore();goto done;}
    rc=bsdInitialize(NULL,1,BsdServiceType_System);logline("bsd:s rc=0x%08x",rc);
    if(R_SUCCEEDED(rc)){passive(pad);restore();bsdExit();}
done:
    if(serviceIsActive(&iface))serviceClose(&iface);
    if(serviceIsActive(&driver))serviceClose(&driver);
    serviceClose(&creator);
}
int main(void){
    consoleInit(NULL);PadState pad;padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);
    mkdir("sdmc:/switch/UU-IPv6-Inspector",0777);char path[180];
    snprintf(path,sizeof path,"sdmc:/switch/UU-IPv6-Inspector/report-%llu.txt",(unsigned long long)armGetSystemTick());
    report=fopen(path,"wx");logline("inspector_version=0.16.0 EXPERIMENT: filter change requires authorization");
    logline("A: append one IPv6 multicast MAC, passive60s, restore. X: guarded recovery only. +: exit.");
    int attempted=0;
    while(appletMainLoop()){
        padUpdate(&pad);u64 down=padGetButtonsDown(&pad);if(down&HidNpadButton_Plus)break;
        if(!attempted&&(down&(HidNpadButton_A|HidNpadButton_X))){
            attempted=1;
            if(hosversionGet()!=MAKEHOSVERSION(23,0,1)||sizeof(struct ifreq)!=32){logline("Wrong HOS/ABI; stopped.");continue;}
            int pending=marker_exists(pending_path);
            if((down&HidNpadButton_X)&&pending)run(&pad,1);
            else if(down&HidNpadButton_A){
                if(pending||marker_exists(once_path))logline("Prior attempt present; no new experiment. X can recover pending state.");
                else run(&pad,0);
            }
            logline("Finished; verify restore status before exiting. Reconnect DBI MTP.");
        }
        consoleUpdate(NULL);
    }
    if(report)fclose(report);
    consoleExit(NULL);return 0;
}
