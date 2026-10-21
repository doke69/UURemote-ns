// Live UI controller. No nested render loop, SDL polling, or presentation here.
namespace tvui {
struct LiveInput {bool confirm=false,cancel=false,up=false,down=false,left=false,right=false,toolbar=false,network=false,qualityMenu=false,keyboard=false,disconnect=false;uint64_t held=0;};
class LiveFrontend {
    uu::Client &client;std::shared_ptr<uu::Media> media;SDL_Renderer *renderer;
    std::vector<Frame> stack;std::vector<Button> buttons;
    int scheme=2,draft=2,fps=60,resolution=720,codec=0,quality=3,bufferMs=16,touchMode=0,inputMode=0,xboxLayout=0,volume=100,mouseSpeed=1;
    int selectedDevice=0,deviceOffset=0,touchTarget=-1,touchStartY=0,hit=-1,pointerX=0,pointerY=0;
    bool vsync=true,sound=true,autoUnlock=false,autoLock=false,remoteMute=false,restoreSound=false;
    bool nativeMultitouch=true;Uint32 modeNoticeUntil=0;
    bool resetTouchZoom=false,touchDown=false,wasStreaming=false,wasAuthenticated=false,draining=false;
    Uint32 settingsSentAt=0,connectedAt=0;int requestedCap=0;
    std::string device,keyboardText,pendingShortcut,notice;
    const char *qualityNames[5]={"自动","省流","清晰","高清","原画"};
    static constexpr const char *prefsPath=
#ifdef __SWITCH__
        "sdmc:/switch/UURemote-ns/ui-state.json";
#else
        "ui-state.json";
#endif
    static std::string number(double value){if(value<0)return "--";char out[32];std::snprintf(out,sizeof out,"%.1f",value);return out;}
    void save(){
        std::ofstream(prefsPath)<<uu::Json{{"network_overlay",networkOverlay},{"sound",sound},{"volume",volume},{"mouse_speed",mouseSpeed},{"touch_mode",touchMode},{"scheme",scheme},{"xbox_layout",xboxLayout},{"multitouch",nativeMultitouch}}.dump(2);
    }
    void push(Page p){touchTarget=-1;touchDown=false;hit=-1;stack.push_back({p,0});notice.clear();}
    void back(){touchTarget=-1;touchDown=false;hit=-1;if(stack.size()>1)stack.pop_back();else if(wasStreaming){stack.clear();draining=true;}}
    void loadSettings(const uu::Snapshot &s){fps=s.fps;resolution=s.height;codec=s.hevc;quality=s.quality==5?0:std::clamp(s.quality,1,4);vsync=s.vsync;bufferMs=s.playoutDelayMs;requestedCap=s.maxBitrateMbps;}
    void applyQuality(const uu::Snapshot &s){
        if(s.qualityPending){notice="正在应用上一次设置，请稍候";loadSettings(s);return;}
        if(uu::display::vsync(renderer,vsync?1:0)!=0){notice="当前显示后端不支持该垂直同步设置";loadSettings(s);return;}
        client.command("quality-settings",{{"width",resolution==540?960:resolution==1080?1920:1280},{"height",resolution},{"fps",fps},
            {"quality",quality==0?5:quality},{"vsync",vsync},{"hevc",bool(codec)},{"playout_delay_ms",bufferMs},{"max_bitrate_mbps",requestedCap}});
        settingsSentAt=SDL_GetTicks();notice="正在应用设置；部分设置需要重新连接";
    }
    void chord(std::initializer_list<int> keys){for(int key:keys)client.command("input",{{"action","kbd_press"},{"key",key}});for(auto it=keys.end();it!=keys.begin();){--it;client.command("input",{{"action","kbd_release"},{"key",*it}});}}
    void action(int a,const uu::Snapshot &s){
        if(a>=1000 && a<1100){selectedDevice=a-1000;back();return;}
        if(a>=2000 && a<2200){if(keyboardText.size()<256)keyboardText+=char(a-2000);return;}
        const auto page=stack.back().page;
        if(page==Page::Resolution && a!=99){resolution=a-400;applyQuality(s);back();return;}
        if(page==Page::Codec && a!=99){codec=a-500;applyQuality(s);back();return;}
        if(page==Page::QualityPreset && a!=99){quality=a-510;requestedCap=0;applyQuality(s);back();return;}
        if(page==Page::Playout && a!=99){bufferMs=a-550;applyQuality(s);back();return;}
        if(page==Page::Touch && (a==610 || a==611)){touchMode=a-610;nativeMultitouch=false;client.command("release");resetTouchZoom=true;draining=true;save();return;}
        if(page==Page::Mouse && a!=99){mouseSpeed=a-620;save();back();return;}
        if(page==Page::Audio && a>=665){volume=a-640;media->setVolume(sound?volume:0);save();return;}
        switch(a){
        case 99:case 80:back();break;
        case 10:push(Page::Devices);break;case 11:push(Page::Account);break;
        case 12:if(!s.devices.empty() && selectedDevice<int(s.devices.size()))client.command("connect",{{"deviceId",s.devices[selectedDevice]["device_id"]}});break;
        case 13:push(Page::Gamepad);break;case 14:push(Page::Toolbar);break;
        case 20:push(Page::Disconnect);break;case 21:push(Page::Display);break;case 22:push(Page::Safety);break;
        case 23:push(Page::Peripherals);break;case 24:case 70:push(Page::More);break;case 25:push(Page::Windows);break;
        case 30:loadSettings(s);push(Page::Quality);break;case 31:loadSettings(s);push(Page::Picture);break;
        case 40:fps=60;applyQuality(s);break;case 41:fps=30;applyQuality(s);break;
        case 43:push(Page::QualityPreset);break;case 44:push(Page::Resolution);break;case 45:push(Page::Codec);break;
        case 46:vsync=!vsync;applyQuality(s);break;case 47:push(Page::Playout);break;case 48:push(Page::Audio);break;
        case 50:draft=scheme;push(Page::Schemes);stack.back().focus=draft-1;break;
        case 51:push(Page::Guide);break;case 52:case 58:push(Page::Logout);break;case 53:push(Page::InputMode);break;
        case 54:push(Page::Touch);break;case 55:push(Page::Keyboard);SDL_StartTextInput();break;
        case 56:push(Page::Stats);break;case 57:push(Page::About);break;case 59:push(Page::Mouse);break;
        case 60:draft=1;break;case 61:draft=2;break;case 62:scheme=draft;client.command("release");save();back();break;
        case 602:case 603:xboxLayout=a-602;client.command("release");draining=true;save();break;
        case 614:nativeMultitouch=true;client.command("release");resetTouchZoom=true;draining=true;save();break;
        case 612:resetTouchZoom=true;notice="画面缩放已重置";break;
        case 613:push(Page::XboxLayout);break;
        case 600:case 601:inputMode=a-600;client.command("release");draining=true;
            notice=inputMode?"实验手柄模式：请确认电脑游戏识别到控制器":"已切回键鼠映射";break;
        case 71:case 72:case 73:case 74:case 75:case 76:pendingShortcut=a==71?"显示桌面":a==72?"任务管理器":a==73?"切换窗口":a==74?"锁定桌面":a==75?"重启电脑":"关机";push(Page::WindowsConfirm);break;
        case 630:sound=!sound;media->setVolume(sound?volume:0);save();break;
        case 751:autoLock=!autoLock;client.command("system-action",{{"action",19},{"enabled",autoLock}});notice="自动锁屏请求已提交，效果待电脑确认";break;
        case 752:remoteMute=!remoteMute;client.command("system-action",{{"action",remoteMute?(restoreSound?18:17):16}});notice="电脑静音请求已提交，效果待电脑确认";break;
        case 753:restoreSound=!restoreSound;
            if(remoteMute){client.command("system-action",{{"action",16}});client.command("system-action",{{"action",restoreSound?18:17}});}
            notice="结束恢复设置已选择；再次静音时使用此设置";break;
        case 754:networkOverlay=!networkOverlay;save();break;
        case 760:
            if(s.streaming){
                if(pendingShortcut=="切换窗口")chord({18,9});
                else client.command("system-action",{{"action",pendingShortcut=="显示桌面"?2:pendingShortcut=="任务管理器"?13:pendingShortcut=="锁定桌面"?10:pendingShortcut=="重启电脑"?9:12}});
            }
            back();notice="已提交操作请求，等待电脑端结果";break;
        case 770:client.command("logout");client.command("login");stack.clear();draining=true;break;
        case 81:client.command("disconnect");stack.clear();draining=true;break;
        case 90:client.command("refresh");break;
        case 2300:if(!keyboardText.empty()){auto n=keyboardText.size()-1;while(n && (static_cast<unsigned char>(keyboardText[n])&0xc0)==0x80)--n;keyboardText.resize(n);}break;
        case 2301:keyboardText+=' ';break;case 2302:keyboardText.clear();break;
        case 2303:
            if(!s.streaming){notice="请先连接电脑";break;}
            for(unsigned char ch:keyboardText){if(!((ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||ch==' ')){notice="当前发送支持英文、数字和空格";return;}}
            for(unsigned char ch:keyboardText){const bool shifted=ch>='A'&&ch<='Z';int key=ch>='a'&&ch<='z'?ch-32:ch;
                if(!((key>=65&&key<=90)||(key>=48&&key<=57)||key==32)){notice="当前发送支持英文、数字和空格";return;}
                if(shifted)chord({16,key});else chord({key});}
            keyboardText.clear();notice="键盘输入已发送";break;
        default:notice="此功能尚未支持";break;
        }
    }
public:
    bool networkOverlay=false;
    explicit LiveFrontend(uu::Client &c,std::shared_ptr<uu::Media> m,SDL_Renderer *r):client(c),media(std::move(m)),renderer(r){
        try{std::ifstream file(prefsPath);if(file){std::string data((std::istreambuf_iterator<char>(file)),{});if(data.size()<=4096){auto p=uu::Json::parse(data);
            networkOverlay=p.value("network_overlay",false);sound=p.value("sound",true);volume=std::clamp(p.value("volume",100),0,100);
            mouseSpeed=std::clamp(p.value("mouse_speed",1),0,3);touchMode=std::clamp(p.value("touch_mode",0),0,1);scheme=std::clamp(p.value("scheme",2),1,2);xboxLayout=std::clamp(p.value("xbox_layout",0),0,1);nativeMultitouch=p.value("multitouch",true);}}}catch(...){}
        media->setVolume(sound?volume:0);
    }
    bool active()const{return !stack.empty();}
    bool captures()const{return active()||draining;}
    int mapping()const{return scheme;}
    bool trackpad()const{return touchMode==1;}
    bool multitouch()const{return nativeMultitouch;}
    void toggleInputMode(){
        if(active()||draining)return;
        inputMode^=1;client.command("release");draining=true;
        modeNoticeUntil=SDL_GetTicks()+1800;
    }
    bool gamepadMode()const{return inputMode==1;}
    bool xboxByLetter()const{return xboxLayout==1;}
    bool takeZoomReset(){const bool result=resetTouchZoom;resetTouchZoom=false;return result;}
    float pointerSpeed()const{const float speeds[]={.4f,.7f,1.f,1.4f};return speeds[mouseSpeed];}
    void sync(const uu::Snapshot &s){
        if(s.streaming!=wasStreaming || s.authenticated!=wasAuthenticated){stack.clear();buttons.clear();touchTarget=-1;hit=-1;touchDown=false;draining=true;
            if(s.authenticated && !s.streaming)push(Page::Home);
            if(s.streaming)connectedAt=SDL_GetTicks();}
        wasStreaming=s.streaming;wasAuthenticated=s.authenticated;
        if(s.authenticated && !s.streaming && stack.empty())push(Page::Home);
        if(!s.qualityPending && Uint32(SDL_GetTicks()-settingsSentAt)>1000)loadSettings(s);
        selectedDevice=std::clamp(selectedDevice,0,std::max(0,int(s.devices.size())-1));
        device=s.devices.empty()?"尚未发现电脑":s.devices[selectedDevice].value("alias",std::string("电脑"));
    }
    void pointer(bool down,int x,int y){
        if(!active() || draining)return;
        if(down){pointerX=x;pointerY=y;}SDL_Point point{pointerX,pointerY};
        if(down && !touchDown){touchTarget=-1;touchStartY=y;for(size_t i=0;i<buttons.size();++i)if(buttons[i].enabled&&SDL_PointInRect(&point,&buttons[i].box)){touchTarget=int(i);stack.back().focus=int(i);}}
        if(down && touchTarget>=0 && !SDL_PointInRect(&point,&buttons[touchTarget].box))touchTarget=-1;
        if(down && stack.back().page==Page::Devices && std::abs(y-touchStartY)>40){deviceOffset=std::max(0,deviceOffset+(y<touchStartY?1:-1));touchTarget=-1;touchStartY=y;}
        if(!down && touchDown && touchTarget>=0 && SDL_PointInRect(&point,&buttons[touchTarget].box))hit=touchTarget;
        touchDown=down;if(!down)touchTarget=-1;
    }
    void event(const SDL_Event &e){if(!active()||draining)return;
        if(e.type==SDL_MOUSEBUTTONDOWN && e.button.button==SDL_BUTTON_LEFT)pointer(true,e.button.x,e.button.y);
        if(e.type==SDL_MOUSEBUTTONUP && e.button.button==SDL_BUTTON_LEFT)pointer(false,e.button.x,e.button.y);
        if(e.type==SDL_MOUSEMOTION && touchDown)pointer(true,e.motion.x,e.motion.y);
        if(stack.back().page==Page::Keyboard && e.type==SDL_TEXTINPUT && keyboardText.size()<256)keyboardText+=e.text.text;
        if(e.type==SDL_MOUSEWHEEL && stack.back().page==Page::Devices)deviceOffset=std::max(0,deviceOffset-e.wheel.y);
    }
    bool input(const LiveInput &in,const uu::Snapshot &s,bool touchHeld){
        const bool before=captures();
        if(draining){if(!in.held && !touchHeld && !in.confirm && !in.cancel && !in.toolbar && !in.network && !in.qualityMenu && !in.keyboard && !in.disconnect)draining=false;return true;}
        if(s.streaming && (in.toolbar || in.network || in.qualityMenu || in.keyboard || in.disconnect)){
            client.command("release");if(in.toolbar && active()){stack.clear();buttons.clear();draining=true;return true;}
            stack.clear();push(in.network?Page::More:in.qualityMenu?Page::Quality:in.keyboard?Page::Keyboard:in.disconnect?Page::Disconnect:Page::Toolbar);loadSettings(s);return true;
        }
        if(!active())return before;
        if(in.cancel){back();return true;}
        auto &frame=stack.back();if(frame.focus<0 || frame.focus>=int(buttons.size()))frame.focus=0;
        const int dx=int(in.right)-int(in.left),dy=int(in.down)-int(in.up);
        if((dx||dy) && !buttons.empty()){
            const auto origin=buttons[frame.focus].box;double best=1e9;int next=frame.focus;
            for(size_t i=0;i<buttons.size();++i)if(buttons[i].enabled && int(i)!=frame.focus){auto b=buttons[i].box;double x=b.x+b.w*.5-origin.x-origin.w*.5,y=b.y+b.h*.5-origin.y-origin.h*.5;
                double forward=dx?x*dx:y*dy,side=dx?std::abs(y):std::abs(x);if(forward>1&&forward+side*3<best){best=forward+side*3;next=int(i);}}
            frame.focus=next;if(frame.page==Page::Schemes && next<2)draft=next+1;
        }
        if(hit>=0 && hit<int(buttons.size())){frame.focus=hit;hit=-1;if(buttons[frame.focus].enabled)action(buttons[frame.focus].action,s);}
        else if(in.confirm && !buttons.empty() && buttons[frame.focus].enabled)action(buttons[frame.focus].action,s);
        return true;
    }
    void draw(SDL_Renderer *r,Fonts &f,const uu::Snapshot &snapshot,const uu::StreamStatsSnapshot &stats){
        if(!active()){
            if(snapshot.streaming && int32_t(modeNoticeUntil-SDL_GetTicks())>0){
                rounded(r,{430,78,420,54},panel,12);
                aligned(r,f.small,inputMode?"手柄直连":"键鼠映射",{430,78,420,54},ink,true);
            }return;
        }
        auto &frame=stack.back(); const auto page=frame.page;
        buttons.clear();
        auto add=[&](int x,int y,int w,int h,std::string title,int action,bool enabled=true,bool chosen=false,std::string value="",Kind kind=Kind::Button){
            buttons.push_back({{x,y,w,h},std::move(title),action,enabled,chosen,std::move(value),kind});
        };
        auto row=[&](int n,std::string title,int action,bool enabled=true,bool chosen=false,std::string value=""){
            add(848,176+n*76,376,64,std::move(title),action,enabled,chosen,std::move(value),Kind::Menu);
        };
        auto info=[&](int n,const std::string &name,const std::string &value){
            aligned(r,f.small,name,{848,176+n*62,210,50},label);
            aligned(r,f.small,value,{1064,176+n*62,160,50},ink);
            rect(r,848,226+n*62,376,1,SDL_Color{63,67,74,255});
        };

        const bool drawer=page!=Page::Home && page!=Page::Devices && page!=Page::Gamepad &&
            page!=Page::Schemes && page!=Page::Guide && page!=Page::Login && page!=Page::Keyboard && page!=Page::About && page!=Page::More && page!=Page::Network && page!=Page::Logout && page!=Page::StreamPreview;
        if(!snapshot.streaming || !drawer)backdrop(r);
        if(drawer) {

            rect(r,0,0,1280,720,{0,0,0,216});rounded(r,{824,32,424,624},panel,13);
        }
        std::string title;
        switch(page) {
        case Page::Home:
            brand(r,f);
            add(736,48,176,48,"切换设备",10);add(928,48,128,48,"刷新",90);add(1072,48,144,48,"账号",11);
            rounded(r,{65,144,74,31},SDL_Color{40,62,52,255},15);
            aligned(r,f.small,snapshot.devices.empty()?"待连接":snapshot.devices[selectedDevice].value("status",std::string{})=="offline"?"离线":"在线",{65,144,74,31},SDL_Color{150,213,172,255},true);
            text(r,f.normal,device,158,142,ink,850);
            add(65,202,650,356,"进入桌面",12,!snapshot.devices.empty() && snapshot.stage=="idle" && snapshot.devices[selectedDevice].value("status",std::string{})!="offline",false,"",Kind::Tile);

            aligned(r,f.normal,"你的电脑，随时连接",{775,280,420,56},ink);
            aligned(r,f.small,"连接后显示可用应用",{775,348,420,48},label);
            break;
        case Page::StreamPreview:
            centered(r,f.normal,"串流画面预览",286);
            centered(r,f.small,"正在连接电脑",340,label);
            add(1030,48,184,52,"工具栏",14);
            break;
        case Page::Devices:
            title="我的电脑";
            for(size_t i=size_t(deviceOffset);i<std::min<size_t>(snapshot.devices.size(),size_t(deviceOffset+5));++i) {
                const auto &d=snapshot.devices[i];const bool online=d.value("status",std::string{})!="offline";
                add(80,160+(int(i)-deviceOffset)*85,1090,72,d.value("alias",std::string("电脑"))+(online?" · 在线":" · 离线"),1000+int(i),online);
            }
            if(snapshot.devices.empty())text(r,f.normal,"暂未发现电脑，请返回原界面登录或刷新",100,210,label);
            break;
        case Page::Toolbar:
            add(848,56,112,88,"退出",20,true,false,"",Kind::Shortcut);
            add(976,56,112,88,"键盘",55,true,false,"",Kind::Shortcut);
            add(1104,56,112,88,sound?"声音":"已静音",48,true,false,"",Kind::Shortcut);
            rect(r,848,160,376,1,{73,78,85,255});
            row(0,"显示",21);row(1,"安全",22);row(2,"外设",23);row(3,"更多",24);row(4,"Windows 快捷操作",25);break;
        case Page::Display:title="显示";row(0,"画质与帧率",30,true,false,std::to_string(fps)+" FPS");row(1,"画面设置",31,true,false,std::to_string(resolution)+"p");break;
        case Page::Quality:title="画质与帧率";
            row(0,"60 帧",40,true,fps==60);row(1,"30 帧",41,true,fps==30);
            row(2,"画质档位",43,true,false,qualityNames[quality]);
            row(3,"播放缓冲",47,true,false,std::to_string(bufferMs)+" ms");break;
        case Page::Picture:title="画面设置";row(0,"分辨率",44,true,false,std::to_string(resolution)+"p");row(1,"视频编码",45,true,false,codec?"H.265":"H.264");row(2,"垂直同步",46,true,false,vsync?"开启":"关闭");break;
        case Page::Bitrate:break; // Reserved page ID; independent bitrate control removed.
        case Page::Resolution:title="分辨率";row(0,"960 × 540",400+540,true,resolution==540);row(1,"1280 × 720",400+720,true,resolution==720);row(2,"1920 × 1080",400+1080,true,resolution==1080);break;
        case Page::Codec:title="视频编码";row(0,"H.264",500,true,codec==0);row(1,"H.265",501,true,codec==1);break;
        case Page::QualityPreset:title="画质";{
            const char *rates[]={"--","2 Mbps","8 Mbps","14 Mbps","29 Mbps"};
            for(int i=0;i<5;++i)row(i,qualityNames[i],510+i,true,quality==i,rates[i]);
            aligned(r,f.small,"带宽为参考值，实际随网络变化",{848,562,376,48},label);
            }break;
        case Page::Playout:title="播放缓冲";{int values[]={0,8,16,24,35};for(int i=0;i<5;++i)row(i,std::to_string(values[i])+" ms",550+values[i],true,bufferMs==values[i]);}break;
        case Page::Peripherals:title="外设";row(0,"手柄设备",13);row(1,"键盘输入",55);row(2,"触屏操作",54,true,false,nativeMultitouch?"多点触控":touchMode?"触摸板":"直接指向");row(3,"鼠标设置",59);break;
        case Page::Gamepad:
            title="手柄设备";text(r,f.normal,"当前手柄",90,190,ink);
            rounded(r,{85,245,510,132},panel);image(r,"attachment",{108,272,32,32});
            text(r,f.normal,"掌机内置手柄",160,264,ink);
            text(r,f.small,inputMode?"手柄直连模式":"键鼠映射模式",160,316,label);
            add(680,230,490,72,"输入模式",53,true,false,inputMode?"手柄直连":"键鼠映射",Kind::Menu);
            add(680,322,490,72,"键鼠映射方案",50,true,false,scheme==1?"方案一":"方案二",Kind::Menu);
            add(680,414,490,72,"操作说明",51,true,false,"",Kind::Menu);
            add(680,506,490,72,"Xbox 按键布局",613,true,false,xboxLayout?"按字母":"按位置",Kind::Menu);break;
        case Page::Schemes: {
            title="键鼠映射方案";add(445,111,165,49,"方案一",60,true,draft==1,"",Kind::Tab);add(650,111,165,49,"方案二",61,true,draft==2,"",Kind::Tab);
            const bool second=draft==2;
            const char *head[]={"左摇杆","方向键","右摇杆","正面按键"};
            const char *one[]={"移动鼠标\n按下：显示键盘","上 Ctrl\n下 Shift\n左 Backspace\n右 Delete","方向键\n按下：显示桌面","A 回车\nB Esc\nX 鼠标左键\nY 鼠标右键"};
            const char *two[]={"上下：滚轮\n按下：显示键盘","上 / 下 / 左 / 右","移动鼠标\n按下：Shift","A 回车\nB Esc\nX 空格\nY Backspace"};
            image(r,"gamepad",{492,190,296,212},420,300);
            text(r,f.small,second?"ZL  鼠标右键":"ZL  滚轮向上",100,222,label,320);
            text(r,f.small,second?"L  Ctrl     -  Tab":"L  Alt     -  Windows",100,269,label,340);
            text(r,f.small,second?"ZR  鼠标左键":"ZR  滚轮向下",885,222,label,300);
            text(r,f.small,second?"R  Alt     +  工具栏":"R  Tab     +  工具栏",885,269,label,300);
            for(int i=0;i<4;++i) {
                text(r,f.normal,head[i],90+i*300,408,{149,191,255,255});
                std::string lines=second?two[i]:one[i];size_t start=0;int line=0;
                do{const auto end=lines.find('\n',start);text(r,f.small,lines.substr(start,end-start),90+i*300,452+line++*27,ink,275);
                    if(end==std::string::npos)break;
                    start=end+1;
                }while(start<lines.size());
            }
            add(440,584,400,48,draft==scheme?"当前方案":"使用此方案",62,draft!=scheme);
            break;}

        case Page::XboxLayout:title="Xbox 按键布局";
            row(0,"按位置",602,true,xboxLayout==0);row(1,"按字母",603,true,xboxLayout==1);
            aligned(r,f.small,xboxLayout?"Switch A/B/X/Y 对应 Xbox 同名字母":"Switch B/A/Y/X 对应 Xbox A/B/X/Y",{848,365,376,100},label);
            aligned(r,f.small,"仅影响手柄直连；菜单仍 A 确认、B 返回",{848,480,376,100},label);break;
        case Page::InputMode:title="输入模式";row(0,"键鼠映射",600,true,inputMode==0);row(1,"手柄直连（实验）",601,true,inputMode==1);break;
        case Page::Touch:title="触屏操作";
            row(0,"平板多点触控",614,true,nativeMultitouch);
            row(1,"鼠标：直接指向",610,true,!nativeMultitouch && touchMode==0);
            row(2,"鼠标：触摸板",611,true,!nativeMultitouch && touchMode==1);
            row(3,"重置画面缩放",612);
            aligned(r,f.small,nativeMultitouch?"全部输入模式共用真实触点":"单指鼠标 · 双指滚动 / 缩放",{848,495,376,45},label);
            aligned(r,f.small,nativeMultitouch?"缩放与手势由电脑应用处理":"双指点按右键 · 长按拖动",{848,544,376,45},label);break;
        case Page::Mouse:title="鼠标速度";{const char *names[]={"慢速","标准","快速","很快"};for(int i=0;i<4;++i)row(i,names[i],620+i,true,mouseSpeed==i);}break;
        case Page::Audio:title="声音";row(0,"播放声音",630,true,false,sound?"开启":"关闭");{int values[]={25,50,75,100};for(int i=0;i<4;++i)row(i+1,std::to_string(values[i])+"%",640+values[i],true,volume==values[i]);}break;
        case Page::Guide:title="操作说明";
            rounded(r,{90,160,1100,420},panel,12);
            {const char *left[]={"本地界面","键鼠映射","手柄直连","触屏操作","切换模式"};
             const char *right[]={"A 确认 / B 返回，也可直接点选","用摇杆、按键控制桌面与键盘","实验功能，需电脑端识别虚拟手柄","直接定位或滑动指针，可在外设中选择","长按 − 切换输入；键鼠 + / 手柄长按 + 菜单"};
             for(int i=0;i<5;++i){aligned(r,f.normal,left[i],{120,185+i*72,200,56},ink);aligned(r,f.small,right[i],{355,185+i*72,800,56},label);}}break;
        case Page::More:
        case Page::Network:title=page==Page::More?"更多 · 网络信息":"网络信息";{
            aligned(r,f.normal,"网络延迟  "+(stats.rttMs>=0?std::to_string(stats.rttMs)+" ms":"--"),{90,130,700,60},ink);
            const char *names[]={"解码耗时","显示帧率","视频码率","初传缺口","连接时间","连接通道","被控端地址","控制端地址","客户端版本","客户端系统"};
            const std::string values[]={formatMillis(stats.decodeMs),number(stats.presentFps)+" FPS",formatMbps(stats.video.bitrateMbps),formatPercent(stats.video.lossPercent),
                snapshot.streaming?std::to_string((SDL_GetTicks()-connectedAt)/1000)+" 秒":"--",stats.transport,stats.remoteEndpoint,stats.localEndpoint,UU_VERSION,"Nintendo Switch"};
            for(int i=0;i<10;++i){
                const int col=i/5,line=i%5,x=90+col*540,y=220+line*53;
                aligned(r,f.small,names[i],{x,y,200,42},label);
                aligned(r,f.small,values[i],{x+220,y,250,42},ink);
            }
            rect(r,90,502,1100,1,{63,67,74,255});
            const char *codecNames[]={"视频编码","编码器信息","解码器"};const std::string codecValues[]={stats.codec,stats.encoder,stats.decoder};
            for(int i=0;i<3;++i){const int x=90+i*370;aligned(r,f.small,codecNames[i],{x,525,190,42},label);aligned(r,f.small,codecValues[i],{x+160,525,200,42},ink);}
            add(875,130,315,60,"常驻网络信息",754,true,networkOverlay,"",Kind::Toggle);
            add(930,579,260,48,"详细运行统计",56);
            }break;
        case Page::Stats:title="运行统计";
            info(0,"解码 / 显示 FPS",number(stats.decodeFps)+" / "+number(stats.presentFps));
            info(1,"解码耗时",formatMillis(stats.decodeMs));info(2,"初传缺口",formatPercent(stats.video.lossPercent));
            info(3,"NACK / RTX",std::to_string(stats.nackSent)+" / "+std::to_string(stats.rtxRecovered));
            info(4,"解码错误 / 丢帧",std::to_string(stats.decodeErrors)+" / "+std::to_string(stats.droppedFrames));break;
        case Page::Safety:title="安全";
            add(848,176,376,72,"自动解锁被控端",750,true,autoUnlock,"",Kind::Toggle);
            add(848,260,376,72,"被控端自动锁屏",751,true,autoLock,"",Kind::Toggle);
            add(848,344,376,72,"被控端静音运行",752,true,remoteMute,"",Kind::Toggle);
            add(848,428,376,100,"远程结束后，恢复\n被控端声音设置",753,true,restoreSound,"",Kind::Toggle);
            aligned(r,f.small,"自动解锁待支持，其余显示请求状态",{848,552,376,55},label);break;
        case Page::Windows:title="Windows 快捷操作";
            row(0,"锁定桌面",74);row(1,"重启电脑",75);row(2,"关机",76);
            row(3,"任务管理器",72);row(4,"显示桌面",71);row(5,"切换窗口",73);break;
        case Page::WindowsConfirm:title=pendingShortcut;
            aligned(r,f.small,pendingShortcut=="重启电脑"?"确认重启当前连接的电脑？":pendingShortcut=="关机"?"确认关闭当前连接的电脑？":"确认在已连接电脑上执行？",{848,180,376,72},label);row(2,"取消",80);row(3,"确认操作",760);break;
        case Page::AppLaunch:title=pendingShortcut;
            aligned(r,f.small,"示例应用入口",{848,180,376,64},ink);
            aligned(r,f.small,"连接电脑后可读取真实应用列表",{848,258,376,96},label);row(3,"返回",80);break;
        case Page::Account:
            title="账号";
            rounded(r,{1000,166,72,72},blue,36);image(r,"account",{1020,186,32,32});
            aligned(r,f.normal,snapshot.nickname.empty()?"已登录账号":snapshot.nickname,{848,254,376,48},ink,true);
            aligned(r,f.small,"与电脑端使用同一账号",{848,303,376,40},label,true);
            add(848,374,376,64,"重新登录",52,true,false,"",Kind::Menu);
            add(848,450,376,64,"关于",57,true,false,"",Kind::Menu);
            add(848,526,376,64,"退出账号",58,true,false,"",Kind::Menu);break;
        case Page::Login:
            centered(r,f.normal,"手机扫码登录 uu远程",116);
            centered(r,f.small,"登录与电脑端相同的 UU远程账号",162,label);
            rounded(r,{506,227,268,268},panel,12);
            rounded(r,{520,241,240,240},{238,240,243,255},6);
            image(r,"device",{616,300,48,48});
            aligned(r,f.small,"二维码占位",{535,373,210,42},{70,76,85,255},true);
            centered(r,f.small,"二维码由实际登录流程生成",507,label);
            centered(r,f.small,"请使用手机 App 扫码并确认",548,label);break;
        case Page::Logout:
            centered(r,f.large,"是否确定退出登录？",260);
            centered(r,f.small,"退出后需要重新扫码登录",332,label);
            add(330,438,290,72,"退出",770);add(660,438,290,72,"取消",80);break;
        case Page::About:title="关于";
            aligned(r,f.large,"uu远程",{120,177,550,72},ink);
            aligned(r,f.normal,"Switch 远程桌面",{120,265,550,56},ink);
            aligned(r,f.small,std::string("版本 ")+UU_VERSION,{120,333,600,48},label);
            aligned(r,f.small,"鼠标、触屏与手柄均可操作",{120,392,600,48},label);
            rounded(r,{760,180,420,320},panel,12);
            aligned(r,f.normal,"当前版本",{790,202,350,48},ink);
            aligned(r,f.small,"保持现有传输与硬件解码路径",{790,267,350,50},label);
            aligned(r,f.small,"显示组件：SDL2 / SDL_ttf",{790,329,350,50},label);
            aligned(r,f.small,"SDL 组件使用对应开源许可",{790,391,350,50},label);
            add(120,506,300,60,"重新登录",52);break;
        case Page::Keyboard:title="键盘输入";
            rounded(r,{90,145,1100,78},panel,10);aligned(r,f.normal,keyboardText.empty()?"点选按键，或使用电脑键盘输入":keyboardText,{115,155,1050,56},keyboardText.empty()?label:ink);
            {const std::string lines[]={"1234567890","QWERTYUIOP","ASDFGHJKL","ZXCVBNM"};
             for(int n=0;n<4;++n){const int x=(1280-int(lines[n].size())*90)/2;for(size_t i=0;i<lines[n].size();++i)add(x+int(i)*90,246+n*66,78,54,std::string(1,lines[n][i]),2000+lines[n][i]);}}
            add(190,510,168,56,"清空",2302);add(374,510,420,56,"空格",2301);add(810,510,168,56,"删除",2300);
            add(994,510,196,56,"发送文字",2303);break;
        case Page::Disconnect:title="退出远控？";aligned(r,f.small,"退出后返回电脑首页",{848,180,376,72},label);row(2,"继续远控",80);row(3,"返回首页",81);break;
        }
        if(!title.empty()) {

            aligned(r,f.normal,title,{drawer?904:124,40,drawer?320:1000,64},ink);
            if(drawer)rect(r,848,136,376,1,{73,78,85,255});
        }
        if(page==Page::Toolbar)add(848,584,376,56,"返回",99);
        else if(page!=Page::Home)add(drawer?840:59,48,48,48,"",99,true,false,"",Kind::Icon);
        for(auto &button:buttons) {
            const int a=button.action;
            const bool pendingSetting=snapshot.qualityPending && (a==40||a==41||a==46||(a>=510&&a<=514)||(page==Page::Resolution&&a!=99)||(page==Page::Codec&&a!=99)||(page==Page::Playout&&a!=99));
            const bool unsupported=a==750;
            const bool needsStream=(a>=71&&a<=76)||a==2303||(a>=751&&a<=753);
            if(pendingSetting || unsupported || (needsStream&&!snapshot.streaming))button.enabled=false;
            if(unsupported && button.kind==Kind::Menu)button.value="暂未支持";
        }
        deviceOffset=std::clamp(deviceOffset,0,std::max(0,int(snapshot.devices.size())-5));
        if(frame.focus<0 || frame.focus>=int(buttons.size()))frame.focus=0;
        for(size_t i=0;i<buttons.size();++i) {
            const auto &b=buttons[i];const bool focused=int(i)==frame.focus;
            const auto fill=!b.enabled?SDL_Color{36,36,41,255}:(touchDown && touchTarget==int(i))?SDL_Color{96,151,255,255}:focused?blue:b.chosen?SDL_Color{71,72,79,255}:panel;
            rounded(r,b.box,fill,10);const char *asset=buttonIcon(b.action,b.title);
            const auto tint=b.enabled?(focused?ink:label):SDL_Color{118,121,128,255};
            if(b.kind==Kind::Tile) {
                for(int n=0;n<12;++n)rect(r,b.box.x+5,b.box.y+5+n*24,b.box.w-10,24,{Uint8(33-n),Uint8(57-n),Uint8(89-n),255});
                image(r,"device",{b.box.x+277,b.box.y+100,96,96});
                aligned(r,f.normal,"进入桌面",{b.box.x+28,b.box.y+b.box.h-68,b.box.w-100,34},ink);
                aligned(r,f.small,"远程访问你的电脑",{b.box.x+28,b.box.y+b.box.h-34,b.box.w-100,30},label);
                image(r,"arrow",{b.box.x+b.box.w-54,b.box.y+b.box.h-55,28,28});
            }else if(b.kind==Kind::Shortcut) {
                if(asset)image(r,asset,{b.box.x+(b.box.w-30)/2,b.box.y+13,30,30});
                aligned(r,f.small,b.title,{b.box.x+8,b.box.y+51,b.box.w-16,30},tint,true);
            }else if(b.kind==Kind::Toggle) {
                const int sx=b.box.x+b.box.w-68,sy=b.box.y+(b.box.h-28)/2;
                rounded(r,{sx,sy,48,28},b.chosen?SDL_Color{41,185,184,255}:SDL_Color{99,104,113,255},14);
                rounded(r,{sx+(b.chosen?23:3),sy+3,22,22},ink,11);
                const auto split=b.title.find('\n');
                if(split==std::string::npos)aligned(r,f.small,b.title,{b.box.x+20,b.box.y,b.box.w-100,b.box.h},tint);
                else {aligned(r,f.small,b.title.substr(0,split),{b.box.x+20,b.box.y+17,b.box.w-100,32},tint);
                    aligned(r,f.small,b.title.substr(split+1),{b.box.x+20,b.box.y+51,b.box.w-100,32},tint);}
            }else if(b.kind==Kind::Icon) {
                if(asset)image(r,asset,{b.box.x+(b.box.w-24)/2,b.box.y+(b.box.h-24)/2,24,24});
            }else if(b.kind==Kind::Button || b.kind==Kind::Tab) {
                int tw=0,th=0;const auto display=fit(f.small,b.title,b.box.w-(asset?62:32));TTF_SizeUTF8(f.small,display.c_str(),&tw,&th);
                const int total=tw+(asset?36:0),x=b.box.x+(b.box.w-total)/2;
                if(asset)image(r,asset,{x,b.box.y+(b.box.h-24)/2,24,24});
                aligned(r,f.small,display,{x+(asset?36:0),b.box.y,tw,b.box.h},tint);
            }else {
                const bool arrow=opensPage(b.action);const int left=asset?64:20;
                const bool radio=page==Page::Quality?(b.action==40 || b.action==41):
                    page==Page::Bitrate || page==Page::Resolution || page==Page::Codec || page==Page::QualityPreset || page==Page::Playout || page==Page::InputMode || page==Page::XboxLayout || (page==Page::Touch && b.action!=612) || page==Page::Mouse || (page==Page::Audio && b.action!=630);
                int right=(arrow||radio)?48:20,vw=0,vh=0;TTF_SizeUTF8(f.small,b.value.c_str(),&vw,&vh);vw=std::min(vw,140);
                if(asset)image(r,asset,{b.box.x+20,b.box.y+(b.box.h-26)/2,26,26});
                if(vw)aligned(r,f.small,b.value,{b.box.x+b.box.w-right-vw,b.box.y,vw,b.box.h},tint);
                aligned(r,f.small,b.title,{b.box.x+left,b.box.y,b.box.w-left-right-(vw?vw+16:0),b.box.h},tint);
                if(arrow)image(r,"arrow",{b.box.x+b.box.w-36,b.box.y+(b.box.h-22)/2,22,22});
                if(radio)image(r,b.chosen?"radio":"radio_empty",{b.box.x+b.box.w-38,b.box.y+(b.box.h-26)/2,26,26});
            }
        }

        if(notice.empty())aligned(r,f.small,snapshot.qualityNotice.empty()?snapshot.detail:snapshot.qualityNotice,{65,638,710,32},label);
        else aligned(r,f.small,notice,{65,638,710,32},SDL_Color{149,191,255,255});
        rect(r,0,672,1280,48,SDL_Color{23,24,27,255});aligned(r,f.small,"A 确定   B 返回   方向键选择   也可直接点选",{65,678,900,36},label);

    }
};
} // namespace tvui
