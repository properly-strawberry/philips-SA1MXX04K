#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include "recorder.h"
#include <fstream>
#include <chrono>
#include <array>
#include <algorithm>

namespace fs=std::filesystem;
static void wavHeader(std::ostream& out,uint32_t bytes) {
    auto u16=[&](uint16_t n){out.put(char(n));out.put(char(n>>8));};
    auto u32=[&](uint32_t n){u16(uint16_t(n));u16(uint16_t(n>>16));};
    out.seekp(0);out.write("RIFF",4);u32(bytes+36);out.write("WAVEfmt ",8);u32(16);
    u16(1);u16(1);u32(22050);u32(44100);u16(2);u16(16);out.write("data",4);u32(bytes);
}
Recorder::~Recorder(){finish(true);}
player::Recording Recorder::snapshot(){std::lock_guard<std::mutex> l(mutex_);return state_;}
void Recorder::pause(bool value){std::lock_guard<std::mutex> l(mutex_);paused_=value;state_.paused=value;}
void Recorder::finish(bool keep) {
    {std::lock_guard<std::mutex> l(mutex_);stop_=true;keep_=keep;}
    if(thread_.joinable())thread_.join();
}
void Recorder::start(const fs::path& directory) {
    finish(true);
    std::error_code ec;fs::create_directories(directory,ec);
    if(ec){std::lock_guard<std::mutex> l(mutex_);state_={};state_.error=L"Cannot create Recordings/Voice";return;}
    SYSTEMTIME time;GetLocalTime(&time);wchar_t name[80];
    swprintf(name,80,L"VOICE%04u%02u%02u_%02u%02u%02u_%03u.wav",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds);
    fs::path path=directory/name;int suffix=1;
    while(fs::exists(path)||fs::exists(fs::path(path.wstring()+L".part")))path=directory/(std::wstring(name)+L"_"+std::to_wstring(suffix++)+L".wav");
    {std::lock_guard<std::mutex> l(mutex_);state_={};state_.active=true;state_.path=path;stop_=paused_=false;keep_=true;}
    thread_=std::thread(&Recorder::run,this,path);
}
void Recorder::run(fs::path path) {
    HWAVEIN device=nullptr;WAVEFORMATEX format{};
    format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=1;format.nSamplesPerSec=22050;format.wBitsPerSample=16;format.nBlockAlign=2;format.nAvgBytesPerSec=44100;
    std::array<WAVEHDR,4> headers{};std::array<std::array<int16_t,2048>,4> samples{};
    auto temp=fs::path(path.wstring()+L".part");bool opened=false;std::wstring error;
    uint64_t bytes=0;
    try {
        auto result=waveInOpen(&device,WAVE_MAPPER,&format,0,0,CALLBACK_NULL);
        if(result!=MMSYSERR_NOERROR)throw std::runtime_error("Microphone unavailable (check Windows microphone permissions)");
        std::ofstream output(temp,std::ios::binary|std::ios::trunc);if(!output)throw std::runtime_error("Cannot write voice recording");opened=true;wavHeader(output,0);
        for(size_t i=0;i<headers.size();i++) {
            auto& h=headers[i];h.lpData=reinterpret_cast<char*>(samples[i].data());h.dwBufferLength=DWORD(samples[i].size()*2);
            if(waveInPrepareHeader(device,&h,sizeof(h))!=MMSYSERR_NOERROR||waveInAddBuffer(device,&h,sizeof(h))!=MMSYSERR_NOERROR)throw std::runtime_error("Cannot prepare microphone buffers");
        }
        if(waveInStart(device)!=MMSYSERR_NOERROR)throw std::runtime_error("Cannot start microphone");
        bool paused=false,ending=false;
        for(;;) {
            bool stop,wantedPause;
            {std::lock_guard<std::mutex> l(mutex_);stop=stop_;wantedPause=paused_;}
            if(stop&&!ending){waveInStop(device);waveInReset(device);ending=true;}
            if(!ending&&paused!=wantedPause){if(wantedPause)waveInStop(device);else waveInStart(device);paused=wantedPause;}
            double peak=0;
            for(size_t i=0;i<headers.size();i++) {
                auto& h=headers[i];if(!(h.dwFlags&WHDR_DONE))continue;
                if(h.dwBytesRecorded) {
                    // Stop before the RIFF 32-bit size limit instead of wrapping it.
                    if(bytes+h.dwBytesRecorded>0xffff0000ULL)throw std::runtime_error("Recording reached the WAV size limit");
                    output.write(h.lpData,h.dwBytesRecorded);if(!output)throw std::runtime_error("Recording storage is full");
                    bytes+=h.dwBytesRecorded;
                    for(size_t j=0;j<h.dwBytesRecorded/2;j++)peak=std::max(peak,std::abs(double(samples[i][j]))/32768.0);
                }
                if(!ending){h.dwBytesRecorded=0;h.dwFlags&=~WHDR_DONE;if(waveInAddBuffer(device,&h,sizeof(h))!=MMSYSERR_NOERROR)throw std::runtime_error("Microphone buffer failed");}
            }
            {std::lock_guard<std::mutex> l(mutex_);state_.seconds=double(bytes)/44100;state_.peak=peak;}
            if(ending)break;
            Sleep(10);
        }
        wavHeader(output,uint32_t(bytes));output.close();
    }catch(const std::exception& e){error=wide(e.what());}
    if(device){waveInReset(device);for(auto& h:headers)if(h.dwFlags&WHDR_PREPARED)waveInUnprepareHeader(device,&h,sizeof(h));waveInClose(device);}
    bool keep;{std::lock_guard<std::mutex> l(mutex_);keep=keep_;}
    std::error_code ec;
    if(opened&&fs::exists(temp)) {
        // Recover a valid WAV even if a device/storage error ended the capture.
        {std::fstream out(temp,std::ios::binary|std::ios::in|std::ios::out);if(out)wavHeader(out,uint32_t(bytes));}
        if(keep&&bytes) {fs::rename(temp,path,ec);if(ec&&error.empty())error=L"Recording kept as .wav.part; rename failed";}
        else fs::remove(temp,ec);
    }
    {std::lock_guard<std::mutex> l(mutex_);state_.active=false;state_.paused=false;state_.error=error;}
}
