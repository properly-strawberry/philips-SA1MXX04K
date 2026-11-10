#include "audio_buffer.h"
#include <array>
#include <cassert>
#include <chrono>
#include <iostream>

using namespace std::chrono_literals;
template<class Predicate>static void until(Predicate ready) {
    auto deadline=std::chrono::steady_clock::now()+5s;
    while(!ready()){assert(std::chrono::steady_clock::now()<deadline);std::this_thread::sleep_for(1ms);}
}
static int16_t sample(size_t frame,int channel) {
    int value=int(frame*17%60001)-30000;return int16_t(channel?-value:value);
}
static void encode(std::vector<int16_t>& pcm,size_t first,size_t count) {
    pcm.resize(count*2);for(size_t i=0;i<count;i++)for(int c=0;c<2;c++)pcm[i*2+c]=sample(first+i,c);
}
static size_t drain(PcmReadAhead& buffer,size_t first=0) {
    std::array<int16_t,2048*2> block{};size_t consumed=0;
    auto deadline=std::chrono::steady_clock::now()+5s;
    for(;;) {
        assert(std::chrono::steady_clock::now()<deadline);
        size_t count=buffer.pop(block.data(),2048);auto state=buffer.state();
        assert(state.frames<=PcmReadAhead::CapacityFrames);
        if(count) {
            assert(count==2048||state.finished);
            for(size_t i=0;i<count;i++)for(int c=0;c<2;c++)assert(block[i*2+c]==sample(first+consumed+i,c));
            consumed+=count;
        } else if(state.finished&&!state.frames)break;
        else std::this_thread::sleep_for(1ms);
    }
    return consumed;
}
static void packetSizes() {
    for(size_t packet:{size_t(1),size_t(17),size_t(1024),size_t(1152),size_t(4096),size_t(20000)}) {
        PcmReadAhead buffer(4096);size_t cursor=0,total=48000*3+37;
        buffer.start([&](auto& pcm){size_t count=std::min(packet,total-cursor);encode(pcm,cursor,count);cursor+=count;return count!=0;},[](double){});
        assert(drain(buffer)==total);assert(buffer.state().error.empty());
    }
}
static void stalledSource() {
    PcmReadAhead buffer;size_t cursor=0,total=PcmReadAhead::CapacityFrames*3;
    std::atomic<bool> stalled{false},release{false};
    buffer.start([&](auto& pcm){
        if(cursor>=PcmReadAhead::CapacityFrames&&!release) {
            stalled=true;
            while(!release&&!buffer.interrupted())std::this_thread::sleep_for(1ms);
            if(buffer.interrupted())throw std::runtime_error("Cancelled fixture read");
        }
        size_t count=std::min(size_t(1024),total-cursor);encode(pcm,cursor,count);cursor+=count;return count!=0;
    },[](double){});
    until([&]{return buffer.state().frames==PcmReadAhead::CapacityFrames;});
    std::array<int16_t,2048*2> block{};
    assert(buffer.pop(block.data(),2048)==2048);
    for(size_t i=0;i<2048;i++)for(int c=0;c<2;c++)assert(block[i*2+c]==sample(i,c));
    until([&]{return stalled.load();});
    // Consume over a second of PCM with the decoder deliberately unable to
    // produce anything. No wall-clock scheduling assumption is needed.
    size_t consumed=2048;
    for(int n=0;n<25;n++) {
        assert(buffer.pop(block.data(),2048)==2048);
        for(size_t i=0;i<2048;i++)for(int c=0;c<2;c++)assert(block[i*2+c]==sample(consumed+i,c));
        consumed+=2048;
    }
    assert(buffer.state().frames>0&&!buffer.state().finished);release=true;
    assert(drain(buffer,consumed)+consumed==total);
}
static void seekAndStop() {
    // Interrupt an in-flight read, discard its exception and PCM, then seek
    // again after EOF. Only samples from the latest seek may reach output.
    PcmReadAhead buffer;std::atomic<bool> entered{false};bool first=true;size_t cursor=0,base=0;
    buffer.start([&](auto& pcm){
        if(first){first=false;entered=true;until([&]{return buffer.interrupted();});throw std::runtime_error("Old read interrupted");}
        size_t count=std::min(size_t(1152),size_t(5000)-cursor);encode(pcm,base+cursor,count);cursor+=count;return count!=0;
    },[&](double seconds){base=size_t(seconds)*10000;cursor=0;});
    until([&]{return entered.load();});buffer.seek(7);assert(drain(buffer,70000)==5000);assert(buffer.state().error.empty());
    buffer.seek(8);assert(drain(buffer,80000)==5000);
    buffer.seek(9);buffer.seek(10);assert(drain(buffer,100000)==5000);
    buffer.stop();

    PcmReadAhead full(4096);size_t pos=0;
    full.start([&](auto& pcm){encode(pcm,pos,20000);pos+=20000;return true;},[&](double seconds){pos=size_t(seconds)*10000;});
    until([&]{return full.state().frames==4096;});full.seek(3);
    until([&]{return full.state().frames==4096;});
    std::array<int16_t,2048*2> block{};assert(full.pop(block.data(),2048)==2048);
    for(size_t i=0;i<2048;i++)for(int c=0;c<2;c++)assert(block[i*2+c]==sample(30000+i,c));
    full.stop(); // Also wake a producer blocked on a full ring.

    PcmReadAhead blocked;entered=false;
    blocked.start([&](auto&)->bool{entered=true;until([&]{return blocked.interrupted();});return false;},[](double){});
    until([&]{return entered.load();});blocked.stop();
}
static void shortAndError() {
    PcmReadAhead buffer;std::atomic<bool> release{false};int reads=0;
    buffer.start([&](auto& pcm){
        if(!reads++){encode(pcm,0,17);return true;}
        until([&]{return release.load()||buffer.interrupted();});throw std::runtime_error("Fixture decoder error");
    },[](double){});
    until([&]{return buffer.state().frames==17;});std::array<int16_t,128> block{};
    assert(buffer.pop(block.data(),64)==0);release=true;
    until([&]{return buffer.state().finished;});assert(buffer.pop(block.data(),64)==17);
    for(size_t i=0;i<17;i++)for(int c=0;c<2;c++)assert(block[i*2+c]==sample(i,c));
    assert(buffer.state().error=="Fixture decoder error"&&buffer.pop(block.data(),64)==0);
    PcmReadAhead empty;empty.start([](auto&){return false;},[](double){});
    until([&]{return empty.state().finished;});assert(empty.pop(block.data(),64)==0);
}
int main() {
    packetSizes();stalledSource();seekAndStop();shortAndError();
    std::cout<<"PASS exact PCM across codec packet sizes/ring wraps/EOF; bounded two-second read-ahead; stalled-source reserve; interrupted and repeated seek; EOF seek; full-ring/read cancellation; short-file/error draining\n";
}
