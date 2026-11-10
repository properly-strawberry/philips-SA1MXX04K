#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include "audio.h"
#include "equalizer.h"
#include "audio_buffer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <memory>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

std::string utf8(const std::wstring& s) {
    if(s.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);
    std::string r(n,0); WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),r.data(),n,nullptr,nullptr); return r;
}
std::wstring wide(const std::string& s) {
    if(s.empty()) return {};
    int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);
    std::wstring r(n,0); MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),r.data(),n); return r;
}
static void check(int code,const char* where) {
    if(code>=0) return;
    char err[AV_ERROR_MAX_STRING_SIZE]; av_strerror(code,err,sizeof(err));
    throw std::runtime_error(std::string(where)+": "+err);
}

class Decoder {
    AVFormatContext* format_=nullptr;
    AVCodecContext* codec_=nullptr;
    SwrContext* swr_=nullptr;
    AVFrame* frame_=nullptr;
    AVPacket* packet_=nullptr;
    int stream_=-1;
    bool eof_=false, flushed_=false, drained_=false;
    double discardBefore_=-1;
    std::function<bool()> interrupt_;
public:
    TrackInfo info;
    Decoder()=default;
    ~Decoder() {
        av_packet_free(&packet_); av_frame_free(&frame_); swr_free(&swr_);
        avcodec_free_context(&codec_); avformat_close_input(&format_);
    }
    void open(const std::wstring& path,const std::function<bool()>& cancel={}) {
        interrupt_=cancel;
        format_=avformat_alloc_context();if(!format_)throw std::bad_alloc();
        format_->interrupt_callback.callback=[](void* context)->int {auto* self=static_cast<Decoder*>(context);return self->interrupt_&&self->interrupt_()?1:0;};
        format_->interrupt_callback.opaque=this;
        bool network=path.rfind(L"https://",0)==0||path.rfind(L"http://",0)==0;
        AVDictionary* options=nullptr;
        av_dict_set(&options,"protocol_whitelist",network?"http,https,tcp,tls,crypto,httpproxy":"file",0);
        if(network) {
            av_dict_set(&options,"rw_timeout","5000000",0);
            av_dict_set(&options,"reconnect","1",0);
            av_dict_set(&options,"reconnect_streamed","1",0);
            av_dict_set(&options,"reconnect_delay_max","2",0);
        }
        int status=avformat_open_input(&format_,utf8(path).c_str(),nullptr,&options);
        av_dict_free(&options); check(status,"Open audio");
        check(avformat_find_stream_info(format_,nullptr),"Read audio stream");
        stream_=av_find_best_stream(format_,AVMEDIA_TYPE_AUDIO,-1,-1,nullptr,0);
        check(stream_,"Find audio stream");
        const auto* parameters=format_->streams[stream_]->codecpar;
        const auto* decoder=avcodec_find_decoder(parameters->codec_id);
        if(!decoder) throw std::runtime_error("No decoder for this audio format");
        codec_=avcodec_alloc_context3(decoder);
        if(!codec_) throw std::bad_alloc();
        check(avcodec_parameters_to_context(codec_,parameters),"Audio parameters");
        check(avcodec_open2(codec_,decoder,nullptr),"Start decoder");
        AVChannelLayout output=AV_CHANNEL_LAYOUT_STEREO;
        if(!codec_->ch_layout.nb_channels || codec_->sample_rate<=0) throw std::runtime_error("Invalid audio layout");
        check(swr_alloc_set_opts2(&swr_,&output,AV_SAMPLE_FMT_S16,48000,
              &codec_->ch_layout,codec_->sample_fmt,codec_->sample_rate,0,nullptr),"Create resampler");
        check(swr_init(swr_),"Start resampler");
        frame_=av_frame_alloc(); packet_=av_packet_alloc();
        if(!frame_ || !packet_) throw std::bad_alloc();
        auto* st=format_->streams[stream_];
        info.duration=st->duration!=AV_NOPTS_VALUE ? st->duration*av_q2d(st->time_base) :
            (format_->duration!=AV_NOPTS_VALUE ? double(format_->duration)/AV_TIME_BASE : 0);
        if(network)info.duration=0;
        auto tag=[&](const char* name) {
            auto* t=av_dict_get(st->metadata,name,nullptr,0);
            if(!t) t=av_dict_get(format_->metadata,name,nullptr,0);
            return t ? wide(t->value) : std::wstring();
        };
        info.title=tag("title"); info.artist=tag("artist"); info.album=tag("album");
        if(info.artist.empty())info.artist=tag("album_artist");
        if(info.artist.empty())info.artist=tag("author");
    }
    void seek(double seconds) {
        auto* stream=format_->streams[stream_];
        auto offset=stream->start_time==AV_NOPTS_VALUE ? 0 : stream->start_time;
        int64_t timestamp=int64_t(seconds/av_q2d(stream->time_base))+offset;
        check(avformat_seek_file(format_,stream_,INT64_MIN,timestamp,timestamp,AVSEEK_FLAG_BACKWARD),"Seek");
        avcodec_flush_buffers(codec_); swr_close(swr_); check(swr_init(swr_),"Reset resampler");
        av_packet_unref(packet_); av_frame_unref(frame_);
        eof_=flushed_=drained_=false; discardBefore_=seconds;
    }
    // One decoded frame, converted to 48 kHz interleaved stereo signed 16-bit.
    // Bounded storage even for multi-hour files; codec and resampler are drained.
    bool read(std::vector<int16_t>& pcm) {
        pcm.clear();
        for(;;) {
            int r=avcodec_receive_frame(codec_,frame_);
            if(r==AVERROR_EOF) {
                if(drained_) return false;
                int capacity=std::max(32,swr_get_out_samples(swr_,0));
                pcm.resize(size_t(capacity)*2); uint8_t* output=reinterpret_cast<uint8_t*>(pcm.data());
                int n=swr_convert(swr_,&output,capacity,nullptr,0); check(n,"Drain resampler");
                pcm.resize(size_t(n)*2); drained_=n==0; return n!=0;
            }
            if(r==AVERROR(EAGAIN)) {
                if(eof_) {
                    if(flushed_) throw std::runtime_error("Decoder made no progress after flush");
                    check(avcodec_send_packet(codec_,nullptr),"Drain decoder"); flushed_=true; continue;
                }
                do {
                    r=av_read_frame(format_,packet_);
                    if(r<0) { if(r!=AVERROR_EOF) check(r,"Read packet"); eof_=true; break; }
                    if(packet_->stream_index==stream_) break;
                    av_packet_unref(packet_);
                } while(true);
                if(!eof_) { r=avcodec_send_packet(codec_,packet_); av_packet_unref(packet_); check(r,"Decode packet"); }
                continue;
            }
            check(r,"Receive audio frame");
            int capacity=swr_get_out_samples(swr_,frame_->nb_samples);
            check(capacity,"Size resampler buffer");
            pcm.resize(size_t(capacity)*2); uint8_t* output=reinterpret_cast<uint8_t*>(pcm.data());
            int count=swr_convert(swr_,&output,capacity,const_cast<const uint8_t**>(frame_->extended_data),frame_->nb_samples);
            check(count,"Resample audio"); pcm.resize(size_t(count)*2);
            if(discardBefore_>=0 && frame_->best_effort_timestamp!=AV_NOPTS_VALUE) {
                auto* st=format_->streams[stream_];
                auto start=st->start_time==AV_NOPTS_VALUE ? 0 : st->start_time;
                double time=(frame_->best_effort_timestamp-start)*av_q2d(st->time_base);
                int skip=int(std::max(0.0,std::min(double(count),(discardBefore_-time)*48000)));
                pcm.erase(pcm.begin(),pcm.begin()+size_t(skip)*2);
                if(skip<count) discardBefore_=-1;
            } else discardBefore_=-1;
            av_frame_unref(frame_);
            if(!pcm.empty()) return true;
        }
    }
};

