#pragma once
#include "xbox_guest_address_space.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <dirent.h>
#include <fcntl.h>
#include <map>
#include <memory>
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <limits>
#include <unistd.h>
#include <vector>
#include <optional>
namespace hulkpocket {
// Read-only disc directory. Every guest component is opened relative to a
// directory descriptor with O_NOFOLLOW; guest paths never become host paths.
class XbeDiscFiles {
    XboxGuestAddressSpace& mem_;
    int root_=-1, partition1_=-1;
    static constexpr uint32_t kDosDevicesDirectory=0xfffffffdu; // Xbox ObDosDevicesDirectory() == (HANDLE)-3
    struct DirEntry {std::string name;struct stat metadata;};
    std::optional<uint64_t> disc_units_;
    struct File {
        int fd; bool directory,partition1; uint64_t pos=0;
        bool listed=false;
        size_t next_entry=0;
        std::vector<DirEntry> entries;
        explicit File(int value,bool dir,bool hdd):fd(value),directory(dir),partition1(hdd){}
        ~File(){if(fd>=0)close(fd);}
        File(const File&)=delete;
        File& operator=(const File&)=delete;
    };
    std::map<uint32_t,std::shared_ptr<File>> files_;
    // Guest object namespace only; never create host symbolic links.
    std::map<std::string,std::string> links_{{"D:","\\Device\\CdRom0"}};
    std::map<uint32_t,std::string> link_handles_;
    uint32_t next_=0x4000;
    static constexpr uint32_t invalid=0xc000000d, denied=0xc0000022,
        missing=0xc0000034, bad_handle=0xc0000008, small=0xc0000023;
    static std::string Upper(std::string s) {
        for(auto& c:s) if(c>='a'&&c<='z') c=char(c-32);
        return s;
    }
    static bool DriveName(std::string name,std::string& key){
        std::replace(name.begin(),name.end(),'/','\\');
        name=Upper(name);
        if(name.rfind("\\??\\",0)==0)name.erase(0,4);
        if(name.size()!=2||name[0]<'A'||name[0]>'Z'||name[1]!=':')return false;
        key=name;return true;
    }
    bool Ansi(uint32_t desc,std::string& out,bool allow_empty=false){
        if(!desc||!mem_.IsCommitted(desc,8))return false;
        const auto lengths=mem_.Read32(desc);
        const auto len=lengths&0xffffu,max=lengths>>16,p=mem_.Read32(desc+4);
        if(len>max||len>4096||(!len&&!allow_empty)||(len&&(!p||!mem_.IsCommitted(p,len))))return false;
        out.resize(len);if(len)mem_.Read(p,out.data(),len);
        return out.find('\0')==std::string::npos;
    }
    static bool DeviceTarget(std::string& target){
        std::replace(target.begin(),target.end(),'/','\\');
        const auto up=Upper(target);
        // A namespace binding may name an unmounted Xbox device. Opening
        // such a target must still fail; there is no fallback to host paths.
        if(up.rfind("\\DEVICE\\",0)!=0||target.size()<=8)return false;
        size_t start=8;
        while(start<target.size()){
            auto end=target.find('\\',start);
            auto part=target.substr(start,end==std::string::npos?end:end-start);
            if(part.empty()||part=="."||part==".."||part.find(':')!=std::string::npos)return false;
            for(unsigned char c:part)if(c<32||c>126)return false;
            if(end==std::string::npos)break;
            start=end+1;
        }
        while(target.size()>8&&target.back()=='\\')target.pop_back();
        return true;
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
    // Enumerate descriptors, never follow symlinks or special files.
    // Bounded for malformed or accidentally oversized selected directories.
    static bool List(int dir,std::vector<DirEntry>& entries,size_t limit){
        int copy=openat(dir,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
        if(copy<0)return false;
        DIR* d=fdopendir(copy);
        if(!d){close(copy);return false;}
        bool ok=true;
        size_t seen=0;
        while(true){
            errno=0;auto* e=readdir(d);
            if(!e){if(errno)ok=false;break;}
            if(++seen>limit+2){ok=false;break;}
            std::string name=e->d_name;
            if(name=="."||name=="..")continue;
            struct stat st{};
            if(fstatat(dir,name.c_str(),&st,AT_SYMLINK_NOFOLLOW)){ok=false;break;}
            if(!S_ISREG(st.st_mode)&&!S_ISDIR(st.st_mode))continue;
            if(entries.size()>=limit||st.st_size<0){ok=false;break;}
            entries.push_back({std::move(name),st});
        }
        closedir(d);
        if(ok)std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){
            const auto x=Upper(a.name),y=Upper(b.name);
            return x==y?a.name<b.name:x<y;
        });
        return ok;
    }
    static bool WildMatch(std::string pattern,std::string name){
        pattern=Upper(std::move(pattern));name=Upper(std::move(name));
        if(pattern=="*.*")pattern="*";
        size_t p=0,n=0,star=std::string::npos,retry=0;
        while(n<name.size()){
            if(p<pattern.size()&&(pattern[p]=='?'||pattern[p]==name[n])){++p;++n;}
            else if(p<pattern.size()&&pattern[p]=='*'){star=p++;retry=n;}
            else if(star!=std::string::npos){p=star+1;n=++retry;}
            else return false;
        }
        while(p<pattern.size()&&pattern[p]=='*')++p;
        return p==pattern.size();
    }
    static bool MeasureDisc(int dir,unsigned depth,size_t& remaining,uint64_t& sectors){
        if(depth>32||!remaining)return false;
        std::vector<DirEntry> entries;
        if(!List(dir,entries,std::min<size_t>(remaining,65536)))return false;
        remaining-=entries.size();
        for(const auto& entry:entries){
            if(S_ISDIR(entry.metadata.st_mode)){
                int child=openat(dir,entry.name.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
                if(child<0)return false;
                const bool ok=MeasureDisc(child,depth+1,remaining,sectors);
                close(child);if(!ok)return false;
            }else{
                const auto count=(uint64_t(entry.metadata.st_size)+2047)/2048;
                if(count>uint64_t(INT64_MAX)-sectors)return false;
                sectors+=count;
            }
        }
        return true;
    }
    int Resolve(std::string path,uint32_t root=0,bool* on_partition1=nullptr) {
        std::replace(path.begin(),path.end(),'/','\\');
        size_t start=0;
        int parent=root_;
        bool hdd=false;
        if(root&&root!=kDosDevicesDirectory){
            auto it=files_.find(root);
            if(it==files_.end()||!it->second->directory)return -1;
            if(!path.empty()&&(path.front()=='\\'||path.find(':')!=std::string::npos))return -1;
            parent=it->second->fd;hdd=it->second->partition1;
        }else{
            start=std::string::npos;
            // Resolve one DOS drive alias to an absolute guest device.
            // Targets are restricted to the Device namespace, so no cycles.
            std::string dos=path;
            if(root!=kDosDevicesDirectory&&Upper(dos).rfind("\\??\\",0)==0)dos.erase(0,4);
            if(dos.size()>=2&&dos[1]==':'){
                if(dos.size()>2&&dos[2]!='\\')return -1;
                auto it=links_.find(Upper(dos.substr(0,2)));
                if(it==links_.end())return -1;
                path=it->second+dos.substr(2);
            }else if(root==kDosDevicesDirectory)return -1;
            const auto upper=Upper(path);
            for(const std::string prefix:{"\\DEVICE\\CDROM0","\\DEVICE\\HARDDISK0\\PARTITION1"}){
                if(upper==prefix)start=prefix.size();
                else if(upper.rfind(prefix+"\\",0)==0)start=prefix.size()+1;
                else continue;
                if(prefix=="\\DEVICE\\HARDDISK0\\PARTITION1"){parent=partition1_;hdd=true;}
                break;
            }
            if(start==std::string::npos||parent<0)return -1;
        }
        if(on_partition1)*on_partition1=hdd;
        int dir=openat(parent,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
        if(dir<0)return -1;
        if(start==path.size())return dir;
        while(start<path.size()) {
            auto end=path.find('\\',start);
            auto part=path.substr(start,end==std::string::npos?end:end-start);
            if(part.empty()||part=="."||part==".."||part.find(':')!=std::string::npos||
               part.find('\0')!=std::string::npos){close(dir);return -1;}
            auto match=Match(dir,part);
            if(match.empty()){close(dir);return -1;}
            const bool last=end==std::string::npos||end+1==path.size();
            int fd=openat(dir,match.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK|
                          (end==std::string::npos?0:O_DIRECTORY));
            close(dir);
            if(fd<0)return -1;
            if(last){
                struct stat st{};
                if(fstat(fd,&st)||(!S_ISREG(st.st_mode)&&!S_ISDIR(st.st_mode))){close(fd);return -1;}
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
    bool Name(uint32_t attrs,std::string& out,uint32_t& root) {
        if(!mem_.IsCommitted(attrs,12)) return false;
        root=mem_.Read32(attrs);
        return Ansi(mem_.Read32(attrs+4),out,root!=0);
    }
    bool ValidRoot(uint32_t root)const{
        return root==0||root==kDosDevicesDirectory||
            (Contains(root)&&files_.at(root)->directory);
    }
    static std::string LogName(std::string name){
        for(auto& c:name)if(static_cast<unsigned char>(c)<32||static_cast<unsigned char>(c)>126)c='?';
        return name;
    }
public:
    explicit XbeDiscFiles(XboxGuestAddressSpace& mem,const std::string& root={},const std::string& partition1={}):mem_(mem) {
        if(!root.empty()) {
            root_=open(root.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
            if(root_<0) throw std::runtime_error("Cannot open selected game directory");
            std::fprintf(stderr,"DISC ROOT OPEN PASS (read-only directory)\n");
        }else std::fprintf(stderr,"DISC ROOT NOT SELECTED\n");
        if(!partition1.empty()){
            partition1_=open(partition1.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
            if(partition1_<0){if(root_>=0)close(root_);throw std::runtime_error("Cannot open selected private Xbox partition1 directory");}
            struct stat disc{},data{};
            if(root_>=0&&(fstat(root_,&disc)||fstat(partition1_,&data)||
                (disc.st_dev==data.st_dev&&disc.st_ino==data.st_ino))){
                close(root_);close(partition1_);throw std::runtime_error("Disc and Xbox partition1 must be distinct");
            }
            std::fprintf(stderr,"XBOX PARTITION1 ROOT OPEN PASS (read-only directory)\n");
        }
    }
    ~XbeDiscFiles(){files_.clear();if(root_>=0)close(root_);if(partition1_>=0)close(partition1_);}
    XbeDiscFiles(const XbeDiscFiles&)=delete;
    XbeDiscFiles& operator=(const XbeDiscFiles&)=delete;
    uint32_t CreateLink(uint32_t name_desc,uint32_t target_desc){
        std::string name,target,key;
        if(!Ansi(name_desc,name)||!Ansi(target_desc,target)||!DriveName(name,key)||!DeviceTarget(target)){
            std::fprintf(stderr,"XBOX LINK CREATE unsupported name=%s target=%s\n",LogName(name).c_str(),LogName(target).c_str());
            return invalid;
        }
        auto it=links_.find(key);
        if(it!=links_.end()&&Upper(it->second)!=Upper(target)){
            std::fprintf(stderr,"XBOX LINK COLLISION name=%s existing=%s requested=%s\n",key.c_str(),it->second.c_str(),target.c_str());
            return 0xc0000035u;
        }
        links_[key]=target;
        std::fprintf(stderr,"XBOX LINK CREATE name=%s target=%s\n",key.c_str(),target.c_str());
        return 0;
    }
    uint32_t DeleteLink(uint32_t name_desc){
        std::string name,key;
        if(!Ansi(name_desc,name)||!DriveName(name,key))return invalid;
        const bool removed=links_.erase(key)!=0;
        std::fprintf(stderr,"XBOX LINK DELETE name=%s removed=%u\n",key.c_str(),unsigned(removed));
        return removed?0:missing;
    }
    uint32_t OpenLink(uint32_t out,uint32_t attrs){
        if(!out||!mem_.IsCommitted(out,4))return invalid;
        std::string name,key;uint32_t root=0;
        if(!Name(attrs,name,root)||!DriveName(name,key))return invalid;
        if(root!=0&&root!=kDosDevicesDirectory)return bad_handle;
        auto it=links_.find(key);if(it==links_.end())return missing;
        if(files_.size()+link_handles_.size()>=256)return 0xc000011fu;
        const auto h=next_;next_+=4;link_handles_.emplace(h,it->second);
        mem_.Write32(out,h);return 0;
    }
    uint32_t QueryLink(uint32_t h,uint32_t desc,uint32_t returned){
        auto it=link_handles_.find(h);if(it==link_handles_.end())return bad_handle;
        if(!desc||!mem_.IsCommitted(desc,8)||(returned&&!mem_.IsCommitted(returned,4)))return invalid;
        const auto max=mem_.Read32(desc)>>16,p=mem_.Read32(desc+4);
        const auto n=uint32_t(it->second.size());
        if(returned)mem_.Write32(returned,n);
        if(max<n)return small;
        if(!p||!mem_.IsCommitted(p,n))return invalid;
        mem_.Write(p,it->second.data(),n);mem_.Write32(desc,(max<<16)|n);
        return 0;
    }
    bool Close(uint32_t h) {
        if(link_handles_.erase(h))return true;
        auto it=files_.find(h);if(it==files_.end())return false;
        files_.erase(it);return true;
    }
    uint32_t Open(uint32_t out,uint32_t access,uint32_t attrs,uint32_t iosb,uint32_t disposition,uint32_t options) {
        if(!mem_.IsCommitted(out,4)||!mem_.IsCommitted(iosb,8))return invalid;
        // Decode first for diagnostics even when policy denies the request.
        std::string name;uint32_t root=0;
        if(!Name(attrs,name,root)){
            std::fprintf(stderr,"DISC NAME INVALID attrs=%08X root=%08X options=%08X\n",attrs,root,options);
            return Finish(iosb,invalid);
        }
        std::fprintf(stderr,"DISC REQUEST root=%08X access=%08X disposition=%u options=%08X name=%s\n",
            root,access,disposition,options,LogName(name).c_str());
        // FILE_DIRECTORY_FILE is not a write. FILE_DELETE_ON_CLOSE is.
        // Never create or modify a file; OPEN_IF only opens existing entries.
        if((disposition!=1&&disposition!=3)||(options&0x1000u)||
           (access&0x500d0116u))return Finish(iosb,denied);
        if((options&0x41u)==0x41u)return Finish(iosb,invalid);
        if(!ValidRoot(root))return Finish(iosb,bad_handle);
        if(files_.size()+link_handles_.size()>=256)return Finish(iosb,0xc000011fu);
        bool hdd=false;
        int fd=Resolve(name,root,&hdd);
        if(fd<0)return Finish(iosb,missing);
        struct stat st{};
        if(fstat(fd,&st)){close(fd);return Finish(iosb,0xc0000185u);}
        const bool directory=S_ISDIR(st.st_mode);
        if(directory&&(options&0x40u)){close(fd);return Finish(iosb,0xc00000bau);}
        if(!directory&&(options&1u)){close(fd);return Finish(iosb,0xc0000103u);}
        std::shared_ptr<File> file;
        try{file=std::make_shared<File>(fd,directory,hdd);}catch(...){close(fd);throw;}
        std::fprintf(stderr,"DISC OPEN PASS type=%s name=%s\n",directory?"directory":"file",LogName(name).c_str());
        auto h=next_;next_+=4;
        files_.emplace(h,std::move(file));mem_.Write32(out,h);
        return Finish(iosb,0,1);
    }
    bool Contains(uint32_t h)const{return files_.count(h)!=0;}
    uint32_t Duplicate(uint32_t h,uint32_t out,bool close_source){
        auto it=files_.find(h);if(it==files_.end())return bad_handle;
        if(!mem_.IsCommitted(out,4))return invalid;
        if(files_.size()+link_handles_.size()>=256)return 0xc000011fu;
        auto alias=next_;next_+=4;
        files_.emplace(alias,it->second);
        mem_.Write32(out,alias);
        if(close_source)files_.erase(h);
        return 0;
    }
    uint32_t Flush(uint32_t h,uint32_t iosb){
        if(!mem_.IsCommitted(iosb,8))return invalid;
        if(!Contains(h))return Finish(iosb,bad_handle);
        // Read-only disc handles have no dirty host data to flush.
        return Finish(iosb,0);
    }
    uint32_t FullAttributes(uint32_t attrs,uint32_t out){
        if(!mem_.IsCommitted(out,56))return invalid;
        std::string name;uint32_t root=0;if(!Name(attrs,name,root))return invalid;
        if(!ValidRoot(root))return bad_handle;
        int fd=Resolve(name,root);if(fd<0)return missing;
        struct stat st{};int status=fstat(fd,&st);close(fd);
        if(status||st.st_size<0)return 0xc0000185u;
        std::array<uint32_t,14> data{};
        auto put64=[&](unsigned i,uint64_t v){data[i]=uint32_t(v);data[i+1]=uint32_t(v>>32);};
        auto ticks=[](int64_t seconds){return seconds < -11644473600LL?0ull:uint64_t(seconds+11644473600LL)*10000000ull;};
        // POSIX has no portable creation timestamp. Match the existing
        // FileBasicInformation convention using last-modified time for creation.
        put64(0,ticks(st.st_mtime));put64(2,ticks(st.st_atime));
        put64(4,ticks(st.st_mtime));put64(6,ticks(st.st_ctime));
        const bool directory=S_ISDIR(st.st_mode);
        const auto size=directory?0ull:uint64_t(st.st_size);
        put64(8,(size+4095)&~uint64_t(4095));put64(10,size);
        data[12]=directory?0x11:1; // READONLY and optionally DIRECTORY
        mem_.Write(out,data.data(),sizeof(data));return 0;
    }
    uint32_t Read(uint32_t h,uint32_t iosb,uint32_t buf,uint32_t len,uint32_t offset) {
        if(!mem_.IsCommitted(iosb,8)||!mem_.IsCommitted(buf,len)||len>64u*1024*1024)return invalid;
        auto it=files_.find(h);if(it==files_.end())return Finish(iosb,bad_handle);
        auto& f=*it->second;
        if(f.directory)return Finish(iosb,0xc00000bau);
        int64_t pos=-2;
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
    // Xbox FILE_DIRECTORY_INFORMATION (ANSI/OCHAR name, offset 0x40).
    // One entry per call, as in the pinned XboxXBERecomp implementation.
    uint32_t QueryDirectory(uint32_t h,uint32_t iosb,uint32_t out,uint32_t len,
                            uint32_t cls,uint32_t mask,bool restart){
        if(!mem_.IsCommitted(iosb,8))return invalid;
        auto it=files_.find(h);
        if(it==files_.end()||!it->second->directory)return Finish(iosb,bad_handle);
        if(cls!=1)return Finish(iosb,0xc0000003u);
        auto& file=*it->second;
        if(!file.listed||restart){
            std::string pattern="*";
            if(mask&&(!Ansi(mask,pattern)||pattern.find_first_of("\\/:")!=std::string::npos))
                return Finish(iosb,invalid);
            std::vector<DirEntry> found;
            if(!List(file.fd,found,65536))return Finish(iosb,0xc0000185u);
            found.erase(std::remove_if(found.begin(),found.end(),[&](const auto& e){
                return !WildMatch(pattern,e.name);
            }),found.end());
            file.entries=std::move(found);file.next_entry=0;file.listed=true;
        }
        if(file.next_entry>=file.entries.size())
            return Finish(iosb,file.next_entry?0x80000006u:0xc000000fu);
        const auto& entry=file.entries[file.next_entry];
        const auto need=uint32_t(64+entry.name.size());
        if(len<need)return Finish(iosb,small);
        if(!mem_.IsCommitted(out,need))return Finish(iosb,invalid);
        std::array<uint32_t,16> data{};
        auto put64=[&](unsigned i,uint64_t v){data[i]=uint32_t(v);data[i+1]=uint32_t(v>>32);};
        auto ticks=[](int64_t seconds){return seconds < -11644473600LL?0ull:uint64_t(seconds+11644473600LL)*10000000ull;};
        const auto& st=entry.metadata;
        put64(2,ticks(st.st_mtime));put64(4,ticks(st.st_atime));
        put64(6,ticks(st.st_mtime));put64(8,ticks(st.st_ctime));
        const bool directory=S_ISDIR(st.st_mode);
        const auto size=directory?0ull:uint64_t(st.st_size);
        put64(10,size);put64(12,(size+4095)&~uint64_t(4095));
        data[14]=directory?0x11:1;data[15]=uint32_t(entry.name.size());
        mem_.Write(out,data.data(),64);mem_.Write(out+64,entry.name.data(),entry.name.size());
        ++file.next_entry;return Finish(iosb,0,need);
    }
    // Xbox FS_INFORMATION_CLASS layout from nxdk xboxkrnl.h.
    // The directory-backed partition exposes backing filesystem capacity
    // in Xbox-style 16 KiB units. It does not claim a mounted FATX image.
    uint32_t QueryVolume(uint32_t h,uint32_t iosb,uint32_t out,uint32_t len,uint32_t cls){
        if(!mem_.IsCommitted(iosb,8))return invalid;
        auto it=files_.find(h);if(it==files_.end())return Finish(iosb,bad_handle);
        const bool hdd=it->second->partition1;
        std::array<uint32_t,8> data{};
        auto put64=[&](unsigned i,uint64_t v){data[i]=uint32_t(v);data[i+1]=uint32_t(v>>32);};
        uint32_t need=0;
        switch(cls){
            case 3: // FileFsSizeInformation
            case 7: { // FileFsFullSizeInformation
                need=cls==3?24:32;
                if(len<need)return Finish(iosb,small);
                if(!hdd){
                    // Extracted data only: logical payload sectors, not the
                    // physical DVD image layout or host free storage.
                    if(!disc_units_){
                        size_t remaining=262144;uint64_t sectors=0;
                        if(root_<0||!MeasureDisc(root_,0,remaining,sectors))
                            return Finish(iosb,0xc0000185u);
                        disc_units_=sectors;
                        std::fprintf(stderr,"XBOX VOLUME SIZE disc logical_payload_sectors=%llu sector_bytes=2048 (read-only extracted files)\n",
                            static_cast<unsigned long long>(sectors));
                    }
                    put64(0,*disc_units_);
                    if(cls==7){data[6]=1;data[7]=2048;}
                    else {data[4]=1;data[5]=2048;}
                    break;
                }
                struct statvfs st{};
                if(fstatvfs(it->second->fd,&st))return Finish(iosb,0xc0000185u);
                const uint64_t block=st.f_frsize?st.f_frsize:st.f_bsize;
                if(!block)return Finish(iosb,invalid);
                auto units=[&](uint64_t n){
                    // Avoid overflow, including on unusually large host volumes.
                    if(n>std::numeric_limits<uint64_t>::max()/block)
                        return std::numeric_limits<uint64_t>::max()/16384;
                    return n*block/16384;
                };
                const auto total=units(st.f_blocks);
                const auto available=std::min(total,units(st.f_bavail));
                const auto actual=std::min(total,units(st.f_bfree));
                put64(0,total);put64(2,available);
                if(cls==7){put64(4,actual);data[6]=32;data[7]=512;}
                else {data[4]=32;data[5]=512;}
                std::fprintf(stderr,"XBOX VOLUME SIZE partition1 total_units=%llu available_units=%llu unit_bytes=16384 (host backed; writes disabled)\n",
                    static_cast<unsigned long long>(total),static_cast<unsigned long long>(available));
                break;
            }
            case 4: // FileFsDeviceInformation: virtual device, read-only
                need=8;data[0]=hdd?7u:2u;data[1]=hdd?2u:3u;break;
            default:return Finish(iosb,0xc0000003u); // STATUS_INVALID_INFO_CLASS
        }
        if(len<need)return Finish(iosb,small);
        if(!mem_.IsCommitted(out,need))return Finish(iosb,invalid);
        mem_.Write(out,data.data(),need);return Finish(iosb,0,need);
    }
    uint32_t Query(uint32_t h,uint32_t iosb,uint32_t out,uint32_t len,uint32_t cls) {
        if(!mem_.IsCommitted(iosb,8))return invalid;
        auto it=files_.find(h);if(it==files_.end())return Finish(iosb,bad_handle);
        struct stat st{};if(fstat(it->second->fd,&st))return Finish(iosb,0xc0000185u);
        std::array<uint32_t,14> data{};
        auto put64=[&](unsigned i,uint64_t v){data[i]=uint32_t(v);data[i+1]=uint32_t(v>>32);};
        uint32_t need=0;const bool directory=it->second->directory;
        const auto size=directory?0ull:uint64_t(st.st_size);
        const auto time=uint64_t(int64_t(st.st_mtime)+11644473600LL)*10000000;
        switch(cls) {
            case 4:need=40;for(unsigned i=0;i<8;i+=2)put64(i,time);data[8]=directory?0x11:0x80;break;
            case 5:need=24;put64(0,(size+4095)&~uint64_t(4095));put64(2,size);data[4]=1;data[5]=directory?0x100:0;break;
            case 14:if(directory)return Finish(iosb,invalid);need=8;put64(0,it->second->pos);break;
            case 34:need=56;for(unsigned i=0;i<8;i+=2)put64(i,time);
                put64(8,(size+4095)&~uint64_t(4095));put64(10,size);data[12]=directory?0x11:0x80;break;
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
        if(it->second->directory)return Finish(iosb,invalid);
        if(len<8||!mem_.IsCommitted(in,8))return Finish(iosb,invalid);
        int64_t pos;mem_.Read(in,&pos,8);
        if(pos<0)return Finish(iosb,invalid);
        it->second->pos=static_cast<uint64_t>(pos);return Finish(iosb,0);
    }
};
}
