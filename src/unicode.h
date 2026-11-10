#pragma once
#include <string>
#include <stdexcept>
inline std::wstring decodeUtf8(const std::string& input) {
    std::wstring output;
    for(size_t i=0;i<input.size();) {
        unsigned first=static_cast<unsigned char>(input[i++]),cp=0,extra=0,minimum=0;
        if(first<0x80)cp=first;
        else if(first>=0xc2&&first<=0xdf){cp=first&31;extra=1;minimum=0x80;}
        else if(first>=0xe0&&first<=0xef){cp=first&15;extra=2;minimum=0x800;}
        else if(first>=0xf0&&first<=0xf4){cp=first&7;extra=3;minimum=0x10000;}
        else throw std::runtime_error("Invalid UTF-8");
        if(i+extra>input.size())throw std::runtime_error("Truncated UTF-8");
        for(unsigned j=0;j<extra;j++){unsigned next=static_cast<unsigned char>(input[i++]);if((next&0xc0)!=0x80)throw std::runtime_error("Invalid UTF-8 continuation");cp=(cp<<6)|(next&63);}
        if(cp<minimum||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))throw std::runtime_error("Invalid Unicode scalar");
        if(sizeof(wchar_t)==2&&cp>0xffff){cp-=0x10000;output.push_back(wchar_t(0xd800+(cp>>10)));output.push_back(wchar_t(0xdc00+(cp&1023)));}
        else output.push_back(wchar_t(cp));
    }
    return output;
}