// The reader must stop before FFmpeg closes, and its interrupt state must
// remain alive until the decoder's network/file cleanup has completed.
struct BufferedDecoder {
    PcmReadAhead ahead;
    Decoder decoder;
    ~BufferedDecoder(){ahead.stop();}
};

struct Output {
    HWAVEOUT handle=nullptr;
    HANDLE event=nullptr;
    static constexpr int N=6, Frames=2048;
    std::array<WAVEHDR,N> headers{};
    std::array<std::array<int16_t,Frames*2>,N> samples{};
    Output() {
        WAVEFORMATEX f{}; f.wFormatTag=WAVE_FORMAT_PCM; f.nChannels=2; f.nSamplesPerSec=48000;
        f.wBitsPerSample=16; f.nBlockAlign=4; f.nAvgBytesPerSec=192000;
        event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!event)throw std::runtime_error("Cannot create audio output event");
        try {
            if(waveOutOpen(&handle,WAVE_MAPPER,&f,reinterpret_cast<DWORD_PTR>(event),0,CALLBACK_EVENT)!=MMSYSERR_NOERROR)
                throw std::runtime_error("Windows could not open an audio output device");
            pause();
            for(int i=0;i<N;i++) {
                headers[i].lpData=reinterpret_cast<char*>(samples[i].data());headers[i].dwBufferLength=sizeof(samples[i]);
                if(waveOutPrepareHeader(handle,&headers[i],sizeof(WAVEHDR))!=MMSYSERR_NOERROR)
                    throw std::runtime_error("Cannot prepare audio output buffers");
            }
        } catch(...) {close();throw;}
    }
    ~Output(){close();}
    void close() {
        if(handle) {
            waveOutReset(handle);
            for(auto& h:headers) if(h.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(handle,&h,sizeof(h));
            waveOutClose(handle);handle=nullptr;
        }
        if(event){CloseHandle(event);event=nullptr;}
    }
    void reset() {if(waveOutReset(handle)!=MMSYSERR_NOERROR)throw std::runtime_error("Cannot reset audio output");pause();}
    void pause() {if(waveOutPause(handle)!=MMSYSERR_NOERROR)throw std::runtime_error("Cannot pause audio output");}
    void restart() {if(waveOutRestart(handle)!=MMSYSERR_NOERROR)throw std::runtime_error("Cannot resume audio output");}
    void wait() {WaitForSingleObject(event,5);}
    void poll() {ResetEvent(event);}
    int freeSlot() {
        for(int i=0;i<N;i++) if(!(headers[i].dwFlags&WHDR_INQUEUE)) return i;
        return -1;
    }
    bool empty() { return freeCount()==N; }
    int freeCount() { int n=0; for(auto& h:headers) if(!(h.dwFlags&WHDR_INQUEUE)) n++; return n; }
    size_t queuedFrames() {size_t n=0;for(auto& h:headers)if(h.dwFlags&WHDR_INQUEUE)n+=h.dwBufferLength/4;return n;}
    void submit(int slot,const int16_t* pcm,size_t count,int volume,Equalizer& eq) {
        auto& h=headers[slot];
        for(size_t i=0;i<count;i++) samples[slot][i]=eq.process(pcm[i],int(i%2),volume);
        h.dwBufferLength=DWORD(count*sizeof(int16_t));
        if(waveOutWrite(handle,&h,sizeof(h))!=MMSYSERR_NOERROR) throw std::runtime_error("Audio output failed");
    }
    double position() {
        MMTIME t{}; t.wType=TIME_SAMPLES;
        if(waveOutGetPosition(handle,&t,sizeof(t))!=MMSYSERR_NOERROR) return 0;
        if(t.wType==TIME_BYTES) return double(t.u.cb)/192000;
        if(t.wType==TIME_MS) return double(t.u.ms)/1000;
        return double(t.u.sample)/48000;
    }
};

