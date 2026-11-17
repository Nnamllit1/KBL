#include "kbl/linux_boot.hpp"
#include "display_probe.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace kbl;
static void check(bool ok,const char* why) {if(!ok) {std::cerr<<why<<'\n';std::exit(1);}}
static void word(std::vector<uint8_t>& b,uint32_t n) {
    for(int shift=24;shift>=0;shift-=8) b.push_back(uint8_t(n>>shift));
}
static void wide(std::vector<uint8_t>& b,uint64_t n) {word(b,uint32_t(n>>32));word(b,uint32_t(n));}
static void pad(std::vector<uint8_t>& b) {while(b.size()%4) b.push_back(0);}
static void begin(std::vector<uint8_t>& b,const char* s) {word(b,1);do {b.push_back(uint8_t(*s));} while(*s++);pad(b);}
static void property(std::vector<uint8_t>& b,std::vector<uint8_t>& names,const char* n,std::vector<uint8_t> value) {
    word(b,3);word(b,uint32_t(value.size()));word(b,uint32_t(names.size()));
    do {names.push_back(uint8_t(*n));} while(*n++);
    b.insert(b.end(),value.begin(),value.end());pad(b);
}
static std::vector<uint8_t> be(uint32_t n) {std::vector<uint8_t> b;word(b,n);return b;}
static std::vector<uint8_t> fixture(bool no_map=false) {
    std::vector<uint8_t> s,n;
    begin(s,"");property(s,n,"#address-cells",be(2));property(s,n,"#size-cells",be(2));
    begin(s,"chosen");property(s,n,"bootargs",{'o','l','d',0});
    property(s,n,"linux,initrd-start",be(0x49000000));property(s,n,"atag,kept",{1,2,3,4});word(s,2);
    begin(s,"reserved-memory");property(s,n,"#address-cells",be(2));property(s,n,"#size-cells",be(2));
    property(s,n,"ranges",{});begin(s,"framebuffer");
    std::vector<uint8_t> framebuffer_reg;wide(framebuffer_reg,0xff32b000);wide(framebuffer_reg,0xcd4000);property(s,n,"reg",framebuffer_reg);
    if(no_map) property(s,n,"no-map",{});
    word(s,2);word(s,2);
    word(s,2);word(s,9);
    std::vector<uint8_t> result;
    for(auto v:{0xd00dfeedu,uint32_t(56+s.size()+n.size()),56u,uint32_t(56+s.size()),40u,17u,16u,0u,uint32_t(n.size()),uint32_t(s.size())}) word(result,v);
    result.resize(56);result.insert(result.end(),s.begin(),s.end());result.insert(result.end(),n.begin(),n.end());return result;
}
int main(int argc,char** argv) {
    const char* error=nullptr;LinuxImage planned{};
    std::vector<uint8_t> image(4096);std::memcpy(image.data()+56,"ARM\x64",4);
    image[18]=0x80;image[24]=10;
    check(plan_linux_image({image.data(),image.size()},0x40000000,0x42a50000,planned,error) &&
          planned.address==0x40000000 && planned.memory_bytes==0x800000,"valid raw Linux Image rejected");
    check(!plan_linux_image({image.data(),image.size()},0x40001000,0x42a50000,planned,error),"unaligned RAM accepted");
    check(!plan_linux_image({image.data(),image.size()},0x40000000,0x40200000,planned,error),"kernel may overwrite running loader");
    image[24]|=1;check(!plan_linux_image({image.data(),image.size()},0x40000000,0x42a50000,planned,error),"big endian kernel accepted");image[24]=10;
    image[10]=8;check(plan_linux_image({image.data(),image.size()},0x40000000,0x42a50000,planned,error) &&
                         planned.address==0x40080000,"text offset ignored");
    LinuxTree o{text("console=tty0 rdinit=/init"),0x43000000,4096,0x42a50000,0x1000000,
                0xff32b000,0xc50000,800,1340,800,1344,3};
    for(bool no_map:{false,true}) {
        auto input=fixture(no_map),before=input;Fdt tree{};size_t bytes=99;
        check(Fdt::open({input.data(),input.size()},tree,error),"fixture DT invalid");
        std::vector<uint8_t> output(8194,0x5a);
        check(build_linux_tree(tree,o,output.data()+1,8192,bytes,error),"valid Linux DT clone failed");
        check(input==before && output.front()==0x5a && output.back()==0x5a,"DT clone changed input or buffer guard");
        Fdt copy{};check(Fdt::open({output.data()+1,bytes},copy,error),"cloned DT invalid");Bytes p{};
        check(copy.property(text("/chosen"),text("bootargs"),p) && p.size==o.arguments.size+1 &&
              std::memcmp(p.data,o.arguments.data,o.arguments.size)==0 && !p.data[p.size-1],"Linux command line not installed");
        check(copy.property(text("/chosen"),text("linux,initrd-start"),p) && p.size==8 && big64(p.data)==o.initrd_start,"initrd address wrong");
        check(copy.property(text("/chosen"),text("linux,initrd-end"),p) && big64(p.data)==o.initrd_start+o.initrd_bytes,"initrd end wrong");
        check(copy.property(text("/chosen"),text("atag,kept"),p) && p.size==4 && p.data[0]==1,"unrelated chosen property lost");
        check(copy.property(text("/chosen"),text("kbl,framebuffer"),p) && p.size==36 && big64(p.data)==o.framebuffer &&
              big64(p.data+28)==o.framebuffer_bytes,"framebuffer contract wrong");
        check(copy.reservations.size==32 && big64(copy.reservations.data)==o.loader_start &&
              big64(copy.reservations.data+8)==o.loader_bytes,"loader memory not reserved");
        unsigned properties=0;
        copy.walk([](Text path,Text name,Bytes,void* p) {
            if(equal(path,text("/reserved-memory/framebuffer")) && equal(name,text("no-map"))) ++*static_cast<unsigned*>(p);
            return true;
        },&properties,error);
        check(properties==1,"framebuffer no-map absent or duplicated");
        auto shared=o;shared.framebuffer_no_map=false;
        std::vector<uint8_t> shared_output(8192);size_t shared_size=0;
        check(build_linux_tree(tree,shared,shared_output.data(),shared_output.size(),shared_size,error),"shared-kernel DT clone failed");
        Fdt shared_tree{};check(Fdt::open({shared_output.data(),shared_size},shared_tree,error),"shared-kernel DT invalid");
        check(shared_tree.property(text("/reserved-memory/framebuffer"),text("no-map"),p)==no_map,
              "shared-kernel DT changed framebuffer mapping attributes");
        auto desktop=o;desktop.simple_framebuffer=true;
        check(build_linux_tree(tree,desktop,shared_output.data(),shared_output.size(),shared_size,error),"simple framebuffer DT failed");
        check(Fdt::open({shared_output.data(),shared_size},shared_tree,error),"simple framebuffer DT invalid");
        const auto fb=text("/chosen/framebuffer@ff32b000");
        check(shared_tree.property(fb,text("compatible"),p) && p.size==19 && !std::memcmp(p.data,"simple-framebuffer",19),"simple framebuffer binding missing");
        check(shared_tree.property(fb,text("reg"),p) && p.size==16 && big64(p.data)==o.framebuffer &&
              big64(p.data+8)==uint64_t(o.stride)*o.stored_height*4,"simple framebuffer range wrong");
        check(shared_tree.property(fb,text("stride"),p) && big32(p.data)==3200,"simple framebuffer pitch wrong");
        check(shared_tree.property(fb,text("format"),p) && p.size==9 && !std::memcmp(p.data,"x8r8g8b8",9),"simple framebuffer format wrong");
        size_t duplicate=0;
        check(!build_linux_tree(shared_tree,desktop,output.data()+1,8192,duplicate,error),"duplicate framebuffer node accepted");
        check(!build_linux_tree(tree,o,output.data()+1,128,bytes,error) && !bytes,"short DT output accepted");
        check(!build_linux_tree(tree,o,input.data(),input.size(),bytes,error) && input==before,"overlapping output accepted");
        auto invalid=o;invalid.initrd_start=0x40000000;
        check(!build_linux_tree(tree,invalid,output.data()+1,8192,bytes,error),"unreserved initrd accepted");
        invalid=o;invalid.arguments={"bad\0args",8};
        check(!build_linux_tree(tree,invalid,output.data()+1,8192,bytes,error),"embedded command-line NUL accepted");
        auto adapted=o;
        adapted.target_property=[](Text path,Text name,Bytes& value) {
            if(equal(path,text("/chosen")) && equal(name,text("atag,kept"))) {
                static const uint8_t replacement[]={7,8};value={replacement,2};
                return LinuxPropertyAction::replace;
            }
            return LinuxPropertyAction::keep;
        };
        check(build_linux_tree(tree,adapted,shared_output.data(),shared_output.size(),shared_size,error),"external property replacement failed");
        check(Fdt::open({shared_output.data(),shared_size},shared_tree,error) && shared_tree.property(text("/chosen"),text("atag,kept"),p) && p.size==2 && p.data[0]==7,"external property replacement lost");
        adapted.target_property=[](Text path,Text name,Bytes&) {
            return equal(path,text("/chosen")) && equal(name,text("atag,kept")) ? LinuxPropertyAction::omit : LinuxPropertyAction::keep;
        };
        check(build_linux_tree(tree,adapted,shared_output.data(),shared_output.size(),shared_size,error),"external property omission failed");
        check(Fdt::open({shared_output.data(),shared_size},shared_tree,error) && !shared_tree.property(text("/chosen"),text("atag,kept"),p),"external property omission lost");
        adapted.target_property=[](Text,Text,Bytes& value) {value={nullptr,1};return LinuxPropertyAction::replace;};
        check(!build_linux_tree(tree,adapted,shared_output.data(),shared_output.size(),shared_size,error) && input==before,"invalid adapter value accepted or source changed");
    }
    if(argc==2) {
        std::ifstream file(argv[1],std::ios::binary);
        std::vector<uint8_t> input{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()},before=input;
        Fdt tree{};gta9wifi::Display display{};
        check(Fdt::open({input.data(),input.size()},tree,error),"captured live DT invalid");
        check(gta9wifi::discover_display(tree,display,error),"captured live framebuffer invalid");
        auto options=o;options.framebuffer=display.address;options.framebuffer_bytes=display.reserved_bytes;
        options.width=display.width;options.height=display.height;options.stride=display.stride;
        options.stored_height=display.stored_height;options.planes=display.buffers;
        std::vector<uint8_t> copy(2*1024*1024);size_t size=0;
        check(build_linux_tree(tree,options,copy.data(),copy.size(),size,error),"captured live DT clone failed");
        check(input==before,"captured live DT was modified");
        Fdt prepared{};check(Fdt::open({copy.data(),size},prepared,error),"prepared live DT invalid");
        struct Compare {const Fdt& prepared;bool okay=true;} compare{prepared};
        tree.walk([](Text path,Text name,Bytes value,void* p) {
            auto& q=*static_cast<Compare*>(p);
            if(equal(path,text("/chosen")) && (equal(name,text("bootargs")) || equal(name,text("linux,initrd-start")) ||
                equal(name,text("linux,initrd-end")) || equal(name,text("kbl,framebuffer")))) return true;
            Bytes actual{};
            if(!q.prepared.property(path,name,actual) || actual.size!=value.size ||
               (value.size && std::memcmp(actual.data,value.data,value.size)!=0)) q.okay=false;
            return true;
        },&compare,error);
        check(compare.okay,"Linux preparation lost an unrelated live DT property");
        std::cout<<"Captured live DT preserved except explicit Linux properties\n";
    }
    std::cout<<"Linux Image bounds, DT isolation, initrd and framebuffer reservations passed\n";
}
