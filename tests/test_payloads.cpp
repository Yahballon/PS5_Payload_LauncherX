// SPDX-License-Identifier: GPL-3.0-or-later
#include "payloads.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
using namespace launcher;
int main(){
    char pattern[]="/tmp/payload-launcher-tests-XXXXXX";
    char* dir=mkdtemp(pattern);assert(dir);
    const std::string root=dir;
    assert(mkdir((root+"/ftpsrv").c_str(),0700)==0);
    {FILE* f=fopen((root+"/ftpsrv/payload.json").c_str(),"wb");assert(f);
     const char* j="{\"name\":\"FTP Server\",\"version\":\"1.2.3\",\"author\":\"Dev\",\"description\":\"Transfer files\\nOver TCP\",\"file\":\"ftpsrv.efl\"}";
     assert(fputs(j,f)>=0);fclose(f);}
    {FILE* f=fopen((root+"/ftpsrv/ftpsrv.efl").c_str(),"wb");assert(f);
     const unsigned char elf[]={0x7f,'E','L','F',2,1};assert(fwrite(elf,1,sizeof(elf),f)==sizeof(elf));fclose(f);}
    assert(!is_safe_filename("../../escape.elf"));
    assert(!is_safe_filename("folder\\escaped.elf"));
    assert(is_safe_filename("ftpsrv.efl"));
    auto result=scan(root);
    assert(result.payloads.size()==1);
    assert(result.payloads[0].name=="FTP Server");
    assert(result.payloads[0].version=="1.2.3");
    assert(result.payloads[0].author=="Dev");
    assert(result.payloads[0].filename=="ftpsrv.efl");
    assert(result.payloads[0].description=="Transfer files\nOver TCP");
    assert(is_elf_file(result.payloads[0].path));
    assert(request_launch(result.payloads[0]).find("Invalid payload")!=std::string::npos);
    unlink((root+"/ftpsrv/payload.json").c_str());
    unlink((root+"/ftpsrv/ftpsrv.efl").c_str());
    rmdir((root+"/ftpsrv").c_str());rmdir(root.c_str());
    puts("Payload discovery, JSON and path safety tests passed");
    return 0;
}