Audio::Audio() { thread_=std::thread(&Audio::run,this); }
Audio::~Audio() { { std::lock_guard<std::mutex> l(mutex_); quit_=true; } thread_.join(); }
void Audio::play(const std::wstring& path) { std::lock_guard<std::mutex> l(mutex_); requested_=path; generation_++; paused_=false; seek_=-1; info_={}; info_.playing=true; }
void Audio::pause() { std::lock_guard<std::mutex> l(mutex_); if(info_.playing) { paused_=!paused_; info_.paused=paused_; } }
void Audio::stop() { std::lock_guard<std::mutex> l(mutex_); requested_.clear(); generation_++; info_={}; }
void Audio::seek(double s) { std::lock_guard<std::mutex> l(mutex_); if(info_.playing) seek_=std::clamp(s,0.0,std::max(0.0,info_.duration-0.05)); }
void Audio::volume(int v) { std::lock_guard<std::mutex> l(mutex_); volume_=std::clamp(v,0,100); }
void Audio::equalizer(const std::array<double,5>& gains) {std::lock_guard<std::mutex> l(mutex_);gains_=gains;}
TrackInfo Audio::probe(const std::wstring& source,const std::function<bool()>& cancel) {
    try {Decoder decoder;decoder.open(source,cancel);return decoder.info;}
    catch(const std::exception& e){TrackInfo result;result.error=wide(e.what());return result;}
}
TrackInfo Audio::snapshot() { std::lock_guard<std::mutex> l(mutex_); return info_; }
void Audio::run() {
    av_log_set_level(AV_LOG_ERROR);
    SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_ABOVE_NORMAL);
    unsigned active=0;
    for(;;) {
        std::wstring path; unsigned generation;
        { std::lock_guard<std::mutex> l(mutex_); if(quit_) return; generation=generation_; path=requested_; }
        if(generation==active || path.empty()) { Sleep(10); continue; }
        active=generation;
        try {
            BufferedDecoder source;auto& decoder=source.decoder;auto& ahead=source.ahead;
            decoder.open(path,[this,active,&ahead]{std::lock_guard<std::mutex> l(mutex_);return quit_||generation_!=active||ahead.interrupted();});
            Output output;Equalizer eq;
            ahead.start([&decoder](auto& pcm){return decoder.read(pcm);},[&decoder](double seconds){decoder.seek(seconds);});
            { std::lock_guard<std::mutex> l(mutex_); if(generation_!=active) continue; info_=decoder.info; info_.playing=true;info_.buffering=true; }
            std::array<int16_t,Output::Frames*2> pcm{};
            bool priming=true,running=false;unsigned underruns=0;double base=0;
            for(;;) {
                output.poll();
                bool paused, abort; double seek; int volume;std::array<double,5> gains;
                { std::lock_guard<std::mutex> l(mutex_); abort=quit_||generation_!=active; paused=paused_; seek=seek_; seek_=-1; volume=volume_;gains=gains_; }
                if(abort) break;
                if(seek>=0) {output.reset();ahead.seek(seek);eq.reset();base=seek;priming=true;running=false;}
                if(paused&&running){output.pause();running=false;}
                auto state=ahead.state();
                if(!paused&&running&&output.empty()&&(!state.finished||state.frames)) {
                    ++underruns;output.pause();running=false;priming=true;
                }
                // Build a 500-ms reserve, then queue every free output block
                // before releasing the paused device. Short files start at EOF.
                if(!paused&&priming&&(state.frames>=PcmReadAhead::Rate/2||state.finished))priming=false;
                if(!paused&&!priming) {
                    eq.configure(gains);
                    for(int slot;(slot=output.freeSlot())>=0;) {
                        size_t frames=ahead.pop(pcm.data(),Output::Frames);
                        if(!frames)break;
                        output.submit(slot,pcm.data(),frames*2,volume,eq);
                    }
                    if(!running&&!output.empty()){output.restart();running=true;}
                }
                state=ahead.state();
                { std::lock_guard<std::mutex> l(mutex_); if(generation_==active) {
                    info_.position=base+output.position();info_.paused=paused;
                    info_.bufferedSeconds=double(state.frames+output.queuedFrames())/PcmReadAhead::Rate;
                    info_.buffering=priming&&!paused;info_.underruns=underruns;
                } }
                if(!paused&&state.finished&&!state.frames&&output.empty()) {
                    if(!state.error.empty())throw std::runtime_error(state.error);
                    std::lock_guard<std::mutex> l(mutex_);
                    if(generation_==active){info_.playing=false;info_.finished=true;info_.position=info_.duration;info_.bufferedSeconds=0;info_.buffering=false;}
                    break;
                }
                output.wait();
            }
        } catch(const std::exception& error) {
            std::lock_guard<std::mutex> l(mutex_);
            if(generation_==active) { info_.playing=false;info_.buffering=false;info_.bufferedSeconds=0; info_.error=wide(error.what()); }
        }
    }
}

