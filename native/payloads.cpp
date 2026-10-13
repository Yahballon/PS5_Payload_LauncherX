// SPDX-License-Identifier: GPL-3.0-or-later
#include "payloads.hpp"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string_view>
#include <vector>
#include <dirent.h>
#include <map>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <unistd.h>
namespace launcher {
namespace {
constexpr std::size_t kMaxEntries = 512;
constexpr std::size_t kMaxJson = 16 * 1024;
bool regular_file(const std::string& p) {
    struct stat st{};
    return lstat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}
bool directory(const std::string& p) {
    struct stat st{};
    return lstat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
bool suffix(std::string_view s, std::string_view end) {
    return s.size() >= end.size() &&
        std::equal(end.rbegin(), end.rend(), s.rbegin(), [](char a,char b) {
            return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
        });
}
bool executable_extension(const std::string& s) {
    return suffix(s, ".elf") || suffix(s, ".efl") || suffix(s, ".bin");
}
std::string trim(const std::string& s, std::size_t maxsize) {
    std::size_t b = s.find_first_not_of(" \r\n\t");
    if (b == std::string::npos) return {};
    std::size_t e = s.find_last_not_of(" \r\n\t");
    std::string r = s.substr(b, e - b + 1);
    if (r.size() > maxsize) r.resize(maxsize);
    return r;
}
// Strictly parse top-level JSON object with string values. Unknown values are
// skipped, never evaluated; malformed metadata falls back to folder labels.
class Parser {
public:
    explicit Parser(std::string_view s) : s_(s) {}
    std::map<std::string,std::string> fields() {
        std::map<std::string,std::string> out;
        ws(); if (!take('{')) return {};
        ws(); if (take('}')) return out;
        for (int n = 0; n < 96; ++n) {
            std::string key, value; if (!quoted(key)) return {};
            ws(); if (!take(':')) return {};
            ws(); if (!quoted(value)) {
                // Unknown non-string scalar/object is ignored safely.
                if (!skip()) return {};
            } else { out[key] = value; }
            ws(); if (take('}')) return out;
            if (!take(',')) return {};
        }
        return {};
    }
private:
    std::string_view s_; std::size_t p_ = 0;
    void ws(){while(p_<s_.size()&&(s_[p_]==' '||s_[p_]=='\t'||s_[p_]=='\r'||s_[p_]=='\n'))++p_;}
    bool take(char c){ws(); if(p_<s_.size()&&s_[p_]==c){++p_;return true;}return false;}
    static void unicode(std::string& o, unsigned cp){
        if(cp<0x80)o.push_back((char)cp);
        else if(cp<0x800){o.push_back((char)(0xc0|(cp>>6)));o.push_back((char)(0x80|(cp&63)));}
        else {o.push_back((char)(0xe0|(cp>>12)));o.push_back((char)(0x80|((cp>>6)&63)));o.push_back((char)(0x80|(cp&63)));}
    }
    bool quoted(std::string& o) {
        ws();if(p_>=s_.size()||s_[p_]!='"')return false;
        ++p_;
        while(p_<s_.size()) {
            unsigned char c=(unsigned char)s_[p_++];
            if(c=='"')return true;
            if(c<32)return false;
            if(c=='\\') {
                if(p_>=s_.size())return false;
                char esc=s_[p_++];
                if(esc=='n')c='\n'; else if(esc=='r')c='\r'; else if(esc=='t')c='\t';
                else if(esc=='"'||esc=='\\'||esc=='/')c=(unsigned char)esc;
                else if(esc=='u') {
                    if(p_+4>s_.size())return false;
                    unsigned n=0;for(int i=0;i<4;++i){char h=s_[p_++];unsigned d= h>='0'&&h<='9'?h-'0':h>='a'&&h<='f'?h-'a'+10:h>='A'&&h<='F'?h-'A'+10:16;if(d>15)return false;n=n*16+d;}
                    if(n>=0xd800&&n<=0xdfff)return false;
                    if(o.size()<4096)unicode(o,n);
                    continue;
                } else return false;
            }
            if(o.size()<4096)o.push_back((char)c);
        }
        return false;
    }
    bool skip(){
        ws();if(p_>=s_.size())return false;
        if(s_[p_]=='"'){std::string ignore;return quoted(ignore);}
        if(s_[p_]=='{'||s_[p_]=='['){char start=s_[p_++];char end=start=='{'?'}':']';int depth=1;bool in=false,esc=false;while(p_<s_.size()&&depth){char c=s_[p_++];if(in){if(esc)esc=false;else if(c=='\\')esc=true;else if(c=='"')in=false;}else if(c=='"')in=true;else if(c==start)++depth;else if(c==end)--depth;}return depth==0;}
        std::size_t begin=p_;while(p_<s_.size()&&s_[p_]!=','&&s_[p_]!='}'&&!std::isspace((unsigned char)s_[p_]))++p_;return p_>begin;
    }
};
std::map<std::string,std::string> read_meta(const std::string& path){
    FILE* f=fopen(path.c_str(),"rb");if(!f)return {};
    if(fseek(f,0,SEEK_END)){fclose(f);return {};}
    long size=ftell(f);rewind(f);
    if(size<2 || size>(long)kMaxJson){fclose(f);return {};}
    std::string s((std::size_t)size,'\0');
    bool good=fread(s.data(),1,s.size(),f)==s.size();fclose(f);
    return good?Parser(s).fields():std::map<std::string,std::string>{};
}
std::string get(const std::map<std::string,std::string>& m,const char* key,std::size_t limit=180){
    auto it=m.find(key);return it==m.end()?std::string():trim(it->second,limit);
}

// The native title runs in a PS5 sandbox. Even when a payload can enumerate
// /mnt/usb0, the title might not see it. When available, PS5 websrv's local
// filesystem endpoint provides a read-only bridge to the same USB folder.
std::string url_path(std::string_view path) {
    constexpr char digits[]="0123456789ABCDEF";
    std::string result;
    for(unsigned char c:path) {
        if(std::isalnum(c)||c=='/'||c=='-'||c=='_'||c=='.')result.push_back((char)c);
        else {result+='%';result+=digits[c>>4];result+=digits[c&15];}
    }
    return result;
}
bool http_get_local(int port,const std::string& route,std::size_t limit,std::string& result){
    result.clear();
    if(port<1||port>65535||route.empty()||route[0]!='/'||route.size()>1024)return false;
    int sock=socket(AF_INET,SOCK_STREAM,0);
    if(sock<0)return false;
    struct timeval timeout{2,0};
    setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    setsockopt(sock,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    sockaddr_in server{};server.sin_family=AF_INET;
    server.sin_port=htons((uint16_t)port);
    server.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(connect(sock,(sockaddr*)&server,sizeof(server))!=0){close(sock);return false;}
    std::string request="GET "+route+" HTTP/1.1\r\nHost: 127.0.0.1\r\nAccept: */*\r\nConnection: close\r\n\r\n";
    std::size_t sent=0;
    while(sent<request.size()){
        ssize_t n=send(sock,request.data()+sent,request.size()-sent,0);
        if(n<=0){close(sock);return false;}
        sent+=(std::size_t)n;
    }
    std::string response;
    char buffer[8192];
    while(response.size()<=limit+65536){
        ssize_t n=recv(sock,buffer,sizeof(buffer),0);
        if(n==0)break;
        if(n<0){close(sock);return false;}
        response.append(buffer,(std::size_t)n);
    }
    close(sock);
    if(response.size()>limit+65536)return false;
    auto h=response.find("\r\n\r\n");
    if(h==std::string::npos||h>16384||response.substr(0,12).find(" 200 ")==std::string::npos)return false;
    std::string headers=response.substr(0,h);
    for(auto& c:headers)c=(char)std::tolower((unsigned char)c);
    std::string_view body(response.data()+h+4,response.size()-h-4);
    if(headers.find("transfer-encoding: chunked")!=std::string::npos){
        std::size_t pos=0;
        for(;;){
            std::size_t e=body.find("\r\n",pos);
            if(e==std::string_view::npos||e-pos>24)return false;
            std::string chunkSize(body.substr(pos,e-pos));
            char* end=nullptr;unsigned long long size=std::strtoull(chunkSize.c_str(),&end,16);
            if(end==chunkSize.c_str()||(*end && *end!=';'))return false;
            pos=e+2;
            if(size==0)return true;
            if(size>limit-result.size()||size>body.size()-pos)return false;
            result.append(body.substr(pos,(std::size_t)size));
            pos+=(std::size_t)size;
            if(pos+2>body.size()||body.substr(pos,2)!="\r\n")return false;
            pos+=2;
        }
    }
    if(body.size()>limit)return false;
    result.assign(body);
    return true;
}
struct RemoteEntry {std::string name;std::string mode;};
std::vector<RemoteEntry> directory_json(std::string_view json){
    std::vector<RemoteEntry> out;
    if(json.empty()||json[0]!='[')return out;
    std::size_t pos=0;
    while(out.size()<kMaxEntries){
        auto start=json.find('{',pos);
        if(start==std::string_view::npos)break;
        auto end=json.find('}',start);
        if(end==std::string_view::npos)break;
        const auto fields=Parser(json.substr(start,end-start+1)).fields();
        const std::string name=get(fields,"name",127),mode=get(fields,"mode",2);
        if(is_safe_filename(name))out.push_back({name,mode});
        pos=end+1;
    }
    return out;
}
[[maybe_unused]] ScanResult scan_websrv(const std::string& root,int port){
    ScanResult result;
    std::string body;
    if(!http_get_local(port,"/fs"+url_path(root)+"?fmt=json",128*1024,body)) {
        result.message="Sandbox cannot read USB. Start PS5 websrv (port 8080), then press TRIANGLE.";
        return result;
    }
    const auto folders=directory_json(body);
    for(const auto& folder:folders){
        if(folder.mode!="d"&&folder.mode!="m")continue;
        const std::string dir=root+"/"+folder.name;
        std::string filesJson;
        if(!http_get_local(port,"/fs"+url_path(dir)+"?fmt=json",64*1024,filesJson))continue;
        const auto files=directory_json(filesJson);
        const auto has_file=[&](const std::string& target) {
            for(const auto& f:files)if(f.name==target&&f.mode=="f")return true;
            return false;
        };
        std::map<std::string,std::string> metadata;
        std::string raw;
        if(has_file("payload.json") && http_get_local(port,"/fs"+url_path(dir+"/payload.json"),kMaxJson,raw))
            metadata=Parser(raw).fields();
        std::string exe=get(metadata,"file",127);
        if(exe.empty())exe=get(metadata,"payload",127);
        if(!is_safe_filename(exe)||!executable_extension(exe)||!has_file(exe))exe.clear();
        if(exe.empty()){
            for(auto ext:{".elf",".efl",".bin"}){
                std::string candidate=folder.name+ext;
                if(has_file(candidate)){exe=candidate;break;}
            }
        }
        if(exe.empty()){
            std::vector<std::string> candidates;
            for(const auto& f:files)if(f.mode=="f"&&executable_extension(f.name))candidates.push_back(f.name);
            std::sort(candidates.begin(),candidates.end());
            if(!candidates.empty())exe=candidates.front();
        }
        if(exe.empty())continue;
        Payload p;
        p.folder=folder.name;p.filename=exe;p.path=dir+"/"+exe;p.remote=true;
        p.name=get(metadata,"name",64);if(p.name.empty())p.name=p.folder;
        p.version=get(metadata,"version",20);if(p.version.empty())p.version="Unversioned";
        p.description=get(metadata,"description",240);if(p.description.empty())p.description="Local USB payload";
        p.author=get(metadata,"author",60);if(p.author.empty())p.author="Unknown author";
        if(has_file("payload.png")){
            http_get_local(port,"/fs"+url_path(dir+"/payload.png"),3*1024*1024,p.icon_png);
        }
        result.payloads.push_back(std::move(p));
    }
    result.message=result.payloads.empty()?"Websrv connected, but no ELF files found in USB subfolders":
        "Loaded "+std::to_string(result.payloads.size())+" payload(s) via local websrv";
    return result;
}
ScanResult scan_local(const std::string& visibleRoot,const std::string& canonicalRoot) {
    ScanResult result;
    DIR* d=opendir(visibleRoot.c_str());
    if(!d){result.message="Cannot open "+visibleRoot+": "+std::strerror(errno);return result;}
    std::vector<std::string> dirs;
    while(auto* ent=readdir(d)){
        const std::string name(ent->d_name);
        if(!is_safe_filename(name))continue;
        if(directory(visibleRoot+"/"+name))dirs.push_back(name);
        if(dirs.size()>=kMaxEntries)break;
    }
    closedir(d);std::sort(dirs.begin(),dirs.end());
    for(const auto& folder:dirs){
        const std::string dir=visibleRoot+"/"+folder;
        auto metadata=read_meta(dir+"/payload.json");
        std::string exe=get(metadata,"file",127);
        if(exe.empty())exe=get(metadata,"payload",127);
        if(!is_safe_filename(exe)||!executable_extension(exe)||!regular_file(dir+"/"+exe))exe.clear();
        if(exe.empty()){
            for(auto ext:{".elf",".efl",".bin"}){
                const std::string candidate=folder+ext;
                if(regular_file(dir+"/"+candidate)){exe=candidate;break;}
            }
        }
        if(exe.empty()){
            DIR* sub=opendir(dir.c_str());
            if(sub){
                std::vector<std::string> candidates;
                while(auto* e=readdir(sub)){
                    const std::string candidate=e->d_name;
                    if(is_safe_filename(candidate)&&executable_extension(candidate)&&regular_file(dir+"/"+candidate))candidates.push_back(candidate);
                }
                closedir(sub);std::sort(candidates.begin(),candidates.end());
                if(!candidates.empty())exe=candidates.front();
            }
        }
        if(exe.empty())continue;
        Payload p;p.folder=folder;p.filename=exe;p.path=canonicalRoot+"/"+folder+"/"+exe;
        p.remote=(visibleRoot!=canonicalRoot);
        p.name=get(metadata,"name",64);if(p.name.empty())p.name=folder;
        p.version=get(metadata,"version",20);if(p.version.empty())p.version="Unversioned";
        p.description=get(metadata,"description",240);if(p.description.empty())p.description="Local USB payload";
        p.author=get(metadata,"author",60);if(p.author.empty())p.author="Unknown author";
        if(regular_file(dir+"/payload.png"))p.icon=dir+"/payload.png";
        result.payloads.push_back(std::move(p));
    }
    result.message=result.payloads.empty()?"USB folder found, but no ELF files in subfolders":
       "Loaded "+std::to_string(result.payloads.size())+" payload(s) from "+visibleRoot;
    return result;
}
} // namespace
bool is_safe_filename(const std::string& s) {
    if(s.empty() || s.size()>127 || s=="." || s==".." || s[0]=='.')return false;
    for(unsigned char c : s){if(c=='/' || c=='\\' || c==':' || c<32 || c==127)return false;}
    return true;
}
bool is_elf_file(const std::string& path){
    FILE* f=fopen(path.c_str(),"rb");if(!f)return false;
    unsigned char h[5]{};std::size_t n=fread(h,1,sizeof(h),f);fclose(f);
    return n==5 && h[0]==0x7f && h[1]=='E' && h[2]=='L' && h[3]=='F' && h[4]==2;
}

ScanResult scan_embedded_helper(const std::string& root) {
    ScanResult result;
    FILE* helper=fopen("/app0/usbscan.elf","rb");
    if(!helper){result.message="Bundled USB scanner unavailable in /app0";return result;}
    int fd=socket(AF_INET,SOCK_STREAM,0);
    if(fd<0){fclose(helper);result.message="Cannot create ELF loader socket";return result;}
    timeval tv{5,0};
    setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
    setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
    sockaddr_in addr{};
    addr.sin_family=AF_INET;addr.sin_port=htons(9021);
    addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(connect(fd,(sockaddr*)&addr,sizeof(addr))){
        fclose(helper);close(fd);
        result.message="USB requires running elfldr at 127.0.0.1:9021";
        return result;
    }
    char buf[8192];bool sent=true;
    for(;;){
        std::size_t n=fread(buf,1,sizeof(buf),helper);
        if(!n){if(ferror(helper))sent=false;break;}
        std::size_t off=0;
        while(off<n){ssize_t x=send(fd,buf+off,n-off,0);if(x<=0){sent=false;break;}off+=(std::size_t)x;}
        if(!sent)break;
    }
    fclose(helper);
    if(!sent){close(fd);result.message="Cannot submit USB helper to elfldr";return result;}
    shutdown(fd,SHUT_WR);
    std::string data;data.reserve(8192);
    while(data.size()<32*1024*1024){
        ssize_t n=recv(fd,buf,sizeof(buf),0);
        if(n==0)break;
        if(n<0)break;
        data.append(buf,(std::size_t)n);
        if(data.size()>=5 && data.compare(data.size()-5,5,"DONE\n")==0)break;
    }
    close(fd);
    auto start=data.find("PLUSB2\n");
    if(start==std::string::npos){
        result.message="USB helper did not return a valid catalogue";
        return result;
    }
    std::size_t pos=start+7;
    while(pos<data.size() && result.payloads.size()<128){
        auto e=data.find('\n',pos);
        if(e==std::string::npos || e-pos>128)break;
        const std::string hdr=data.substr(pos,e-pos);
        pos=e+1;
        if(hdr=="DONE")break;
        if(hdr.rfind("ERROR ",0)==0){result.message=hdr;return result;}
        std::size_t folderLen=0,fileLen=0,jsonLen=0,pngLen=0;
        if(std::sscanf(hdr.c_str(),"ITEM %zu %zu %zu %zu",
                       &folderLen,&fileLen,&jsonLen,&pngLen)!=4)break;
        if(folderLen<1||folderLen>127||fileLen<1||fileLen>127||
           jsonLen>kMaxJson||pngLen>1024*1024||
           folderLen+fileLen+jsonLen+pngLen>data.size()-pos)break;
        const std::string folder=data.substr(pos,folderLen);pos+=folderLen;
        const std::string filename=data.substr(pos,fileLen);pos+=fileLen;
        const std::string metadata=data.substr(pos,jsonLen);pos+=jsonLen;
        const std::string icon=data.substr(pos,pngLen);pos+=pngLen;
        if(!is_safe_filename(folder)||!is_safe_filename(filename)||
           !executable_extension(filename))continue;
        auto fields=Parser(metadata).fields();
        Payload p;
        p.folder=folder;p.filename=filename;p.path=root+"/"+folder+"/"+filename;p.remote=true;
        p.name=get(fields,"name",64);if(p.name.empty())p.name=folder;
        p.version=get(fields,"version",20);if(p.version.empty())p.version="Unversioned";
        p.description=get(fields,"description",240);if(p.description.empty())p.description="Local USB payload";
        p.author=get(fields,"author",60);if(p.author.empty())p.author="Unknown author";
        p.icon_png=icon;
        result.payloads.push_back(std::move(p));
    }
    result.message=result.payloads.empty()?"USB helper returned no usable payloads":
        "Loaded "+std::to_string(result.payloads.size())+" payloads via bundled USB helper";
    return result;
}

ScanResult scan(const std::string& root){
    ScanResult first=scan_local(root,root);
    if(!first.payloads.empty())return first;
    if(root!=kUsbRoot)return first;
    ScanResult alias=scan_local("/usb0/payloads",root);
    if(!alias.payloads.empty())return alias;
    ScanResult helper=scan_embedded_helper(root);
    if(!helper.payloads.empty())return helper;
    if(first.message.find("Cannot open")==std::string::npos)return first;
    return helper;
}
std::string request_launch(const Payload& p,int port){
    if(!is_safe_filename(p.folder)||!is_safe_filename(p.filename)||!executable_extension(p.filename) ||
       p.path!=std::string(kUsbRoot)+"/"+p.folder+"/"+p.filename)
        return "Invalid payload selection";
    // Sandboxed native titles cannot always stat USB paths: the privileged
    // elfldr independently opens and validates the ELF on the host filesystem.
    if(!p.remote && regular_file(p.path) && !is_elf_file(p.path))
        return "Invalid payload: expected an ELF64 executable on USB";
    if(port<1 || port>65535)return "Invalid loader port";
    int fd=socket(AF_INET,SOCK_STREAM,0);
    if(fd<0)return "Unable to create local socket";
    struct timeval tv{2,0};
    setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
    setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
    struct sockaddr_in endpoint{};endpoint.sin_family=AF_INET;endpoint.sin_port=htons((uint16_t)port);
    endpoint.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(connect(fd,(const struct sockaddr*)&endpoint,sizeof(endpoint))!=0){close(fd);return "No local ELF loader on 127.0.0.1:"+std::to_string(port);}

    // Native app cannot read USB directly. Our bounded read-only scanner
    // exposes the selected ELF to localhost on 9174 after discovery.
    int bridge=socket(AF_INET,SOCK_STREAM,0);
    if(bridge<0){close(fd);return "USB transfer socket failed";}
    struct timeval timeout{5,0};
    setsockopt(bridge,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    setsockopt(bridge,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    sockaddr_in usb{};
    usb.sin_family=AF_INET;usb.sin_port=htons(9174);
    usb.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(connect(bridge,(sockaddr*)&usb,sizeof(usb))){
        close(bridge);close(fd);return "USB helper port 9174 not ready - refresh with TRIANGLE";
    }
    const std::string fetch="FETCH "+p.folder+"/"+p.filename+"\n";
    if(send(bridge,fetch.data(),fetch.size(),0)!=(ssize_t)fetch.size()){
        close(bridge);close(fd);return "USB helper request failed";
    }
    char line[80]{};
    std::size_t len=0;
    for(;len<sizeof(line)-1;len++){
        char c=0;
        if(recv(bridge,&c,1,0)!=1)break;
        if(c=='\n')break;
        line[len]=c;
    }
    unsigned long long expected=0;
    if(std::sscanf(line,"DATA %llu",&expected)!=1 || expected<64 || expected>32ULL*1024*1024){
        close(bridge);close(fd);return "USB helper could not read selected ELF";
    }
    char bytes[16384];
    unsigned long long transferred=0;
    while(transferred<expected){
        const std::size_t size=(std::size_t)std::min<unsigned long long>(sizeof(bytes),expected-transferred);
        ssize_t n=recv(bridge,bytes,size,0);
        if(n<=0)break;
        std::size_t off=0;
        while(off<(std::size_t)n){
            ssize_t wrote=send(fd,bytes+off,(std::size_t)n-off,0);
            if(wrote<=0)break;
            off+=(std::size_t)wrote;
        }
        if(off!=(std::size_t)n)break;
        transferred+=(unsigned long long)n;
    }
    close(bridge);
    close(fd);
    if(transferred!=expected)return "ELF transfer interrupted: "+std::to_string(transferred)+"/"+std::to_string(expected);
    return "ELF bytes sent: "+p.name+" ("+std::to_string(expected)+" bytes; unconfirmed)";
}
} // namespace launcher
