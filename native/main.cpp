// SPDX-License-Identifier: GPL-3.0-or-later
// (AI-assisted) Native PS5 USB Payload LauncherX by YAHBALLON, v0.2.4.
#include "gfx/renderer.hpp"
#include "platform/ps5/display_egl.hpp"
#include "platform/ps5/pad.hpp"
#include "platform/ps5/system.hpp"
#include "payloads.hpp"
#include "settings.hpp"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_FAILURE_STRINGS
#include "stb_image.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <pthread.h>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
using hui::gfx::Color;
using hui::gfx::Rect;
using hui::gfx::DrawList;
using hui::gfx::Align;
namespace {
constexpr auto kTitle="PPSA99873";
constexpr auto kVersion="v0.2.4";
const Color kWhite=Color::rgb(0xf1f6ff);
const Color kMuted=Color::rgb(0xa1bbdc);
const Color kBlue=Color::rgb(0x119bff);
const Color kBorder=Color::rgb(0x1c3b65);
const Color kPanel=Color::rgb(0x061528,0.88f);
std::mutex g_lock;
std::string g_message;
std::atomic<bool> g_launching{false};
struct Job {launcher::Payload payload;};
void* launch_worker(void* ptr) {
    Job* job=static_cast<Job*>(ptr);
    std::string result=launcher::request_launch(job->payload);
    {std::lock_guard<std::mutex> hold(g_lock);g_message=result;}
    delete job;g_launching.store(false);return nullptr;
}
void start(const launcher::Payload& payload){
    if(g_launching.exchange(true))return;
    {std::lock_guard<std::mutex> hold(g_lock);g_message="Sending launch request: "+payload.name;}
    Job* j=new Job{payload};pthread_t id{};
    if(pthread_create(&id,nullptr,launch_worker,j)!=0) {
        delete j;g_launching.store(false);
        std::lock_guard<std::mutex> hold(g_lock);g_message="Unable to start loader worker";
    } else pthread_detach(id);
}
std::string read_file(const std::string& path,std::size_t limit=16*1024*1024){
    FILE* f=fopen(path.c_str(),"rb");if(!f)return {};
    if(fseek(f,0,SEEK_END)!=0){fclose(f);return {};}
    long n=ftell(f);rewind(f);
    if(n<1 || (std::size_t)n>limit){fclose(f);return {};}
    std::string value((std::size_t)n,'\0');
    bool ok=fread(value.data(),1,value.size(),f)==value.size();
    fclose(f);return ok?value:std::string{};
}
std::uint32_t load_png(hui::gfx::Renderer& renderer,const std::string& path,int limit=2048){
    std::string png=read_file(path,4*1024*1024);
    if(png.empty())return 0;
    int w=0,h=0,channels=0;
    stbi_uc* pixels=stbi_load_from_memory((const stbi_uc*)png.data(),(int)png.size(),&w,&h,&channels,4);
    if(!pixels)return 0;
    std::uint32_t tex=0;
    if(w>0&&h>0&&w<=limit&&h<=limit)tex=renderer.batch().create_texture(w,h,pixels);
    stbi_image_free(pixels);return tex;
}
std::uint32_t load_png_bytes(hui::gfx::Renderer& renderer,const std::string& png){
    if(png.empty()||png.size()>3*1024*1024)return 0;
    int w=0,h=0,channels=0;
    stbi_uc* pixels=stbi_load_from_memory((const stbi_uc*)png.data(),(int)png.size(),&w,&h,&channels,4);
    if(!pixels)return 0;
    std::uint32_t tex=0;
    if(w>0&&h>0&&w<=1024&&h<=1024)tex=renderer.batch().create_texture(w,h,pixels);
    stbi_image_free(pixels);return tex;
}
void label(DrawList& u,const hui::gfx::Font& f,unsigned a,std::string_view s,float x,float y,float sz=22,Color color=kWhite,Align align=Align::left,float tracking=0){
    u.text(f,a,s,x,y,sz,color,align,tracking);
}
void logo(DrawList& ui,float x,float y,float scale){
    ui.glow({x,y,88*scale,88*scale},22*scale,17*scale,Color::rgb(0x006cf7,0.20f));
    // Original angular monogram; not a console-platform trademark.
    ui.chamfer_rect({x,y,88*scale,88*scale},15*scale,Color::rgb(0x021025),1.5f*scale,kBlue);
    const float pts1[]={x+19*scale,y+23*scale,x+39*scale,y+12*scale,x+39*scale,y+57*scale,x+19*scale,y+69*scale};
    ui.polygon(pts1,4,Color::rgb(0x138cf2));
    const float pts2[]={x+45*scale,y+12*scale,x+70*scale,y+28*scale,x+70*scale,y+57*scale,x+45*scale,y+72*scale,x+45*scale,y+60*scale,x+58*scale,y+51*scale,x+58*scale,y+34*scale,x+45*scale,y+25*scale};
    ui.polygon(pts2,8,Color::rgb(0xddecff));
}
void hint(DrawList& ui,const hui::gfx::Font& f,unsigned a,float x,const char* button,const char* desc){
    ui.ring(x,103,18,2,Color::rgb(0x88beff));
    if(std::strcmp(button,"square")==0){
        ui.bordered_rect({x-8,95,16,16},1,Color::rgb(0x061a35),2,Color::rgb(0x9ecbff));
    }else if(std::strcmp(button,"triangle")==0){
        ui.triangle({x-10,93,20,19},Color::rgb(0x9ecbff),2);
    }else label(ui,f,a,button,x,111,19,Color::rgb(0x9ecbff),Align::center);
    label(ui,f,a,desc,x+30,111,20,kWhite);
}
void placeholder(DrawList& ui,float x,float y){
    ui.bordered_rect({x,y,85,80},14,Color::rgb(0x041024),1.3f,Color::rgb(0x173e79));
    ui.glow({x+22,y+22,42,35},12,14,Color::rgb(0x0089ff,0.17f));
    ui.bordered_rect({x+25,y+28,36,28},7,Color::rgb(0x0677db),2,Color::rgb(0x21b1ff));
    ui.circle(x+43,y+42,8,Color::rgb(0x001932));
    for(int i=0;i<8;i++){
        float a=(float)i*0.785398f;
        ui.line(x+43+20*std::cos(a),y+42+20*std::sin(a),x+43+26*std::cos(a),y+42+26*std::sin(a),5,Color::rgb(0x169bff));
    }
}
std::array<std::string,2> lines(const std::string& text,unsigned width=72){
    std::array<std::string,2> out{};
    std::string cleaned=text;for(char& c:cleaned)if(c=='\r'||c=='\t')c=' ';
    std::size_t nl=cleaned.find('\n');
    if(nl!=std::string::npos){
        out[0]=cleaned.substr(0,std::min((std::size_t)width,nl));
        out[1]=cleaned.substr(nl+1,width);
        return out;
    }
    if(cleaned.size()<=width){out[0]=cleaned;return out;}
    std::size_t split=cleaned.rfind(' ',width);if(split==std::string::npos||split<width/2)split=width;
    out[0]=cleaned.substr(0,split);
    out[1]=cleaned.substr(std::min(split+1,cleaned.size()),width);
    if(cleaned.size()>split+width && out[1].size()>3)out[1].replace(out[1].size()-3,3,"...");
    return out;
}
struct Icon {std::string path;std::uint32_t texture=0;};
struct App {
    launcher::Settings settings;
    launcher::ScanResult data;
    std::vector<Icon> icons;
    std::uint32_t background=0;
    int selected=0,scroll=0,frame=0;
    bool inSettings=false,inDetails=false;
    int settingsRow=0;
    std::string status;
    void refresh(hui::gfx::Renderer& r){
        std::string key;
        if(selected>=0&&selected<(int)data.payloads.size())key=data.payloads[selected].folder;
        auto next=launcher::scan();
        if(settings.alphabetical)std::stable_sort(next.payloads.begin(),next.payloads.end(),[](const auto& a,const auto& b){return a.name<b.name;});
        for(auto& i:icons)if(i.texture){GLuint tex=i.texture;glDeleteTextures(1,&tex);}
        icons.clear();icons.reserve(next.payloads.size());
        for(const auto& p:next.payloads)icons.push_back({p.icon,!p.icon_png.empty()?load_png_bytes(r,p.icon_png):(p.icon.empty()?0:load_png(r,p.icon,1024))});
        data=std::move(next);status=data.message;selected=0;
        for(int i=0;i<(int)data.payloads.size();i++)if(data.payloads[i].folder==key)selected=i;
        scroll=std::min(scroll,std::max(0,(int)data.payloads.size()-9));
        frame=0;
    }
    void navigate(int change){
        int n=(int)data.payloads.size();
        if(n<=0)return;
        selected=std::max(0,std::min(n-1,selected+change));
        if(selected<scroll)scroll=selected;
        if(selected>scroll+8)scroll=selected-8;
    }
};
void draw_screen(DrawList& ui,App& app,const hui::gfx::Font& font,unsigned atlas){
    using namespace hui::gfx;
    ui.gradient_rect({0,0,1920,1080},0,Color::rgb(0x020918),Color::rgb(0x03122b));
    if(app.background)ui.image(app.background,{0,0,1920,1080},kFullUv,Color::rgb(0xffffff));
    ui.gradient_rect({0,0,1920,155},0,Color::rgb(0x020815,0.25f),Color::rgb(0x011128,0.58f));
    logo(ui,87,25,1.0f);
    label(ui,font,atlas,"Payload",222,78,43,kWhite);
    label(ui,font,atlas,"LauncherX",397,78,43,Color::rgb(0x159efe));
    label(ui,font,atlas,"PS5 HOMEBREW PAYLOADS",224,113,18,Color::rgb(0x80bdff),Align::left,5.3f);
    label(ui,font,atlas,"by YAHBALLON",223,141,15,Color::rgb(0x709aca),Align::left,2.1f);
    hint(ui,font,atlas,1078,"X","Launch");
    hint(ui,font,atlas,1231,"square","Details");
    hint(ui,font,atlas,1403,"triangle","Refresh");
    label(ui,font,atlas,"≡",1578,110,35,Color::rgb(0x96c4ff),Align::center);
    label(ui,font,atlas,"Options",1608,111,19);
    label(ui,font,atlas,kVersion,1860,109,19,Color::rgb(0xb5d2f6),Align::right);
    if(app.data.payloads.empty()){
        ui.bordered_rect({270,310,1380,480},26,Color::rgb(0x020d1c,0.84f),2,Color::rgb(0x1267b3));
        ui.glow({600,388,720,180},60,60,Color::rgb(0x0079fa,0.11f));
        label(ui,font,atlas,"NO USB PAYLOADS FOUND",960,448,43,kWhite,Align::center);
        label(ui,font,atlas,"Place each executable in a separate subfolder:",960,505,26,kMuted,Align::center);
        label(ui,font,atlas,"/mnt/usb0/payloads/<folder>/<payload>.elf",960,565,27,Color::rgb(0x69c4ff),Align::center);
        label(ui,font,atlas,"Add payload.json and payload.png, then press TRIANGLE to refresh.",960,632,23,kMuted,Align::center);
    } else {
        ui.push_clip({65,156,1780,864});
        for(int row=app.scroll;row<(int)app.data.payloads.size()&&row<app.scroll+10;++row){
            const auto& p=app.data.payloads[row];
            float y=161+(row-app.scroll)*98.0f;
            const bool chosen=row==app.selected;
            if(chosen){
                ui.glow({75,y,1770,98},20,24,Color::rgb(0x008cff,0.38f));
                ui.bordered_rect({79,y+1,1754,102},19,Color::rgb(0x04316a,0.82f),4,Color::rgb(0x009bff));
            } else {
                ui.bordered_rect({82,y+2,1749,88},17,kPanel,1.35f,kBorder);
            }
            const float top=y+(chosen?8:8);
            if(row<(int)app.icons.size()&&app.icons[row].texture){
                ui.bordered_rect({108,top+2,85,80},14,Color::rgb(0x030e20),1.5f,Color::rgb(0x17406e));
                ui.image(app.icons[row].texture,{115,top+8,71,67},kFullUv,Color::rgb(0xffffff),9);
            }else placeholder(ui,108,top+2);
            label(ui,font,atlas,p.name,227,y+47,25,kWhite);
            label(ui,font,atlas,p.filename,227,y+80,20,Color::rgb(0x93bcf8));
            const auto desc=lines(p.description,75);
            label(ui,font,atlas,desc[0],639,y+47,20,Color::rgb(0xdbebff));
            label(ui,font,atlas,desc[1],639,y+73,20,Color::rgb(0xdbebff));
            ui.bordered_rect({1544,y+22,109,37},12,Color::rgb(0x061d3b),1.4f,Color::rgb(0x128aff));
            label(ui,font,atlas,p.version,1598,y+47,20,kWhite,Align::center);
            label(ui,font,atlas,"by "+p.author,1546,y+81,17,Color::rgb(0x8fb4ea));
            for(int dot=0;dot<3;++dot)ui.circle(1772+11*dot,y+56,2.8f,Color::rgb(0xb2d4ff));
        }
        ui.pop_clip();
        if(app.data.payloads.size()>9){
            ui.rounded_rect({1864,171,5,820},2,Color::rgb(0x1c3761));
            float total=(float)app.data.payloads.size();
            float h=std::max(35.f,820*9.f/total);
            float yy=171+(820-h)*((float)app.scroll/(std::max(1.f,total-9)));
            ui.rounded_rect({1863,yy,7,h},3,kBlue);
        }
    }
    ui.gradient_rect({0,1014,1920,66},0,Color::rgb(0x031225,0.0f),Color::rgb(0x031225,0.94f));
    label(ui,font,atlas,app.status,95,1060,18,Color::rgb(0x88b5ed));
    if(!app.data.payloads.empty())label(ui,font,atlas,std::to_string(app.selected+1)+" / "+std::to_string(app.data.payloads.size()),1840,1060,18,kMuted,Align::right);
}
void draw_overlay(DrawList& ui,App& app,const hui::gfx::Font& font,unsigned atlas){
    if(!app.inDetails&&!app.inSettings)return;
    ui.rounded_rect({0,0,1920,1080},0,Color::rgb(0x000712,0.75f));
    ui.shadow({450,226,1020,625},31,60,Color::rgb(0x007cff,0.33f));
    ui.bordered_rect({450,225,1020,626},26,Color::rgb(0x071931),2.5f,Color::rgb(0x168aff));
    if(app.inSettings){
        label(ui,font,atlas,"LAUNCHER SETTINGS",504,304,40);
        label(ui,font,atlas,"Runtime configuration",505,345,22,kMuted);
        const char* settingNames[]={"Automatic USB refresh", "Alphabetical sorting"};
        const char* values[]={app.settings.autoRefresh?"ON":"OFF",app.settings.alphabetical?"ON":"OFF"};
        for(int i=0;i<2;i++){
            bool selected=i==app.settingsRow;
            ui.bordered_rect({504,396+i*102.0f,907,83},15,Color::rgb(0x0b284d),selected?2:1,selected?kBlue:kBorder);
            label(ui,font,atlas,settingNames[i],531,445+i*102,26);
            label(ui,font,atlas,values[i],1371,444+i*102,25,selected?Color::rgb(0x51caff):kMuted,Align::right);
        }
        label(ui,font,atlas,"Payload root: /mnt/usb0/payloads",514,683,23,Color::rgb(0x8cbaee));
        label(ui,font,atlas,"Launch service: localhost TCP 9021 (elfldr required)",514,721,22,kMuted);
        label(ui,font,atlas,"D-PAD Select    X Toggle    CIRCLE Close",513,802,21,kMuted);
    } else if(app.selected<(int)app.data.payloads.size()) {
        const auto& p=app.data.payloads[app.selected];
        label(ui,font,atlas,"PAYLOAD DETAILS",502,307,38);
        label(ui,font,atlas,p.name,504,372,30,kWhite);
        label(ui,font,atlas,"Version: "+p.version+"    Author: "+p.author,504,415,23,Color::rgb(0x9bc6ff));
        const auto desc=lines(p.description,68);
        label(ui,font,atlas,desc[0],504,471,24);
        label(ui,font,atlas,desc[1],504,508,24);
        label(ui,font,atlas,"File: "+p.filename,504,565,22,Color::rgb(0x96bce7));
        label(ui,font,atlas,"Folder: "+p.folder,504,607,22,Color::rgb(0x96bce7));
        ui.bordered_rect({510,683,430,75},14,Color::rgb(0x053f84),2,kBlue);
        label(ui,font,atlas,"X  Launch payload",725,731,26,kWhite,Align::center);
        label(ui,font,atlas,"CIRCLE  Close",974,810,20,kMuted,Align::center);
    }
}
} // namespace
int main(){
    hui::ps5::Display display;if(!display.open())hui::sys::park();
    hui::gfx::Renderer renderer;if(!renderer.init())hui::sys::park();
    hui::gfx::Font font;
    std::string fontData=read_file("/app0/assets/fonts/inter-semibold.huifont");
    if(fontData.empty())fontData=read_file(std::string("/data/homebrew/")+kTitle+"/assets/fonts/inter-semibold.huifont");
    if(!font.load(fontData))hui::sys::park();
    unsigned atlas=renderer.batch().create_font_texture(font);
    hui::ps5::Pad pad;pad.open();
    App app;app.settings.load();
    app.background=load_png(renderer,"/app0/assets/launcher-background.png",2048);
    if(!app.background)app.background=load_png(renderer,std::string("/data/homebrew/")+kTitle+"/assets/launcher-background.png",2048);
    app.refresh(renderer);
    unsigned held=0;int repeat=0,stickRepeat=0;
    for(;;){
        std::array<hui::PadSample,64> samples{};
        std::uint32_t pressed=0;std::size_t n=pad.read(samples);
        int stick=0;
        for(std::size_t i=0;i<n;i++){
            pressed|=samples[i].buttons&~held;
            held=samples[i].buttons;
            if(samples[i].connected){
                if(samples[i].left_y<60)stick=-1;
                else if(samples[i].left_y>195)stick=1;
            }
        }
        std::uint32_t movement=pressed&(hui::pad_bits::kUp|hui::pad_bits::kDown);
        if(held&(hui::pad_bits::kUp|hui::pad_bits::kDown)){
            if(++repeat>22&&(repeat-23)%7==0)movement|=held&(hui::pad_bits::kUp|hui::pad_bits::kDown);
        }else repeat=0;
        if(stick!=0){if(++stickRepeat==1||(stickRepeat>20&&(stickRepeat-20)%7==0))movement|=stick<0?hui::pad_bits::kUp:hui::pad_bits::kDown;}
        else stickRepeat=0;
        if(pressed&hui::pad_bits::kCircle){
            if(app.inSettings)app.inSettings=false;
            else if(app.inDetails)app.inDetails=false;
            else {pad.close();for(auto& i:app.icons)if(i.texture){GLuint v=i.texture;glDeleteTextures(1,&v);}renderer.release();display.close();hui::sys::quit();}
        } else if(pressed&hui::pad_bits::kOptions){app.inSettings=!app.inSettings;app.inDetails=false;}
        else if(app.inSettings){
            if(movement&hui::pad_bits::kUp)app.settingsRow=std::max(0,app.settingsRow-1);
            if(movement&hui::pad_bits::kDown)app.settingsRow=std::min(1,app.settingsRow+1);
            if(pressed&hui::pad_bits::kCross){
                if(app.settingsRow==0)app.settings.autoRefresh=!app.settings.autoRefresh;
                else app.settings.alphabetical=!app.settings.alphabetical;
                app.status=app.settings.save()?"Settings saved":"Warning: Could not save settings";
                if(app.settingsRow==1)app.refresh(renderer);
            }
        } else {
            if(movement&hui::pad_bits::kUp)app.navigate(-1);
            if(movement&hui::pad_bits::kDown)app.navigate(1);
            if(pressed&hui::pad_bits::kL1)app.navigate(-9);
            if(pressed&hui::pad_bits::kR1)app.navigate(9);
            if(pressed&hui::pad_bits::kTriangle){app.inDetails=false;app.refresh(renderer);}
            if(pressed&hui::pad_bits::kSquare && !app.data.payloads.empty())app.inDetails=!app.inDetails;
            if(pressed&hui::pad_bits::kCross && !app.data.payloads.empty()){
                start(app.data.payloads[app.selected]);app.inDetails=false;
            }
        }
        if(app.settings.autoRefresh&&!app.inSettings&&!app.inDetails&&++app.frame>=60*15 && !g_launching.load())app.refresh(renderer);
        {std::lock_guard<std::mutex> hold(g_lock);if(!g_message.empty()){app.status=g_message;g_message.clear();}}
        DrawList ui;draw_screen(ui,app,font,atlas);draw_overlay(ui,app,font,atlas);
        renderer.begin();renderer.draw(ui);renderer.present(0,display.width(),display.height());display.swap();
        hui::sys::hide_splash_screen();hui::sys::sleep_us(16000);
    }
}
