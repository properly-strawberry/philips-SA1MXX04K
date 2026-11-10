#include "interface.h"
#include "controls.h"
#include <algorithm>
#include <cassert>
#include <iostream>

using namespace player;

struct KeyBackend:Backend {
    TrackInfo state;
    Recording mic;
    int vol=70,plays=0,pauses=0,stops=0,saves=0,discards=0,recordPauses=0,scans=0;
    std::vector<int> volumes;
    std::vector<double> seeks;
    std::wstring source;
    TrackInfo audio()override{return state;}
    void play(const std::wstring& path)override{source=path;plays++;state={};state.playing=true;state.duration=3600;}
    void pause()override{state.paused=!state.paused;pauses++;}
    void stop()override{state={};stops++;}
    void seek(double seconds)override{state.position=seconds;seeks.push_back(seconds);}
    void volume(int value)override{vol=value;volumes.push_back(value);}
    void equalizer(const std::array<double,5>&)override{}
    Recording recording()override{return mic;}
    void recordStart(const fs::path& directory)override{mic={};mic.active=true;mic.path=directory/"VOICE.wav";}
    void recordPause(bool paused)override{mic.paused=paused;recordPauses++;}
    void recordFinish(bool keep)override{mic.active=false;keep?saves++:discards++;}
    void rescan()override{scans++;}
};

struct Fixture {
    KeyBackend backend;
    Interface ui;
    uint64_t now=100;
    explicit Fixture(const fs::path& root):ui(backend,root/"Music") {
        ui.tick(now);ui.skipBoot();
        std::vector<Song> songs;
        for(int i=0;i<5;i++) {
            Song song{root/"Music"/("track"+std::to_string(i)+".wav"),{}};
            song.tags.title=L"Track "+std::to_wstring(i);songs.push_back(song);
        }
        ui.setLibrary(std::move(songs));
    }
    void advance(uint64_t milliseconds){now+=milliseconds;ui.tick(now);}
    void tap(Key key){ui.pressKey(key);ui.releaseKey(key);}
    void hold(Key key){ui.pressKey(key);advance(Interface::KeyHoldDelay);}
    void play(){ui.navigate(Page::Songs);tap(Key::Select);assert(ui.page()==Page::Playing&&backend.plays==1);}
};

static void volumeTests(const fs::path& root) {
    Fixture f(root);f.ui.settings.volume=50;f.ui.applySettings();
    f.ui.pressKey(Key::VolumeUp);assert(f.ui.settings.volume==50);
    f.advance(300);f.ui.pressKey(Key::VolumeUp); // OS duplicate must not restart the delay.
    f.ui.releaseKey(Key::VolumeDown); // An unrelated key-up must not release the active key.
    f.advance(Interface::KeyHoldDelay-301);assert(f.ui.settings.volume==50);
    f.advance(1);assert(f.ui.settings.volume==58); // Long-start plus immediate first repeat: two levels.
    f.ui.pressKey(Key::VolumeUp);f.advance(Interface::VolumeRepeatDelay-1);assert(f.ui.settings.volume==58);
    f.advance(1);assert(f.ui.settings.volume==61);
    f.ui.releaseKey(Key::VolumeUp);f.advance(1000);assert(f.ui.settings.volume==61);
    f.tap(Key::VolumeDown);assert(f.ui.settings.volume==58);

    f.ui.settings.volumeLimit=35;f.ui.settings.volume=32;f.ui.applySettings();f.backend.volumes.clear();
    f.hold(Key::VolumeUp);assert(f.ui.settings.volume==35);
    for(int i=0;i<15;i++)f.advance(Interface::VolumeRepeatDelay);
    assert(f.ui.settings.volume==35&&std::all_of(f.backend.volumes.begin(),f.backend.volumes.end(),[](int value){return value<=35;}));
    f.ui.releaseKey(Key::VolumeUp);f.ui.settings.volume=0;f.ui.applySettings();
    f.hold(Key::VolumeDown);f.advance(Interface::VolumeRepeatDelay);f.ui.releaseKey(Key::VolumeDown);
    assert(f.ui.settings.volume==0);

    Fixture limit(root);limit.ui.settings.volume=50;limit.ui.applySettings();limit.ui.navigate(Page::VolumeLimit);
    int preview=limit.backend.vol;
    limit.hold(Key::Up);limit.advance(Interface::NavigationRepeatDelay);limit.ui.releaseKey(Key::Up);
    limit.hold(Key::Down);limit.advance(Interface::NavigationRepeatDelay);limit.ui.releaseKey(Key::Down);
    assert(limit.backend.vol==preview&&limit.ui.settings.volumeLimit==100);
    limit.hold(Key::VolumeUp);limit.ui.releaseKey(Key::VolumeUp);assert(limit.backend.vol>preview);
    limit.tap(Key::Back);assert(limit.backend.vol==50&&limit.ui.settings.volumeLimit==100);
}

