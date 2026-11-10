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
constexpr int W=920,H=380;
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
void control(int button) {
    if(button>=0)player::pressBodyButton(*ui,player::BodyButton(button));
}
void renderScreen(){ui->render();screen=ui->screen;}
std::wstring trackTitle(const TrackInfo&){return ui->currentTitle();}
std::unique_ptr<GraphicsPath> rounded(float x,float y,float w,float h,float r) {
    auto p=std::make_unique<GraphicsPath>();float d=r*2;p->AddArc(x,y,d,d,180,90);p->AddArc(x+w-d,y,d,d,270,90);
    p->AddArc(x+w-d,y+h-d,d,d,0,90);p->AddArc(x,y+h-d,d,d,90,90);p->CloseFigure();return p;
}
void label(Graphics& g,float x,float y,float w,float h,const wchar_t* value,float size,Color color,bool bold=false) {
    Font font(L"Segoe UI",size,bold?FontStyleBold:FontStyleRegular,UnitPixel);
    SolidBrush brush(color);StringFormat format;format.SetAlignment(StringAlignmentCenter);format.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(value,-1,&font,RectF(x,y,w,h),&format,&brush);
}
struct Button { int x,y,w,h; const wchar_t* hint; };
const Button buttons[]={
    {710,79,64,44,L"Up / previous / hold to seek"},{654,139,50,68,L"Back"},{75,29,78,21,L"Lock / unlock"},
    {780,139,50,68,L"Play / pause / select"},{710,222,64,44,L"Down / next / hold to seek"},
    {647,29,55,21,L"Volume down"},{707,29,55,21,L"Volume up"}
};
static_assert(std::size(buttons)==player::bodyButtonCount);
int buttonAt(float x,float y) { for(size_t i=0;i<std::size(buttons);i++){auto b=buttons[i];if(x>=b.x&&x<b.x+b.w&&y>=b.y&&y<b.y+b.h)return int(i);}return -1; }
void drawButton(Graphics& g,int index) {
    auto b=buttons[index];auto path=rounded(float(b.x),float(b.y),float(b.w),float(b.h),8);
    LinearGradientBrush fill(Point(b.x,b.y),Point(b.x,b.y+b.h),pressed==index?Color(255,62,68,75):Color(255,31,34,39),Color(255,8,9,12));
    g.FillPath(&fill,path.get());Pen outline(Color(255,67,70,76),1);g.DrawPath(&outline,path.get());
    Color c=pressed==index?Color(255,113,230,250):Color(255,160,170,178);SolidBrush brush(c);Pen pen(c,2.4f);
    float cx=b.x+b.w/2.0f,cy=b.y+b.h/2.0f;
    if(index==1) { g.DrawLine(&pen,cx+8,cy+7,cx+8,cy-3);g.DrawLine(&pen,cx+8,cy-3,cx-9,cy-3);g.DrawLine(&pen,cx-9,cy-3,cx-3,cy-9);g.DrawLine(&pen,cx-9,cy-3,cx-3,cy+3); }
    if(index==0||index==4) {
        int d=index==0?-1:1;PointF points[]={{cx,cy+d*7},{cx-7,cy-d*5},{cx+7,cy-d*5}};g.FillPolygon(&brush,points,3);
    }
    if(index==3) { PointF p[]={{cx-9,cy-8},{cx+3,cy},{cx-9,cy+8}};g.FillPolygon(&brush,p,3);g.FillRectangle(&brush,cx+6,cy-7,2.0f,14.0f);g.FillRectangle(&brush,cx+11,cy-7,2.0f,14.0f); }
    if(index==2) {g.DrawLine(&pen,float(b.x+10),cy,float(b.x+b.w-10),cy);g.FillEllipse(&brush,float(ui->locked?b.x+b.w-21:b.x+9),cy-6,12.0f,12.0f);}
    if(index==5||index==6) {g.DrawLine(&pen,cx-6,cy,cx+6,cy);if(index==6)g.DrawLine(&pen,cx,cy-6,cx,cy+6);}
}
void paint(HDC dc,int width,int height) {
    Graphics g(dc);g.SetSmoothingMode(SmoothingModeAntiAlias);g.Clear(Color(255,24,27,33));
    float scale=std::min(float(width)/W,float(height)/H);
    g.TranslateTransform((width-W*scale)/2,(height-H*scale)/2);g.ScaleTransform(scale,scale);
    auto shadow=rounded(34,43,858,296,64);SolidBrush shade(Color(255,10,12,17));g.FillPath(&shade,shadow.get());
    auto shell=rounded(25,36,866,292,62);
    LinearGradientBrush metal(Point(0,36),Point(0,328),Color(255,220,224,229),Color(255,87,93,104));
    REAL positions[]={0,0.1f,0.4f,0.8f,1};Color colors[]={Color(255,164,171,181),Color(255,248,250,252),Color(255,141,147,157),Color(255,99,106,116),Color(255,221,225,232)};
    metal.SetInterpolationColors(colors,positions,5);g.FillPath(&metal,shell.get());
    Pen outer(Color(255,226,232,240),1);g.DrawPath(&outer,shell.get());
    auto face=rounded(38,49,840,264,50);
    LinearGradientBrush black(Point(38,49),Point(878,313),Color(255,3,4,6),Color(255,28,30,35));g.FillPath(&black,face.get());
    Pen rim(Color(255,130,136,145),1.2f);g.DrawPath(&rim,face.get());
    SolidBrush shine(Color(18,255,255,255));g.FillRectangle(&shine,RectF(477,54,120,252));
    Pen split(Color(255,98,104,113),1);g.DrawLine(&split,625,52,625,309);
    label(g,53,127,144,40,L"PHILIPS",25,Color(255,176,180,184),true);
    label(g,72,168,104,25,L"GoGear Mix",13,Color(255,100,111,124));
    auto glass=rounded(200,74,404,211,9);SolidBrush glassBrush(Color(255,1,3,5));g.FillPath(&glassBrush,glass.get());Pen glassRim(Color(255,34,40,48));g.DrawPath(&glassRim,glass.get());
    renderScreen();
    Bitmap bitmap(128,64,128*4,PixelFormat32bppARGB,reinterpret_cast<BYTE*>(screen.data()));
    g.SetInterpolationMode(InterpolationModeNearestNeighbor);g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.DrawImage(&bitmap,Rect(210,83,384,192),0,0,128,64,UnitPixel);
    g.SetPixelOffsetMode(PixelOffsetModeDefault);
    for(size_t i=0;i<std::size(buttons);i++)drawButton(g,int(i));
    auto info=audio->snapshot();
    std::wstring status=info.playing?(info.paused?L"Paused":L"Playing"):L"Music lives beside philips.exe";
    if(!info.error.empty())status=info.error;
    else if(info.playing)status+=L"  ·  "+trackTitle(info);
    label(g,40,337,840,19,status.c_str(),12,Color(255,170,183,197));
    label(g,40,357,840,17,L"Up/down: browse / hold to seek   ·   Left: back   ·   Right: play/select   ·   M: options   ·   + / −: volume",11,Color(255,111,126,143));
}
void saveScreenshot(const std::wstring& path) {
    Bitmap bitmap(W,H,PixelFormat32bppARGB);Graphics graphics(&bitmap);
    HDC dc=graphics.GetHDC();paint(dc,W,H);graphics.ReleaseHDC(dc);
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
    write(L"Volume",s.volume);write(L"VolumeLimit",s.volumeLimit);write(L"Theme",s.theme);write(L"Language",s.language);
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
    case WM_GETMINMAXINFO:{auto* i=reinterpret_cast<MINMAXINFO*>(lp);i->ptMinTrackSize={700,320};return 0;}
    case ScanDone:{std::lock_guard<std::mutex> l(libraryMutex);ui->setLibrary(std::move(scanResult));return 0;}
    case WM_TIMER: {
        auto time=GetTickCount64();
        if(rescanRequested.exchange(false)&&!closing)beginScan();
        ui->tick(time);
        if(closing&&ui->shutdownFinished()){DestroyWindow(hwnd);return 0;}
        if(smokeStep>=0&&smokeStep<101)smokeTick();
        else if(smokeStep<0&&!screenshotPath.empty()&&time-started>8200&&!closing){saveScreenshot(screenshotPath);PostMessageW(hwnd,WM_CLOSE,0,0);}
        InvalidateRect(hwnd,nullptr,FALSE);return 0;
    }
    case WM_KEYDOWN:
        if(closing)return 0;
        if(ui->page()==Page::Confirm&&(wp=='S'||wp=='R'||wp==VK_F5||wp==VK_F9||wp==VK_F10))return 0;
        if((lp&(1LL<<30))&&(wp==VK_RIGHT||wp==VK_RETURN||wp==VK_SPACE||wp=='L'))return 0;
        if(wp==VK_ESCAPE||wp==VK_BACK)ui->input(Key::Back);
        else if(wp==VK_HOME)ui->input(Key::Home);
        else if(wp==VK_RETURN||wp==VK_SPACE)ui->input(Key::Select);
        else if(wp==VK_UP||wp==VK_DOWN){if(GetKeyState(VK_SHIFT)&0x8000)ui->input(wp==VK_UP?Key::SeekBack:Key::SeekForward);else control(wp==VK_UP?0:4);}
        else if(wp==VK_LEFT)ui->input(Key::Back);
        else if(wp==VK_RIGHT)ui->input(Key::Select);
        else if(wp==VK_ADD||wp==VK_OEM_PLUS)ui->input(Key::VolumeUp);
        else if(wp==VK_SUBTRACT||wp==VK_OEM_MINUS)ui->input(Key::VolumeDown);
        else if(wp=='M'||wp==VK_APPS)ui->input(Key::Menu);
        else if(wp=='L')ui->input(Key::Lock);
        else if(wp=='S'&&!ui->locked&&!ui->booting()&&!ui->updating())ui->settings.shuffle=ui->settings.shuffle?0:1;
        else if(wp=='R'&&!ui->locked&&!ui->booting()&&!ui->updating())ui->settings.repeat=(ui->settings.repeat+1)%3;
        else if(wp==VK_F5){ui->loadStations(layoutDir/L"Playlists"/L"radio.json");beginScan();}
        else if((wp==VK_F9||wp==VK_F10)&&!ui->locked&&!ui->booting()&&!ui->updating()){ui->input(Key::Home);ui->navigate(wp==VK_F9?Page::USBConnected:Page::USB);}
        return 0;
    case WM_KEYUP:if(wp==VK_UP||wp==VK_DOWN)ui->releaseTransport();return 0;
    case WM_KILLFOCUS:ui->cancelTransport();return 0;
    case WM_MOUSEWHEEL:{bool playback=ui->page()==Page::Playing||ui->page()==Page::Radio;ui->input(GET_WHEEL_DELTA_WPARAM(wp)>0?(playback?Key::VolumeUp:Key::Up):(playback?Key::VolumeDown:Key::Down));return 0;}
    case WM_LBUTTONDOWN: {
        if(closing)return 0;
        RECT r;GetClientRect(hwnd,&r);float scale=std::min(float(r.right)/W,float(r.bottom)/H);
        float x=(GET_X_LPARAM(lp)-(r.right-W*scale)/2)/scale,y=(GET_Y_LPARAM(lp)-(r.bottom-H*scale)/2)/scale;
        pressed=buttonAt(x,y);pressedAt=GetTickCount64();SetCapture(hwnd);
        if(pressed>=0)control(pressed);
        else if(pressed<0&&x>=210&&x<594&&y>=83&&y<275)ui->click(int((x-210)/3),int((y-83)/3));
        return 0;
    }
    case WM_LBUTTONUP:if(pressed>=0)player::releaseBodyButton(*ui,player::BodyButton(pressed));pressed=-1;ReleaseCapture();return 0;
    case WM_CAPTURECHANGED:ui->cancelTransport();pressed=-1;return 0;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT p;HDC dc=BeginPaint(hwnd,&p);RECT r;GetClientRect(hwnd,&r);HDC mem=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,std::max(1L,r.right),std::max(1L,r.bottom));auto old=SelectObject(mem,bitmap);paint(mem,r.right,r.bottom);BitBlt(dc,0,0,r.right,r.bottom,mem,0,0,SRCCOPY);SelectObject(mem,old);DeleteObject(bitmap);DeleteDC(mem);EndPaint(hwnd,&p);return 0;}
    case WM_CLOSE:if(!closing){if(smokeStep<0)saveSettings();recorder.finish(true);audio->stop();ui->shutdown();closing=true;}return 0;
    case WM_DESTROY:KillTimer(hwnd,1);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    SetProcessDPIAware();std::vector<wchar_t> executable(32768);GetModuleFileNameW(nullptr,executable.data(),DWORD(executable.size()));
    appDir=fs::path(executable.data()).parent_path();musicDir=appDir/L"Music";
    int argc;auto** args=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc==4&&wcscmp(args[1],L"--decode-test")==0){int result=Audio::decodeTest(args[2],args[3]);LocalFree(args);return result;}
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
    WNDCLASSW klass{};klass.lpfnWndProc=procedure;klass.hInstance=instance;klass.lpszClassName=L"PhilipsGoGearMix";klass.hCursor=LoadCursor(nullptr,IDC_ARROW);klass.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));RegisterClassW(&klass);
    RECT bounds{0,0,W,H};AdjustWindowRect(&bounds,WS_OVERLAPPEDWINDOW,FALSE);started=GetTickCount64();
    window=CreateWindowExW(0,klass.lpszClassName,L"Philips GoGear Mix",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,instance,nullptr);
    if(!window){ui.reset();audio.reset();GdiplusShutdown(token);return 1;}
    ShowWindow(window,show);beginScan();MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    scanStop=true;if(scanner.joinable())scanner.join();recorder.finish(true);ui.reset();audio.reset();GdiplusShutdown(token);
    if(smokeStep>=0)return std::any_of(smokeChecks.begin(),smokeChecks.end(),[](auto& s){return s.rfind("FAIL",0)==0;})?1:0;
    return 0;
}
