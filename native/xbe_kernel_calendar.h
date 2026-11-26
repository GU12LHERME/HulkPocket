#pragma once
#include <cstdint>
#include <stdexcept>
namespace hulkpocket {
struct XboxTimeFields {int16_t year,month,day,hour,minute,second,milliseconds,weekday;};
static_assert(sizeof(XboxTimeFields)==16);
inline int64_t CivilDays(int y,unsigned m,unsigned d){
    y-=m<=2;int era=(y>=0?y:y-399)/400;unsigned yoe=unsigned(y-era*400);
    unsigned doy=(153*(m>2?m-3:m+9)+2)/5+d-1;
    return int64_t(era)*146097+yoe*365+yoe/4-yoe/100+doy-719468;
}
inline bool XboxFieldsToTicks(const XboxTimeFields& f,uint64_t& out){
    if(f.year<1601||f.year>30827||f.month<1||f.month>12||f.day<1||
       f.hour<0||f.hour>23||f.minute<0||f.minute>59||f.second<0||f.second>59||
       f.milliseconds<0||f.milliseconds>999)return false;
    const int lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};
    bool leap=f.year%4==0&&(f.year%100!=0||f.year%400==0);
    if(f.day>lengths[f.month-1]+(f.month==2&&leap))return false;
    uint64_t secs=uint64_t((CivilDays(f.year,f.month,f.day)-CivilDays(1601,1,1))*86400+
        f.hour*3600+f.minute*60+f.second);
    uint64_t ms=uint64_t(f.milliseconds)*10000;
    if(secs>(uint64_t(INT64_MAX)-ms)/10000000)return false;
    out=secs*10000000+ms;return true;
}
inline XboxTimeFields XboxTicksToFields(uint64_t ticks){
    if(ticks>uint64_t(INT64_MAX))throw std::invalid_argument("Negative Xbox time");
    int64_t secs=int64_t(ticks/10000000),days=secs/86400+CivilDays(1601,1,1),rem=secs%86400;
    int64_t z=days+719468,era=(z>=0?z:z-146096)/146097;
    unsigned doe=unsigned(z-era*146097),yoe=(doe-doe/1460+doe/36524-doe/146096)/365;
    int y=int(yoe+era*400);unsigned doy=doe-(365*yoe+yoe/4-yoe/100),mp=(5*doy+2)/153;
    unsigned d=doy-(153*mp+2)/5+1,m=mp<10?mp+3:mp-9;y+=m<=2;
    return {int16_t(y),int16_t(m),int16_t(d),int16_t(rem/3600),int16_t(rem/60%60),
        int16_t(rem%60),int16_t(ticks/10000%1000),int16_t((days%7+11)%7)};
}
}