static void navigationTests(const fs::path& root) {
    Fixture home(root);home.ui.pressKey(Key::Up);assert(home.ui.selection()==0);
    home.advance(Interface::KeyHoldDelay-1);assert(home.ui.selection()==0);
    home.advance(1);assert(home.ui.selection()==5);
    home.advance(Interface::NavigationRepeatDelay-1);assert(home.ui.selection()==5);
    home.advance(1);assert(home.ui.selection()==4);
    home.ui.releaseKey(Key::Up);home.advance(1000);assert(home.ui.selection()==4);
    home.tap(Key::Down);assert(home.ui.selection()==5);home.tap(Key::Down);assert(home.ui.selection()==0);

    Fixture list(root);list.ui.navigate(Page::Songs);list.hold(Key::Up);assert(list.ui.selection()==4);
    list.advance(Interface::NavigationRepeatDelay);assert(list.ui.selection()==3);
    list.ui.releaseKey(Key::Up);list.tap(Key::Down);assert(list.ui.selection()==4);
    list.tap(Key::Down);assert(list.ui.selection()==0);
    list.ui.pressKey(Key::Down);list.ui.navigate(Page::Settings);list.advance(Interface::KeyHoldDelay+1000);
    list.ui.releaseKey(Key::Down);assert(list.ui.page()==Page::Settings&&list.ui.selection()==0);
    list.tap(Key::Down);assert(list.ui.selection()==1);

    Fixture select(root);select.hold(Key::Select);assert(select.ui.page()==Page::Music);
    select.advance(3*Interface::NavigationRepeatDelay);select.ui.releaseKey(Key::Select);
    assert(select.ui.page()==Page::Music);select.tap(Key::Select);assert(select.ui.page()==Page::Songs);

    Fixture eq(root);eq.ui.navigate(Page::CustomEQ);eq.hold(Key::Select);
    eq.ui.releaseKey(Key::Select);assert(eq.ui.page()==Page::CustomEQ&&eq.ui.selection()==0);
    eq.tap(Key::Select);assert(eq.ui.selection()==1);
    eq.hold(Key::Up);assert(eq.ui.settings.bands[1]==2);
    for(int i=0;i<10;i++)eq.advance(Interface::NavigationRepeatDelay);
    eq.ui.releaseKey(Key::Up);assert(eq.ui.settings.bands[1]==10);
    eq.hold(Key::Down);assert(eq.ui.settings.bands[1]==8);
    for(int i=0;i<15;i++)eq.advance(Interface::NavigationRepeatDelay);
    eq.ui.releaseKey(Key::Down);assert(eq.ui.settings.bands[1]==-10);
}

static void backAndModalTests(const fs::path& root) {
    Fixture f(root);f.ui.navigate(Page::Music);f.ui.navigate(Page::Songs);
    f.tap(Key::Back);assert(f.ui.page()==Page::Music);
    f.ui.navigate(Page::Songs);f.hold(Key::Back);assert(f.ui.page()==Page::Home);
    f.advance(1000);f.ui.releaseKey(Key::Back);assert(f.ui.page()==Page::Home);

    Fixture recording(root);recording.ui.navigate(Page::Recordings);recording.tap(Key::Select);
    assert(recording.ui.page()==Page::Recording&&recording.backend.mic.active);
    recording.hold(Key::Back);assert(recording.ui.page()==Page::Confirm&&recording.backend.mic.paused);
    recording.advance(3*Interface::NavigationRepeatDelay);recording.ui.releaseKey(Key::Back);
    assert(recording.ui.page()==Page::Confirm&&recording.backend.mic.active);
    assert(recording.backend.recordPauses==1&&recording.backend.saves==0&&recording.backend.discards==0);
    recording.hold(Key::Back);assert(recording.ui.page()==Page::Recordings&&recording.backend.discards==1);
    recording.advance(1000);recording.ui.releaseKey(Key::Back);
    assert(recording.ui.page()==Page::Recordings&&recording.backend.discards==1);

    Fixture confirm(root);confirm.ui.navigate(Page::Settings);
    for(int i=0;i<6;i++)confirm.tap(Key::Down);
    confirm.tap(Key::Select);assert(confirm.ui.page()==Page::Confirm);
    int volume=confirm.backend.vol;
    confirm.hold(Key::VolumeUp);confirm.advance(Interface::VolumeRepeatDelay);confirm.ui.releaseKey(Key::VolumeUp);
    assert(confirm.ui.page()==Page::Confirm&&confirm.backend.vol==volume);
    confirm.hold(Key::Back);assert(confirm.ui.page()==Page::Settings);
    confirm.advance(1000);confirm.ui.releaseKey(Key::Back);assert(confirm.ui.page()==Page::Settings);
}

