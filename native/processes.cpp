// SPDX-License-Identifier: GPL-3.0-or-later
// Integration with itsPLK/ps5-payload-manager's localhost API.
// This title cannot assume its sandbox has privileged sysctl/kill access.
// See https://github.com/itsPLK/ps5-payload-manager (GPL-3.0).
#include "processes.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace launcher {
namespace {
bool json_string(std::string_view s, std::size_t& pos, std::string& out) {
    if(pos>=s.size() || s[pos]!='"')return false;
    pos++; out.clear();
    while(pos<s.size()) {
        const unsigned char c=(unsigned char)s[pos++];
        if(c=='"')return true;
        if(c<32)return false;
        if(c=='\\'){
            if(pos>=s.size())return false;
            const char e=s[pos++];
            switch(e) {
                case '"': out+='"';break;
                case '\\': out+='\\';break;
                case '/': out+='/';break;
                case 'n': out+='\n';break;
                case 't': out+='\t';break;
                case 'r': out+='\r';break;
                case 'b': out+='\b';break;
                case 'f': out+='\f';break;
                case 'u': {
                    if(pos+4>s.size())return false;
                    unsigned code=0;
                    for(int i=0;i<4;i++){
                        char x=s[pos++]; unsigned d=
                            x>='0'&&x<='9'?x-'0':x>='A'&&x<='F'?x-'A'+10:
                            x>='a'&&x<='f'?x-'a'+10:16;
                        if(d>15)return false;
                        code=(code<<4)|d;
                    }
                    // Process names returned by this API are ASCII.
                    if(code>0 && code<128)out+=(char)code;
                    else out+='?';
                    break;
                }
                default:return false;
            }
        }else out+=(char)c;
        if(out.size()>127)return false;
    }
    return false;
}
void space(std::string_view s,std::size_t& pos){
    while(pos<s.size()&&(s[pos]==' '||s[pos]=='\n'||s[pos]=='\r'||s[pos]=='\t'))pos++;
}
bool field(std::string_view obj,std::string_view key,std::size_t& pos){
    const std::string needle="\""+std::string(key)+"\"";
    pos=obj.find(needle);
    if(pos==std::string_view::npos)return false;
    pos+=needle.size(); space(obj,pos);
    if(pos>=obj.size()||obj[pos++]!=':')return false;
    space(obj,pos);return true;
}
bool number_field(std::string_view obj,std::string_view key,double& value){
    std::size_t pos=0;if(!field(obj,key,pos))return false;
    // Bounded ASCII number with no allocation.
    const std::size_t start=pos;
    if(pos<obj.size()&&obj[pos]=='-')pos++;
    while(pos<obj.size()&&obj[pos]>='0'&&obj[pos]<='9')pos++;
    if(pos<obj.size()&&obj[pos]=='.'){pos++;while(pos<obj.size()&&obj[pos]>='0'&&obj[pos]<='9')pos++;}
    if(pos==start || pos-start>30)return false;
    std::string text(obj.substr(start,pos-start));
    char* end=nullptr;errno=0;
    value=std::strtod(text.c_str(),&end);
    return errno==0&&end&&*end==0&&std::isfinite(value);
}
bool bool_field(std::string_view obj,std::string_view key,bool& value){
    std::size_t pos=0;if(!field(obj,key,pos))return false;
    if(obj.substr(pos,4)=="true"){value=true;return true;}
    if(obj.substr(pos,5)=="false"){value=false;return true;}
    return false;
}
bool decode_process(std::string_view obj,RunningProcess& p){
    double pid=0;
    if(!number_field(obj,"pid",pid)||pid<1||pid>2147483647||pid!=(int)pid)return false;
    if(!number_field(obj,"memory",p.memoryMiB)||p.memoryMiB<0||p.memoryMiB>1024*1024)return false;
    if(!bool_field(obj,"is_daemon",p.daemon))return false;
    std::size_t pos=0;
    if(!field(obj,"name",pos)||!json_string(obj,pos,p.name))return false;
    if(p.name.empty()||p.name.size()>64)return false;
    p.pid=(int)pid;return true;
}
bool send_all(int fd,const std::string& request){
    std::size_t pos=0;
    while(pos<request.size()){
        ssize_t n=send(fd,request.data()+pos,request.size()-pos,0);
        if(n<=0)return false;
        pos+=(std::size_t)n;
    }
    return true;
}
bool localhost_http(int port,const std::string& route,bool post,std::string& body){
    body.clear();
    if(port<1||port>65535||route.size()>120||route.empty()||route[0]!='/')return false;
    int fd=socket(AF_INET,SOCK_STREAM,0);
    if(fd<0)return false;
    struct timeval timeout{2,0};
    setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    sockaddr_in addr{};addr.sin_family=AF_INET;
    addr.sin_port=htons((uint16_t)port);addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(connect(fd,(sockaddr*)&addr,sizeof(addr))!=0){close(fd);return false;}
    const std::string request=std::string(post?"POST ":"GET ")+route+
        " HTTP/1.1\r\nHost: 127.0.0.1:"+std::to_string(port)+
        "\r\nConnection: close\r\nAccept: application/json\r\n"+
        (post?"Content-Length: 0\r\nX-PLDMGR-Request: 1\r\n":"")+"\r\n";
    if(!send_all(fd,request)){close(fd);return false;}
    std::string response;
    char chunk[4096];
    while(response.size()<128*1024){
        ssize_t n=recv(fd,chunk,sizeof(chunk),0);
        if(n==0)break;
        if(n<0){close(fd);return false;}
        response.append(chunk,(std::size_t)n);
    }
    close(fd);
    const auto headerEnd=response.find("\r\n\r\n");
    if(headerEnd==std::string::npos||headerEnd>16384||response.size()>=128*1024)return false;
    const auto e=response.find("\r\n");
    const std::string status=response.substr(0,e);
    if(status.find(" 200 ")==std::string::npos)return false;
    const std::string headers=response.substr(e,headerEnd-e);
    // The local MHD implementation sends Content-Length for its buffer responses.
    if(headers.find("Transfer-Encoding: chunked")!=std::string::npos ||
       headers.find("transfer-encoding: chunked")!=std::string::npos)return false;
    body=response.substr(headerEnd+4);
    return body.size()<64*1024;
}
} // namespace

