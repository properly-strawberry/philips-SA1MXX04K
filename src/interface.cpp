#include "interface.h"
#include "assets.h"
#include "radio_config.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <random>
#include <set>
#include <sstream>

namespace player {
// Preserve the physical five-dot rail; the requested internet radio is a sixth stop.
static constexpr std::array<int,6> homeFamilies={0,2,3,4,5,1};
static constexpr uint64_t shutdownHold=1000,shutdownFrame=500,shutdownDuration=shutdownHold+7*shutdownFrame;
static std::wstring timeText(double seconds) {
    int s=std::max(0,int(seconds));auto two=[](int n){return(n<10?L"0":L"")+std::to_wstring(n);};return(s>=3600?std::to_wstring(s/3600)+L":"+two(s/60%60):std::to_wstring(s/60))+L":"+two(s%60);
}
static bool isAudio(const fs::path& path) {
    auto ext=path.extension().wstring();std::transform(ext.begin(),ext.end(),ext.begin(),[](wchar_t c){return towlower(c);});
    static const std::set<std::wstring> types={L".mp3",L".wav",L".flac",L".ogg",L".opus",L".m4a",L".aac",L".wma",L".aif",L".aiff",L".ape",L".wv",L".alac",L".m4b"};
    return types.count(ext)!=0;
}
static bool isBook(const fs::path& p) {
    auto s=p.wstring();std::transform(s.begin(),s.end(),s.begin(),[](wchar_t c){return towlower(c);});
    return p.extension()==L".m4b"||s.find(L"audiobook")!=std::wstring::npos;
}
static const Asset* findAsset(unsigned id) {
    auto it=std::lower_bound(std::begin(assets),std::end(assets),id,[](const Asset& a,unsigned b){return a.id<b;});
    return it!=std::end(assets)&&it->id==id?it:nullptr;
}
static const Asset* findGlyph(unsigned id,bool large) {
    auto* begin=large?std::begin(largeGlyphs):std::begin(glyphs);
    auto* end=large?std::end(largeGlyphs):std::end(glyphs);
    auto* it=std::lower_bound(begin,end,id,[](const Asset& a,unsigned b){return a.id<b;});
    if(it!=end&&it->id==id)return it;
    return id=='?'?nullptr:findGlyph('?',large);
}
// Firmware colors are ABGR and are quantized by its RGB565 drawing driver.
static uint32_t firmwareColor(uint32_t abgr) {
    unsigned r=(abgr&0xf8)>>3,g=(abgr&0xfc00)>>10,b=(abgr&0xf80000)>>19;
    return (r*255/31<<16)|(g*255/63<<8)|(b*255/31);
}
Interface::Interface(Backend& backend,fs::path music):backend_(backend),music_(std::move(music)),voice_(music_.parent_path()/L"Recordings"/L"Voice") {
    frame_.directory=music_;
}
std::wstring Interface::tr(int id) const { return firmwareText[std::clamp(settings.language,0,23)][std::clamp(id,0,234)]; }
void Interface::setLibrary(std::vector<Song> songs) {
    library=std::move(songs);scanning=false;
    songLookup_.clear();for(size_t i=0;i<library.size();i++)songLookup_[library[i].path]=i;
    cachedDirectory_.clear();
    int count=frame_.page==Page::Home?int(homeFamilies.size()):int(items().size());frame_.selected=std::clamp(frame_.selected,0,std::max(0,count-1));frame_.scroll=std::min(frame_.scroll,frame_.selected);
}
void Interface::navigate(Page page) {
    transportDirection_=0;
    auto directory=frame_.directory;history_.push_back(frame_);frame_={page,0,0,L"",0,directory};selectedAt_=now_;
}
void Interface::back() {
    if(frame_.page==Page::Recording && backend_.recording().active) {
        backend_.recordPause(true);confirm(tr(90),[this]{backend_.recordFinish(true);backend_.rescan();auto error=backend_.recording().error;notice(error.empty()?tr(91):error);});return;
    }
    if(frame_.page==Page::Confirm && backend_.recording().active) {
        backend_.recordFinish(false);
        if(!history_.empty()){frame_=history_.back();history_.pop_back();}
        if(frame_.page==Page::Recording&&!history_.empty()){frame_=history_.back();history_.pop_back();}
        selectedAt_=now_;return;
    }
    if(frame_.page==Page::Folders&&frame_.directory!=music_) {
        auto old=frame_.directory;frame_.directory=old.parent_path();frame_.selected=frame_.scroll=0;
        auto rows=items();for(size_t i=0;i<rows.size();i++)if(rows[i].label==old.filename().wstring())frame_.selected=int(i);
        frame_.scroll=std::max(0,frame_.selected-2);selectedAt_=now_;return;
    }
    if(history_.empty())frame_.page=Page::Home;
    else {frame_=history_.back();history_.pop_back();}
    selectedAt_=now_;
}
void Interface::notice(std::wstring value,int milliseconds) { notice_=std::move(value);noticeUntil_=now_+milliseconds; }
void Interface::confirm(std::wstring value,std::function<void()> yes) {prompt_=std::move(value);confirm_=std::move(yes);navigate(Page::Confirm);}
std::vector<fs::path> Interface::filteredSongs(int mode,const std::wstring& group) {
    std::vector<fs::path> result;
    for(auto& song:library) {
        if((mode==0||mode==1||mode==2)&&song.path.parent_path()==voice_)continue;
        if(mode==1 && song.tags.artist!=group)continue;
        if(mode==2 && song.tags.album!=group)continue;
        if(mode==3 && !isBook(song.path))continue;
        if(mode==4 && song.path.parent_path()!=voice_)continue;
        if(mode==5 && (!isBook(song.path)||song.tags.artist!=group))continue;
        result.push_back(song.path);
    }
    return result;
}
std::vector<Interface::Item> Interface::items() {
    std::vector<Item> rows;
    auto open=[&](int id,Page page){rows.push_back({tr(id),[this,page]{navigate(page);}});};
    auto choice=[&](std::wstring label,bool checked,std::function<void()> fn){rows.push_back({std::move(label),std::move(fn),checked});};
    auto songs=[&](std::vector<fs::path> paths) {
        auto shared=std::make_shared<std::vector<fs::path>>(std::move(paths));
        for(size_t i=0;i<shared->size();i++) {
            const auto& path=(*shared)[i];std::wstring title=path.stem().wstring();
            auto found=songLookup_.find(path);
            if(found!=songLookup_.end()&&!library[found->second].tags.title.empty())title=library[found->second].tags.title;
            rows.push_back({title,[this,shared,i]{playQueue(*shared,int(i));}});
        }
    };
    switch(frame_.page) {
    case Page::Music:
        open(14,Page::Songs);open(15,Page::Artists);open(16,Page::Albums);open(17,Page::Playlists);break;
    case Page::Artists:case Page::Albums: {
        std::set<std::wstring> names;bool artists=frame_.page==Page::Artists;
        bool authors=artists&&frame_.groupMode==5;
        for(auto& s:library)if(s.path.parent_path()!=voice_&&(!authors||isBook(s.path)))names.insert(artists?s.tags.artist:s.tags.album);
        for(auto name:names)rows.push_back({name.empty()?L"Unknown":name,[this,name,artists,authors]{navigate(Page::Group);frame_.group=name;frame_.groupMode=authors?5:artists?1:2;}});
        break;
    }
    case Page::Group:songs(frame_.groupMode==6?playlist:filteredSongs(frame_.groupMode,frame_.group));break;
    case Page::Songs:songs(filteredSongs(0));break;
    case Page::Folders: {
        if(cachedDirectory_!=frame_.directory) {
        cachedFiles_.clear();std::error_code ec;
        for(fs::directory_iterator it(frame_.directory,fs::directory_options::skip_permission_denied,ec),end;it!=end;it.increment(ec)) {
            if(ec)break;
            std::error_code e;if(it->is_symlink(e))continue;
            bool dir=it->is_directory(e);if(dir||isAudio(it->path()))cachedFiles_.emplace_back(it->path(),dir);
        }
        std::sort(cachedFiles_.begin(),cachedFiles_.end(),[](auto& a,auto& b){return a.second!=b.second?a.second:a.first.filename()<b.first.filename();});
        cachedDirectory_=frame_.directory;
        }
        auto paths=std::make_shared<std::vector<fs::path>>();for(auto& f:cachedFiles_)if(!f.second)paths->push_back(f.first);
        for(auto file:cachedFiles_) {
            if(file.second)rows.push_back({file.first.filename().wstring(),[this,file]{frame_.directory=file.first;frame_.selected=frame_.scroll=0;selectedAt_=now_;},false,true});
            else {int index=int(std::find(paths->begin(),paths->end(),file.first)-paths->begin());rows.push_back({file.first.filename().wstring(),[this,paths,index]{playQueue(*paths,index);}});}
        }
        break;
    }
    case Page::Playlists:
        rows.push_back({tr(30),[this]{navigate(Page::Group);frame_.groupMode=6;frame_.group=tr(30);}});break;
    case Page::Books:
        rows.push_back({tr(47),[this]{navigate(Page::Group);frame_.groupMode=3;frame_.group=tr(47);}});
        rows.push_back({tr(48),[this]{navigate(Page::Artists);frame_.groupMode=5;}});open(49,Page::Bookmarks);break;
    case Page::Bookmarks:
        for(size_t i=0;i<bookmarks.size();i++)rows.push_back({bookmarks[i].first.stem().wstring()+L" "+timeText(bookmarks[i].second),[this,i]{auto item=bookmarks[i];playQueue({item.first},0);pendingResume_=item.second;}});
        break;
    case Page::Options:
        open(108,Page::PlayMode);open(119,Page::Sound);
        rows.push_back({tr(31),[this]{if(queueIndex_>=0){auto path=queue_[queueIndex_];if(std::find(playlist.begin(),playlist.end(),path)==playlist.end())playlist.push_back(path);notice(tr(36));}}});
        rows.push_back({tr(32),[this]{if(queueIndex_>=0){auto path=queue_[queueIndex_];playlist.erase(std::remove(playlist.begin(),playlist.end(),path),playlist.end());notice(tr(39)+L" "+tr(40));}}});
        rows.push_back({tr(50),[this]{if(queueIndex_>=0){bookmarks.emplace_back(queue_[queueIndex_],backend_.audio().position);notice(tr(91));}}});
        rows.push_back({tr(43),[this]{if(queueIndex_>=0){auto path=queue_[queueIndex_];confirm(tr(45)+L" "+path.filename().wstring(),[this,path]{backend_.stop();std::error_code ec;fs::remove(path,ec);backend_.rescan();notice(ec?tr(207):tr(44));});}}});
        break;
    case Page::Settings:
        open(108,Page::PlayMode);open(119,Page::Sound);open(134,Page::Display);open(147,Page::Language);open(172,Page::Information);open(6,Page::Legal);
        rows.push_back({tr(178),[this]{confirm(tr(179)+L" "+tr(180),[this]{settings=Settings{};applySettings();notice(tr(181));});}});
        break;
    case Page::PlayMode:open(109,Page::Repeat);open(110,Page::Shuffle);break;
    case Page::Repeat:
        for(int i:{0,2,1})choice(tr(111+(i==1?2:i==2?1:0)),settings.repeat==i,[this,i]{settings.repeat=i;back();});
        break;
    case Page::Shuffle:
        for(int i=0;i<2;i++)choice(tr(114+i),settings.shuffle==i,[this,i]{settings.shuffle=i;back();});
        break;
    case Page::Sound:
        open(120,Page::Equalizer);open(129,Page::VolumeLimit);break;
    case Page::Equalizer: {
        const int names[]={198,121,122,123,124,125,126,127};
        for(int i=0;i<8;i++)choice(tr(names[i]),settings.equalizer==i,[this,i]{settings.equalizer=i;applySettings();if(i==7)navigate(Page::CustomEQ);else back();});
        break;
    }
    case Page::Display:
        open(104,Page::Theme);open(136,Page::ScreenOff);open(137,Page::Rotation);break;
    case Page::Theme:
        for(int i=0;i<3;i++)choice(tr(105+i),settings.theme==i,[this,i]{settings.theme=i;back();});
        break;
    case Page::Backlight:case Page::ScreenOff: {
        bool light=frame_.page==Page::Backlight;
        for(int i=0;i<4;i++){int value=i==3?0:(light?i+1:i+4)*10;choice(tr(i==3?146:(light?140:143)+i),(light?settings.backlight:settings.screenOff)==value,[this,value,light]{(light?settings.backlight:settings.screenOff)=value;back();});}break;
    }
    case Page::Rotation:
        for(int i=0;i<2;i++)choice(tr(138+i),settings.rotation==i,[this,i]{settings.rotation=i;back();});
        break;
    case Page::Language:
        for(int i=0;i<24;i++)choice(firmwareText[2][148+i],settings.language==i,[this,i]{settings.language=i;back();});
        break;
    case Page::Information: {
        std::error_code ec;auto capacity=fs::space(music_,ec);
        rows.push_back({tr(173)+L" "+(ec?L"?":std::to_wstring(capacity.capacity/(1024*1024))+L" MB"),[]{}});
        rows.push_back({tr(174)+L" "+(ec?L"?":std::to_wstring(capacity.available/(1024*1024))+L" MB"),[]{}});
        rows.push_back({tr(175)+L" 1.21 / Desktop",[]{}});rows.push_back({tr(176)+L" SA1MXX04",[]{}});rows.push_back({tr(177)+L" philips.com",[]{}});break;
    }
    case Page::Legal:
        for(const auto* line:firmwareLegal)rows.push_back({line,[]{}});
        break;
    case Page::RadioOptions:
        open(62,Page::Presets);
        rows.push_back({tr(63),[this]{navigate(Page::Presets);notice(tr(61));}});
        rows.push_back({tr(64),[this]{while(frame_.page!=Page::Radio&&!history_.empty())back();}});
        rows.push_back({L"Station table",[this]{notice(L"Edit Playlists/radio.json",3000);}});break;
    case Page::Presets:
        for(auto station:stations){std::wostringstream line;line<<std::fixed<<std::setprecision(1)<<station.tenths/10.0<<L" MHz  "<<station.name;rows.push_back({line.str(),[this,station]{frequency_=station.tenths;while(frame_.page!=Page::Radio&&!history_.empty())back();tuneAt_=now_+100;radioCandidate_=-2;}});}break;
    case Page::Recordings:
        rows.push_back({tr(80),[this]{remember();radio=false;backend_.stop();backend_.recordStart(voice_);navigate(Page::Recording);}});open(81,Page::Recorded);break;
    case Page::Recorded:songs(filteredSongs(4));break;
    case Page::Confirm:
        rows.push_back({tr(199),[this]{auto fn=confirm_;if(!history_.empty()){frame_=history_.back();history_.pop_back();}if(fn)fn();if(frame_.page==Page::Recording&&!backend_.recording().active)back();}});
        rows.push_back({tr(200),[this]{if(backend_.recording().active)backend_.recordFinish(false);back();if(frame_.page==Page::Recording)back();}});break;
    case Page::USB:
        choice(tr(231),false,[this]{notice(L"Music folder is already on this PC",2500);});
        choice(tr(232),false,[this]{back();});break;
    case Page::USBConnected:
        rows.push_back({L"FullyCharged",[]{}});rows.push_back({L"Charging",[]{}});rows.push_back({L"Do not disconnect",[]{}});break;
    default:break;
    }
    return rows;
}
void Interface::select() {
    if(frame_.page==Page::Home) {
        switch(frame_.selected) {
            case 0:navigate(Page::Music);break;
            case 1:navigate(Page::Recordings);break;
            case 2:navigate(Page::Folders);frame_.directory=music_;break;
            case 3:navigate(Page::Settings);break;
            case 4:
                if(!radio&&!backend_.audio().playing&&!lastPlayed.empty()){auto path=lastPlayed;double position=resumePosition;playQueue({path},0);pendingResume_=position;}
                else navigate(radio?Page::Radio:Page::Playing);
                break;
            case 5:
                if(!radio){remember();radio=true;backend_.stop();radioCandidate_=-2;tuneAt_=now_+250;if(!stations.empty())frequency_=stations.front().tenths;}
                navigate(Page::Radio);break;
        }
        return;
    }
    if(frame_.page==Page::Playing){auto info=backend_.audio();if(info.finished&&queueIndex_>=0)playQueue(queue_,queueIndex_);else backend_.pause();return;}
    if(frame_.page==Page::Radio){navigate(Page::RadioOptions);return;}
    if(frame_.page==Page::Recording){auto r=backend_.recording();if(!r.error.empty()){back();return;}backend_.recordPause(!r.paused);return;}
    if(frame_.page==Page::CustomEQ){settings.equalizer=7;applySettings();back();return;}
    if(frame_.page==Page::VolumeLimit){applySettings();back();notice(tr(132));return;}
    auto rows=items();if(frame_.selected>=0&&frame_.selected<int(rows.size()))rows[frame_.selected].action();
}
void Interface::move(int delta) {
    int count=frame_.page==Page::Home?int(homeFamilies.size()):int(items().size());
    if(!count)return;
    frame_.selected=(frame_.selected+delta+count)%count;
    if(frame_.selected<frame_.scroll)frame_.scroll=frame_.selected;
    if(frame_.selected>=frame_.scroll+3)frame_.scroll=frame_.selected-2;
    selectedAt_=now_;
}
void Interface::playQueue(std::vector<fs::path> paths,int index) {
    if(index<0||index>=int(paths.size()))return;
    transportDirection_=0;
    remember();pendingResume_=-1;
    queue_=std::move(paths);queueIndex_=index;radio=false;finishedHandled_=false;
    backend_.play(queue_[index].wstring());
    if(frame_.page!=Page::Playing)navigate(Page::Playing);
    selectedAt_=now_;
}
void Interface::next(int direction,bool automatic) {
    if(queue_.empty())return;
    if(automatic&&settings.repeat==2){auto frame=frame_;auto history=history_;playQueue(queue_,queueIndex_);if(frame.page!=Page::Playing){frame_=std::move(frame);history_=std::move(history);}return;}
    if(direction<0&&backend_.audio().position>3){backend_.seek(0);return;}
    int nextIndex=queueIndex_+direction;
    if(settings.shuffle&&queue_.size()>1) {
        std::vector<int> choices;
        for(size_t i=0;i<queue_.size();i++) {
            bool match=true;
            if(settings.shuffle==4)match=queue_[i].parent_path()==queue_[queueIndex_].parent_path();
            if(settings.shuffle==2||settings.shuffle==3) {
                std::wstring current,candidate;
                for(auto& s:library){if(s.path==queue_[queueIndex_])current=settings.shuffle==2?s.tags.artist:s.tags.album;if(s.path==queue_[i])candidate=settings.shuffle==2?s.tags.artist:s.tags.album;}
                match=current==candidate;
            }
            if(match&&int(i)!=queueIndex_)choices.push_back(int(i));
        }
        if(!choices.empty()){static std::mt19937 random{std::random_device{}()};nextIndex=choices[std::uniform_int_distribution<size_t>(0,choices.size()-1)(random)];}
    }
    if(automatic&&nextIndex>=int(queue_.size())&&settings.repeat==0&&!settings.shuffle)return;
    nextIndex=(nextIndex+int(queue_.size()))%int(queue_.size());
    auto frame=frame_;auto history=history_;playQueue(queue_,nextIndex);
    if(automatic&&frame.page!=Page::Playing){frame_=std::move(frame);history_=std::move(history);}
}
void Interface::remember(){auto info=backend_.audio();if(!radio&&queueIndex_>=0&&(info.playing||info.finished)){lastPlayed=queue_[queueIndex_];resumePosition=info.finished?0:info.position;}}
void Interface::volume(int delta) {settings.volume=std::clamp(settings.volume+delta,0,settings.volumeLimit);backend_.volume(settings.volume);volumeUntil_=now_+1000;}
void Interface::applySettings() {
    settings.volumeLimit=std::clamp(settings.volumeLimit,0,100);settings.volume=std::clamp(settings.volume,0,settings.volumeLimit);
    settings.theme=std::clamp(settings.theme,0,2);settings.language=std::clamp(settings.language,0,23);
    settings.shuffle=std::clamp(settings.shuffle,0,1);
    backend_.volume(settings.volume);
    static const std::array<double,5> presets[]={{{0,0,0,0,0}},{{6,3,-2,3,5}},{{5,-1,3,1,4}},{{6,4,-1,2,1}},{{3,1,-1,2,4}},{{3,1,0,1,3}},{{5,2,-2,4,5}}};
    backend_.equalizer(settings.equalizer>=7?settings.bands:presets[std::clamp(settings.equalizer,0,6)]);
}
void Interface::tune(int delta,bool preset) {
    if(preset&&!stations.empty()) {
        int index=0;
        if(delta>0){while(index<int(stations.size())&&stations[index].tenths<=frequency_)index++;if(index==int(stations.size()))index=0;}
        else {index=int(stations.size())-1;while(index>=0&&stations[index].tenths>=frequency_)index--;if(index<0)index=int(stations.size())-1;}
        frequency_=stations[index].tenths;
    } else {frequency_+=delta;if(frequency_>1080)frequency_=875;if(frequency_<875)frequency_=1080;}
    tuneAt_=now_+350;radioCandidate_=-2;backend_.stop();
}
void Interface::input(Key key) {
    if(booting_||updating_)return;
    bool wake=blank_||dim_;lastInput_=now_;blank_=dim_=false;
    if(key==Key::Lock){locked=!locked;noticeUntil_=0;return;}
    if(locked)return;
    if(wake)return;
    transportDirection_=0;
    booting_=false;
    if(key==Key::VolumeUp){volume(5);return;}if(key==Key::VolumeDown){volume(-5);return;}
    if(key==Key::Home){if(frame_.page==Page::Recording){back();return;}if(frame_.page==Page::Confirm&&backend_.recording().active)back();history_.clear();frame_.page=Page::Home;frame_.selected=frame_.scroll=0;selectedAt_=now_;return;}
    if(key==Key::Back){back();return;}
    if(key==Key::Menu){if(frame_.page==Page::Recording){back();return;}navigate(frame_.page==Page::Radio?Page::RadioOptions:Page::Options);return;}
    if(key==Key::Select){select();return;}
    int delta=(key==Key::Previous||key==Key::Up||key==Key::SeekBack)?-1:1;
    if(frame_.page==Page::Radio) {if(key==Key::Up||key==Key::Down)volume(-delta*5);else tune(delta,key==Key::SeekBack||key==Key::SeekForward);return;}
    if(frame_.page==Page::Playing) {
        if(key==Key::Up||key==Key::Down)volume(-delta*5);
        else if(key==Key::SeekBack||key==Key::SeekForward){auto info=backend_.audio();if(info.duration>0)backend_.seek(std::clamp(info.position+delta*5,0.0,std::max(0.0,info.duration-.05)));}
        else next(delta);
        return;
    }
    if(frame_.page==Page::CustomEQ) {
        if(key==Key::Previous||key==Key::Next)frame_.selected=(frame_.selected+delta+5)%5;
        else settings.bands[frame_.selected]=std::clamp(settings.bands[frame_.selected]-delta,-12.0,12.0);
        applySettings();return;
    }
    if(frame_.page==Page::VolumeLimit){settings.volumeLimit=std::clamp(settings.volumeLimit+delta*5,0,100);applySettings();return;}
    move(delta);
}
void Interface::pressTransport(int direction) {
    if(frame_.page!=Page::Playing||locked||booting_||updating_||blank_||dim_){input(direction<0?Key::Previous:Key::Next);return;}
    if(transportDirection_==direction)return;
    transportDirection_=direction;transportPressed_=transportTick_=lastInput_=now_;transportRepeats_=0;
}
void Interface::releaseTransport() {
    int direction=transportDirection_;bool seeking=transportRepeats_>0;transportDirection_=0;
    if(direction&&!seeking&&frame_.page==Page::Playing)input(direction<0?Key::Previous:Key::Next);
}
void Interface::shutdown() {
    if(shuttingDown_)return;
    shuttingDown_=true;shutdownAt_=now_;transportDirection_=0;
}
bool Interface::shutdownFinished() const { return shuttingDown_&&now_-shutdownAt_>=shutdownDuration; }
void Interface::tick(uint64_t millis) {
    now_=millis;if(!firstTick_){firstTick_=now_?now_:1;lastInput_=selectedAt_=now_;}
    if(shuttingDown_)return;
    if(booting_&&now_>firstTick_+4400)booting_=false;
    if(updating_&&now_>=firstTick_+7400)updating_=false;
    dim_=settings.backlight>0&&now_-lastInput_>uint64_t(settings.backlight)*1000;
    blank_=settings.screenOff>0&&now_-lastInput_>uint64_t(settings.screenOff)*1000;
    auto info=backend_.audio();
    if(transportDirection_&&frame_.page==Page::Playing&&!locked&&now_-transportPressed_>=450&&now_-transportTick_>=250) {
        transportTick_=lastInput_=now_;transportRepeats_++;
        int step=transportRepeats_<10?4:transportRepeats_<20?5:transportRepeats_<40?10:20;
        if(info.duration>0)backend_.seek(std::clamp(info.position+transportDirection_*step,0.0,std::max(0.0,info.duration-.05)));
    }
    if(pendingResume_>=0&&info.duration>0){backend_.seek(pendingResume_);pendingResume_=-1;}
    if(info.finished&&!finishedHandled_&&!radio){remember();finishedHandled_=true;next(1,true);}
    if(radio&&radioCandidate_==-2&&now_>=tuneAt_) {
        radioCandidate_=-1;
        for(size_t i=0;i<stations.size();i++)if(stations[i].tenths==frequency_){radioCandidate_=int(i);backend_.play(stations[i].url);break;}
    }
}
std::wstring Interface::currentTitle() {
    auto info=backend_.audio();if(!info.title.empty())return info.title;
    if(radio&&radioCandidate_>=0)return stations[radioCandidate_].name;
    if(queueIndex_>=0){auto found=songLookup_.find(queue_[queueIndex_]);if(found!=songLookup_.end()&&!library[found->second].tags.title.empty())return library[found->second].tags.title;return queue_[queueIndex_].stem().wstring();}
    return tr(12);
}
std::vector<std::wstring> Interface::labels(){std::vector<std::wstring> out;for(auto& i:items())out.push_back(i.label);return out;}
void Interface::loadStations(const fs::path& file) {
    std::wstring error;stations=readRadioConfig(file,error);
    radioCandidate_=-2;tuneAt_=now_+350;
    if(radio)backend_.stop();
    if(!error.empty())notice(error,4000);
}
uint32_t Interface::background()const {
    auto* a=findAsset(9+settings.theme*36);if(!a)return 0;
    uint16_t p=assetPixels[a->offset];
    return (((p>>11)&31)*255/31<<16)|(((p>>5)&63)*255/63<<8)|((p&31)*255/31);
}
void Interface::put(int x,int y,uint32_t color){if(x>=0&&x<128&&y>=0&&y<64)screen[y*128+x]=0xff000000|color;}
void Interface::rect(int x,int y,int w,int h,uint32_t color){for(int yy=y;yy<y+h;yy++)for(int xx=x;xx<x+w;xx++)put(xx,yy,color);}
void Interface::image(unsigned id,int x,int y,int width,int height) {
    auto* a=findAsset(id);if(!a)return;
    unsigned w=width<0?a->w:std::min(a->w,unsigned(width)),h=height<0?a->h:std::min(a->h,unsigned(height));
    for(unsigned yy=0;yy<h;yy++)for(unsigned xx=0;xx<w;xx++) {
        uint16_t p=assetPixels[a->offset+yy*a->w+xx];if(p==0x07e0)continue;
        put(x+xx,y+yy,(((p>>11)&31)*255/31<<16)|(((p>>5)&63)*255/63<<8)|((p&31)*255/31));
    }
}
int Interface::textWidth(const std::wstring& value,bool large)const{int n=0;for(auto c:value){auto* a=findGlyph(c,large);n+=a?int(a->w):6;}return n;}
void Interface::text(int x,int y,const std::wstring& value,uint32_t color,bool large,int left,int right) {
    for(auto ch:value){auto* a=findGlyph(ch,large);if(!a){x+=6;continue;}for(unsigned yy=0;yy<a->h;yy++)for(unsigned xx=0;xx<a->w;xx++) {
        if(x+int(xx)>=left&&x+int(xx)<right&&(fontBits[a->offset+yy*((a->w+7)/8)+xx/8]&(0x80>>(xx%8))))put(x+xx,y+yy,color);
        }
        x+=a->w;if(x>=right)break;}
}
void Interface::center(int y,const std::wstring& value,uint32_t color,bool large){text(std::max(0,(128-textWidth(value,large))/2),y,value,color,large);}
void Interface::number(int x,int y,const std::wstring& value,uint32_t color,int font,int align,int width) {
    auto* begin=font==1?std::begin(counterGlyphs):font==2?std::begin(dialGlyphs):std::begin(timeGlyphs);auto* end=font==1?std::end(counterGlyphs):font==2?std::end(dialGlyphs):std::end(timeGlyphs);
    auto glyph=[&](wchar_t c){auto* p=std::lower_bound(begin,end,unsigned(c),[](const Asset& a,unsigned n){return a.id<n;});return p!=end&&p->id==unsigned(c)?p:nullptr;};
    int size=0;for(auto c:value)if(auto* a=glyph(c))size+=a->w;
    int right=x+width;if(align==2)x+=std::max(0,width-size);else if(align==1)x+=std::max(0,(width-size)/2);
    for(auto c:value)if(auto* a=glyph(c)){for(unsigned row=0;row<a->h;row++)for(unsigned col=0;col<a->w;col++)if((!width||x+int(col)<right)&&(fontBits[a->offset+row*((a->w+7)/8)+col/8]&(0x80>>(col%8))))put(x+col,y+row,color);x+=a->w;}
}
void Interface::marquee(int x,int y,const std::wstring& value,uint32_t color,int width,bool large) {
    int n=textWidth(value,large),shift=0;if(n>width&&now_>selectedAt_+1200){int period=n+24;shift=int((now_-selectedAt_-1200)/65)%period;text(x-shift+period,y,value,color,large,x,x+width);}text(x-shift,y,value,color,large,x,x+width);
}
void Interface::status(const TrackInfo& info) {
    image(2*256+(transportRepeats_&&transportDirection_?(transportDirection_<0?1:3):info.paused?4:info.playing?5:2),5,35);
    auto padded=[](size_t value){auto s=std::to_wstring(value);return std::wstring(s.size()<4?4-s.size():0,L'0')+s;};
    number(5,55,padded(queueIndex_>=0?size_t(queueIndex_+1):0),0xffffff,true,2,20);number(25,55,L"/",0xffffff,true);number(31,55,padded(queue_.size()),0xffffff,true,0,20);
    if(settings.repeat)image(2*256+(settings.repeat==2?9:12)+settings.theme,55,52);
    if(settings.shuffle)image(2*256+15+settings.theme,70,52);
    if(settings.equalizer)image(2*256+6+settings.theme,85,52);
    image(1*256+4,107,51);
}
void Interface::render() {
    screen.fill(0xff000000|background());auto info=backend_.audio();
    const uint32_t primary[]={0xffcec2bc,0xfff8d6c2,0xffead3fc},selectedColors[]={0xff000000,0xff9c613e,0xff9583d8};
    uint32_t fg=firmwareColor(settings.theme==2?primary[2]:0xffffffff),selectedColor=firmwareColor(selectedColors[settings.theme]),accent=settings.theme?0xffffff:0x36cbe8;
    if(shuttingDown_){screen.fill(0xff000000);uint64_t elapsed=now_-shutdownAt_;if(elapsed<shutdownHold)center(25,tr(229),0xffffff,true);else image(8*256+unsigned(std::min(uint64_t(7),(elapsed-shutdownHold)/shutdownFrame)),15,9);return;}
    if(blank_) {screen.fill(0xff000000);return;}
    if(booting_){image(7*256+std::min(20,int((now_-firstTick_)/170)),0,0);return;}
    if(updating_) {
        screen.fill(0xff000000);center(26,tr(228),0xffffff);
        int percent=int(std::clamp((double(now_)-double(firstTick_)-4400)*100/2850,0.0,100.0));
        center(43,std::to_wstring(percent)+L"%",0xffffff);
        image(7*256+34,5,60);image(7*256+24,5,60,118*percent/100,2);return;
    }
    if(frame_.page==Page::Home) {
        int family=homeFamilies[std::clamp(frame_.selected,0,int(homeFamilies.size())-1)],base=9+settings.theme*36,step=std::min(5,int((now_-selectedAt_)/50));
        image(settings.theme,9,8);image(base+family*6+step,48,0);if(frame_.selected<5)image(6+settings.theme,5,4+frame_.selected*11);image(1*256+4,110,2);
        auto title=tr(family==5&&!info.playing?13:7+family);
        int x=28+std::max(0,(90-textWidth(title,true))/2);marquee(x,48,title,firmwareColor(primary[settings.theme]),118-x,true);
    } else if(frame_.page==Page::Playing) {
        uint32_t labelColor=firmwareColor(primary[settings.theme]);marquee(5,3,currentTitle(),labelColor,118,true);
        auto artist=info.artist,album=info.album;auto found=queueIndex_>=0?songLookup_.find(queue_[queueIndex_]):songLookup_.end();
        if(found!=songLookup_.end()){if(artist.empty())artist=library[found->second].tags.artist;if(album.empty())album=library[found->second].tags.album;}
        bool showAlbum=artist.empty()||(!album.empty()&&((now_-selectedAt_)/3000)%2);
        const auto& metadata=showAlbum?album:artist;
        if(!metadata.empty()){image(2*256+(showAlbum?18:21)+settings.theme,5,22);marquee(17,20,metadata,labelColor,106);}
        number(18,37,timeText(info.position),0xffffff);number(83,37,timeText(info.duration),0xffffff,false,2,40);
        const uint32_t progressColors[]={0xff2d2d2d,0xffa87455,0xffa390e9};rect(5,48,118,2,firmwareColor(progressColors[settings.theme]));if(info.duration>0)rect(5,48,118*int(100*std::clamp(info.position/info.duration,0.0,1.0))/100,2,0xffffff);
        status(info);
        if(!info.error.empty()){rect(0,16,128,36,background());center(17,tr(101),fg);center(32,tr(102),fg);}
    } else if(frame_.page==Page::Radio) {
        auto color=settings.theme==2?firmwareColor(primary[2]):0xffffff;
        image(2*256+24+settings.theme,5,0);image(2*256+27+settings.theme,10,55-((frequency_-875)*100/205)*55/100);image(1*256+4,107,51);
        std::wostringstream frequency;frequency<<std::fixed<<std::setprecision(1)<<frequency_/10.0;number(26,20,frequency.str(),color,2,1,75);text(106,25,L"FM",color,true);
        std::wstring name=radioCandidate_>=0?stations[radioCandidate_].name:radioCandidate_==-2?tr(61):L"--";
        marquee(26,47,info.error.empty()?name:L"Unavailable",color,75);
    } else if(frame_.page==Page::Recording) {
        auto record=backend_.recording();marquee(5,13,record.path.filename().wstring(),firmwareColor(primary[settings.theme]),118,true);
        image(2*256+(record.paused?4:0),5,35);number(18,37,timeText(record.seconds),0xffffff);
        std::error_code error;auto space=fs::space(voice_,error);double remaining=error?14400:std::min(14400.0,space.available/44100.0);
        number(73,37,timeText(remaining),0xffffff,false,2,50);
        const uint32_t progressColors[]={0xff2d2d2d,0xffa87455,0xffa390e9};rect(5,48,118,2,firmwareColor(progressColors[settings.theme]));rect(5,48,int(118*record.seconds/std::max(1.0,record.seconds+remaining)),2,0xffffff);
        image(3*256+28,5,52);image(1*256+4,107,51);
        if(!record.error.empty()){screen.fill(0xff000000|background());center(10,L"Microphone",fg);center(27,L"unavailable",fg);}
    } else if(frame_.page==Page::CustomEQ) {
        image(6*256+settings.theme,0,0);
        for(int i=0;i<5;i++){int x=13+i*24,y=24-int(settings.bands[i]*1.6);rect(x-8,y,16,2,fg);if(i==frame_.selected)image(6*256+3,x-5,0);}
    } else if(frame_.page==Page::VolumeLimit) {
        center(0,tr(129),fg);image(6*256+4+settings.theme,55,10);center(45,std::to_wstring(settings.volumeLimit)+L"%",fg);
        rect(56,12+(100-settings.volumeLimit)*40/100,15,2,accent);
    } else if(frame_.page==Page::Legal) {
        for(int i=0;i<5;i++){int index=frame_.selected+i;if(index<int(std::size(firmwareLegal)))text(1,2+i*12,firmwareLegal[index],fg);}
    } else if(frame_.page==Page::USBConnected) {
        screen.fill(0xff000000);
        image(0x10000+6,81,17);image(0x10000+7,19,26);
        bool transfer=frame_.selected==2;int state=transfer?8:frame_.selected==1?10:12;
        image(0x10000+(transfer?14+int((now_/100)%10):14),50,30);
        image(0x10000+(frame_.selected==1?int((now_/250)%5):5),110,2);
        image(0x10000+state,13,50);image(0x10000+state+1,13,56);
    } else if(frame_.page==Page::USB) {
        image(1*256+4,111,0);image(9*256+settings.language*2+std::min(1,frame_.selected),0,20);
    } else if(frame_.page==Page::Confirm) {
        marquee(2,0,prompt_,fg,124);auto rows=items();
        for(int i=0;i<2;i++){if(frame_.selected==i)image(3*256+12+settings.theme,1,22+i*21);center(25+i*21,rows[i].label,frame_.selected==i?0x101820:fg,true);}
    } else {
        auto rows=items();
        for(int row=0;row<3;row++) {
            int index=frame_.selected+row-1;if(index<0||index>=int(rows.size()))continue;int y=row*21+8;bool selected=row==1;
            if(selected)image(3*256+12+settings.theme,0,25);
            int x=rows[index].folder?26:9;
            if(rows[index].folder)image(3*256+6+settings.theme+(selected?3:0),9,y);
            int width=rows[index].folder?94:rows[index].checked?98:111;
            if(selected)marquee(x,y,rows[index].label,selectedColor,width,true);
            else if(textWidth(rows[index].label,true)>width) {
                std::wstring prefix;int used=0;
                for(auto ch:rows[index].label){auto* glyph=findGlyph(ch,true);int advance=glyph?int(glyph->w):6;if(used+advance>width-12)break;prefix+=ch;used+=advance;}
                text(x,y,prefix,fg,true);image(3*256+21+settings.theme,x+used,y);
            } else text(x,y,rows[index].label,fg,true,0,x+width);
            if(rows[index].checked)image(3*256+2+(selected?settings.theme:3),107,y);
        }
        if(rows.empty()) {
            if(scanning){image(8*256+unsigned((now_/100)%8),15,0);center(50,tr(228),fg);}
            else {center(19,frame_.page==Page::Recorded?tr(82):tr(99),fg);if(frame_.page==Page::Recorded)center(34,tr(83),fg);}
        }
        if(rows.size()>3)image(3*256+25+settings.theme,126,frame_.selected*39/std::max(1,int(rows.size())-1));
    }
    if(now_<volumeUntil_) {
        rect(12,4,104,55,background());center(5,tr(128),fg);image(2*256+6+settings.theme,25,25);text(49,24,std::to_wstring(settings.volume),fg,true);
        rect(20,46,88,3,0x6b7b8b);rect(20,46,settings.volume*88/100,3,accent);
    }
    if(now_<noticeUntil_) {
        rect(0,5,128,54,background());image(3*256+2,57,7);
        std::vector<std::wstring> lines;std::wstring line;std::wistringstream input(notice_);std::wstring word;
        while(input>>word){if(textWidth(line+L" "+word)>123&&!line.empty()){lines.push_back(line);line.clear();}if(!line.empty())line+=L" ";line+=word;}if(!line.empty())lines.push_back(line);
        for(size_t i=0;i<std::min(size_t(3),lines.size());i++)center(24+int(i)*12,lines[i],fg);
    }
    if(locked){rect(39,0,50,64,background());image(3*256+15+settings.theme,52,3);center(44,L"Hold MENU",fg);}
    if(dim_)for(auto& p:screen)p=0xff000000|((p&0x00fefefe)>>1);
    if(settings.rotation)std::reverse(screen.begin(),screen.end());
}
void Interface::click(int x,int y) {
    if(locked||booting_||updating_)return;
    if(blank_||dim_){input(Key::Select);return;}
    if(settings.rotation){x=127-x;y=63-y;}
    if(frame_.page==Page::Home){input(Key::Select);return;}
    if(frame_.page==Page::Playing){if(y>=46&&y<52){auto info=backend_.audio();if(info.duration>0){lastInput_=now_;backend_.seek(std::clamp((x-5)/118.0,0.0,1.0)*std::max(0.0,info.duration-.05));}}else input(Key::Select);return;}
    if(frame_.page==Page::Radio){if(x<23){frequency_=875+int(std::clamp((59-y)/55.0,0.0,1.0)*205);tuneAt_=now_+350;radioCandidate_=-2;backend_.stop();lastInput_=now_;}else input(Key::Select);return;}
    if(frame_.page==Page::Recording||frame_.page==Page::VolumeLimit||frame_.page==Page::CustomEQ){input(Key::Select);return;}
    if(frame_.page==Page::USBConnected){input(Key::Down);return;}
    if(frame_.page==Page::USB){if(y>=20&&y<56){frame_.selected=(y-20)/18;input(Key::Select);}return;}
    int index=frame_.page==Page::Confirm?(y>=43?1:0):frame_.selected+(y<25?-1:y<46?0:1);
    auto rows=items();if(index>=0&&index<int(rows.size())){frame_.selected=index;input(Key::Select);}
}
}