static void seekTests(const fs::path& root) {
    Fixture f(root);f.play();f.backend.state.position=1000;
    f.ui.pressKey(Key::Down);f.advance(Interface::KeyHoldDelay-1);
    assert(f.backend.plays==1&&f.backend.seeks.empty());
    f.advance(1);assert(f.backend.seeks.size()==1&&f.backend.state.position==1004&&f.ui.queueIndex()==0);
    for(int count=2;count<=40;count++) {
        double previous=f.backend.state.position;
        f.advance(Interface::NavigationRepeatDelay);
        double step=count<10?4:count<20?5:count<40?10:20;
        assert(f.backend.seeks.size()==size_t(count)&&f.backend.state.position==previous+step&&f.backend.plays==1);
    }
    f.ui.releaseKey(Key::Down);f.advance(1000);assert(f.backend.seeks.size()==40&&f.backend.plays==1);
    f.ui.pressKey(Key::Up);f.advance(100);f.ui.cancelKeys();f.ui.releaseKey(Key::Up);
    assert(f.backend.seeks.size()==40&&f.backend.plays==1);

    // Opposite-key ownership: releasing the old direction must neither skip a
    // track nor release/commit the newer gesture.
    f.ui.pressKey(Key::Up);f.advance(100);f.ui.pressKey(Key::Down);f.ui.releaseKey(Key::Up);
    f.advance(Interface::KeyHoldDelay-1);assert(f.backend.seeks.size()==40&&f.backend.plays==1);
    f.advance(1);assert(f.backend.seeks.size()==41&&f.backend.plays==1);
    f.ui.cancelKeys();f.ui.releaseKey(Key::Down);f.advance(1000);assert(f.backend.seeks.size()==41&&f.backend.plays==1);
    f.tap(Key::Down);assert(f.backend.plays==2&&f.ui.queueIndex()==1);

    Fixture noDuration(root);noDuration.play();noDuration.backend.state.duration=0;
    noDuration.hold(Key::Down);noDuration.advance(Interface::NavigationRepeatDelay);noDuration.ui.releaseKey(Key::Down);
    assert(noDuration.backend.seeks.empty()&&noDuration.backend.plays==1);
}

static void wakeLockAndCancelTests(const fs::path& root) {
    Fixture wake(root);wake.ui.navigate(Page::Songs);wake.ui.settings.screenOff=1;wake.advance(1001);
    wake.ui.pressKey(Key::Select);wake.ui.settings.screenOff=0;
    wake.advance(Interface::KeyHoldDelay+1000);wake.ui.releaseKey(Key::Select);
    assert(wake.ui.page()==Page::Songs&&wake.backend.plays==0);
    wake.tap(Key::Select);assert(wake.ui.page()==Page::Playing&&wake.backend.plays==1);

    Fixture lock(root);lock.ui.navigate(Page::Songs);lock.ui.moveSlider(SliderPosition::Lock);
    assert(lock.ui.locked&&lock.ui.sliderPosition()==SliderPosition::Lock);
    lock.ui.pressKey(Key::Down);lock.advance(Interface::KeyHoldDelay);
    assert(lock.ui.selection()==0);lock.ui.moveSlider(SliderPosition::Unlock);
    lock.advance(Interface::NavigationRepeatDelay);lock.ui.releaseKey(Key::Down);assert(lock.ui.selection()==0);
    lock.tap(Key::Down);assert(lock.ui.selection()==1);
    lock.ui.moveSlider(SliderPosition::Lock);lock.ui.cancelKeys();
    assert(lock.ui.locked&&lock.ui.sliderPosition()==SliderPosition::Lock);
    lock.ui.moveSlider(SliderPosition::Unlock);lock.ui.pressKey(Key::Select);lock.ui.cancelKeys();
    lock.ui.releaseKey(Key::Select);assert(lock.ui.page()==Page::Songs&&lock.backend.plays==0);
    lock.tap(Key::Select);assert(lock.ui.page()==Page::Playing&&lock.backend.plays==1);
}