int Audio::decodeTest(const std::wstring& input,const std::wstring& output,double seekSeconds) {
    try {
        Decoder d; d.open(input);if(seekSeconds>=0)d.seek(std::clamp(seekSeconds,0.0,std::max(0.0,d.info.duration-.05)));
        FILE* file=_wfopen(output.c_str(),L"wb"); if(!file) throw std::runtime_error("Cannot create PCM output");
        std::unique_ptr<FILE,decltype(&fclose)> holder(file,fclose);
        std::vector<int16_t> pcm; uint64_t total=0;
        while(d.read(pcm)) { if(fwrite(pcm.data(),2,pcm.size(),file)!=pcm.size()) throw std::runtime_error("Write PCM failed"); total+=pcm.size()/2; }
        printf("Decoded %llu stereo frames at 48000 Hz; duration=%.6f; title=%s\n",(unsigned long long)total,d.info.duration,utf8(d.info.title).c_str());
        return total ? 0 : 1;
    } catch(const std::exception& e) { fprintf(stderr,"%s\n",e.what()); return 1; }
}

int Audio::playbackTest(const std::wstring& input,const std::wstring& report) {
    Audio audio;audio.volume(0);audio.play(input);
    auto started=GetTickCount64();TrackInfo info;double peak=0;
    for(;;) {
        info=audio.snapshot();peak=std::max(peak,info.bufferedSeconds);
        if(info.finished||!info.error.empty())break;
        double timeout=15000+(std::isfinite(info.duration)?std::max(0.0,info.duration)*1000:0);
        if(GetTickCount64()-started>timeout)break;
        Sleep(10);
    }
    FILE* file=_wfopen(report.c_str(),L"wb");if(!file)return 1;
    // Diagnostic contains no source path, tags or other library information.
    fprintf(file,"finished=%d\nunderruns=%u\npeak_buffer_seconds=%.6f\nerror=%d\n",int(info.finished),info.underruns,peak,int(!info.error.empty()));
    fclose(file);
    return info.finished&&info.error.empty()&&!info.underruns?0:1;
}
