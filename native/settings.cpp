// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings.hpp"
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>
namespace launcher {
namespace {
constexpr auto* kDir="/data/ps5-payload-launcher";
constexpr auto* kPath="/data/ps5-payload-launcher/settings.txt";
}
void Settings::load(){
    FILE* f=fopen(kPath,"rb");if(!f)return;
    char line[80];unsigned version=0;int refresh=1,sort=1;
    if(fgets(line,sizeof(line),f)==nullptr || sscanf(line,"format=%u",&version)!=1 || version!=1){fclose(f);return;}
    while(fgets(line,sizeof(line),f)){
        if(sscanf(line,"auto_refresh=%d",&refresh)==1)autoRefresh=(refresh!=0);
        if(sscanf(line,"alphabetical=%d",&sort)==1)alphabetical=(sort!=0);
    }fclose(f);
}
bool Settings::save() const {
    if(mkdir(kDir,0755)!=0 && access(kDir,F_OK)!=0)return false;
    const char* tmp="/data/ps5-payload-launcher/settings.tmp";
    FILE* f=fopen(tmp,"wb");if(!f)return false;
    int n=fprintf(f,"format=1\nauto_refresh=%d\nalphabetical=%d\n",autoRefresh?1:0,alphabetical?1:0);
    bool ok=n>0;
    if(fflush(f)!=0 || fsync(fileno(f))!=0)ok=false;
    if(fclose(f)!=0)ok=false;
    if(!ok){unlink(tmp);return false;}
    if(rename(tmp,kPath)!=0){unlink(tmp);return false;}return true;
}
}
