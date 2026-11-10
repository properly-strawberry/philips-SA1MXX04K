#pragma once
#include "audio.h"
#include <array>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace player {
namespace fs=std::filesystem;
struct Song { fs::path path; TrackInfo tags; };
struct Station { int tenths=0; std::wstring name,url; };
struct Recording { bool active=false,paused=false; double seconds=0,peak=0; fs::path path; std::wstring error; };
struct Settings {
    int theme=0,language=2,volume=70,volumeLimit=100,repeat=0,shuffle=0,equalizer=0;
    int backlight=0,screenOff=0,rotation=0;
    std::array<double,5> bands{};
};
class Backend {
public:
    virtual ~Backend()=default;
    virtual TrackInfo audio()=0;
    virtual void play(const std::wstring& source)=0;
    virtual void pause()=0;
    virtual void stop()=0;
    virtual void seek(double seconds)=0;
    virtual void volume(int value)=0;
    virtual void equalizer(const std::array<double,5>& gains)=0;
    virtual Recording recording()=0;
    virtual void recordStart(const fs::path& directory)=0;
    virtual void recordPause(bool value)=0;
    virtual void recordFinish(bool keep)=0;
    virtual void rescan()=0;
};
enum class Page { Home,Music,Artists,Albums,Group,Songs,Folders,Playing,Options,
    Settings,Theme,PlayMode,Repeat,Shuffle,Sound,Equalizer,CustomEQ,VolumeLimit,Display,
    Backlight,ScreenOff,Rotation,Language,Information,Legal,Confirm,Message,Radio,
    RadioOptions,Presets,Recordings,Recording,Recorded,Playlists,Books,Bookmarks,
    USB,USBConnected,PlaylistRemove };
enum class Key { Previous,Next,Up,Down,Select,Back,Home,Menu,VolumeUp,VolumeDown,
    SeekBack,SeekForward,Lock };
struct Frame { Page page; int selected=0,scroll=0; std::wstring group; int groupMode=0; fs::path directory; };

// The same interface/state machine is compiled into Windows and native tests.
// Resources are original; behavior here adapts firmware menus to desktop I/O.
class Interface {
public:
    Interface(Backend& backend,fs::path music);
    Settings settings;
    std::vector<fs::path> playlist;
    std::vector<std::pair<fs::path,double>> bookmarks;
    std::vector<Station> stations;
    std::vector<Song> library;
    fs::path lastPlayed;
    double resumePosition=0;
    bool scanning=true,locked=false,radio=false;
    std::array<uint32_t,128*64> screen{};
    void setLibrary(std::vector<Song> songs);
    void input(Key key);
    void tick(uint64_t millis);
    void render();
    void click(int x,int y);
    void pressTransport(int direction);
    void releaseTransport();
    void cancelTransport(){transportDirection_=0;}
    void applySettings();
    void remember();
    void loadStations(const fs::path& file);
    Page page() const { return frame_.page; }
    int selection() const { return frame_.selected; }
    int queueIndex() const { return queueIndex_; }
    bool booting() const { return booting_; }
    bool updating() const { return updating_; }
    void skipBoot() { booting_=false; updating_=false; }
    void shutdown();
    bool shutdownFinished() const;
    void navigate(Page page);
    std::wstring currentTitle();
    std::vector<std::wstring> labels();
private:
    struct Item { std::wstring label; std::function<void()> action; bool checked=false; bool folder=false; };
    Backend& backend_;
    fs::path music_,voice_;
    Frame frame_{Page::Home,0,0,L"",0,{}};
    std::vector<Frame> history_;
    std::vector<fs::path> queue_;
    std::map<fs::path,size_t> songLookup_;
    fs::path cachedDirectory_;
    std::vector<std::pair<fs::path,bool>> cachedFiles_;
    int queueIndex_=-1,frequency_=890,radioCandidate_=-2;
    uint64_t now_=0,firstTick_=0,selectedAt_=0,lastInput_=0,noticeUntil_=0,tuneAt_=0,volumeUntil_=0;
    bool booting_=true,finishedHandled_=false,dim_=false,blank_=false;
    bool shuttingDown_=false;
    uint64_t shutdownAt_=0;
    uint64_t lockUntil_=0;
    bool updating_=true;
    int transportDirection_=0,transportRepeats_=0;
    uint64_t transportPressed_=0,transportTick_=0;
    std::array<int,4> clip_{0,0,128,64};
    std::array<std::wstring,2> metadataLabels_;
    uint64_t metadataAt_=0;
    bool metadataStarted_=false;
    std::wstring titleLabel_;
    uint64_t titleAt_=0,titleDoneAt_=0;
    bool titleStarted_=false;
    int limitDraft_=-1,volumeBeforeLimit_=0;
    double pendingResume_=-1;
    std::wstring notice_;
    std::vector<std::pair<int,std::wstring>> promptRows_;
    bool restorePrompt_=false;
    fs::path optionPath_;
    std::function<void()> confirm_;
    std::vector<Item> items();
    std::wstring tr(int id) const;
    std::vector<fs::path> filteredSongs(int mode,const std::wstring& group=L"");
    void select();
    void back();
    void move(int delta);
    void playQueue(std::vector<fs::path> paths,int index);
    void next(int direction,bool automatic=false);
    void volume(int delta);
    void adjustLimit(int delta);
    void drawVolume(bool editing);
    void drawMetadata(const std::wstring& artist,const std::wstring& album,uint32_t color);
    void drawTitle(const std::wstring& value,uint32_t color);
    void tune(int delta,bool preset=false);
    void notice(std::wstring value,int milliseconds=1600);
    void confirm(std::wstring value,std::function<void()> yes,bool restore=false);
    void confirmRows(std::vector<std::pair<int,std::wstring>> rows,std::function<void()> yes,bool restore=false);
    void playlistAction(const fs::path& path,bool remove);
    void openOptions();
    void put(int x,int y,uint32_t color);
    void rect(int x,int y,int w,int h,uint32_t color);
    void image(unsigned id,int x,int y,int sourceWidth=-1,int sourceHeight=-1);
    int textWidth(const std::wstring& value,bool large=false) const;
    void text(int x,int y,const std::wstring& value,uint32_t color,bool large=false,int left=0,int right=128);
    void number(int x,int y,const std::wstring& value,uint32_t color,int font=0,int align=0,int width=0);
    void center(int y,const std::wstring& value,uint32_t color,bool large=false);
    void marquee(int x,int y,const std::wstring& value,uint32_t color,int width,bool large=false);
    void status(const TrackInfo& info);
    uint32_t background() const;
};
}
