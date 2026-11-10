#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Five RBJ peaking biquads per channel. Gain-dependent headroom is applied
// before quantization; Off is an exact bypass, preserving the decoder tests.
class Equalizer {
    struct Filter {double b0=1,b1=0,b2=0,a1=0,a2=0;std::array<double,2> z1{},z2{};};
    std::array<Filter,5> filters_;
    std::array<double,5> gains_{};
    bool bypass_=true;
    double headroom_=1;
public:
    void reset(){for(auto& f:filters_){f.z1={};f.z2={};}}
    void configure(const std::array<double,5>& gains) {
        if(gains==gains_)return;
        gains_=gains;bypass_=true;double highest=0;
        const double hz[]={60,230,910,3600,14000};
        for(int i=0;i<5;i++) {
            double gain=std::clamp(gains[i],-12.0,12.0);highest=std::max(highest,gain);if(gain!=0)bypass_=false;
            double a=std::pow(10.0,gain/40),omega=2*3.14159265358979323846*hz[i]/48000;
            double alpha=std::sin(omega)/(2*0.8),c=std::cos(omega),norm=1+alpha/a;
            auto& f=filters_[i];f.b0=(1+alpha*a)/norm;f.b1=-2*c/norm;f.b2=(1-alpha*a)/norm;f.a1=-2*c/norm;f.a2=(1-alpha/a)/norm;
        }
        headroom_=std::pow(10.0,-highest/20);
    }
    int16_t process(int16_t sample,int channel,int volume) {
        if(bypass_)return int16_t(int(sample)*volume/100);
        double value=sample*headroom_;
        for(auto& f:filters_) {double out=f.b0*value+f.z1[channel];f.z1[channel]=f.b1*value-f.a1*out+f.z2[channel];f.z2[channel]=f.b2*value-f.a2*out;value=out;}
        return int16_t(std::clamp(value*volume/100,-32768.0,32767.0));
    }
};
