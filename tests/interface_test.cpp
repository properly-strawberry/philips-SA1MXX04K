#include "interface.h"
#include "radio_config.h"
#include "equalizer.h"
#include "assets.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <cmath>

using namespace player;
struct Fake:Backend {
    TrackInfo state;Recording mic;int vol=70,plays=0,saves=0,discards=0,scans=0;double seekPosition=-1;std::wstring source;std::array<double,5> gains{};
    TrackInfo audio()override{return state;}
    void play(const std::wstring& s)override{source=s;plays++;state={};state.playing=true;state.duration=s.find(L"http")==0?0:180;}
    void pause()override{state.paused=!state.paused;}
    void stop()override{state={};}
    void seek(double p)override{seekPosition=p;state.position=p;}
    void volume(int p)override{vol=p;}
    void equalizer(const std::array<double,5>& g)override{gains=g;}
    Recording recording()override{return mic;}
    void recordStart(const fs::path& dir)override{mic={};mic.active=true;mic.path=dir/"VOICE.wav";}
    void recordPause(bool p)override{mic.paused=p;}
    void recordFinish(bool keep)override{mic.active=false;keep?saves++:discards++;}
    void rescan()override{scans++;}
};
static void dump(Interface& ui,const fs::path& path) {
    ui.render();std::ofstream out(path,std::ios::binary);out<<"P6\n128 64\n255\n";
    for(auto p:ui.screen){out.put(char(p>>16));out.put(char(p>>8));out.put(char(p));}
}
// Pixel evidence: original payloads at coordinates recovered from ARM/Thumb.
static void requireAsset(const Interface& ui,unsigned id,int x,int y) {
    const auto* asset=std::find_if(std::begin(assets),std::end(assets),[id](const auto& a){return a.id==id;});assert(asset!=std::end(assets));
    for(unsigned row=0;row<asset->h;row++)for(unsigned col=0;col<asset->w;col++){
        auto p=assetPixels[asset->offset+row*asset->w+col];if(p==0x07e0)continue;
        auto rgb=0xff000000|(((p>>11)&31)*255/31<<16)|(((p>>5)&63)*255/63<<8)|((p&31)*255/31);
        assert(ui.screen[(y+row)*128+x+col]==rgb);
    }
}
template<size_t N>static void requireText(const Interface& ui,const Asset(&font)[N],const std::wstring& value,int x,int y) {
    auto glyph=[&](wchar_t c){auto* p=std::find_if(std::begin(font),std::end(font),[c](const auto& a){return a.id==unsigned(c);});assert(p!=std::end(font));return p;};
    if(x<0){int width=0;for(auto c:value)width+=glyph(c)->w;x=(128-width)/2;}
    for(auto c:value){auto* a=glyph(c);for(unsigned row=0;row<a->h;row++)for(unsigned col=0;col<a->w;col++){bool ink=fontBits[a->offset+row*((a->w+7)/8)+col/8]&(0x80>>(col%8));assert(ui.screen[(y+row)*128+x+col]==(ink?0xffffffffu:0xff000000u));}x+=a->w;}
}
int main(int argc,char** argv) {
    assert(argc==2);fs::path root=fs::absolute(argv[1]);fs::create_directories(root/"Music"/"Album");
    std::ofstream(root/"Music"/"one.wav").put(0);std::ofstream(root/"Music"/"Album"/"two.flac").put(0);
    Fake backend;Interface ui(backend,root/"Music");ui.tick(100);ui.skipBoot();
    {Fake b;Interface boot(b,root/"Music");boot.tick(100);assert(boot.booting());boot.tick(2300);assert(boot.booting());boot.tick(4501);assert(!boot.booting()&&boot.updating());dump(boot,root/"updating.ppm");requireText(boot,glyphs,L"Updating\u2026",-1,26);requireText(boot,glyphs,L"0%",-1,43);boot.input(Key::Select);assert(boot.page()==Page::Home);boot.click(60,40);assert(boot.page()==Page::Home);boot.tick(5925);dump(boot,root/"updating-50.ppm");requireText(boot,glyphs,L"50%",-1,43);assert(boot.screen[60*128+63]!=boot.screen[60*128+64]);boot.tick(7350);dump(boot,root/"updating-100.ppm");requireText(boot,glyphs,L"100%",-1,43);requireAsset(boot,7*256+24,5,60);boot.tick(7499);assert(boot.updating());boot.tick(7500);assert(!boot.updating());boot.input(Key::Select);assert(boot.page()==Page::Music);}
    {Fake b;Interface emptyRadio(b,root/"Music");emptyRadio.tick(100);emptyRadio.skipBoot();emptyRadio.input(Key::Up);assert(emptyRadio.selection()==5);emptyRadio.input(Key::Select);assert(emptyRadio.page()==Page::Radio);emptyRadio.tick(1000);assert(b.plays==0);emptyRadio.input(Key::Back);assert(emptyRadio.page()==Page::Home&&emptyRadio.selection()==5);emptyRadio.input(Key::Down);assert(emptyRadio.selection()==0);}
    {Fake b;Interface bye(b,root/"Music");bye.tick(100);bye.skipBoot();bye.shutdown();dump(bye,root/"shutdown-bye.ppm");auto first=bye.screen;bye.tick(1099);bye.render();assert(bye.screen==first&&!bye.shutdownFinished());for(int frame=0;frame<7;frame++){bye.tick(1100+frame*500);dump(bye,root/("shutdown-"+std::to_string(frame)+".ppm"));requireAsset(bye,8*256+frame,15,9);assert(!bye.shutdownFinished());}bye.tick(4599);assert(!bye.shutdownFinished());bye.tick(4600);bye.render();assert(bye.shutdownFinished()&&std::all_of(bye.screen.begin(),bye.screen.end(),[](auto p){return p==0xff000000;}));assert(b.plays==0);}
    Song a{root/"Music"/"one.wav",{}},b{root/"Music"/"Album"/"two.flac",{}};a.tags.title=L"First song";a.tags.artist=L"Artist A";a.tags.album=L"Album A";b.tags.title=L"Second song";b.tags.artist=L"Artist B";b.tags.album=L"Album B";
    ui.setLibrary({a,b});
    uint64_t clock=1000;
    for(int theme=0;theme<3;theme++)for(int icon=0;icon<6;icon++) {
        ui.settings.theme=theme;ui.input(Key::Home);for(int i=0;i<icon;i++)ui.input(Key::Down);ui.tick(clock+=1000);dump(ui,root/("home-"+std::to_string(theme)+"-"+std::to_string(icon)+".ppm"));if(icon<5)requireAsset(ui,6+theme,5,4+icon*11);else requireAsset(ui,theme,9,8);requireAsset(ui,1*256+4,110,2);
    }
    ui.input(Key::Home);ui.input(Key::Up);assert(ui.selection()==5);ui.setLibrary({a,b});assert(ui.selection()==5);ui.input(Key::Down);assert(ui.selection()==0);
    ui.settings.theme=0;ui.input(Key::Home);ui.input(Key::Select);assert(ui.page()==Page::Music);assert(ui.labels().size()==4);dump(ui,root/"music-menu.ppm");
    ui.input(Key::Select);assert(ui.page()==Page::Songs);ui.input(Key::Select);assert(ui.page()==Page::Playing&&backend.plays==1);dump(ui,root/"now-playing.ppm");requireText(ui,counterGlyphs,L"0001/0002",5,55);
    ui.input(Key::Select);assert(backend.state.paused);ui.input(Key::Select);assert(!backend.state.paused);
    ui.input(Key::SeekForward);assert(backend.seekPosition==5);ui.input(Key::Next);assert(ui.queueIndex()==1);
    {int plays=backend.plays;ui.pressTransport(1);ui.tick(clock+=100);assert(backend.plays==plays);ui.releaseTransport();assert(backend.plays==plays+1);plays=backend.plays;
     ui.pressTransport(1);ui.tick(clock+=500);assert(backend.seekPosition==4);ui.render();requireAsset(ui,2*256+3,5,35);ui.tick(clock+=250);assert(backend.seekPosition==8);ui.releaseTransport();assert(backend.plays==plays);
     backend.state.position=179;ui.input(Key::SeekForward);assert(backend.seekPosition==179.95);backend.state.position=1;ui.input(Key::SeekBack);assert(backend.seekPosition==0);
     ui.click(64,48);assert(backend.seekPosition>89&&backend.seekPosition<91);ui.click(127,48);assert(backend.seekPosition==179.95);
     ui.render();requireAsset(ui,2*256+5,5,35);requireAsset(ui,1*256+4,107,51);
     ui.input(Key::Next);backend.state.finished=true;backend.state.playing=false;backend.state.position=180;ui.settings.repeat=0;ui.tick(clock+=1000);assert(ui.lastPlayed.filename()==L"two.flac"&&ui.resumePosition==0);
     ui.navigate(Page::Playing);backend.state.finished=false;backend.state.playing=true;backend.state.duration=0;backend.seekPosition=-1;ui.input(Key::SeekForward);ui.click(60,48);assert(backend.seekPosition==-1);backend.state.duration=180;}
    ui.input(Key::Menu);assert(ui.page()==Page::Options);auto options=ui.labels();assert(options.size()>=6);dump(ui,root/"options.ppm");
    for(int language=0;language<24;language++){ui.settings.language=language;ui.navigate(Page::Settings);assert(!ui.labels().empty());ui.render();}
    ui.settings.language=2;ui.settings.volumeLimit=35;ui.settings.volume=70;ui.applySettings();assert(backend.vol==35);ui.input(Key::VolumeUp);assert(backend.vol==35);
    ui.settings.equalizer=7;ui.settings.bands={3,-2,4,0,1};ui.applySettings();assert(backend.gains==ui.settings.bands);
    ui.navigate(Page::CustomEQ);ui.input(Key::Up);assert(ui.settings.bands[0]==4);ui.tick(clock+=1500);dump(ui,root/"equalizer.ppm");
    for(auto page:{Page::Settings,Page::Theme,Page::PlayMode,Page::Repeat,Page::Shuffle,Page::Sound,Page::Equalizer,Page::Display,Page::Backlight,Page::ScreenOff,Page::Rotation,Page::Language,Page::Information,Page::Legal,Page::Books,Page::Recordings,Page::USB}){ui.navigate(page);assert(!ui.labels().empty());ui.render();}
    ui.navigate(Page::Folders);assert(ui.labels().front()==L"Album");dump(ui,root/"folder-selected.ppm");requireAsset(ui,3*256+9,9,29);ui.input(Key::Next);dump(ui,root/"folder-unselected.ppm");requireAsset(ui,3*256+6,9,8);ui.input(Key::Previous);ui.input(Key::Select);assert(ui.labels().front()==L"two.flac");ui.input(Key::Back);assert(ui.labels().front()==L"Album");
    ui.navigate(Page::Settings);assert(ui.labels().size()==7);ui.navigate(Page::Display);assert(ui.labels().size()==3);ui.navigate(Page::Repeat);assert(ui.labels()==std::vector<std::wstring>({L"Repeat Off",L"Repeat 1",L"Repeat All"}));ui.navigate(Page::Shuffle);assert(ui.labels().size()==2);
    ui.navigate(Page::USBConnected);dump(ui,root/"usb-fully-charged.ppm");requireAsset(ui,0x10000+6,81,17);requireAsset(ui,0x10000+12,13,50);requireAsset(ui,0x10000+13,13,56);ui.input(Key::Down);dump(ui,root/"usb-charging.ppm");ui.input(Key::Down);dump(ui,root/"usb-transfer.ppm");
    ui.settings.backlight=10;ui.settings.screenOff=20;ui.tick(90000);ui.render();assert(std::all_of(ui.screen.begin(),ui.screen.end(),[](auto p){return p==0xff000000;}));ui.input(Key::Select);ui.render();assert(std::any_of(ui.screen.begin(),ui.screen.end(),[](auto p){return p!=0xff000000;}));ui.settings.backlight=ui.settings.screenOff=0;
    {std::ofstream out(root/"radio.json");out<<R"({"stations":[{"mhz":99.5,"name":"Test \u017D","url":"https://example.invalid/audio"},{"mhz":89.0,"name":"Other","url":"http://example.invalid/other"}]})";}
    std::wstring error;auto stations=readRadioConfig(root/"radio.json",error);assert(error.empty()&&stations.size()==2&&stations[0].tenths==890&&stations[1].name.back()==L'\u017D');
    ui.loadStations(root/"radio.json");ui.navigate(Page::Settings);assert(ui.labels().size()==7);ui.input(Key::Home);ui.input(Key::Up);assert(ui.selection()==5);ui.input(Key::Select);assert(ui.page()==Page::Radio);ui.tick(91000);assert(backend.source==L"http://example.invalid/other");int plays=backend.plays;dump(ui,root/"radio.ppm");ui.input(Key::Next);ui.tick(92000);assert(backend.plays==plays);ui.input(Key::SeekForward);ui.tick(93000);assert(backend.source==L"https://example.invalid/audio");
    for(auto invalid:{R"({"stations":[{"mhz":89.01,"name":"a","url":"https://example.invalid"}]})",R"({"stations":[{"mhz":89.0,"name":"a","url":"file:///secret"}]})",R"({"stations":[{"mhz":89.0,"name":"a","url":"https://a"},{"mhz":89.0,"name":"b","url":"https://b"}]})",R"({"stations":[],})",R"({"stations":[{"mhz":1e999,"name":"a","url":"https://a"}]})"}) {std::ofstream(root/"bad-radio.json")<<invalid;assert(readRadioConfig(root/"bad-radio.json",error).empty()&&!error.empty());}
    ui.input(Key::Home);ui.input(Key::Next);ui.input(Key::Select);assert(ui.page()==Page::Recordings);ui.input(Key::Select);assert(backend.mic.active&&backend.mic.path.parent_path()==root/"Recordings"/"Voice");dump(ui,root/"recording.ppm");ui.input(Key::Select);assert(backend.mic.paused);ui.input(Key::Back);assert(ui.page()==Page::Confirm);ui.input(Key::Select);assert(backend.saves==1&&backend.discards==0&&!backend.mic.active);
    ui.navigate(Page::Recordings);ui.input(Key::Select);ui.input(Key::Back);ui.input(Key::Next);ui.input(Key::Select);assert(backend.discards==1&&!backend.mic.active);
    ui.input(Key::Home);ui.input(Key::Lock);ui.input(Key::Select);assert(ui.page()==Page::Home);ui.input(Key::Lock);ui.input(Key::Select);assert(ui.page()==Page::Music);
    {Fake b;Interface locked(b,root/"Music");locked.tick(100);locked.skipBoot();locked.navigate(Page::Playing);locked.input(Key::Lock);locked.click(60,48);assert(b.seekPosition==-1);}
    {Fake b;b.state.playing=true;b.state.duration=1000;Interface held(b,root/"Music");held.tick(100);held.skipBoot();held.navigate(Page::Playing);held.pressTransport(1);for(int i=0;i<11;i++)held.tick(600+i*250);assert(b.seekPosition==46);held.releaseTransport();assert(b.plays==0);held.pressTransport(1);held.cancelTransport();held.releaseTransport();assert(b.plays==0);}
    Equalizer eq;for(int s=-32000;s<=32000;s+=1000)assert(eq.process(int16_t(s),0,100)==s);eq.configure({12,-12,12,-12,12});for(int i=0;i<48000;i++){auto sample=int16_t(32000*std::sin(i*.057));eq.process(sample,0,100);eq.process(sample,1,100);}eq.reset();assert(eq.process(0,0,100)==0&&eq.process(0,1,100)==0);
    // A large library must share queue storage rather than copying it per row.
    std::vector<Song> large;for(int i=0;i<5000;i++)large.push_back({root/"Music"/(std::to_string(i)+".mp3"),{}});
    ui.setLibrary(std::move(large));ui.navigate(Page::Songs);assert(ui.labels().size()==5000);ui.render();
    std::cout<<"PASS original assets and 18 home/theme screens; hidden sixth radio with five-dot rail; zero-padded counters; centered Updating and 0/50/100% bottom progress; original shutdown dot frames and duration; 24 languages, pause/seek/next; settings and EQ; sleep/wake; JSON validation and MHz tuning; microphone save/discard and sibling folders; lock; DSP bypass/stability\n";
}
