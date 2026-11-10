#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlwapi.h>
#include "audio.h"
#include "interface.h"
#include "controls.h"
#include "recorder.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <memory>
#include <cmath>

namespace fs=std::filesystem;
using namespace Gdiplus;
using player::Key;
using player::Page;
using player::SliderPosition;
constexpr int W=920,H=412;
constexpr UINT ScanDone=WM_APP+1;
HWND window;
fs::path appDir,musicDir,layoutDir;
std::unique_ptr<Audio> audio;
Recorder recorder;
std::unique_ptr<player::Interface> ui;
std::atomic<bool> scanStop{false},rescanRequested{false};
std::thread scanner;
std::mutex libraryMutex;
std::vector<player::Song> scanResult;
std::array<uint32_t,128*64> screen;
ULONGLONG started=0,pressedAt=0;
int pressed=-1;
bool closing=false;
std::array<bool,256> keyboardHeld{};
std::array<Key,256> keyboardAction{};
bool sliderDragging=false,powerKeyDown=false,releasingCapture=false;
player::SliderMotion sliderMotion;
// Dragging the frameless player's corner mark: where the pointer and the window were when it began.
bool resizing=false;POINT resizeFrom{};RECT resizeStart{};
std::wstring screenshotPath,smokeReport;
int smokeStep=-1;
std::vector<std::string> smokeChecks;

class DesktopBackend:public player::Backend {
public:
    TrackInfo audio()override{return ::audio->snapshot();}
    void play(const std::wstring& source)override{::audio->play(source);}
    void pause()override{::audio->pause();}
    void stop()override{::audio->stop();}
    void seek(double value)override{::audio->seek(value);}
    void volume(int value)override{::audio->volume(value);}
    void equalizer(const std::array<double,5>& value)override{::audio->equalizer(value);}
    player::Recording recording()override{return recorder.snapshot();}
    void recordStart(const fs::path& path)override{recorder.start(path);}
    void recordPause(bool value)override{recorder.pause(value);}
    void recordFinish(bool keep)override{recorder.finish(keep);}
    void rescan()override{rescanRequested=true;}
} backend;

