#pragma once
#include "interface.h"
#include <algorithm>
#include <cmath>

namespace player {
// Visual travel is continuous on the spring side; Lock remains a hard detent.
// Pointer coordinates are expressed along the handle's center-to-center travel.
class SliderMotion {
public:
    float position() const {return position_;}
    SliderPosition press(float pointer,bool onHandle) {
        dragging_=true;offset_=onHandle?position_-pointer:0;
        if(onHandle)return drag(pointer);
        state_=pointer<.5f?SliderPosition::Lock:SliderPosition::Power;
        target_=state_==SliderPosition::Lock?0:1;
        if(state_==SliderPosition::Lock)position_=0;
        return state_;
    }
    SliderPosition drag(float pointer) {
        if(!dragging_)return state_;
        float value=std::clamp(pointer+offset_,0.0f,1.0f);
        if(value<=.25f){state_=SliderPosition::Lock;position_=target_=0;}
        else if(value<=.5f){state_=SliderPosition::Unlock;position_=target_=.5f;}
        else {
            bool atStop=value>=.98f||(state_==SliderPosition::Power&&value>=.94f);
            state_=atStop?SliderPosition::Power:SliderPosition::Unlock;
            position_=target_=value;
        }
        return state_;
    }
    void release() {
        dragging_=false;
        if(state_!=SliderPosition::Lock){state_=SliderPosition::Unlock;target_=.5f;}
    }
    void tick(uint64_t milliseconds,SliderPosition state) {
        if(!dragging_) {
            float target=state==SliderPosition::Lock?0:state==SliderPosition::Power?1:.5f;
            if(state==SliderPosition::Lock)position_=0;
            else if(state_==SliderPosition::Lock)position_=.5f;
            state_=state;target_=target;
        }
        if(lastTick_) {
            float elapsed=float(std::min<uint64_t>(milliseconds-lastTick_,100))/1000;
            position_+=(target_-position_)*(1-std::exp(-elapsed/.045f));
            if(std::abs(target_-position_)<.0005f)position_=target_;
        }
        lastTick_=milliseconds;
    }
private:
    float position_=.5f,target_=.5f,offset_=0;
    bool dragging_=false;
    SliderPosition state_=SliderPosition::Unlock;
    uint64_t lastTick_=0;
};

// Four face buttons from ref, a separate lock slider, and the two volume keys.
enum class BodyButton { Up, Back, Lock, Play, Down, VolumeDown, VolumeUp, Count };
inline constexpr size_t bodyButtonCount=size_t(BodyButton::Count);
inline constexpr std::array<Key,bodyButtonCount> bodyButtonKeys={Key::Up,Key::Back,Key::Lock,Key::Select,Key::Down,Key::VolumeDown,Key::VolumeUp};
inline void pressBodyButton(Interface& ui,BodyButton button) {
    auto index=size_t(button);if(index>=bodyButtonCount)return;
    ui.pressKey(bodyButtonKeys[index]);
}
inline void releaseBodyButton(Interface& ui,BodyButton button) {
    auto index=size_t(button);if(index<bodyButtonCount)ui.releaseKey(bodyButtonKeys[index]);
}
}
