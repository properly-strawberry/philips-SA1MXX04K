#include "radio_config.h"
#include "unicode.h"
#include <fstream>
#include <map>
#include <variant>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <set>

namespace player {
namespace {
struct Json {
    using Array=std::vector<Json>;using Object=std::map<std::string,Json>;
    std::variant<std::nullptr_t,bool,double,std::string,Array,Object> value=nullptr;
};
class Reader {
    const std::string& source_;size_t pos_=0;
    [[noreturn]]void fail(){throw std::runtime_error("Invalid radio.json near byte "+std::to_string(pos_));}
    void ws(){while(pos_<source_.size()&&(source_[pos_]==' '||source_[pos_]=='\r'||source_[pos_]=='\n'||source_[pos_]=='\t'))pos_++;}
    bool take(char c){ws();if(pos_<source_.size()&&source_[pos_]==c){pos_++;return true;}return false;}
    unsigned hex4(){unsigned n=0;for(int i=0;i<4;i++){if(pos_==source_.size())fail();char c=source_[pos_++];int v=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;if(v<0)fail();n=n*16+unsigned(v);}return n;}
    void utf8(std::string& out,unsigned cp) {
        if(cp<0x80)out+=char(cp);
        else if(cp<0x800){out+=char(0xc0|(cp>>6));out+=char(0x80|(cp&63));}
        else if(cp<0x10000){out+=char(0xe0|(cp>>12));out+=char(0x80|((cp>>6)&63));out+=char(0x80|(cp&63));}
        else {out+=char(0xf0|(cp>>18));out+=char(0x80|((cp>>12)&63));out+=char(0x80|((cp>>6)&63));out+=char(0x80|(cp&63));}
    }
    std::string string() {
        if(!take('"'))fail();
        std::string out;
        while(pos_<source_.size()) {
            unsigned char c=source_[pos_++];if(c=='"')return out;if(c<32)fail();
            if(c!='\\'){out+=char(c);continue;}
            if(pos_==source_.size())fail();
            char escape=source_[pos_++];
            switch(escape) {
            case '"':case '\\':case '/':out+=escape;break;
            case 'b':out+='\b';break;case 'f':out+='\f';break;case 'n':out+='\n';break;case 'r':out+='\r';break;case 't':out+='\t';break;
            case 'u':{unsigned cp=hex4();if(cp>=0xd800&&cp<=0xdbff){if(pos_+2>source_.size()||source_.substr(pos_,2)!="\\u")fail();pos_+=2;unsigned low=hex4();if(low<0xdc00||low>0xdfff)fail();cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);}else if(cp>=0xdc00&&cp<=0xdfff)fail();utf8(out,cp);break;}
            default:fail();
            }
        }
        fail();
    }
    Json value(int depth) {
        if(depth>24)fail();
        ws();if(pos_==source_.size())fail();
        Json result;char c=source_[pos_];
        if(c=='"'){result.value=string();return result;}
        if(take('{')){Json::Object object;if(!take('}')){do{auto key=string();if(!take(':'))fail();if(object.count(key))fail();object.emplace(std::move(key),value(depth+1));}while(take(','));if(!take('}'))fail();}result.value=std::move(object);return result;}
        if(take('[')){Json::Array array;if(!take(']')){do{array.push_back(value(depth+1));if(array.size()>10000)fail();}while(take(','));if(!take(']'))fail();}result.value=std::move(array);return result;}
        for(auto literal:{"true","false","null"})if(source_.compare(pos_,strlen(literal),literal)==0){pos_+=strlen(literal);if(literal[0]=='t')result.value=true;else if(literal[0]=='f')result.value=false;return result;}
        size_t start=pos_;if(source_[pos_]=='-')pos_++;if(pos_==source_.size())fail();
        if(source_[pos_]=='0')pos_++;else {if(source_[pos_]<'1'||source_[pos_]>'9')fail();while(pos_<source_.size()&&source_[pos_]>='0'&&source_[pos_]<='9')pos_++;}
        if(pos_<source_.size()&&source_[pos_]=='.'){pos_++;size_t digits=pos_;while(pos_<source_.size()&&source_[pos_]>='0'&&source_[pos_]<='9')pos_++;if(pos_==digits)fail();}
        if(pos_<source_.size()&&(source_[pos_]=='e'||source_[pos_]=='E')){pos_++;if(pos_<source_.size()&&(source_[pos_]=='+'||source_[pos_]=='-'))pos_++;size_t digits=pos_;while(pos_<source_.size()&&source_[pos_]>='0'&&source_[pos_]<='9')pos_++;if(pos_==digits)fail();}
        result.value=std::stod(source_.substr(start,pos_-start));if(!std::isfinite(std::get<double>(result.value)))fail();return result;
    }
public:
    explicit Reader(const std::string& source):source_(source){if(source.compare(0,3,"\xef\xbb\xbf")==0)pos_=3;}
    Json parse(){auto result=value(0);ws();if(pos_!=source_.size())fail();return result;}
};
}
std::vector<Station> readRadioConfig(const std::filesystem::path& file,std::wstring& error) {
    error.clear();
    try {
        std::ifstream input(file,std::ios::binary);if(!input)throw std::runtime_error("Cannot open Playlists/radio.json");
        std::string data;data.resize(1024*1024+1);input.read(data.data(),std::streamsize(data.size()));data.resize(size_t(input.gcount()));
        if(data.size()>1024*1024)throw std::runtime_error("radio.json exceeds 1 MiB");
        Json root=Reader(data).parse();auto* object=std::get_if<Json::Object>(&root.value);
        if(!object||!object->count("stations"))throw std::runtime_error("radio.json needs a stations array");
        auto* rows=std::get_if<Json::Array>(&object->at("stations").value);if(!rows)throw std::runtime_error("stations must be an array");
        std::vector<Station> result;std::set<int> frequencies;
        for(auto& item:*rows) {
            auto* station=std::get_if<Json::Object>(&item.value);
            if(!station||!station->count("mhz")||!station->count("name")||!station->count("url"))throw std::runtime_error("Each station needs mhz, name and url");
            auto* mhz=std::get_if<double>(&station->at("mhz").value);auto* name=std::get_if<std::string>(&station->at("name").value);auto* url=std::get_if<std::string>(&station->at("url").value);
            if(!mhz||!name||!url||name->empty()||name->size()>256||url->size()>8192)throw std::runtime_error("Invalid station field type or length");
            if(*mhz<87.5||*mhz>108)throw std::runtime_error("Frequency outside 87.5-108.0 MHz");
            int tenths=int(std::lround(*mhz*10));
            if(*mhz<87.5||*mhz>108||std::abs(*mhz*10-tenths)>.00001||!frequencies.insert(tenths).second)throw std::runtime_error("Frequencies must be unique 87.5-108.0 MHz, in 0.1 MHz steps");
            if((url->rfind("http://",0)!=0&&url->rfind("https://",0)!=0)||url->find_first_of("\r\n\t")!=std::string::npos||url->find('\0')!=std::string::npos)throw std::runtime_error("Station URL must use HTTP or HTTPS");
            result.push_back({tenths,decodeUtf8(*name),decodeUtf8(*url)});
        }
        std::sort(result.begin(),result.end(),[](auto& a,auto& b){return a.tenths<b.tenths;});return result;
    }catch(const std::exception& e){try{error=decodeUtf8(e.what());}catch(...){error=L"Invalid radio.json";}return {};}
}
}
