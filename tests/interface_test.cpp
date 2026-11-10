#include "interface.h"
#include "radio_config.h"
#include "equalizer.h"
#include "assets.h"
#include "controls.h"
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
static void metadataTests(const fs::path& root) {
    auto init=[](Fake& b,Interface& v,int theme,const std::wstring& artist,const std::wstring& album){b.state.playing=true;b.state.duration=180;b.state.title=L"Title";b.state.artist=artist;b.state.album=album;v.settings.theme=theme;v.tick(100);v.skipBoot();v.navigate(Page::Playing);v.render();};
    for(int theme=0;theme<3;theme++) {
        Fake ba,bb,bs;Interface a(ba,root/"Music"),b(bb,root/"Music"),slide(bs,root/"Music");
        init(ba,a,theme,L"Artist A",L"");init(bb,b,theme,L"",L"Album A");init(bs,slide,theme,L"Artist A",L"Album A");
        const auto artist=a.screen,album=b.screen;
        auto expected=[&](int ay,int by){auto out=artist;auto bg=artist[0];for(int y=20;y<34;y++)for(int x=5;x<123;x++){auto& p=out[y*128+x];p=bg;for(int n=0;n<2;n++){int source=y-(n?by:ay);if(source>=20&&source<34){auto v=(n?album:artist)[source*128+x];if(v!=bg)p=v;}}}assert(slide.screen==out);};
        for(int frame=0;frame<=46;frame++) {
            slide.tick(100+frame*100);slide.render();
            if(frame==0||frame==9)expected(0,14);
            if(frame==16)expected(-6,8);
            if(frame==23)expected(14,1);
            if(frame==39)expected(8,-5);
            if(frame==46)expected(1,14);
            if(theme==0)dump(slide,root/("metadata-"+std::to_string(frame)+".ppm"));
        }
        a.tick(10000);a.render();assert(a.screen==artist);b.tick(10000);b.render();assert(b.screen==album);
        bs.state.artist=L"";slide.render();assert(slide.screen==album);
        auto repeat=slide.screen;slide.render();assert(slide.screen==repeat);
    }
    Fake bl;Interface longName(bl,root/"Music");init(bl,longName,0,L"A very long artist name that must finish scrolling before the album slides in",L"Album A");auto initial=longName.screen;
    longName.tick(1700);longName.render();requireAsset(longName,2*256+21,5,22);
    for(int y=20;y<34;y++)for(int x=17;x<117;x++)assert(longName.screen[y*128+x]==initial[y*128+x+6]);
    longName.tick(6000);longName.render();requireAsset(longName,2*256+21,5,22);
}
static void titleTests(const fs::path& root) {
    const std::wstring label=L"A title long enough to cross the whole screen";
    int width=0,prefix=0;for(auto ch:label){auto* g=std::find_if(std::begin(largeGlyphs),std::end(largeGlyphs),[ch](const auto& a){return a.id==unsigned(ch);});assert(g!=std::end(largeGlyphs));width+=g->w;if(prefix==width-int(g->w)&&width<=106)prefix=width;}assert(width>118);
    auto row=[](const auto& screen,int top,int bottom){return std::vector<uint32_t>(screen.begin()+top*128,screen.begin()+bottom*128);};
    for(int theme=0;theme<3;theme++) {
        Fake b;b.state.title=label;b.state.artist=L"Artist A";b.state.album=L"Album A";b.state.playing=true;b.state.duration=180;
        Interface v(b,root/"Music");v.settings.theme=theme;v.tick(100);v.skipBoot();v.navigate(Page::Playing);dump(v,root/("title-hold-"+std::to_string(theme)+".ppm"));auto initial=v.screen;
        uint64_t done=100+1000+uint64_t(width+1)*100;
        v.tick(1099);v.render();assert(v.screen==initial);
        v.tick(1800);v.render();assert(row(v.screen,3,17)!=row(initial,3,17));assert(row(v.screen,20,34)==row(initial,20,34));
        v.tick(done-1);v.render();assert(row(v.screen,20,34)==row(initial,20,34));
        v.tick(done);dump(v,root/("title-done-"+std::to_string(theme)+".ppm"));requireAsset(v,3*256+21+theme,5+prefix,3);auto ended=row(v.screen,3,17);assert(row(v.screen,20,34)==row(initial,20,34));
        v.tick(done+900);v.render();assert(row(v.screen,20,34)==row(initial,20,34));v.tick(done+1600);dump(v,root/("title-metadata-"+std::to_string(theme)+".ppm"));assert(row(v.screen,20,34)!=row(initial,20,34));assert(row(v.screen,3,17)==ended);
        v.tick(done+60000);v.render();assert(row(v.screen,3,17)==ended);auto repeated=v.screen;v.render();assert(v.screen==repeated);
        b.state.title=L"Changed";v.render();assert(row(v.screen,3,17)!=ended&&row(v.screen,20,34)==row(initial,20,34));
        v.tick(done+61600);v.render();assert(row(v.screen,20,34)!=row(initial,20,34));
        b.state.title=label;v.render();assert(row(v.screen,3,17)==row(initial,3,17)&&row(v.screen,20,34)==row(initial,20,34));
    }
}
static void dialogTests(const fs::path& root) {
    Fake b;Interface v(b,root/"Music");v.tick(100);v.skipBoot();v.input(Key::Menu);assert(v.page()==Page::Home);
    auto path=root/"Music"/"dialog-song.wav",neighbor=root/"Music"/"dialog-neighbor.wav";std::ofstream(path)<<"fixture";std::ofstream(neighbor)<<"keep";
    Song song{path,{}};song.tags.title=L"Fixture";v.setLibrary({song});v.navigate(Page::Songs);v.input(Key::Select);v.input(Key::Menu);
    assert(v.labels()==std::vector<std::wstring>({L"Play Mode",L"Sound Settings",L"Add To Playlist",L"Delete"}));v.input(Key::Down);v.input(Key::Down);v.input(Key::Select);assert(v.page()==Page::Confirm&&v.playlist.empty());
    dump(v,root/"confirm-playlist-add.ppm");requireText(v,glyphs,L"Add",-1,1);requireText(v,glyphs,L"\"Fixture\"",-1,17);requireText(v,glyphs,L"to playlist?",-1,33);requireText(v,glyphs,L"\u2381 OK",2,49);
    auto prompt=v.screen;int volume=b.vol;for(auto key:{Key::Down,Key::Up,Key::VolumeUp,Key::Menu})v.input(key);v.render();assert(v.page()==Page::Confirm&&v.playlist.empty()&&b.vol==volume&&v.screen==prompt);v.click(60,15);assert(v.page()==Page::Confirm&&v.playlist.empty());v.click(110,55);assert(v.page()==Page::Options&&v.playlist.empty());
    v.input(Key::Select);v.click(15,55);assert(v.page()==Page::Options&&v.playlist==std::vector<fs::path>({path}));assert(v.labels()[2]==L"Remove From Playlist");v.input(Key::Select);dump(v,root/"confirm-playlist-remove.ppm");requireText(v,glyphs,L"Remove",-1,1);requireText(v,glyphs,L"from playlist?",-1,33);v.input(Key::Back);assert(v.playlist.size()==1);v.input(Key::Select);v.input(Key::Select);assert(v.playlist.empty());
    v.input(Key::Home);v.input(Key::Select);for(int i=0;i<3;i++)v.input(Key::Down);v.input(Key::Select);assert(v.page()==Page::Playlists&&v.labels()==std::vector<std::wstring>({L"Playlist-on-the-Go",L"Add To Playlist",L"Remove From Playlist"}));dump(v,root/"playlists-menu.ppm");v.input(Key::Down);v.input(Key::Select);v.input(Key::Select);assert(v.playlist.size()==1);v.input(Key::Down);v.input(Key::Select);assert(v.page()==Page::PlaylistRemove&&v.labels()==std::vector<std::wstring>({L"Fixture"}));v.input(Key::Select);v.input(Key::Back);assert(v.playlist.size()==1);v.input(Key::Select);v.input(Key::Select);assert(v.playlist.empty()&&v.page()==Page::PlaylistRemove);
    v.navigate(Page::Settings);v.settings.volume=33;v.settings.repeat=2;for(int i=0;i<6;i++)v.input(Key::Down);v.input(Key::Select);dump(v,root/"confirm-reset.ppm");requireText(v,glyphs,L"Restore factory",-1,6);requireText(v,glyphs,L"settings?",-1,28);requireText(v,glyphs,L"\u2381 Restore",2,49);v.input(Key::Back);assert(v.settings.volume==33&&v.settings.repeat==2);v.input(Key::Select);v.input(Key::Select);assert(v.settings.volume==70&&v.settings.repeat==0&&v.page()==Page::Settings);
    v.navigate(Page::Recordings);v.input(Key::Select);v.input(Key::Back);dump(v,root/"confirm-recording.ppm");requireText(v,glyphs,L"Save recording?",-1,6);requireText(v,glyphs,L"\"VOICE.wav\"",-1,28);v.input(Key::Down);v.input(Key::Select);assert(b.saves==1&&b.discards==0);v.navigate(Page::Recordings);v.input(Key::Select);v.input(Key::Back);v.input(Key::Back);assert(b.discards==1&&!b.mic.active);
    v.navigate(Page::Songs);v.input(Key::Select);v.input(Key::Menu);for(int i=0;i<3;i++)v.input(Key::Down);v.input(Key::Select);dump(v,root/"confirm-delete.ppm");requireText(v,glyphs,L"Delete?",-1,6);assert(fs::exists(path));v.input(Key::Back);assert(fs::exists(path));v.input(Key::Select);v.input(Key::Down);v.click(60,20);assert(fs::exists(path));v.input(Key::Select);assert(!fs::exists(path)&&fs::exists(neighbor)&&v.queueIndex()==-1);
    v.input(Key::Home);v.input(Key::Up);v.input(Key::Select);v.input(Key::Menu);assert(v.labels()==std::vector<std::wstring>({L"Presets",L"Manual tune",L"Auto tune"}));v.input(Key::Down);v.input(Key::Down);v.input(Key::Select);dump(v,root/"confirm-auto-tune.ppm");requireText(v,glyphs,L"Do you want",-1,1);requireText(v,glyphs,L"to auto-tune",-1,17);requireText(v,glyphs,L"the presets?",-1,33);v.input(Key::Back);assert(v.page()==Page::RadioOptions);v.input(Key::Select);v.input(Key::Select);assert(v.page()==Page::Presets);
}
static void volumeTests(const fs::path& root) {
    for(int theme=0;theme<3;theme++){Fake b;Interface v(b,root/"Music");v.tick(100);v.skipBoot();v.navigate(Page::Playing);v.settings.theme=theme;v.settings.volume=50;v.applySettings();v.input(Key::VolumeUp);assert(v.settings.volume==55&&b.vol==55);dump(v,root/("volume-"+std::to_string(theme)+".ppm"));requireAsset(v,4*256,17,26);requireAsset(v,4*256+1+theme,95,26);requireAsset(v,4*256+4,65,21);requireAsset(v,4*256+8,110,21);requireAsset(v,4*256+6,1,26);requireAsset(v,4*256+7,115,26);auto popup=v.screen;v.tick(1500);v.render();assert(v.screen!=popup);}
    Fake b;Interface v(b,root/"Music");v.tick(100);v.skipBoot();v.settings.volumeLimit=35;v.settings.volume=35;v.applySettings();v.navigate(Page::VolumeLimit);v.input(Key::VolumeUp);assert(b.vol>35&&v.settings.volumeLimit==35&&v.settings.volume==35);v.input(Key::Back);assert(b.vol==35&&v.settings.volumeLimit==35);v.navigate(Page::VolumeLimit);v.input(Key::Down);v.input(Key::Select);assert(v.settings.volumeLimit==32&&v.settings.volume==32&&b.vol==32);
    v.navigate(Page::Equalizer);v.navigate(Page::CustomEQ);v.input(Key::Select);assert(v.selection()==1);v.input(Key::Back);assert(v.page()==Page::CustomEQ&&v.selection()==0);for(int i=0;i<5;i++)v.input(Key::Select);assert(v.page()==Page::Equalizer);
    v.settings.theme=2;v.navigate(Page::Theme);assert(v.selection()==2);v.settings.repeat=1;v.navigate(Page::Repeat);assert(v.selection()==2);
}
static void buttonTests(const fs::path& root) {
    Fake b;Interface v(b,root/"Music");v.tick(100);v.skipBoot();
    pressBodyButton(v,BodyButton::Up);assert(v.selection()==5);pressBodyButton(v,BodyButton::Down);assert(v.selection()==0);
    pressBodyButton(v,BodyButton::Play);assert(v.page()==Page::Music);pressBodyButton(v,BodyButton::Back);assert(v.page()==Page::Home);
    pressBodyButton(v,BodyButton::Lock);assert(v.locked);int before=b.vol;pressBodyButton(v,BodyButton::VolumeUp);pressBodyButton(v,BodyButton::Play);assert(b.vol==before&&v.page()==Page::Home);pressBodyButton(v,BodyButton::Lock);assert(!v.locked);
    pressBodyButton(v,BodyButton::VolumeUp);assert(b.vol>before);pressBodyButton(v,BodyButton::VolumeDown);assert(b.vol<before+4);
    b.state.playing=true;b.state.duration=180;v.navigate(Page::Playing);pressBodyButton(v,BodyButton::Play);assert(b.state.paused);pressBodyButton(v,BodyButton::Play);assert(!b.state.paused);
    pressBodyButton(v,BodyButton::Down);v.tick(600);releaseBodyButton(v,BodyButton::Down);assert(b.seekPosition==4&&b.plays==0);pressBodyButton(v,BodyButton::Back);assert(v.page()==Page::Home);
}
static void lockTests(const fs::path& root) {
    for(int theme=0;theme<3;theme++) {
        Fake b;b.state.playing=true;b.state.duration=180;b.state.title=L"Title";
        Interface v(b,root/"Music");v.settings.theme=theme;v.tick(100);v.skipBoot();v.navigate(Page::Playing);v.render();auto playing=v.screen;
        auto expected=[&](bool closed){Fake blankBackend;Interface blank(blankBackend,root/"Music");blank.settings.theme=theme;blank.tick(100);blank.skipBoot();blank.navigate(Page::Playing);blank.input(Key::VolumeUp);blank.render();auto bg=blank.screen[0];
            unsigned id=3*256+(closed?15:18)+theme;auto* a=std::find_if(std::begin(assets),std::end(assets),[id](const auto& a){return a.id==id;});
            auto wanted=v.screen;wanted.fill(bg);for(unsigned y=0;y<a->h;y++)for(unsigned x=0;x<a->w;x++){auto p=assetPixels[a->offset+y*a->w+x];if(p!=0x07e0)wanted[(11+y)*128+52+x]=0xff000000|(((p>>11)&31)*255/31<<16)|(((p>>5)&63)*255/63<<8)|((p&31)*255/31);}
            assert(v.screen==wanted);requireAsset(v,id,52,11);
        };
        v.input(Key::VolumeUp);v.pressTransport(1);v.input(Key::Lock);assert(v.locked);dump(v,root/("lock-"+std::to_string(theme)+".ppm"));expected(true);
        auto closed=v.screen;v.tick(1349);v.render();assert(v.screen==closed);v.tick(1350);v.render();assert(v.locked&&v.screen!=closed);requireAsset(v,1*256+4,107,51);assert(b.seekPosition==-1);
        int volume=b.vol;v.input(Key::VolumeDown);v.render();expected(true);assert(b.vol==volume);v.tick(2600);v.render();assert(v.screen!=closed);v.click(60,48);v.render();expected(true);assert(b.seekPosition==-1);
        v.tick(2700);v.input(Key::Lock);assert(!v.locked);dump(v,root/("unlock-"+std::to_string(theme)+".ppm"));expected(false);auto open=v.screen;assert(open!=closed);v.tick(3949);v.render();assert(v.screen==open);v.tick(3950);v.render();assert(v.screen==playing);
        v.input(Key::Lock);v.input(Key::Lock);v.input(Key::Select);v.render();assert(!v.locked&&b.state.paused&&v.screen!=open);
        v.settings.rotation=1;v.input(Key::Lock);v.render();std::reverse(closed.begin(),closed.end());assert(v.screen==closed);
    }
    Fake b;Interface scan(b,root/"Music");scan.tick(100);scan.skipBoot();scan.navigate(Page::Songs);dump(scan,root/"library-scanning.ppm");requireAsset(scan,5*256+1,57,8);
}
int main(int argc,char** argv) {
    assert(argc==2);fs::path root=fs::absolute(argv[1]);fs::create_directories(root/"Music"/"Album");
    std::ofstream(root/"Music"/"one.wav").put(0);std::ofstream(root/"Music"/"Album"/"two.flac").put(0);
    metadataTests(root);titleTests(root);dialogTests(root);volumeTests(root);buttonTests(root);lockTests(root);
    Fake backend;Interface ui(backend,root/"Music");ui.tick(100);ui.skipBoot();
    {Fake b;Interface boot(b,root/"Music");boot.tick(100);assert(boot.booting());boot.tick(2300);assert(boot.booting());boot.tick(4501);assert(!boot.booting()&&boot.updating());dump(boot,root/"updating.ppm");requireText(boot,glyphs,L"Updating\u2026",-1,30);requireText(boot,glyphs,L"0%",-1,45);requireAsset(boot,5*256,57,8);boot.input(Key::Select);assert(boot.page()==Page::Home);boot.click(60,40);assert(boot.page()==Page::Home);boot.tick(5925);dump(boot,root/"updating-50.ppm");requireText(boot,glyphs,L"50%",-1,45);requireAsset(boot,7*256+29,5,59);boot.tick(7350);dump(boot,root/"updating-100.ppm");requireText(boot,glyphs,L"100%",-1,45);requireAsset(boot,7*256+34,5,59);boot.tick(7499);assert(boot.updating());boot.tick(7500);assert(!boot.updating());boot.input(Key::Select);assert(boot.page()==Page::Music);}
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
    ui.input(Key::Menu);assert(ui.page()==Page::Options);auto options=ui.labels();assert(options==std::vector<std::wstring>({L"Play Mode",L"Sound Settings",L"Add To Playlist",L"Delete"}));dump(ui,root/"options.ppm");
    for(int language=0;language<24;language++){ui.settings.language=language;ui.navigate(Page::Settings);assert(!ui.labels().empty());ui.render();}
    ui.settings.language=2;ui.settings.volumeLimit=35;ui.settings.volume=70;ui.applySettings();assert(backend.vol==35);ui.input(Key::VolumeUp);assert(backend.vol==35);
    ui.settings.equalizer=7;ui.settings.bands={3,-2,4,0,1};ui.applySettings();assert(backend.gains==ui.settings.bands);
    ui.navigate(Page::CustomEQ);ui.input(Key::Up);assert(ui.settings.bands[0]==5);ui.tick(clock+=1500);dump(ui,root/"equalizer.ppm");
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
    ui.navigate(Page::Recordings);ui.input(Key::Select);ui.input(Key::Back);ui.input(Key::Back);assert(backend.discards==1&&!backend.mic.active);
    ui.input(Key::Home);ui.input(Key::Lock);ui.input(Key::Select);assert(ui.page()==Page::Home);ui.input(Key::Lock);ui.input(Key::Select);assert(ui.page()==Page::Music);
    {Fake b;Interface locked(b,root/"Music");locked.tick(100);locked.skipBoot();locked.navigate(Page::Playing);locked.input(Key::Lock);locked.click(60,48);assert(b.seekPosition==-1);}
    {Fake b;b.state.playing=true;b.state.duration=1000;Interface held(b,root/"Music");held.tick(100);held.skipBoot();held.navigate(Page::Playing);held.pressTransport(1);for(int i=0;i<11;i++)held.tick(600+i*250);assert(b.seekPosition==46);held.releaseTransport();assert(b.plays==0);held.pressTransport(1);held.cancelTransport();held.releaseTransport();assert(b.plays==0);}
    Equalizer eq;for(int s=-32000;s<=32000;s+=1000)assert(eq.process(int16_t(s),0,100)==s);eq.configure({12,-12,12,-12,12});for(int i=0;i<48000;i++){auto sample=int16_t(32000*std::sin(i*.057));eq.process(sample,0,100);eq.process(sample,1,100);}eq.reset();assert(eq.process(0,0,100)==0&&eq.process(0,1,100)==0);
    // A large library must share queue storage rather than copying it per row.
    std::vector<Song> large;for(int i=0;i<5000;i++)large.push_back({root/"Music"/(std::to_string(i)+".mp3"),{}});
    ui.setLibrary(std::move(large));ui.navigate(Page::Songs);assert(ui.labels().size()==5000);ui.render();
    std::cout<<"PASS finite title pass/ellipsis/metadata gating; original playlist menu and modal OK/Cancel dialogs; fullscreen lock/unlock timeout/restore/retrigger in three themes; metadata sliding/clipping; original 31-step volume and cancelable limit; EQ button flow; 18 home/theme screens; hidden radio; zero-padded counters; firmware Updating spinner/layout/progress; shutdown dot frames; 24 languages; playback/seek; settings; radio JSON; microphone save/discard; DSP\n";
}