void beginScan() {
    scanStop=true;if(scanner.joinable())scanner.join();scanStop=false;ui->scanning=true;
    scanner=std::thread([] {
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
        std::vector<player::Song> found;
        for(auto root:{musicDir,layoutDir/L"Recordings"/L"Voice"}) {
            std::error_code ec;
            fs::recursive_directory_iterator it(root,fs::directory_options::skip_permission_denied,ec),end;
            while(it!=end&&!scanStop) {
                auto attributes=GetFileAttributesW(it->path().c_str());std::error_code statusError;
                if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM))) {
                    if(it->is_directory(statusError))it.disable_recursion_pending();
                } else if(it->is_regular_file(statusError)) {
                    auto extension=it->path().extension().wstring();std::transform(extension.begin(),extension.end(),extension.begin(),[](wchar_t c){return towlower(c);});
                    const std::wstring types=L"|.mp3|.flac|.wav|.ogg|.opus|.m4a|.aac|.wma|.aif|.aiff|.ape|.wv|.alac|.m4b|";
                    if(types.find(L"|"+extension+L"|")!=std::wstring::npos)found.push_back({it->path(),Audio::probe(it->path().wstring(),[]{return scanStop.load();})});
                }
                it.increment(ec);if(ec)ec.clear();
            }
        }
        std::sort(found.begin(),found.end(),[](const player::Song& a,const player::Song& b){return StrCmpLogicalW(a.path.c_str(),b.path.c_str())<0;});
        {std::lock_guard<std::mutex> lock(libraryMutex);scanResult=std::move(found);}
        if(!scanStop)PostMessageW(window,ScanDone,0,0);
    });
}
Key bodyKey(int button) {
    return player::bodyButtonKeys[size_t(button)];
}
bool keyHeld(Key key) {
    for(size_t i=0;i<keyboardHeld.size();i++)if(keyboardHeld[i]&&keyboardAction[i]==key)return true;
    return pressed>=0&&pressed!=2&&bodyKey(pressed)==key;
}
void pressKeyboard(WPARAM code,Key key) {
    if(code>=keyboardHeld.size()||keyboardHeld[code])return;
    bool alreadyHeld=keyHeld(key);keyboardHeld[code]=true;keyboardAction[code]=key;
    if(!alreadyHeld)ui->pressKey(key);
}
void releaseKeyboard(WPARAM code) {
    if(code>=keyboardHeld.size()||!keyboardHeld[code])return;
    Key key=keyboardAction[code];keyboardHeld[code]=false;
    if(!keyHeld(key))ui->releaseKey(key);
}
void releaseMouseCapture(HWND hwnd) {
    if(GetCapture()==hwnd){releasingCapture=true;ReleaseCapture();releasingCapture=false;}
}
void cancelHeldControls(HWND hwnd,bool releaseCapture) {
    keyboardHeld.fill(false);powerKeyDown=false;sliderDragging=false;pressed=-1;resizing=false;
    ui->cancelKeys();ui->releaseSlider();
    sliderMotion.release();
    if(releaseCapture)releaseMouseCapture(hwnd);
}
void renderScreen(){ui->render();screen=ui->screen;}
std::wstring trackTitle(const TrackInfo&){return ui->currentTitle();}
void label(Graphics& g,float x,float y,float w,float h,const wchar_t* value,float size,Color color,bool bold=false) {
    Font font(L"Segoe UI",size,bold?FontStyleBold:FontStyleRegular,UnitPixel);
    SolidBrush brush(color);StringFormat format;format.SetAlignment(StringAlignmentCenter);format.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(value,-1,&font,RectF(x,y,w,h),&format,&brush);
}
// The shell is assets/shell/shell.svg, prerendered by tools/render_shell.py and
// embedded as PNGs (src/player.rc). These rectangles are that drawing's, in the
// window's logical pixels: the click areas, and where each prerendered patch goes.
struct Button { int x,y,w,h; const wchar_t* hint; };
const Button buttons[]={
    {700,92,96,68,L"Up / previous / hold to seek"},{622,160,68,96,L"Back"},{635,27,149,24,L"Lock / unlock / hold power"},
    {806,160,68,96,L"Play / pause / select"},{700,256,96,68,L"Down / next / hold to seek"},
    {181,27,85,24,L"Volume down"},{350,27,85,24,L"Volume up"}
};
static_assert(std::size(buttons)==player::bodyButtonCount);
const RectF rockerPatch(175,17,266,44),padPatch(612,68,274,280),handlePatch(655,23,110,34);
const Rect lcdArea(208,112,384,192);
int buttonAt(float x,float y) { for(size_t i=0;i<std::size(buttons);i++){auto b=buttons[i];if(x>=b.x&&x<b.x+b.w&&y>=b.y&&y<b.y+b.h)return int(i);}return -1; }
PointF playerPoint(HWND hwnd,LPARAM point) {
    RECT r;GetClientRect(hwnd,&r);float scale=std::max(.001f,std::min(float(r.right)/W,float(r.bottom)/H));
    return PointF((GET_X_LPARAM(point)-(r.right-W*scale)/2)/scale,(GET_Y_LPARAM(point)-(r.bottom-H*scale)/2)/scale);
}
float sliderPointer(float x) {
    auto b=buttons[2];float handleWidth=(b.w-4)*2/3.0f,travel=b.w-4-handleWidth;
    return (x-b.x-2-handleWidth/2)/travel;
}
bool sliderHandleAt(float x) {
    auto b=buttons[2];float handleWidth=(b.w-4)*2/3.0f,travel=b.w-4-handleWidth;
    float left=b.x+2+sliderMotion.position()*travel;
    return x>=left&&x<left+handleWidth;
}
// pad: up, down, back, play. backdrop: the resting shell scaled to the window, redrawn when its size changes.
struct ShellArt { std::unique_ptr<Bitmap> base,rockerMinus,rockerPlus,handle,pad[4],backdrop; } art;
std::unique_ptr<Bitmap> loadArt(int id) {
    HRSRC found=FindResourceW(nullptr,MAKEINTRESOURCEW(id),RT_RCDATA);
    HGLOBAL loaded=found?LoadResource(nullptr,found):nullptr;
    const void* bytes=loaded?LockResource(loaded):nullptr;DWORD size=found?SizeofResource(nullptr,found):0;
    HGLOBAL copy=bytes&&size?GlobalAlloc(GMEM_MOVEABLE,size):nullptr;
    if(!copy)return nullptr;
    memcpy(GlobalLock(copy),bytes,size);GlobalUnlock(copy);
    IStream* stream=nullptr;
    if(CreateStreamOnHGlobal(copy,TRUE,&stream)!=S_OK){GlobalFree(copy);return nullptr;}
    // Redraw into a bitmap of our own, so nothing depends on the stream afterwards.
    std::unique_ptr<Bitmap> result;
    {
        Bitmap decoded(stream);
        if(decoded.GetLastStatus()==Ok&&decoded.GetWidth()>0) {
            result=std::make_unique<Bitmap>(int(decoded.GetWidth()),int(decoded.GetHeight()),PixelFormat32bppPARGB);
            Graphics copyTo(result.get());copyTo.SetCompositingMode(CompositingModeSourceCopy);
            copyTo.DrawImage(&decoded,0,0,int(decoded.GetWidth()),int(decoded.GetHeight()));
        }
    }
    stream->Release();
    return result;
}
bool loadShell() {
    art.base=loadArt(101);art.rockerMinus=loadArt(102);art.rockerPlus=loadArt(103);art.handle=loadArt(108);
    for(int i=0;i<4;i++)art.pad[i]=loadArt(104+i);
    return art.base&&art.rockerMinus&&art.rockerPlus&&art.handle&&art.pad[0]&&art.pad[1]&&art.pad[2]&&art.pad[3];
}
// Draws a whole prerendered picture into a logical rectangle.
void drawArt(Graphics& g,Bitmap* image,RectF where) {
    ImageAttributes edges;edges.SetWrapMode(WrapModeTileFlipXY);
    g.DrawImage(image,where,0,0,REAL(image->GetWidth()),REAL(image->GetHeight()),UnitPixel,&edges);
}
// By default the player floats on the desktop without a window frame; a right
// click swaps that for an ordinary window with the status lines, and back.
bool borderless=true;
// A 32-bit picture the size of the window that both GDI and GDI+ can reach.
struct Canvas {
    HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;void* bits=nullptr;int w=0,h=0;
    void ensure(int width,int height) {
        if(width==w&&height==h&&bitmap)return;
        release();
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        dc=CreateCompatibleDC(nullptr);bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
        previous=SelectObject(dc,bitmap);w=width;h=height;
    }
    void release() {
        if(dc){SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);}
        dc=nullptr;bitmap=nullptr;bits=nullptr;w=h=0;
    }
} playerCanvas,windowCanvas;
const Rect gripArea(872,336,24,24);   // resize corner of the frameless player, tucked beside the shell's lower right corner
// Draws the player alone, on a transparent ground, into playerCanvas.
void drawPlayer(int width,int height,bool grip) {
    playerCanvas.ensure(width,height);
    Bitmap target(width,height,width*4,PixelFormat32bppPARGB,static_cast<BYTE*>(playerCanvas.bits));
    Graphics g(&target);
    // The resting shell is scaled once per window size; only the parts that move are drawn each frame.
    auto& backdrop=art.backdrop;
    float scale=std::min(float(width)/W,float(height)/H),left=(width-W*scale)/2,top=(height-H*scale)/2;
    if(!backdrop||int(backdrop->GetWidth())!=width||int(backdrop->GetHeight())!=height) {
        backdrop=std::make_unique<Bitmap>(width,height,PixelFormat32bppPARGB);
        Graphics b(backdrop.get());b.Clear(Color(0,0,0,0));
        b.SetInterpolationMode(InterpolationModeHighQualityBicubic);b.SetPixelOffsetMode(PixelOffsetModeHalf);
        drawArt(b,art.base.get(),RectF(left,top,W*scale,H*scale));
    }
    g.SetCompositingMode(CompositingModeSourceCopy);g.DrawImage(backdrop.get(),0,0,width,height);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.TranslateTransform(left,top);g.ScaleTransform(scale,scale);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);g.SetPixelOffsetMode(PixelOffsetModeHalf);
    // Each patch has a margin identical to the resting picture; replacing the pixels inside it leaves no seam.
    auto patch=[&](Bitmap* image,RectF where) {
        auto saved=g.Save();g.SetClip(RectF(where.X+3,where.Y+3,where.Width-6,where.Height-6));
        drawArt(g,image,where);g.Restore(saved);
    };
    bool minus=keyHeld(Key::VolumeDown),plus=keyHeld(Key::VolumeUp);
    if(minus!=plus)patch((minus?art.rockerMinus:art.rockerPlus).get(),rockerPatch);
    static constexpr int padFor[]={0,2,-1,3,1};   // body button -> up, back, (slider), play, down
    if(pressed>=0&&pressed<=4&&padFor[pressed]>=0)patch(art.pad[padFor[pressed]].get(),padPatch);
    g.SetCompositingMode(CompositingModeSourceOver);
    auto groove=buttons[2];float travel=(groove.w-4)/3.0f;
    drawArt(g,art.handle.get(),RectF(handlePatch.X+(sliderMotion.position()-.5f)*travel,handlePatch.Y,handlePatch.Width,handlePatch.Height));
    renderScreen();
    float zoom=lcdArea.Width*scale/128;
    if(zoom>=1&&std::abs(zoom-std::round(zoom))<.002f) {
        // A whole number of window pixels per screen pixel: keep them square and sharp.
        Bitmap bitmap(128,64,128*4,PixelFormat32bppARGB,reinterpret_cast<BYTE*>(screen.data()));
        g.SetInterpolationMode(InterpolationModeNearestNeighbor);
        g.DrawImage(&bitmap,lcdArea,0,0,128,64,UnitPixel);
    } else {
        // Otherwise enlarge the screen 4x first and scale that down smoothly, so
        // its pixels stay even instead of some coming out wider than others.
        static std::array<uint32_t,512*256> large;
        for(int y=0;y<256;y++)for(int x=0;x<512;x++)large[size_t(y)*512+x]=screen[size_t(y/4)*128+x/4];
        Bitmap bitmap(512,256,512*4,PixelFormat32bppARGB,reinterpret_cast<BYTE*>(large.data()));
        ImageAttributes edges;edges.SetWrapMode(WrapModeTileFlipXY);
        g.DrawImage(&bitmap,lcdArea,0,0,512,256,UnitPixel,&edges);
    }
    g.SetPixelOffsetMode(PixelOffsetModeDefault);
    if(grip) {
        // A faint corner to drag. Its backing is all but invisible, yet enough for the corner to take clicks.
        SolidBrush backing(Color(3,0,0,0));g.FillRectangle(&backing,gripArea);
        Pen line(Color(170,160,172,186),1.6f);float right=float(gripArea.X+gripArea.Width-3),bottom=float(gripArea.Y+gripArea.Height-3);
        for(int i=1;i<=3;i++)g.DrawLine(&line,right-i*5,bottom,right,bottom-i*5);
    }
}
// The framed window: the player on the dark ground, with the two status lines.
void composeWindow(int width,int height) {
    drawPlayer(width,height,false);
    windowCanvas.ensure(width,height);
    Bitmap target(width,height,width*4,PixelFormat32bppRGB,static_cast<BYTE*>(windowCanvas.bits));
    Bitmap player(width,height,width*4,PixelFormat32bppPARGB,static_cast<BYTE*>(playerCanvas.bits));
    Graphics g(&target);g.Clear(Color(255,24,27,33));g.DrawImage(&player,0,0,width,height);
    float scale=std::min(float(width)/W,float(height)/H);
    g.TranslateTransform((width-W*scale)/2,(height-H*scale)/2);g.ScaleTransform(scale,scale);
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);
    auto info=audio->snapshot();
    std::wstring status=info.playing?(info.paused?L"Paused":L"Playing"):L"Music lives beside philips.exe";
    if(!info.error.empty())status=info.error;
    else if(info.playing)status+=L"  ·  "+trackTitle(info);
    label(g,40,H-44,W-80,19,status.c_str(),12,Color(255,170,183,197));
    label(g,40,H-24,W-80,17,L"Up/down: browse / hold to seek   ·   Left: back   ·   Right: play/select   ·   M: options   ·   + / −: volume   ·   P: hold power   ·   Right click: frameless",11,Color(255,111,126,143));
}
void paint(HDC dc,int width,int height) {
    composeWindow(width,height);
    BitBlt(dc,0,0,width,height,windowCanvas.dc,0,0,SRCCOPY);
}
// The frameless player is a layered window: Windows blends the picture's own transparency with the desktop.
void presentFloating(HWND hwnd) {
    RECT r;GetWindowRect(hwnd,&r);int width=r.right-r.left,height=r.bottom-r.top;
    if(width<1||height<1||IsIconic(hwnd))return;
    drawPlayer(width,height,true);
    POINT origin{0,0};SIZE size{width,height};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd,nullptr,nullptr,&size,playerCanvas.dc,&origin,0,&blend,ULW_ALPHA);
}
void refresh(HWND hwnd) { if(borderless)presentFloating(hwnd);else InvalidateRect(hwnd,nullptr,FALSE); }
constexpr DWORD floatingStyle=WS_POPUP|WS_SYSMENU|WS_MINIMIZEBOX,framedStyle=WS_OVERLAPPEDWINDOW;
// Swaps between the two kinds of window. The player keeps its size and its
// place on the desktop; the window around it is what changes.
void setBorderless(HWND hwnd,bool on) {
    if(IsZoomed(hwnd))ShowWindow(hwnd,SW_RESTORE);
    // Where the picture of the player is now, in screen coordinates.
    RECT client;GetClientRect(hwnd,&client);POINT corner{0,0};ClientToScreen(hwnd,&corner);
    float scale=std::min(float(client.right)/W,float(client.bottom)/H);
    int width=std::max(1,int(std::lround(W*scale))),height=std::max(1,int(std::lround(H*scale)));
    RECT want{corner.x+(client.right-width)/2,corner.y+(client.bottom-height)/2,0,0};
    want.right=want.left+width;want.bottom=want.top+height;
    borderless=on;DWORD style=on?floatingStyle:framedStyle,extended=on?WS_EX_LAYERED:0;
    SetWindowLongPtrW(hwnd,GWL_STYLE,style|WS_VISIBLE);SetWindowLongPtrW(hwnd,GWL_EXSTYLE,extended);
    RECT outer=want;AdjustWindowRectEx(&outer,style,FALSE,extended);
    for(int pass=0;pass<2;pass++) {
        SetWindowPos(hwnd,nullptr,outer.left,outer.top,outer.right-outer.left,outer.bottom-outer.top,SWP_NOZORDER|SWP_FRAMECHANGED);
        // Trust what the window actually became, not the prediction: correct any difference once.
        RECT got;GetClientRect(hwnd,&got);POINT at{0,0};ClientToScreen(hwnd,&at);
        if(at.x==want.left&&at.y==want.top&&got.right==width&&got.bottom==height)break;
        outer.left+=want.left-at.x;outer.top+=want.top-at.y;
        outer.right+=want.left-at.x+(width-got.right);outer.bottom+=want.top-at.y+(height-got.bottom);
    }
    if(on)presentFloating(hwnd);else RedrawWindow(hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_FRAME|RDW_UPDATENOW);
}
void saveScreenshot(const std::wstring& path) {
    composeWindow(W,H);
    Bitmap bitmap(W,H,W*4,PixelFormat32bppRGB,static_cast<BYTE*>(windowCanvas.bits));
    UINT size=0,count=0;GetImageEncodersSize(&count,&size);std::vector<BYTE> buffer(size);
    auto* enc=reinterpret_cast<ImageCodecInfo*>(buffer.data());GetImageEncoders(count,size,enc);
    for(UINT i=0;i<count;i++)if(wcscmp(enc[i].MimeType,L"image/png")==0){bitmap.Save(path.c_str(),&enc[i].Clsid,nullptr);break;}
}
int iniInt(const wchar_t* name,int fallback,int low,int high) {
    return std::clamp(int(GetPrivateProfileIntW(L"Player",name,fallback,(appDir/L"philips.ini").c_str())),low,high);
}
void atomicText(const fs::path& path,const std::string& text) {
    auto temporary=fs::path(path.wstring()+L".tmp");
    {std::ofstream out(temporary,std::ios::binary|std::ios::trunc);out.write(text.data(),std::streamsize(text.size()));if(!out)return;}
    MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
}
void loadSettings() {
    auto& s=ui->settings;
    s.volume=iniInt(L"Volume",70,0,100);s.volumeLimit=iniInt(L"VolumeLimit",100,0,100);
    s.theme=iniInt(L"Theme",0,0,2);s.language=iniInt(L"Language",2,0,23);
    s.repeat=iniInt(L"Repeat",0,0,2);s.shuffle=iniInt(L"Shuffle",0,0,4);s.equalizer=iniInt(L"Equalizer",0,0,7);
    s.backlight=iniInt(L"Backlight",0,0,60);s.screenOff=iniInt(L"ScreenOff",0,0,60);s.rotation=iniInt(L"Rotation",0,0,1);
    for(int i=0;i<5;i++)s.bands[i]=iniInt((L"Band"+std::to_wstring(i)).c_str(),0,-12,12);
    wchar_t last[32768];GetPrivateProfileStringW(L"Player",L"LastPlayed",L"",last,32768,(appDir/L"philips.ini").c_str());ui->lastPlayed=last;
    ui->resumePosition=iniInt(L"ResumeSeconds",0,0,24*3600);
    auto playlist=layoutDir/L"Playlists"/L"Playlist-on-the-Go.m3u8";
    std::ifstream input(playlist,std::ios::binary);std::string line;
    while(std::getline(input,line)) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(line.empty()||line[0]=='#')continue;
        auto path=fs::path(wide(line));if(path.is_relative())path=playlist.parent_path()/path;
        std::error_code ec;path=fs::weakly_canonical(path,ec);if(!ec&&fs::is_regular_file(path,ec))ui->playlist.push_back(path);
    }
    std::ifstream marks(layoutDir/L"Playlists"/L"bookmarks.tsv");
    while(std::getline(marks,line)){auto tab=line.find('\t');if(tab==std::string::npos)continue;try{double time=std::stod(line.substr(0,tab));auto path=fs::path(wide(line.substr(tab+1)));if(path.is_relative())path=layoutDir/L"Playlists"/path;std::error_code ec;if(time>=0&&fs::is_regular_file(path,ec))ui->bookmarks.emplace_back(path,time);}catch(...) {}}
    ui->applySettings();ui->loadStations(layoutDir/L"Playlists"/L"radio.json");
}
void saveSettings() {
    ui->remember();
    auto ini=appDir/L"philips.ini";auto& s=ui->settings;
    auto write=[&](const wchar_t* key,int value){WritePrivateProfileStringW(L"Player",key,std::to_wstring(value).c_str(),ini.c_str());};
    write(L"Borderless",borderless);write(L"Volume",s.volume);write(L"VolumeLimit",s.volumeLimit);write(L"Theme",s.theme);write(L"Language",s.language);
    write(L"Repeat",s.repeat);write(L"Shuffle",s.shuffle);write(L"Equalizer",s.equalizer);write(L"Backlight",s.backlight);write(L"ScreenOff",s.screenOff);write(L"Rotation",s.rotation);
    for(int i=0;i<5;i++)write((L"Band"+std::to_wstring(i)).c_str(),int(s.bands[i]));
    WritePrivateProfileStringW(L"Player",L"LastPlayed",ui->lastPlayed.c_str(),ini.c_str());write(L"ResumeSeconds",int(ui->resumePosition));
    std::string list="#EXTM3U\n";
    for(auto path:ui->playlist){std::error_code ec;auto relative=fs::relative(path,layoutDir/L"Playlists",ec);list+=(ec?path:relative).generic_u8string()+"\n";}
    atomicText(layoutDir/L"Playlists"/L"Playlist-on-the-Go.m3u8",list);
    std::ostringstream marks;for(auto& mark:ui->bookmarks){std::error_code ec;auto relative=fs::relative(mark.first,layoutDir/L"Playlists",ec);marks<<mark.second<<'\t'<<(ec?mark.first:relative).generic_u8string()<<'\n';}
    atomicText(layoutDir/L"Playlists"/L"bookmarks.tsv",marks.str());
}
void smokeTick() {
    auto fail=[&](const char* why){smokeChecks.push_back(std::string("FAIL ")+why);smokeStep=100;};
    auto info=audio->snapshot();
    if(smokeStep==0&&!ui->scanning) {
        if(ui->library.size()<2){fail("need two fixture tracks");return;}
        ui->skipBoot();smokeChecks.push_back("PASS recursive library scan and metadata");
        ui->input(Key::Home);ui->input(Key::Select);ui->input(Key::Select);ui->input(Key::Select);smokeStep=1;
    } else if(smokeStep==1&&info.playing&&info.position>.15) {
        smokeChecks.push_back("PASS original Music menu and FFmpeg playback");ui->input(Key::Select);smokeStep=2;pressedAt=GetTickCount64();
    } else if(smokeStep==2&&info.paused&&GetTickCount64()-pressedAt>180) {
        smokeChecks.push_back("PASS pause");audio->seek(1.2);ui->input(Key::Select);smokeStep=3;
    } else if(smokeStep==3&&!info.paused&&info.position>=1.2) {
        smokeChecks.push_back("PASS seek and resume");ui->input(Key::Next);smokeStep=4;
    } else if(smokeStep==4&&info.playing&&info.position>.1) {
        smokeChecks.push_back("PASS next track");ui->input(Key::Menu);smokeStep=100;
    }
    if(!info.error.empty()&&smokeStep<100)fail(utf8(info.error).c_str());
    if(smokeStep==100) {
        if(!screenshotPath.empty())saveScreenshot(screenshotPath);
        std::ofstream file(fs::path(smokeReport),std::ios::binary);for(auto& c:smokeChecks)file<<c<<'\n';file.close();
        PostMessageW(window,WM_CLOSE,0,0);smokeStep=101;
    }
}
LRESULT CALLBACK procedure(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_CREATE:SetTimer(hwnd,1,33,nullptr);return 0;
    case WM_GETMINMAXINFO:{auto* i=reinterpret_cast<MINMAXINFO*>(lp);i->ptMinTrackSize={W/2,H/2};return 0;}
    case ScanDone:{std::lock_guard<std::mutex> l(libraryMutex);ui->setLibrary(std::move(scanResult));return 0;}
    case WM_TIMER: {
        auto time=GetTickCount64();
        if(rescanRequested.exchange(false)&&!closing)beginScan();
        ui->tick(time);
        sliderMotion.tick(time,ui->sliderPosition());
        if(!closing&&ui->takePowerOffRequest())SendMessageW(hwnd,WM_CLOSE,0,0);
        if(closing&&ui->shutdownFinished()){DestroyWindow(hwnd);return 0;}
        if(smokeStep>=0&&smokeStep<101)smokeTick();
        else if(smokeStep<0&&!screenshotPath.empty()&&time-started>8200&&!closing){saveScreenshot(screenshotPath);PostMessageW(hwnd,WM_CLOSE,0,0);}
        refresh(hwnd);return 0;
    }
    case WM_KEYDOWN:
        if(closing)return 0;
        if(lp&(1LL<<30))return 0;
        if(ui->page()==Page::Confirm&&(wp=='S'||wp=='R'||wp==VK_F5||wp==VK_F9||wp==VK_F10))return 0;
        if(wp==VK_ESCAPE||wp==VK_BACK)pressKeyboard(wp,Key::Back);
        else if(wp==VK_HOME)ui->input(Key::Home);
        else if(wp==VK_RETURN||wp==VK_SPACE)pressKeyboard(wp,Key::Select);
        else if(wp==VK_UP||wp==VK_DOWN)pressKeyboard(wp,(GetKeyState(VK_SHIFT)&0x8000)?(wp==VK_UP?Key::SeekBack:Key::SeekForward):(wp==VK_UP?Key::Up:Key::Down));
        else if(wp==VK_LEFT)pressKeyboard(wp,Key::Back);
        else if(wp==VK_RIGHT)pressKeyboard(wp,Key::Select);
        else if(wp==VK_ADD||wp==VK_OEM_PLUS)pressKeyboard(wp,Key::VolumeUp);
        else if(wp==VK_SUBTRACT||wp==VK_OEM_MINUS)pressKeyboard(wp,Key::VolumeDown);
        else if(wp=='M'||wp==VK_APPS)ui->input(Key::Menu);
        else if(wp=='L')ui->input(Key::Lock);
        else if(wp=='P'){powerKeyDown=true;ui->moveSlider(SliderPosition::Power);}
        else if(wp=='S'&&!ui->locked&&!ui->booting()&&!ui->updating())ui->settings.shuffle=ui->settings.shuffle?0:1;
        else if(wp=='R'&&!ui->locked&&!ui->booting()&&!ui->updating())ui->settings.repeat=(ui->settings.repeat+1)%3;
        else if(wp==VK_F5){ui->loadStations(layoutDir/L"Playlists"/L"radio.json");beginScan();}
        else if((wp==VK_F9||wp==VK_F10)&&!ui->locked&&!ui->booting()&&!ui->updating()){ui->input(Key::Home);ui->navigate(wp==VK_F9?Page::USBConnected:Page::USB);}
        return 0;
    case WM_KEYUP:
        if(wp=='P'){powerKeyDown=false;if(!sliderDragging||ui->sliderPosition()!=SliderPosition::Power)ui->releaseSlider();}
        else releaseKeyboard(wp);
        return 0;
    case WM_KILLFOCUS:cancelHeldControls(hwnd,true);return 0;
    case WM_MOUSEWHEEL:{bool playback=ui->page()==Page::Playing||ui->page()==Page::Radio;ui->input(GET_WHEEL_DELTA_WPARAM(wp)>0?(playback?Key::VolumeUp:Key::Up):(playback?Key::VolumeDown:Key::Down));return 0;}
    case WM_LBUTTONDOWN: {
        if(closing)return 0;
        auto point=playerPoint(hwnd,lp);float x=point.X,y=point.Y;
        int button=buttonAt(x,y);bool alreadyHeld=button>=0&&button!=2&&keyHeld(bodyKey(button));
        if(borderless&&button<0&&!(x>=lcdArea.X&&x<lcdArea.X+lcdArea.Width&&y>=lcdArea.Y&&y<lcdArea.Y+lcdArea.Height)) {
            // No frame to hold it by: the corner resizes, the rest of the shell drags.
            bool corner=x>=gripArea.X&&x<gripArea.X+gripArea.Width&&y>=gripArea.Y&&y<gripArea.Y+gripArea.Height;
            // A frameless window has no sizing border for Windows to drag, so the corner is followed by hand.
            if(corner){resizing=true;GetCursorPos(&resizeFrom);GetWindowRect(hwnd,&resizeStart);SetCapture(hwnd);return 0;}
            ReleaseCapture();SendMessageW(hwnd,WM_NCLBUTTONDOWN,HTCAPTION,0);return 0;
        }
        pressed=button;pressedAt=GetTickCount64();SetCapture(hwnd);
        if(pressed==2){sliderDragging=true;ui->moveSlider(sliderMotion.press(sliderPointer(x),sliderHandleAt(x)));}
        else if(pressed>=0){if(!alreadyHeld)ui->pressKey(bodyKey(pressed));}
        else if(pressed<0&&x>=lcdArea.X&&x<lcdArea.X+lcdArea.Width&&y>=lcdArea.Y&&y<lcdArea.Y+lcdArea.Height)ui->click(int((x-lcdArea.X)/3),int((y-lcdArea.Y)/3));
        return 0;
    }
    case WM_MOUSEMOVE:
        if(resizing) {
            if(!(wp&MK_LBUTTON)){resizing=false;releaseMouseCapture(hwnd);return 0;}
            // The mark sits a little inside the window's right edge; scale the pointer's travel so it stays under the pointer.
            POINT now;GetCursorPos(&now);float reach=(gripArea.X+gripArea.Width/2.0f)/W;
            int width=std::max(W/2,int(std::lround(resizeStart.right-resizeStart.left+(now.x-resizeFrom.x)/reach)));
            SetWindowPos(hwnd,nullptr,0,0,width,int(std::lround(width*double(H)/W)),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            return 0;
        }
        if(sliderDragging&&!closing) {
            if(!(wp&MK_LBUTTON)){cancelHeldControls(hwnd,true);return 0;}
            auto point=playerPoint(hwnd,lp);
            auto groove=buttons[2];
            if(point.Y>=groove.y-8&&point.Y<groove.y+groove.h+8)ui->moveSlider(sliderMotion.drag(sliderPointer(point.X)));
            else {ui->releaseSlider();sliderMotion.release();sliderDragging=false;}
            refresh(hwnd);
        }
        return 0;
    case WM_LBUTTONUP: {
        if(resizing){resizing=false;releaseMouseCapture(hwnd);return 0;}
        int button=pressed;bool wasSlider=sliderDragging;pressed=-1;sliderDragging=false;
        if(wasSlider){sliderMotion.release();if(!powerKeyDown)ui->releaseSlider();}
        else if(button>=0&&button!=2){Key key=bodyKey(button);if(!keyHeld(key))ui->releaseKey(key);}
        releaseMouseCapture(hwnd);return 0;
    }
    case WM_CAPTURECHANGED:if(!releasingCapture)cancelHeldControls(hwnd,false);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT p;HDC dc=BeginPaint(hwnd,&p);RECT r;GetClientRect(hwnd,&r);if(!borderless&&r.right>0&&r.bottom>0)paint(dc,r.right,r.bottom);EndPaint(hwnd,&p);return 0;}
    case WM_RBUTTONUP:if(!closing){cancelHeldControls(hwnd,true);setBorderless(hwnd,!borderless);}return 0;
    case WM_SIZING:
        if(borderless) {
            // The frameless player keeps its shape: height follows width.
            auto* r=reinterpret_cast<RECT*>(lp);int width=std::max(int(r->right-r->left),W/2);
            r->right=r->left+width;r->bottom=r->top+int(std::lround(width*double(H)/W));return TRUE;
        }
        break;
    case WM_SIZE:if(borderless&&wp!=SIZE_MINIMIZED)presentFloating(hwnd);break;
    case WM_CLOSE:if(!closing){cancelHeldControls(hwnd,true);if(smokeStep<0)saveSettings();recorder.finish(true);audio->stop();ui->shutdown();closing=true;}return 0;
    case WM_DESTROY:KillTimer(hwnd,1);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    SetProcessDPIAware();std::vector<wchar_t> executable(32768);GetModuleFileNameW(nullptr,executable.data(),DWORD(executable.size()));
    appDir=fs::path(executable.data()).parent_path();musicDir=appDir/L"Music";
    int argc;auto** args=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc==4&&wcscmp(args[1],L"--decode-test")==0){int result=Audio::decodeTest(args[2],args[3]);LocalFree(args);return result;}
    if(argc==4&&wcscmp(args[1],L"--playback-test")==0){int result=Audio::playbackTest(args[2],args[3]);LocalFree(args);return result;}
    if(argc==5&&wcscmp(args[1],L"--decode-seek-test")==0){wchar_t* end=nullptr;double seconds=wcstod(args[4],&end);int result=end!=args[4]&&!*end&&std::isfinite(seconds)&&seconds>=0?Audio::decodeTest(args[2],args[3],seconds):1;LocalFree(args);return result;}
    for(int i=1;i<argc;i++) {
        if(wcscmp(args[i],L"--music")==0&&i+1<argc)musicDir=fs::absolute(args[++i]);
        else if(wcscmp(args[i],L"--screenshot")==0&&i+1<argc)screenshotPath=args[++i];
        else if(wcscmp(args[i],L"--smoke-test")==0&&i+1<argc){smokeReport=args[++i];smokeStep=0;}
    }
    LocalFree(args);layoutDir=musicDir.parent_path();std::error_code ec;
    for(auto path:{musicDir,layoutDir/L"Recordings"/L"Voice",layoutDir/L"Playlists"}){fs::create_directories(path,ec);if(ec){MessageBoxW(nullptr,L"Move the player to a writable folder so Music, Recordings and Playlists can be created.",L"GoGear Mix",MB_ICONERROR);return 1;}}
    if(!fs::exists(layoutDir/L"Playlists"/L"radio.json"))atomicText(layoutDir/L"Playlists"/L"radio.json","{\n  \"stations\": []\n}\n");
    if(!fs::exists(layoutDir/L"Playlists"/L"radio.example.json"))atomicText(layoutDir/L"Playlists"/L"radio.example.json","{\n  \"stations\": [{\"mhz\": 99.5, \"name\": \"Example station\", \"url\": \"https://your-station.example/stream.mp3\"}]\n}\n");
    if(!fs::exists(layoutDir/L"Playlists"/L"Playlist-on-the-Go.m3u8"))atomicText(layoutDir/L"Playlists"/L"Playlist-on-the-Go.m3u8","#EXTM3U\n");
    audio=std::make_unique<Audio>();ui=std::make_unique<player::Interface>(backend,musicDir);ui->tick(GetTickCount64());loadSettings();
    ULONG_PTR token;GdiplusStartupInput startup;GdiplusStartup(&token,&startup,nullptr);
    if(!loadShell()){MessageBoxW(nullptr,L"The shell artwork inside philips.exe is missing or damaged.",L"GoGear Mix",MB_ICONERROR);ui.reset();audio.reset();art={};playerCanvas.release();windowCanvas.release();GdiplusShutdown(token);return 1;}
    WNDCLASSW klass{};klass.lpfnWndProc=procedure;klass.hInstance=instance;klass.lpszClassName=L"PhilipsGoGearMix";klass.hCursor=LoadCursor(nullptr,IDC_ARROW);klass.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));RegisterClassW(&klass);
    borderless=iniInt(L"Borderless",1,0,1)!=0;
    DWORD style=borderless?floatingStyle:framedStyle,extended=borderless?WS_EX_LAYERED:0;
    RECT bounds{0,0,W,H};AdjustWindowRectEx(&bounds,style,FALSE,extended);started=GetTickCount64();
    window=CreateWindowExW(extended,klass.lpszClassName,L"Philips GoGear Mix",style,CW_USEDEFAULT,CW_USEDEFAULT,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,instance,nullptr);
    if(!window){ui.reset();audio.reset();art={};playerCanvas.release();windowCanvas.release();GdiplusShutdown(token);return 1;}
    ShowWindow(window,show);if(borderless)presentFloating(window);beginScan();MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    scanStop=true;if(scanner.joinable())scanner.join();recorder.finish(true);ui.reset();audio.reset();art={};playerCanvas.release();windowCanvas.release();GdiplusShutdown(token);
    if(smokeStep>=0)return std::any_of(smokeChecks.begin(),smokeChecks.end(),[](auto& s){return s.rfind("FAIL",0)==0;})?1:0;
    return 0;
}
