#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <array>

struct TrackInfo {
    std::wstring title, artist, album, error;
    double duration=0, position=0;
    bool playing=false, paused=false, finished=false;
    double bufferedSeconds=0;
    unsigned underruns=0;
    bool buffering=false;
};

// FFmpeg owns decoding/resampling; the worker owns every waveOut handle/buffer.
// The UI only exchanges commands and copied snapshots through the mutex.
class Audio {
public:
    Audio();
    ~Audio();
    void play(const std::wstring& path);
    void pause();
    void stop();
    void seek(double seconds);
    void volume(int percent);
    void equalizer(const std::array<double,5>& gains);
    static TrackInfo probe(const std::wstring& source,const std::function<bool()>& cancel={});
    TrackInfo snapshot();
    static int decodeTest(const std::wstring& input, const std::wstring& output,double seekSeconds=-1);
    static int playbackTest(const std::wstring& input,const std::wstring& report);
private:
    void run();
    std::mutex mutex_;
    std::thread thread_;
    std::wstring requested_;
    TrackInfo info_;
    unsigned generation_=0;
    bool quit_=false, paused_=false;
    double seek_=-1;
    int volume_=70;
    std::array<double,5> gains_{};
};

std::string utf8(const std::wstring& text);
std::wstring wide(const std::string& text);
