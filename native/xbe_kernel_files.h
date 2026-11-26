#pragma once
#include "xbox_guest_address_space.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <dirent.h>
#include <fcntl.h>
#include <map>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
namespace hulkpocket {
// Read-only disc directory. Every guest component is opened relative to a
// directory descriptor with O_NOFOLLOW; guest paths never become host paths.
class XbeDiscFiles {
    XboxGuestAddressSpace& mem_;
    int root_=-1;
    struct File { int fd; uint64_t pos; };
    std::map<uint32_t,File> files_;
    uint32_t next_=0x4000;
    static constexpr uint32_t invalid=0xc000000d, denied=0xc0000022,
        missing=0xc0000034, bad_handle=0xc0000008, small=0xc0000023;
    static std::string Upper(std::string s) {
        for(auto& c:s) if(c>='a'&&c<='z') c=char(c-32);
        return s;
    }
    static std::string Match(int dir,const std::string& name) {
        int copy=openat(dir,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
        if(copy<0) return {};
        DIR* d=fdopendir(copy);
        if(!d){close(copy);return {};}
        std::string result; const auto target=Upper(name);
        bool ambiguous=false;
        while(auto* e=readdir(d)) {
            std::string candidate=e->d_name;
            if(candidate==name){result=candidate;ambiguous=false;break;}
            if(Upper(candidate)==target) {
                if(!result.empty()) ambiguous=true;
                else result=candidate;
            }
        }
        closedir(d);return ambiguous?std::string{}:result;
    }
    int Resolve(std::string path) {
        if(root_<0) return -1;
        std::replace(path.begin(),path.end(),'/','\\');
        const auto upper=Upper(path);
        size_t start=std::string::npos;
        for(const std::string prefix:{"\\DEVICE\\CDROM0\\","\\??\\D:\\","D:\\"})
            if(upper.rfind(prefix,0)==0){start=prefix.size();break;}
        if(start==std::string::npos || start==path.size()) return -1;
        int dir=dup(root_); if(dir<0) return -1;
        while(start<path.size()) {
            auto end=path.find('\\',start);
            auto part=path.substr(start,end==std::string::npos?end:end-start);
            if(part.empty()||part=="."||part==".."||part.find(':')!=std::string::npos||
               part.find('\0')!=std::string::npos){close(dir);return -1;}
            auto match=Match(dir,part);
            if(match.empty()){close(dir);return -1;}
            const bool last=end==std::string::npos;
            int fd=openat(dir,match.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK|
                          (last?0:O_DIRECTORY));
            close(dir);
            if(fd<0) return -1;
            if(last) {
                struct stat st{};
                if(fstat(fd,&st)||!S_ISREG(st.st_mode)){close(fd);return -1;}
                return fd;
            }
            dir=fd;start=end+1;
        }
        close(dir);return -1;
    }
    uint32_t Finish(uint32_t iosb,uint32_t status,uint32_t info=0) {
        if(!mem_.IsCommitted(iosb,8)) return invalid;
        mem_.Write32(iosb,status);mem_.Write32(iosb+4,info);return status;
    }
    bool Name(uint32_t attrs,std::string& out) {
        if(!mem_.IsCommitted(attrs,12)||mem_.Read32(attrs)) return false;
        auto desc=mem_.Read32(attrs+4);
        if(!mem_.IsCommitted(desc,8)) return false;
        auto len=mem_.Read32(desc)&0xffffu, max=mem_.Read32(desc)>>16;
        auto p=mem_.Read32(desc+4);
        if(!len||len>max||len>4096||!mem_.IsCommitted(p,len)) return false;
        out.resize(len);mem_.Read(p,out.data(),len);
        return out.find('\0')==std::string::npos;
    }
public:
    explicit XbeDiscFiles(XboxGuestAddressSpace& mem,const std::string& root={}):mem_(mem) {
        if(!root.empty()) {
            root_=open(root.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
            if(root_<0) throw std::runtime_error("Cannot open selected game directory");
        }
    }
    ~XbeDiscFiles(){for(auto& kv:files_) close(kv.second.fd);if(root_>=0)close(root_);}
    XbeDiscFiles(const XbeDiscFiles&)=delete;
    XbeDiscFiles& operator=(const XbeDiscFiles&)=delete;
    bool Close(uint32_t h) {
        auto it=files_.find(h);if(it==files_.end())return false;
        close(it->second.fd);files_.erase(it);return true;
    }
    uint32_t Open(uint32_t out,uint32_t access,uint32_t attrs,uint32_t iosb,uint32_t disposition,uint32_t options) {
        if(!mem_.IsCommitted(out,4)||!mem_.IsCommitted(iosb,8))return invalid;
        // FILE_OPEN / FILE_OPEN_IF are allowed only for existing read-only files.
        if((disposition!=1&&disposition!=3)||(options&0x1001u)||
           (access&0x500d0116u))return Finish(iosb,denied);
        std::string name;
        if(!Name(attrs,name))return Finish(iosb,invalid);
        if(files_.size()>=256)return Finish(iosb,0xc000011fu);
        int fd=Resolve(name);if(fd<0)return Finish(iosb,missing);
        auto h=next_;next_+=4;
        files_.emplace(h,File{fd,0});mem_.Write32(out,h);
        return Finish(iosb,0,1);
    }
    uint32_t Read(uint32_t h,uint32_t iosb,uint32_t buf,uint32_t len,uint32_t offset) {
        if(!mem_.IsCommitted(iosb,8)||!mem_.IsCommitted(buf,len)||len>64u*1024*1024)return invalid;
        auto it=files_.find(h);if(it==files_.end())return Finish(iosb,bad_handle);
        auto& f=it->second;int64_t pos=-2;
        if(offset){if(!mem_.IsCommitted(offset,8))return Finish(iosb,invalid);mem_.Read(offset,&pos,8);}
        if(pos==-2)pos=static_cast<int64_t>(f.pos);
        if(pos<0||uint64_t(pos)+len>uint64_t(INT64_MAX))return Finish(iosb,invalid);
        if(!len)return Finish(iosb,0);
        ssize_t n;
        do{n=pread(f.fd,mem_.Base()+buf,len,static_cast<off_t>(pos));}while(n<0&&errno==EINTR);
        if(n<0)return Finish(iosb,0xc0000185u);
        f.pos=uint64_t(pos)+uint64_t(n);
        return Finish(iosb,n?0:0xc0000011u,static_cast<uint32_t>(n));
    }
    uint32_t Query(uint32_t h,uint32_t iosb,uint32_t out,uint32_t len,uint32_t cls) {
        if(!mem_.IsCommitted(iosb,8))return invalid;
        auto it=files_.find(h);if(it==files_.end())return Finish(iosb,bad_handle);
        struct stat st{};if(fstat(it->second.fd,&st))return Finish(iosb,0xc0000185u);
        std::array<uint32_t,14> data{};
        auto put64=[&](unsigned i,uint64_t v){data[i]=uint32_t(v);data[i+1]=uint32_t(v>>32);};
        uint32_t need=0;const auto size=uint64_t(st.st_size);
        const auto time=uint64_t(int64_t(st.st_mtime)+11644473600LL)*10000000;
        switch(cls) {
            case 4:need=40;for(unsigned i=0;i<8;i+=2)put64(i,time);data[8]=0x80;break;
            case 5:need=24;put64(0,(size+4095)&~uint64_t(4095));put64(2,size);data[4]=1;break;
            case 14:need=8;put64(0,it->second.pos);break;
            case 34:need=56;for(unsigned i=0;i<8;i+=2)put64(i,time);
                put64(8,(size+4095)&~uint64_t(4095));put64(10,size);data[12]=0x80;break;
            default:return Finish(iosb,invalid);
        }
        if(len<need)return Finish(iosb,small);
        if(!mem_.IsCommitted(out,need))return Finish(iosb,invalid);
        mem_.Write(out,data.data(),need);return Finish(iosb,0,need);
    }
    uint32_t Set(uint32_t h,uint32_t iosb,uint32_t in,uint32_t len,uint32_t cls) {
        if(!mem_.IsCommitted(iosb,8))return invalid;
        auto it=files_.find(h);if(it==files_.end())return Finish(iosb,bad_handle);
        if(cls!=14)return Finish(iosb,denied);
        if(len<8||!mem_.IsCommitted(in,8))return Finish(iosb,invalid);
        int64_t pos;mem_.Read(in,&pos,8);
        if(pos<0)return Finish(iosb,invalid);
        it->second.pos=static_cast<uint64_t>(pos);return Finish(iosb,0);
    }
};
}