bool parse_process_snapshot(std::string_view json,ProcessSnapshot& result){
    result=ProcessSnapshot{};
    const auto key=json.find("\"processes\"");
    if(key==std::string_view::npos)return false;
    std::size_t pos=json.find('[',key+11);
    if(pos==std::string_view::npos)return false;
    pos++;
    while(pos<json.size()){
        space(json,pos);
        if(pos>=json.size())return false;
        if(json[pos]==']'){result.available=true;result.message="Process list refreshed";return true;}
        if(json[pos]==','){pos++;continue;}
        if(json[pos]!='{')return false;
        const std::size_t begin=pos;
        bool quoted=false,escape=false;int depth=0;
        for(;pos<json.size();pos++){
            const char c=json[pos];
            if(quoted){if(escape)escape=false;else if(c=='\\')escape=true;else if(c=='"')quoted=false;continue;}
            if(c=='"'){quoted=true;continue;}
            if(c=='{')depth++;
            if(c=='}'&&--depth==0){pos++;break;}
        }
        if(depth!=0)return false;
        RunningProcess proc;
        if(decode_process(json.substr(begin,pos-begin),proc))result.processes.push_back(std::move(proc));
        if(result.processes.size()>512)return false;
    }
    return false;
}
bool process_is_stoppable(const RunningProcess& p){
    if(p.pid<=1||!p.daemon||p.name.size()<5)return false;
    const auto end=p.name.substr(p.name.size()-4);
    if(end!=".elf"&&end!=".ELF")return false;
    const std::string n=p.name;
    // Retain access to the loader / process API and protect known core helpers.
    return n!="elfldr.elf"&&n!="pldmgr.elf"&&n!="mini-syscore.elf"
        &&n!="usbscan.elf"&&n!="procscan.elf";
}
ProcessSnapshot fetch_running_processes(int port) {
    std::string body;
    if(!localhost_http(port,"/processes_list",false,body))
        return {false,"Process Manager is not running on localhost:8084",{}};
    ProcessSnapshot result;
    if(!parse_process_snapshot(body,result))
        return {false,"Process Manager returned an invalid process list",{}};
    // Not every PS5 process is a standalone payload. Match the daemon flag
    // from PLDMGR, not an ELF launch request or a guessed name.
    result.processes.erase(std::remove_if(result.processes.begin(),result.processes.end(),
        [](const RunningProcess& p){return !p.daemon;}),result.processes.end());
    std::sort(result.processes.begin(),result.processes.end(),
              [](const auto& a,const auto& b){return a.name<b.name||(a.name==b.name&&a.pid<b.pid);});
    result.message="Running ELF daemons: "+std::to_string(result.processes.size());
    return result;
}
bool kill_running_process(const RunningProcess& process,std::string& message,int port){
    if(!process_is_stoppable(process)){message="This process cannot be stopped from LauncherX";return false;}
    // Refetch and validate identity immediately before the destructive action;
    // older snapshots may contain reused PIDs.
    const ProcessSnapshot fresh=fetch_running_processes(port);
    if(!fresh.available){message=fresh.message;return false;}
    bool matches=false;
    for(const auto& p:fresh.processes){
        if(p.pid==process.pid&&p.name==process.name&&process_is_stoppable(p)){matches=true;break;}
    }
    if(!matches){message="Process is no longer running or its PID changed";return false;}
    std::string body;
    if(!localhost_http(port,"/process_kill?pid="+std::to_string(process.pid),true,body)){
        message="Process Manager rejected the kill request";return false;
    }
    if(body.find("\"ok\":true")==std::string::npos && body.find("\"ok\": true")==std::string::npos){
        message="Kill response not confirmed";return false;
    }
    message="Kill submitted for "+process.name+" (PID "+std::to_string(process.pid)+")";
    return true;
}
} // namespace launcher
