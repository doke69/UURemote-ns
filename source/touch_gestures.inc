// Mobile-style mouse gestures; no native Windows multitouch injection is claimed.
class StreamTouch {
    bool contact=false,blocked=false,drag=false,moved=false,multi=false;
    int fingers=0,id=-1,startX=0,startY=0,lastX=0,lastY=0,motion=0,twoKind=0;
    float twoX=0,twoY=0,twoDistance=0,scrollX=0,scrollY=0;
    Uint32 began=0,scrollAt=0;
    float zoom=1,originX=0,originY=0;
public:
    SDL_Rect view(const SDL_Rect &base)const {
        if(zoom<=1)return base;
        const int w=int(base.w*zoom),h=int(base.h*zoom);
        return {std::clamp(int(originX),base.x+base.w-w,base.x),std::clamp(int(originY),base.y+base.h-h,base.y),w,h};
    }
    void resetZoom(){zoom=1;originX=originY=0;}
    template<class Send> void cancel(Send send,bool wait=true){
        if(drag)send(uu::Json{{"action","mouse_release"},{"button",1}});
        contact=drag=moved=multi=false;fingers=0;twoKind=0;scrollX=scrollY=0;blocked=wait;id=-1;
    }
#ifdef __SWITCH__
    template<class Send> void update(const HidTouchScreenState &t,Uint32 now,bool captured,bool trackpad,float speed,
        const SDL_Rect &base,float &cursorX,float &cursorY,Send send){
        if(captured){cancel(send);return;}
        if(blocked){if(!t.count)blocked=false;return;}
        auto click=[&](int button){send(uu::Json{{"action","mouse_press"},{"button",button}});send(uu::Json{{"action","mouse_release"},{"button",button}});};
        auto move=[&](float x,float y){cursorX=std::clamp(x,0.f,1.f);cursorY=std::clamp(y,0.f,1.f);
            send(uu::Json{{"action","mouse_move_absolute"},{"abs_x",cursorX},{"abs_y",cursorY}});};
        if(!t.count){
            if(contact && !drag && !moved && now-began<=300)click(multi?2:1);
            cancel(send,false);return;
        }
        if(!contact){
            const auto p=t.touches[0];const auto v=view(base);
            if(int(p.x)<v.x || int(p.x)>=v.x+v.w || int(p.y)<v.y || int(p.y)>=v.y+v.h){blocked=true;return;}
            contact=true;began=now;startX=lastX=p.x;startY=lastY=p.y;id=p.finger_id;motion=0;
            if(!trackpad)move((int(p.x)-v.x)/float(v.w),(int(p.y)-v.y)/float(v.h));
        }
        if(t.count>=2){
            const auto a=t.touches[0],b=t.touches[1];
            const float cx=(a.x+b.x)*.5f,cy=(a.y+b.y)*.5f;
            const float distance=std::hypot(float(int(a.x)-int(b.x)),float(int(a.y)-int(b.y)));
            if(!multi || fingers<2){
                if(drag){send(uu::Json{{"action","mouse_release"},{"button",1}});drag=false;}
                multi=true;twoKind=0;twoX=cx;twoY=cy;twoDistance=distance;scrollX=scrollY=0;
            }else{
                if(!twoKind){
                    if(std::abs(distance-twoDistance)>12)twoKind=2;
                    else if(std::hypot(cx-twoX,cy-twoY)>8)twoKind=1;
                }
                if(twoKind==2 && twoDistance>1){
                    const auto old=view(base);const float px=(cx-old.x)/old.w,py=(cy-old.y)/old.h;
                    zoom=std::clamp(zoom*distance/twoDistance,1.f,3.f);
                    originX=cx-px*base.w*zoom;originY=cy-py*base.h*zoom;
                    moved=true;twoX=cx;twoY=cy;twoDistance=distance;
                }else if(twoKind==1){
                    scrollX+=(cx-twoX)*6;scrollY+=(cy-twoY)*6;twoX=cx;twoY=cy;
                    if(now-scrollAt>=32){const int dx=int(scrollX),dy=int(scrollY);
                        if(dx||dy)send(uu::Json{{"action","mouse_scroll"},{"delta_x",dx},{"delta_y",dy}});
                        scrollX-=dx;scrollY-=dy;scrollAt=now;}
                    moved=true;
                }
            }
            fingers=t.count;return;
        }
        // No stray click/drag when one finger remains after a two-finger gesture.
        if(multi){fingers=1;return;}
        const auto p=t.touches[0];
        if(int(p.finger_id)!=id){cancel(send);return;}
        const int dx=int(p.x)-lastX,dy=int(p.y)-lastY;
        motion=std::max(motion,std::max(std::abs(int(p.x)-startX),std::abs(int(p.y)-startY)));
        if(motion>10)moved=true;
        if(!drag && ((!moved && now-began>=500) || (!trackpad && moved))){
            send(uu::Json{{"action","mouse_press"},{"button",1}});drag=true;
        }
        if(dx||dy){const auto v=view(base);
            if(trackpad)move(cursorX+dx*speed/720.f,cursorY+dy*speed/405.f);
            else move((int(p.x)-v.x)/float(v.w),(int(p.y)-v.y)/float(v.h));}
        lastX=p.x;lastY=p.y;fingers=1;
    }
#endif
};
