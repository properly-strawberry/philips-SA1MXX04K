#include "interface.h"
#include "assets.h"
#include "radio_config.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <locale>
#include <random>
#include <set>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace player {
// Preserve the physical five-dot rail; the requested internet radio is a sixth stop.
static constexpr std::array<int,6> homeFamilies={0,2,3,4,5,1};
static constexpr uint64_t shutdownHold=1000,shutdownFrame=500,shutdownDuration=shutdownHold+7*shutdownFrame;
static int volumeLevel(int percent){return std::clamp((percent*31+50)/100,0,31);}
static int volumePercent(int level){return (std::clamp(level,0,31)*100+15)/31;}
// Ignore case for browsing; retain distinct spellings with a deterministic tie.
static bool nameLess(const std::wstring& a,const std::wstring& b) {
#ifdef _WIN32
    int order=CompareStringOrdinal(a.c_str(),-1,b.c_str(),-1,TRUE);
    if(order==CSTR_LESS_THAN)return true;
    if(order==CSTR_GREATER_THAN)return false;
#else
    static const std::locale locale("");
    const auto& letters=std::use_facet<std::ctype<wchar_t>>(locale);
    for(size_t i=0;i<std::min(a.size(),b.size());i++) {
        auto left=letters.toupper(a[i]),right=letters.toupper(b[i]);
        if(left!=right)return left<right;
    }
    if(a.size()!=b.size())return a.size()<b.size();
#endif
    return a<b;
}
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
    cancelHeldActions();
    library=std::move(songs);scanning=false;
    songLookup_.clear();for(size_t i=0;i<library.size();i++)songLookup_[library[i].path]=i;
    cachedDirectory_.clear();
    int count=frame_.page==Page::Home?int(homeFamilies.size()):int(items().size());frame_.selected=std::clamp(frame_.selected,0,std::max(0,count-1));frame_.scroll=std::min(frame_.scroll,frame_.selected);
}
void Interface::navigate(Page page) {
    auto previous=frame_.page;
    cancelHeldActions();
    if(frame_.page==Page::VolumeLimit&&limitDraft_>=0){backend_.volume(volumeBeforeLimit_);limitDraft_=-1;}
    if(page==Page::Playing){metadataStarted_=false;titleStarted_=false;}
    if(page==Page::VolumeLimit){volumeBeforeLimit_=settings.volume;limitDraft_=settings.volume;volumeUntil_=0;}
    auto directory=frame_.directory;history_.push_back(frame_);frame_={page,0,0,L"",0,directory};selectedAt_=now_;
    auto rows=items();for(size_t i=0;i<rows.size();i++)if(rows[i].checked){frame_.selected=int(i);break;}
    refreshOnHome(previous);
}
void Interface::refreshOnHome(Page previous) {
    if(previous==Page::Home||frame_.page!=Page::Home)return;
    cachedDirectory_.clear();scanning=true;backend_.rescan();
}
void Interface::back() {
    cancelHeldActions();
    auto previous=frame_.page;
    if(frame_.page==Page::VolumeLimit&&limitDraft_>=0){backend_.volume(volumeBeforeLimit_);limitDraft_=-1;}
    if(frame_.page==Page::Recording && backend_.recording().active) {
        backend_.recordPause(true);confirm(tr(90)+L"\n\""+backend_.recording().path.filename().wstring()+L"\"",[this]{backend_.recordFinish(true);backend_.rescan();auto error=backend_.recording().error;notice(error.empty()?tr(91):error);});return;
    }
    if(frame_.page==Page::Confirm && backend_.recording().active) {
        backend_.recordFinish(false);
        if(!history_.empty()){frame_=history_.back();history_.pop_back();}
        if(frame_.page==Page::Recording&&!history_.empty()){frame_=history_.back();history_.pop_back();}
        selectedAt_=now_;refreshOnHome(previous);return;
    }
    if(frame_.page==Page::Folders&&frame_.directory!=music_) {
        auto old=frame_.directory;frame_.directory=old.parent_path();frame_.selected=frame_.scroll=0;
        auto rows=items();for(size_t i=0;i<rows.size();i++)if(rows[i].label==old.filename().wstring())frame_.selected=int(i);
        frame_.scroll=std::max(0,frame_.selected-2);selectedAt_=now_;return;
    }
    if(history_.empty())frame_.page=Page::Home;
    else {frame_=history_.back();history_.pop_back();}
    selectedAt_=now_;
    refreshOnHome(previous);
}
void Interface::notice(std::wstring value,int milliseconds) {cancelHeldActions();notice_=std::move(value);noticeUntil_=now_+milliseconds;volumeUntil_=0; }
void Interface::confirm(std::wstring value,std::function<void()> yes,bool restore) {
    std::vector<std::wstring> lines;std::wistringstream input(value);std::wstring line;while(std::getline(input,line))lines.push_back(line);
    std::vector<std::pair<int,std::wstring>> rows;for(size_t i=0;i<lines.size();i++)rows.emplace_back(lines.size()==1?17:lines.size()==2?(i?28:6):1+int(i)*16,lines[i]);
    confirmRows(std::move(rows),std::move(yes),restore);
}
void Interface::confirmRows(std::vector<std::pair<int,std::wstring>> rows,std::function<void()> yes,bool restore) {promptRows_=std::move(rows);restorePrompt_=restore;confirm_=std::move(yes);volumeUntil_=noticeUntil_=0;navigate(Page::Confirm);}
void Interface::playlistAction(const fs::path& path,bool remove) {
    if(path.empty())return;
    int language=settings.language,first=1,name=17,last=33;
    if(language==4||language==8||language==9||language==18||language==21||(!remove&&language==6)){first=17;name=1;}
    if(remove&&(language==19||language==20)){first=1;last=17;name=33;}
    auto found=songLookup_.find(path);auto title=found!=songLookup_.end()&&!library[found->second].tags.title.empty()?library[found->second].tags.title:path.stem().wstring();
    confirmRows({{first,tr(remove?37:33)},{name,L"\""+title+L"\""},{last,tr(remove?38:34)}},[this,path,remove]{
        if(remove)playlist.erase(std::remove(playlist.begin(),playlist.end(),path),playlist.end());
        else if(std::find(playlist.begin(),playlist.end(),path)==playlist.end())playlist.push_back(path);
        notice(remove?tr(39)+L"\n"+tr(40):tr(36));
    });
}
void Interface::openOptions() {
    if(frame_.page==Page::Recording){back();return;}
    if(frame_.page==Page::Radio){navigate(Page::RadioOptions);return;}
    optionPath_.clear();
    if(frame_.page==Page::Playing&&queueIndex_>=0&&!radio)optionPath_=queue_[queueIndex_];
    if(optionPath_.empty())return;
    navigate(Page::Options);
}
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
        std::set<std::wstring,decltype(&nameLess)> names(nameLess);bool artists=frame_.page==Page::Artists;
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
        std::sort(cachedFiles_.begin(),cachedFiles_.end(),[](const auto& a,const auto& b){return a.second!=b.second?a.second:nameLess(a.first.filename().wstring(),b.first.filename().wstring());});
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
        rows.push_back({tr(30),[this]{navigate(Page::Group);frame_.groupMode=6;frame_.group=tr(30);}});
        rows.push_back({tr(31),[this]{if(queueIndex_>=0&&!radio)playlistAction(queue_[queueIndex_],false);}});
        open(32,Page::PlaylistRemove);break;
    case Page::PlaylistRemove:
        for(auto path:playlist){auto found=songLookup_.find(path);auto title=found!=songLookup_.end()&&!library[found->second].tags.title.empty()?library[found->second].tags.title:path.stem().wstring();rows.push_back({title,[this,path]{playlistAction(path,true);}});}break;
    case Page::Books:
        rows.push_back({tr(47),[this]{navigate(Page::Group);frame_.groupMode=3;frame_.group=tr(47);}});
        rows.push_back({tr(48),[this]{navigate(Page::Artists);frame_.groupMode=5;}});open(49,Page::Bookmarks);break;
    case Page::Bookmarks:
        for(size_t i=0;i<bookmarks.size();i++)rows.push_back({bookmarks[i].first.stem().wstring()+L" "+timeText(bookmarks[i].second),[this,i]{auto item=bookmarks[i];playQueue({item.first},0);pendingResume_=item.second;}});
        break;
    case Page::Options:
        open(108,Page::PlayMode);open(119,Page::Sound);
        if(!optionPath_.empty()){
            bool member=std::find(playlist.begin(),playlist.end(),optionPath_)!=playlist.end();
            rows.push_back({tr(member?32:31),[this,member]{playlistAction(optionPath_,member);}});
            rows.push_back({tr(43),[this]{auto path=optionPath_;confirm(tr(45)+L"\n\""+path.filename().wstring()+L"\"",[this,path]{backend_.stop();std::error_code ec;bool removed=fs::remove(path,ec);if(removed&&!ec){playlist.erase(std::remove(playlist.begin(),playlist.end(),path),playlist.end());queue_.erase(std::remove(queue_.begin(),queue_.end(),path),queue_.end());queueIndex_=-1;optionPath_.clear();if(lastPlayed==path){lastPlayed.clear();resumePosition=0;}back();}cachedDirectory_.clear();backend_.rescan();notice(ec||!removed?tr(207):tr(44));});}});
        }
        break;
    case Page::Settings:
        open(108,Page::PlayMode);open(119,Page::Sound);open(134,Page::Display);open(147,Page::Language);open(172,Page::Information);open(6,Page::Legal);
        rows.push_back({tr(178),[this]{confirm(tr(179)+L"\n"+tr(180),[this]{settings=Settings{};applySettings();notice(tr(181));},true);}});
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
        rows.push_back({tr(64),[this]{while(frame_.page!=Page::Radio&&!history_.empty())back();}});
        rows.push_back({tr(63),[this]{confirm(tr(53)+L"\n"+tr(54)+L"\n"+tr(55),[this]{navigate(Page::Presets);});}});break;
    case Page::Presets:
        for(auto station:stations){std::wostringstream line;line<<std::fixed<<std::setprecision(1)<<station.tenths/10.0<<L" MHz  "<<station.name;rows.push_back({line.str(),[this,station]{frequency_=station.tenths;while(frame_.page!=Page::Radio&&!history_.empty())back();tuneAt_=now_+100;radioCandidate_=-2;}});}break;
    case Page::Recordings:
        rows.push_back({tr(80),[this]{remember();radio=false;backend_.stop();backend_.recordStart(voice_);navigate(Page::Recording);}});open(81,Page::Recorded);break;
    case Page::Recorded:songs(filteredSongs(4));break;
    case Page::Confirm:break;
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
    cancelHeldActions();
    if(frame_.page==Page::Confirm){auto fn=std::move(confirm_);if(!history_.empty()){frame_=history_.back();history_.pop_back();}if(fn)fn();if(frame_.page==Page::Recording&&!backend_.recording().active)back();return;}
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
    if(frame_.page==Page::Playing){auto info=backend_.audio();if(!info.error.empty())return;if(info.finished&&queueIndex_>=0)playQueue(queue_,queueIndex_);else backend_.pause();return;}
    if(frame_.page==Page::Radio){navigate(Page::RadioOptions);return;}
    if(frame_.page==Page::Recording){auto r=backend_.recording();if(!r.error.empty()){back();return;}backend_.recordPause(!r.paused);return;}
    if(frame_.page==Page::CustomEQ){settings.equalizer=7;applySettings();if(frame_.selected<4)frame_.selected++;else back();return;}
    if(frame_.page==Page::VolumeLimit){settings.volumeLimit=std::max(0,limitDraft_);settings.volume=settings.volumeLimit;limitDraft_=-1;applySettings();back();notice(tr(132));return;}
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
    cancelHeldActions();
    metadataStarted_=false;titleStarted_=false;
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
void Interface::volume(int delta) {int level=std::clamp(volumeLevel(settings.volume)+(delta>0?1:-1),0,volumeLevel(settings.volumeLimit));settings.volume=std::min(settings.volumeLimit,volumePercent(level));backend_.volume(settings.volume);volumeUntil_=now_+1400;}
void Interface::adjustLimit(int delta){limitDraft_=volumePercent(volumeLevel(std::max(0,limitDraft_))+(delta>0?1:-1));backend_.volume(limitDraft_);}
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
    if(booting_||updating_||shuttingDown_)return;
    bool wake=blank_||dim_;lastInput_=now_;blank_=dim_=false;
    // Firmware dialog 0x1020, kind 2: full screen, closed/open resource pair.
    // Five timer callbacks at period 25; 1250 ms is a desktop tick interpretation.
    if(key==Key::Lock){cancelHeldActions();powerHeld_=false;locked=!locked;lockUntil_=now_+1250;noticeUntil_=volumeUntil_=0;return;}
    if(locked){lockUntil_=now_+1250;return;}
    lockUntil_=0;
    if(wake)return;
    booting_=false;
    if(frame_.page==Page::Confirm&&key!=Key::Select&&key!=Key::Back&&key!=Key::Home)return;
    noticeUntil_=0;
    if(key==Key::VolumeUp||key==Key::VolumeDown){int delta=key==Key::VolumeUp?1:-1;if(frame_.page==Page::VolumeLimit)adjustLimit(delta);else volume(delta);return;}
    if(frame_.page==Page::VolumeLimit&&(key==Key::Home||key==Key::Menu)){back();if(key==Key::Menu)return;}
    if(frame_.page==Page::CustomEQ&&key==Key::Back){if(frame_.selected>0)frame_.selected--;else back();return;}
    if(key==Key::Home){cancelHeldActions();if(frame_.page==Page::Recording){back();return;}if(frame_.page==Page::Confirm&&backend_.recording().active)back();auto previous=frame_.page;history_.clear();frame_.page=Page::Home;frame_.selected=frame_.scroll=0;selectedAt_=now_;refreshOnHome(previous);return;}
    if(key==Key::Back){back();return;}
    if(frame_.page==Page::Confirm){if(key==Key::Select)select();return;}
    if(key==Key::Menu){openOptions();return;}
    if(key==Key::Select){select();return;}
    int delta=(key==Key::Previous||key==Key::Up||key==Key::SeekBack)?-1:1;
    if(frame_.page==Page::Radio) {tune(delta,key==Key::SeekBack||key==Key::SeekForward);return;}
    if(frame_.page==Page::Playing) {
        if(!backend_.audio().error.empty()&&(key==Key::SeekBack||key==Key::SeekForward))return;
        if(key==Key::SeekBack||key==Key::SeekForward){auto info=backend_.audio();if(info.duration>0)backend_.seek(std::clamp(info.position+delta*5,0.0,std::max(0.0,info.duration-.05)));}
        else next(delta);
        return;
    }
    if(frame_.page==Page::CustomEQ) {
        if(key==Key::Previous||key==Key::Next)frame_.selected=(frame_.selected+delta+5)%5;
        else settings.bands[frame_.selected]=std::clamp(settings.bands[frame_.selected]-delta*2,-10.0,10.0);
        applySettings();return;
    }
    if(frame_.page==Page::VolumeLimit)return;
    move(delta);
}
void Interface::pressTransport(int direction) {
    pressKey(direction<0?Key::Up:Key::Down);
}
void Interface::releaseTransport() {
    for(auto key:{Key::Up,Key::Down})if(heldKeys_[size_t(key)].down&&!heldKeys_[size_t(key)].blocked){releaseKey(key);break;}
}
void Interface::cancelTransport() {
    for(auto key:{Key::Up,Key::Down})heldKeys_[size_t(key)].blocked=true;
}
void Interface::cancelHeldActions() {
    for(auto& held:heldKeys_)if(held.down)held.blocked=true;
}
void Interface::cancelKeys() {
    for(auto& held:heldKeys_)held={};
    releaseSlider();
}
void Interface::pressKey(Key key) {
    size_t index=size_t(key);if(index>=heldKeys_.size())return;
    auto& held=heldKeys_[index];if(held.down)return;
    held={true,false,false,now_+KeyHoldDelay,0};
    if(booting_||updating_||shuttingDown_){held.blocked=true;return;}
    if(key==Key::Lock){input(key);held.blocked=true;return;}
    if(locked||blank_||dim_){input(key);held.blocked=true;return;}
    lastInput_=now_;lockUntil_=0;
    if(frame_.page==Page::Confirm&&key!=Key::Select&&key!=Key::Back){held.blocked=true;return;}
    if(key==Key::Up||key==Key::Down)heldKeys_[size_t(key==Key::Up?Key::Down:Key::Up)].blocked=true;
    if(key!=Key::Up&&key!=Key::Down&&key!=Key::VolumeUp&&key!=Key::VolumeDown&&key!=Key::Back&&key!=Key::Select) {
        input(key);held.blocked=true;
    }
}
void Interface::releaseKey(Key key) {
    size_t index=size_t(key);if(index>=heldKeys_.size())return;
    auto held=heldKeys_[index];heldKeys_[index]={};
    if(held.down&&!held.blocked&&!held.longSent)input(key);
}
void Interface::tickHeldKeys() {
    for(size_t i=0;i<heldKeys_.size();i++) {
        auto& held=heldKeys_[i];if(!held.down||held.blocked||now_<held.next)continue;
        if(locked||blank_||dim_||booting_||updating_){held.blocked=true;continue;}
        Key key=Key(i);bool first=!held.longSent;held.longSent=true;lastInput_=now_;
        if(key==Key::Back) {
            held.blocked=true;
            input(frame_.page==Page::Recording||frame_.page==Page::Confirm?Key::Back:Key::Home);
        } else if(key==Key::Select) {
            held.blocked=true;if(frame_.page!=Page::CustomEQ)input(Key::Select);
        } else if(key==Key::VolumeUp||key==Key::VolumeDown) {
            input(key);if(first&&!held.blocked)input(key); // long-start and its first repeat
            held.next+=VolumeRepeatDelay;if(held.next<=now_)held.next=now_+VolumeRepeatDelay;
        } else if(key==Key::Up||key==Key::Down) {
            if(frame_.page==Page::Playing) {
                auto info=backend_.audio();unsigned count=++held.repeats;
                int step=count<10?4:count<20?5:count<40?10:20;
                if(info.error.empty()&&info.duration>0)backend_.seek(std::clamp(info.position+(key==Key::Up?-step:step),0.0,std::max(0.0,info.duration-.05)));
            } else input(key);
            held.next+=NavigationRepeatDelay;if(held.next<=now_)held.next=now_+NavigationRepeatDelay;
        }
    }
}
void Interface::moveSlider(SliderPosition position) {
    if(booting_||updating_||shuttingDown_)return;
    cancelHeldActions();
    if(position!=SliderPosition::Power)releaseSlider();
    bool lock=position==SliderPosition::Lock;
    if(locked!=lock)input(Key::Lock);
    lastInput_=now_;blank_=dim_=false;
    if(position==SliderPosition::Power&&!powerHeld_) {
        powerHeld_=true;powerSent_=false;powerPressedAt_=now_;
    }
}
void Interface::releaseSlider(){powerHeld_=false;powerSent_=false;}
bool Interface::takePowerOffRequest(){bool requested=powerOffRequested_;powerOffRequested_=false;return requested;}
void Interface::shutdown() {
    if(shuttingDown_)return;
    cancelKeys();shuttingDown_=true;shutdownAt_=now_;
}
bool Interface::shutdownFinished() const { return shuttingDown_&&now_-shutdownAt_>=shutdownDuration; }
void Interface::tick(uint64_t millis) {
    now_=millis;if(!firstTick_){firstTick_=now_?now_:1;lastInput_=selectedAt_=now_;}
    if(shuttingDown_)return;
    if(booting_&&now_>firstTick_+4400)booting_=false;
    if(updating_&&now_>=firstTick_+7400)updating_=false;
    dim_=settings.backlight>0&&now_-lastInput_>uint64_t(settings.backlight)*1000;
    blank_=settings.screenOff>0&&now_-lastInput_>uint64_t(settings.screenOff)*1000;
    tickHeldKeys();
    if(powerHeld_&&!powerSent_&&now_-powerPressedAt_>=PowerHoldDelay){powerSent_=true;powerOffRequested_=true;}
    auto info=backend_.audio();
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
void Interface::put(int x,int y,uint32_t color){if(x>=clip_[0]&&x<clip_[2]&&y>=clip_[1]&&y<clip_[3])screen[y*128+x]=0xff000000|color;}
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
void Interface::drawTitle(const std::wstring& value,uint32_t color) {
    // 0x159ede/0x159e28/0x159d3c: hold, one whole pass, restore prefix+ellipsis.
    // Original timer counts are retained; a 100 ms tick is a desktop choice.
    int width=textWidth(value,true);constexpr uint64_t step=100,hold=10*step;
    if(!titleStarted_||titleLabel_!=value){titleStarted_=true;titleLabel_=value;titleAt_=now_;titleDoneAt_=titleAt_+(width>118?hold+uint64_t(width+1)*step:0);metadataStarted_=false;}
    clip_={5,3,123,17};
    if(width>118&&now_>=titleDoneAt_) {
        int prefix=0;for(auto ch:value){auto* glyph=findGlyph(ch,true);int advance=glyph?int(glyph->w):6;if(prefix+advance>106)break;prefix+=advance;}
        text(5,3,value,color,true,5,5+prefix);image(3*256+21+settings.theme,5+prefix,3);
    } else {
        int shift=width>118&&now_>titleAt_+hold?int((now_-titleAt_-hold)/step):0;
        text(5-shift,3,value,color,true,5,123);
    }
    clip_={0,0,128,64};
}
void Interface::drawMetadata(const std::wstring& artist,const std::wstring& album,uint32_t color) {
    std::array<std::wstring,2> labels{artist,album};
    if(!metadataStarted_||labels!=metadataLabels_){metadataLabels_=labels;metadataAt_=now_;metadataStarted_=true;}
    std::array<int,2> widths{textWidth(artist),textWidth(album)},ys{0,14},shifts{};
    uint64_t start=std::max(metadataAt_,titleDoneAt_),elapsed=now_>start?now_-start:0;constexpr uint64_t step=100,hold=10*step,pause=4*step,slide=13*step;
    if(artist.empty()||album.empty()) {
        int only=artist.empty()?1:0;ys[only]=0;
        if(widths[only]>106&&elapsed<uint64_t(widths[only]-2)*step)shifts[only]=int(elapsed/step);
    } else {
        std::array<uint64_t,2> scroll{},duration{};
        for(int i=0;i<2;i++){scroll[i]=widths[i]>106?uint64_t(widths[i]-2)*step:0;duration[i]=hold+(scroll[i]?scroll[i]+pause:0)+slide;}
        uint64_t cycle=duration[0]+duration[1],local=elapsed%cycle;int active=local<duration[0]?0:1;
        if(active)local-=duration[0];
        ys[active]=(elapsed<duration[0]?0:1);ys[1-active]=14;
        if(local>=hold){local-=hold;if(scroll[active]&&local<scroll[active])shifts[active]=int(local/step);else {uint64_t beforeSlide=scroll[active]?scroll[active]+pause:0;if(local>=beforeSlide){int pixels=std::min(13,int((local-beforeSlide)/step));ys[active]-=pixels;ys[1-active]-=pixels;}}}
    }
    clip_={5,20,123,34};
    for(int i=0;i<2;i++)if(!labels[i].empty()){image(2*256+(i?18:21)+settings.theme,5,22+ys[i]);text(17-shifts[i],20+ys[i],labels[i],color,false,17,123);}
    clip_={0,0,128,64};
}
void Interface::drawVolume(bool editing) {
    beginDialog();int position=volumeLevel(editing?std::max(0,limitDraft_):settings.volume),limit=volumeLevel(settings.volumeLimit);
    for(int i=0;i<position;i++)if(i!=limit)image(4*256,17+i*3,26);
    for(int i=position+1;i<32;i++)if(i!=limit)image(4*256+1+settings.theme,17+i*3,26);
    image(4*256+4,14+position*3,21);image(4*256+8,17+limit*3,21);
    if(position)image(4*256+6,1,26);
    image(4*256+(position<(editing?31:limit)?7:5),115,26);
    if(editing)center(51,tr(131),settings.theme==2?firmwareColor(0xffead3fc):0xffffff);
}
void Interface::beginDialog() {clip_={0,0,128,64};screen.fill(0xff000000|background());}
void Interface::drawMessage(const std::vector<std::pair<int,std::wstring>>& rows,uint32_t color) {
    // All text dialogs use the full bounds of firmware class 0x1020.
    // Reset the clip too: a previous marquee/metadata viewport must not survive.
    beginDialog();
    // Property 0x109=1 centers the 12-pixel font inside each 14-pixel row.
    for(const auto& row:rows){clip_={4,std::max(0,row.first),124,std::min(64,row.first+14)};if(textWidth(row.second)>120)marquee(4,row.first+1,row.second,color,120);else center(row.first+1,row.second,color);}
    clip_={0,0,128,64};
}
void Interface::status(const TrackInfo& info) {
    auto seeking=[this](Key key){const auto& h=heldKeys_[size_t(key)];return h.down&&!h.blocked&&h.repeats>0;};
    image(2*256+(seeking(Key::Up)?1:seeking(Key::Down)?3:info.paused?4:info.playing?5:2),5,35);
    auto padded=[](size_t value){auto s=std::to_wstring(value);return std::wstring(s.size()<4?4-s.size():0,L'0')+s;};
    number(5,55,padded(queueIndex_>=0?size_t(queueIndex_+1):0),0xffffff,true,2,20);number(25,55,L"/",0xffffff,true);number(31,55,padded(queue_.size()),0xffffff,true,0,20);
    if(settings.repeat)image(2*256+(settings.repeat==2?9:12)+settings.theme,55,52);
    if(settings.shuffle)image(2*256+15+settings.theme,70,52);
    if(settings.equalizer)image(2*256+6+settings.theme,85,52);
    image(1*256+4,107,51);
}
void Interface::render() {
    clip_={0,0,128,64};
    screen.fill(0xff000000|background());auto info=backend_.audio();
    const uint32_t primary[]={0xffcec2bc,0xfff8d6c2,0xffead3fc},selectedColors[]={0xff000000,0xff9c613e,0xff9583d8};
    uint32_t fg=firmwareColor(settings.theme==2?primary[2]:0xffffffff),selectedColor=firmwareColor(selectedColors[settings.theme]);
    if(shuttingDown_){screen.fill(0xff000000);uint64_t elapsed=now_-shutdownAt_;if(elapsed<shutdownHold)center(25,tr(229),0xffffff,true);else image(8*256+unsigned(std::min(uint64_t(7),(elapsed-shutdownHold)/shutdownFrame)),15,9);return;}
    if(blank_) {screen.fill(0xff000000);return;}
    if(booting_){image(7*256+std::min(20,int((now_-firstTick_)/170)),0,0);return;}
    if(updating_) {
        int phase=int(std::clamp((double(now_)-double(firstTick_)-4400)*10/2850,0.0,10.0));
        int percent=phase<2?0:phase==9?80:phase*10;
        screen.fill(0xff000000);image(5*256+unsigned((now_-firstTick_-4400)/90%8),57,8);center(30,tr(228),0xffffff);center(45,std::to_wstring(percent)+L"%",0xffffff);image(7*256+24+phase,5,59);return;
    }
    if(frame_.page==Page::Home) {
        int family=homeFamilies[std::clamp(frame_.selected,0,int(homeFamilies.size())-1)],base=9+settings.theme*36,step=std::min(5,int((now_-selectedAt_)/50));
        image(settings.theme,9,8);image(base+family*6+step,48,0);if(frame_.selected<5)image(6+settings.theme,5,4+frame_.selected*11);image(1*256+4,110,2);
        auto title=tr(family==5&&!info.playing?13:7+family);
        int x=28+std::max(0,(90-textWidth(title,true))/2);marquee(x,48,title,firmwareColor(primary[settings.theme]),118-x,true);
    } else if(frame_.page==Page::Playing) {
        uint32_t labelColor=firmwareColor(primary[settings.theme]);drawTitle(currentTitle(),labelColor);
        auto artist=info.artist,album=info.album;auto found=queueIndex_>=0?songLookup_.find(queue_[queueIndex_]):songLookup_.end();
        if(found!=songLookup_.end()){if(artist.empty())artist=library[found->second].tags.artist;if(album.empty())album=library[found->second].tags.album;}
        drawMetadata(artist,album,labelColor);
        number(18,37,timeText(info.position),0xffffff);number(83,37,timeText(info.duration),0xffffff,false,2,40);
        const uint32_t progressColors[]={0xff2d2d2d,0xffa87455,0xffa390e9};rect(5,48,118,2,firmwareColor(progressColors[settings.theme]));if(info.duration>0)rect(5,48,118*int(100*std::clamp(info.position/info.duration,0.0,1.0))/100,2,0xffffff);
        status(info);
        if(!info.error.empty()){
            if(settings.language==21)drawMessage({{6,tr(101)},{25,tr(102)},{44,tr(103)}},fg);
            else drawMessage({{12,tr(101)},{40,tr(102)}},fg);
        }
    } else if(frame_.page==Page::Radio) {
        auto color=settings.theme==2?firmwareColor(primary[2]):0xffffff;
        image(2*256+24+settings.theme,5,0);image(2*256+27+settings.theme,10,55-((frequency_-875)*100/205)*55/100);image(1*256+4,107,51);
        std::wostringstream frequency;frequency<<std::fixed<<std::setprecision(1)<<frequency_/10.0;number(26,20,frequency.str(),color,2,1,75);text(106,25,L"FM",color,true);
        std::wstring name=radioCandidate_>=0?stations[radioCandidate_].name:radioCandidate_==-2?tr(61):L"--";
        if(info.error.empty())marquee(26,47,name,color,75);
        else drawMessage({{25,L"Unavailable"}},fg);
    } else if(frame_.page==Page::Recording) {
        auto record=backend_.recording();marquee(5,13,record.path.filename().wstring(),firmwareColor(primary[settings.theme]),118,true);
        image(2*256+(record.paused?4:0),5,35);number(18,37,timeText(record.seconds),0xffffff);
        std::error_code error;auto space=fs::space(voice_,error);double remaining=error?14400:std::min(14400.0,space.available/44100.0);
        number(73,37,timeText(remaining),0xffffff,false,2,50);
        const uint32_t progressColors[]={0xff2d2d2d,0xffa87455,0xffa390e9};rect(5,48,118,2,firmwareColor(progressColors[settings.theme]));rect(5,48,int(118*record.seconds/std::max(1.0,record.seconds+remaining)),2,0xffffff);
        image(3*256+28,5,52);image(1*256+4,107,51);
        if(!record.error.empty())drawMessage({{12,L"Microphone"},{40,L"unavailable"}},fg);
    } else if(frame_.page==Page::CustomEQ) {
        image(6*256+settings.theme,0,0);
        image(6*256+4+settings.theme,12+frame_.selected*22,0);
        for(int i=0;i<5;i++){int bars=std::abs(int(settings.bands[i]/2)),sign=settings.bands[i]<0?-1:1;for(int k=0;k<=bars;k++)image(6*256+3,15+i*22,27-sign*k*4);}
        image(6*256+7,12+frame_.selected*22,25-int(settings.bands[frame_.selected]/2)*4);
    } else if(frame_.page==Page::VolumeLimit) {
        drawVolume(true);
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
        // Dialog 0x1020 kind 1: centered prompt rows and fixed key captions.
        drawMessage(promptRows_,fg);
        bool wide=settings.language==18||settings.language==19||settings.language==20||settings.language==23;
        int split=wide?66:86;
        clip_={2,49,split-1,63};text(2,50,tr(restorePrompt_?203:201),0xffffff);
        clip_={split,49,126,63};auto cancel=tr(202);text(126-textWidth(cancel),50,cancel,0xffffff);
        clip_={0,0,128,64};
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
            beginDialog();
            if(scanning){image(5*256+unsigned((now_/90)%8),57,8);center(30,tr(228),fg);}
            else {center(19,frame_.page==Page::Recorded?tr(82):tr(99),fg);if(frame_.page==Page::Recorded)center(34,tr(83),fg);}
        }
        if(rows.size()>3)image(3*256+25+settings.theme,126,frame_.selected*39/std::max(1,int(rows.size())-1));
    }
    if(now_<volumeUntil_&&frame_.page!=Page::VolumeLimit)drawVolume(false);
    if(now_<noticeUntil_) {
        std::vector<std::wstring> lines;std::wstring line;std::wistringstream input(notice_);
        while(std::getline(input,line))lines.push_back(line);
        std::vector<std::pair<int,std::wstring>> rows;for(size_t i=0;i<std::min(size_t(3),lines.size());i++)rows.emplace_back(lines.size()==1?25:lines.size()==2?(i?40:12):6+int(i)*19,lines[i]);drawMessage(rows,fg);
    }
    if(now_<lockUntil_){beginDialog();image(3*256+(locked?15:18)+settings.theme,52,11);}
    if(dim_)for(auto& p:screen)p=0xff000000|((p&0x00fefefe)>>1);
    if(settings.rotation)std::reverse(screen.begin(),screen.end());
}
void Interface::click(int x,int y) {
    if(booting_||updating_||shuttingDown_)return;
    if(locked){input(Key::Select);return;}
    if(blank_||dim_){input(Key::Select);return;}
    if(now_<lockUntil_){lockUntil_=0;lastInput_=now_;return;}
    lockUntil_=0;
    if(now_<noticeUntil_){noticeUntil_=0;lastInput_=now_;return;}
    if(now_<volumeUntil_&&frame_.page!=Page::VolumeLimit){volumeUntil_=0;lastInput_=now_;return;}
    if(((frame_.page==Page::Playing||frame_.page==Page::Radio)&&!backend_.audio().error.empty())||(frame_.page==Page::Recording&&!backend_.recording().error.empty()))return;
    if(settings.rotation){x=127-x;y=63-y;}
    if(frame_.page==Page::Home){input(Key::Select);return;}
    if(frame_.page==Page::Playing){if(y>=46&&y<52){auto info=backend_.audio();if(info.duration>0){lastInput_=now_;backend_.seek(std::clamp((x-5)/118.0,0.0,1.0)*std::max(0.0,info.duration-.05));}}else input(Key::Select);return;}
    if(frame_.page==Page::Radio){if(x<23){frequency_=875+int(std::clamp((59-y)/55.0,0.0,1.0)*205);tuneAt_=now_+350;radioCandidate_=-2;backend_.stop();lastInput_=now_;}else input(Key::Select);return;}
    if(frame_.page==Page::Recording||frame_.page==Page::VolumeLimit||frame_.page==Page::CustomEQ){input(Key::Select);return;}
    if(frame_.page==Page::Confirm){if(y>=49&&y<63)input(x<(settings.language==18||settings.language==19||settings.language==20||settings.language==23?66:86)?Key::Select:Key::Back);return;}
    if(frame_.page==Page::USBConnected){input(Key::Down);return;}
    if(frame_.page==Page::USB){if(y>=20&&y<56){frame_.selected=(y-20)/18;input(Key::Select);}return;}
    int index=frame_.selected+(y<25?-1:y<46?0:1);
    auto rows=items();if(index>=0&&index<int(rows.size())){frame_.selected=index;input(Key::Select);}
}
}