static void sliderTests(const fs::path& root) {
    Fixture f(root);assert(!f.ui.locked&&f.ui.sliderPosition()==SliderPosition::Unlock);
    f.ui.moveSlider(SliderPosition::Lock);f.ui.moveSlider(SliderPosition::Power);
    assert(!f.ui.locked&&f.ui.sliderPosition()==SliderPosition::Power);
    f.advance(Interface::PowerHoldDelay-1);assert(!f.ui.takePowerOffRequest());
    f.ui.releaseSlider();assert(f.ui.sliderPosition()==SliderPosition::Unlock);
    f.advance(Interface::PowerHoldDelay+1);assert(!f.ui.takePowerOffRequest());

    f.ui.moveSlider(SliderPosition::Power);f.advance(Interface::PowerHoldDelay-1);
    f.ui.moveSlider(SliderPosition::Power); // A duplicate drag must not restart the timer.
    f.advance(1);assert(f.ui.takePowerOffRequest()&&!f.ui.takePowerOffRequest());
    f.advance(2*Interface::PowerHoldDelay);assert(!f.ui.takePowerOffRequest());
    f.ui.releaseSlider();assert(f.ui.sliderPosition()==SliderPosition::Unlock);
    f.ui.moveSlider(SliderPosition::Power);f.advance(100);f.ui.cancelKeys();
    assert(f.ui.sliderPosition()==SliderPosition::Unlock&&!f.ui.takePowerOffRequest());
    f.advance(Interface::PowerHoldDelay);assert(!f.ui.takePowerOffRequest());
    f.ui.moveSlider(SliderPosition::Power);f.advance(Interface::PowerHoldDelay);
    assert(f.ui.takePowerOffRequest());f.ui.releaseSlider();assert(!f.ui.shutdownFinished());

    // The shell saves an active recording when it consumes the power request.
    // Merely arming, cancelling or firing that request must not discard/finalize it.
    for(bool paused:{false,true}) {
        Fixture recording(root);recording.ui.navigate(Page::Recordings);recording.tap(Key::Select);
        if(paused)recording.tap(Key::Select);
        assert(recording.backend.mic.active&&recording.backend.mic.paused==paused);
        recording.ui.moveSlider(SliderPosition::Power);recording.advance(Interface::PowerHoldDelay-1);
        recording.ui.releaseSlider();assert(!recording.ui.takePowerOffRequest());
        assert(recording.backend.mic.active&&recording.backend.mic.paused==paused);
        recording.ui.moveSlider(SliderPosition::Power);recording.advance(Interface::PowerHoldDelay);
        assert(recording.ui.takePowerOffRequest()&&recording.backend.mic.active&&recording.backend.mic.paused==paused);
        assert(recording.backend.saves==0&&recording.backend.discards==0&&recording.ui.page()==Page::Recording);
    }
}

static void sliderTravelTests(const fs::path& root) {
    Fixture f(root);SliderMotion motion;motion.tick(f.now,f.ui.sliderPosition());
    f.ui.moveSlider(motion.press(.5f,true));
    f.ui.moveSlider(motion.drag(.75f));assert(motion.position()==.75f&&f.ui.sliderPosition()==SliderPosition::Unlock);
    f.advance(2*Interface::PowerHoldDelay);assert(!f.ui.takePowerOffRequest());
    f.ui.moveSlider(motion.drag(1));f.advance(100);
    f.ui.moveSlider(motion.drag(.95f));assert(f.ui.sliderPosition()==SliderPosition::Power);
    f.ui.moveSlider(motion.drag(.9f));assert(f.ui.sliderPosition()==SliderPosition::Unlock);
    f.advance(Interface::PowerHoldDelay);assert(!f.ui.takePowerOffRequest());
    motion.tick(f.now,f.ui.sliderPosition());motion.release();f.ui.releaseSlider();
    f.advance(16);motion.tick(f.now,f.ui.sliderPosition());
    assert(motion.position()>.5f&&motion.position()<.9f); // Spring return has intermediate frames.
    for(int i=0;i<30;i++){f.advance(16);motion.tick(f.now,f.ui.sliderPosition());}
    assert(motion.position()==.5f);
    f.ui.moveSlider(motion.press(-1,false));assert(motion.position()==0&&f.ui.locked);
    motion.release();f.ui.releaseSlider();f.advance(16);motion.tick(f.now,f.ui.sliderPosition());
    assert(motion.position()==0&&f.ui.locked); // Left stays latched, without animation.
    f.ui.moveSlider(SliderPosition::Unlock);f.advance(16);motion.tick(f.now,f.ui.sliderPosition());
    assert(motion.position()==.5f);
    f.ui.moveSlider(SliderPosition::Power);f.advance(16);motion.tick(f.now,f.ui.sliderPosition());
    assert(motion.position()>.5f&&motion.position()<1); // Keyboard power also travels smoothly.
    f.ui.cancelKeys();motion.release();
}

int main(int argc,char** argv) {
    assert(argc==2);auto root=fs::absolute(argv[1]);fs::create_directories(root/"Music");
    volumeTests(root);navigationTests(root);backAndModalTests(root);seekTests(root);
    wakeLockAndCancelTests(root);sliderTests(root);sliderTravelTests(root);
    std::cout<<"PASS timed short/long/repeat keys; volume and navigation bounds; modal/page ownership; seek acceleration/cancel/opposite releases; wake/lock suppression; three-position spring-return power slider and recording preservation\n";
}
