#pragma once
#include "interface.h"

namespace player {
// Four face buttons from ref, a separate lock slider, and the two volume keys.
enum class BodyButton { Up, Back, Lock, Play, Down, VolumeDown, VolumeUp, Count };
inline constexpr size_t bodyButtonCount=size_t(BodyButton::Count);
inline void pressBodyButton(Interface& ui,BodyButton button) {
    static constexpr Key keys[]={Key::Up,Key::Back,Key::Lock,Key::Select,Key::Down,Key::VolumeDown,Key::VolumeUp};
    auto index=size_t(button);if(index>=bodyButtonCount)return;
    if((button==BodyButton::Up||button==BodyButton::Down)&&ui.page()==Page::Playing)ui.pressTransport(button==BodyButton::Up?-1:1);
    else ui.input(keys[index]);
}
inline void releaseBodyButton(Interface& ui,BodyButton button) {
    if(button==BodyButton::Up||button==BodyButton::Down)ui.releaseTransport();
}
}
