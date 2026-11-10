#pragma once
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// Decoder packets have arbitrary sizes. Keep two seconds of raw stereo PCM
// ahead of the output thread, which never waits for disk/network/codec work.
class PcmReadAhead {
public:
    static constexpr size_t Rate=48000, Channels=2, CapacityFrames=2*Rate;
    struct State { size_t frames; bool finished; std::string error; };
    explicit PcmReadAhead(size_t capacity=CapacityFrames):capacity_(capacity),pcm_(capacity*Channels) {
        if(!capacity)throw std::invalid_argument("Empty audio buffer");
    }
    ~PcmReadAhead(){stop();}
    PcmReadAhead(const PcmReadAhead&)=delete;
    PcmReadAhead& operator=(const PcmReadAhead&)=delete;
    bool interrupted() const {return stopping_||revision_.load()!=reading_.load();}
    void start(std::function<bool(std::vector<int16_t>&)> read,std::function<void(double)> seek) {
        worker_=std::thread([this,read=std::move(read),seek=std::move(seek)] {
            std::vector<int16_t> decoded;
            for(;;) {
                unsigned revision;double target;bool reposition;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    changed_.wait(lock,[this]{return stopping_||seekPending_||!finished_;});
                    if(stopping_)return;
                    revision=revision_;reading_=revision;
                    reposition=seekPending_;target=seekTarget_;seekPending_=false;
                }
                try {
                    if(reposition)seek(target);
                    decoded.clear();bool more=read(decoded);
                    if(more&&(decoded.empty()||decoded.size()%Channels))throw std::runtime_error("Invalid decoded PCM block");
                    std::unique_lock<std::mutex> lock(mutex_);
                    if(stopping_)return;
                    if(revision!=revision_)continue;
                    if(!more){finished_=true;changed_.notify_all();continue;}
                    size_t offset=0,frames=decoded.size()/Channels;
                    while(offset<frames) {
                        changed_.wait(lock,[this,revision]{return stopping_||revision!=revision_||available_<capacity_;});
                        if(stopping_)return;
                        if(revision!=revision_)break;
                        size_t tail=(head_+available_)%capacity_;
                        size_t count=std::min({frames-offset,capacity_-available_,capacity_-tail});
                        std::copy_n(decoded.data()+offset*Channels,count*Channels,pcm_.data()+tail*Channels);
                        offset+=count;available_+=count;changed_.notify_all();
                    }
                } catch(const std::exception& error) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if(stopping_)return;
                    if(revision!=revision_)continue;
                    finished_=true;error_=error.what();changed_.notify_all();
                }
            }
        });
    }
    State state() const {
        std::lock_guard<std::mutex> lock(mutex_);return {available_,finished_,error_};
    }
    // Short blocks are allowed only at EOF/error, never at codec boundaries.
    size_t pop(int16_t* output,size_t maxFrames) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(available_<maxFrames&&!finished_)return 0;
        size_t count=std::min(available_,maxFrames),first=std::min(count,capacity_-head_);
        std::copy_n(pcm_.data()+head_*Channels,first*Channels,output);
        std::copy_n(pcm_.data(),(count-first)*Channels,output+first*Channels);
        head_=(head_+count)%capacity_;available_-=count;changed_.notify_all();return count;
    }
    void seek(double seconds) {
        std::lock_guard<std::mutex> lock(mutex_);
        head_=available_=0;finished_=false;error_.clear();seekTarget_=seconds;seekPending_=true;
        ++revision_;changed_.notify_all();
    }
    void stop() {
        {std::lock_guard<std::mutex> lock(mutex_);stopping_=true;changed_.notify_all();}
        if(worker_.joinable())worker_.join();
    }
private:
    const size_t capacity_;
    std::vector<int16_t> pcm_;
    size_t head_=0,available_=0;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::atomic<bool> stopping_{false};
    std::atomic<unsigned> revision_{0},reading_{0};
    bool seekPending_=false,finished_=false;
    double seekTarget_=0;
    std::string error_;
    std::thread worker_;
};
