#pragma once
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <cstdio>
namespace hulkpocket {
// Explicit, bounded diagnostic input. Xbox state layout verified against
// the pinned XboxXBERecomp runtime/xdk/xapi.cpp; no Android device claimed.
class HulkPadScript {
    struct Pulse {unsigned frame;std::array<uint8_t,18> state;};
    std::vector<Pulse> pulses_;
    static void Put16(std::array<uint8_t,18>& a,unsigned i,uint16_t v){
        a[i]=uint8_t(v);a[i+1]=uint8_t(v>>8);
    }
public:
    static constexpr unsigned duration=12;
    using State=std::array<uint8_t,18>;
    bool Enabled()const{return !pulses_.empty();}
    void Configure(const char* value,unsigned max_frame){
        std::vector<Pulse> next;
        std::string_view rest=value?value:"";
        if(rest.size()>2048)throw std::invalid_argument("Input script too long");
        while(!rest.empty()){
            auto comma=rest.find(',');
            auto item=rest.substr(0,comma);
            auto colon=item.find(':');
            if(colon==std::string_view::npos||colon==0||colon+1==item.size()||next.size()>=64)
                throw std::invalid_argument("Invalid input script entry (frame:button)");
            unsigned frame=0;
            auto result=std::from_chars(item.data(),item.data()+colon,frame);
            if(result.ec!=std::errc{}||result.ptr!=item.data()+colon||max_frame<duration||
               frame>max_frame-duration||(!next.empty()&&frame<next.back().frame+duration+1))
                throw std::invalid_argument("Invalid or overlapping input pulse");
            auto button=item.substr(colon+1);
            State s{};
            if(button=="UP")Put16(s,0,1);
            else if(button=="DOWN")Put16(s,0,2);
            else if(button=="LEFT")Put16(s,0,4);
            else if(button=="RIGHT")Put16(s,0,8);
            else if(button=="START")Put16(s,0,16);
            else if(button=="BACK")Put16(s,0,32);
            else if(button=="LS")Put16(s,0,64);
            else if(button=="RS")Put16(s,0,128);
            else if(button=="A")s[2]=255;
            else if(button=="B")s[3]=255;
            else if(button=="X")s[4]=255;
            else if(button=="Y")s[5]=255;
            else if(button=="BLACK")s[6]=255;
            else if(button=="WHITE")s[7]=255;
            else if(button=="LT")s[8]=255;
            else if(button=="RT")s[9]=255;
            else if(button=="LX+")Put16(s,10,32767);
            else if(button=="LX-")Put16(s,10,32768);
            else if(button=="LY+")Put16(s,12,32767);
            else if(button=="LY-")Put16(s,12,32768);
            else if(button=="RX+")Put16(s,14,32767);
            else if(button=="RX-")Put16(s,14,32768);
            else if(button=="RY+")Put16(s,16,32767);
            else if(button=="RY-")Put16(s,16,32768);
            else throw std::invalid_argument("Unknown input script button");
            next.push_back({frame,s});
            if(comma==std::string_view::npos)break;
            rest.remove_prefix(comma+1);
            if(rest.empty())throw std::invalid_argument("Trailing input separator");
        }
        pulses_=std::move(next);
    }
    State At(unsigned frame)const{
        for(const auto& p:pulses_){
            if(frame<p.frame)break;
            if(frame-p.frame<duration)return p.state;
        }
        return {};
    }
    static std::array<uint8_t,22> Packet(uint32_t number,const State& s){
        std::array<uint8_t,22> result{};
        for(unsigned i=0;i<4;++i)result[i]=uint8_t(number>>(8*i));
        for(unsigned i=0;i<s.size();++i)result[i+4]=s[i];
        return result;
    }
    static std::array<uint8_t,25> Capabilities(){
        std::array<uint8_t,25> c{};c[0]=1;c[3]=255;
        for(unsigned i=5;i<21;++i)c[i]=255;
        // Motors are not exposed by the scripted provider.
        return c;
    }
};
}
