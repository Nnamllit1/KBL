#include "kbl/linux_boot.hpp"

namespace kbl {
namespace {
uint64_t little64(const uint8_t* p) {
    uint64_t value=0;
    for(unsigned i=0;i<8;++i) value|=uint64_t(p[i])<<(i*8);
    return value;
}
bool range(uint64_t start,uint64_t bytes) {return bytes && start<=UINT64_MAX-bytes;}
bool begins(Text value,Text prefix) {
    return value.size>=prefix.size && equal({value.data,prefix.size},prefix);
}
struct Writer {
    uint8_t* data;size_t capacity,used=0;bool valid=true;
    void raw(const uint8_t* p,size_t size) {
        if(size>capacity-used) {valid=false;return;}
        for(size_t i=0;i<size;++i) data[used++]=p[i];
    }
    void word(uint32_t n) {
        uint8_t p[4]{uint8_t(n>>24),uint8_t(n>>16),uint8_t(n>>8),uint8_t(n)};raw(p,4);
    }
    void wide(uint64_t n) {word(uint32_t(n>>32));word(uint32_t(n));}
    void pad() {while(valid && used%4) {const uint8_t zero=0;raw(&zero,1);}}
    void str(Text s) {raw(reinterpret_cast<const uint8_t*>(s.data),s.size);const uint8_t zero=0;raw(&zero,1);pad();}
    void property(uint32_t name,Bytes b) {word(3);word(uint32_t(b.size));word(name);raw(b.data,b.size);pad();}
    void value(uint32_t name,uint64_t n) {word(3);word(8);word(name);wide(n);}
    void small(uint32_t name,uint32_t n) {word(3);word(4);word(name);word(n);}
};
}
bool plan_linux_image(Bytes image,uint64_t ram_base,uint64_t loader_start,LinuxImage& out,const char*& error) {
    error="unsupported Linux Image layout";
    if(!image.data || image.size<64 || big32(image.data+56)!=0x41524d64) return false;
    const auto offset=little64(image.data+8),span=little64(image.data+16),flags=little64(image.data+24);
    if(flags&1 || ram_base%0x200000 || offset>=0x200000 || offset>UINT64_MAX-ram_base ||
       span<image.size || image.size<4096 || ram_base+offset>=loader_start ||
       span>loader_start-(ram_base+offset)) return false;
    out={ram_base+offset,span};error=nullptr;return true;
}
bool build_linux_tree(const Fdt& source,const LinuxTree& o,uint8_t* output,size_t capacity,
                      size_t& bytes,const char*& error) {
    bytes=0;error="invalid Linux DT options";
    if(!output || capacity<128 || capacity>2*1024*1024 || source.blob.size>2*1024*1024 ||
       o.arguments.size>4095 || (o.arguments.size && !o.arguments.data) ||
       !range(o.initrd_start,o.initrd_bytes) || !range(o.loader_start,o.loader_bytes) ||
       !range(o.framebuffer,o.framebuffer_bytes) || !o.width || !o.height ||
       o.stride<o.width || o.stored_height<o.height || o.planes<1 || o.planes>3 ||
       (o.simple_framebuffer && o.stride>UINT32_MAX/4) ||
       uint64_t(o.stride)*o.stored_height>o.framebuffer_bytes/(o.planes*4) ||
       o.initrd_start<o.loader_start || o.initrd_start+o.initrd_bytes>o.loader_start+o.loader_bytes) return false;
    for(size_t i=0;i<o.arguments.size;++i)
        if(o.arguments.data[i]<32 || o.arguments.data[i]>126) return false;
    Bytes cells{};
    if(!source.property(text("/reserved-memory"),text("#address-cells"),cells) || cells.size!=4 || big32(cells.data)!=2 ||
       !source.property(text("/reserved-memory"),text("#size-cells"),cells) || cells.size!=4 || big32(cells.data)!=2) {
        error="unsupported reserved-memory layout";return false;
    }
    struct Find {const LinuxTree& o;String<1024> path;unsigned matches=0;} find{o,{}};
    if(!source.walk([](Text p,Text n,Bytes b,void* opaque) {
        auto& q=*static_cast<Find*>(opaque);
        if(begins(p,text("/reserved-memory/")) && equal(n,text("reg")) && b.size==16 &&
           big64(b.data)==q.o.framebuffer && big64(b.data+8)>=q.o.framebuffer_bytes) {
            ++q.matches;q.path.assign(p);
        }
        return true;
    },&find,error)) return false;
    if(find.matches!=1) {error="framebuffer reservation is missing or ambiguous";return false;}
    const auto out_start=reinterpret_cast<uintptr_t>(output),src_start=reinterpret_cast<uintptr_t>(source.blob.data);
    if(out_start>UINTPTR_MAX-capacity || src_start>UINTPTR_MAX-source.blob.size ||
       (out_start<src_start+source.blob.size && src_start<out_start+capacity)) {
        error="Linux DT output overlaps live DT";return false;
    }
    Writer w{output,capacity};
    for(unsigned i=0;i<10;++i) w.word(0);
    w.raw(source.reservations.data,source.reservations.size-16);
    w.wide(o.loader_start);w.wide(o.loader_bytes);w.wide(0);w.wide(0);
    const auto structure_start=w.used;
    const char* names[]={"bootargs","linux,initrd-start","linux,initrd-end","kbl,framebuffer","no-map",
        "compatible","reg","width","height","stride","format","#address-cells","#size-cells","ranges"};
    constexpr unsigned name_count=sizeof(names)/sizeof(names[0]);
    uint32_t offsets[name_count]{};size_t names_size=source.strings.size;
    for(unsigned i=0;i<name_count;++i) {offsets[i]=uint32_t(names_size);names_size+=text(names[i]).size+1;}
    if(names_size>capacity) {error="Linux DT strings exceed capacity";return false;}
    char path[1024]{};size_t lengths[64]{},depth=0,length=0,cursor=0;unsigned chosen=0,fb_nodes=0;
    while(cursor<source.structure.size && w.valid) {
        const auto start=cursor;const auto token=big32(source.structure.data+cursor);cursor+=4;
        if(token==1) {
            const auto name_start=cursor;
            while(source.structure.data[cursor]) ++cursor;
            const auto count=cursor-name_start;cursor=(cursor+4)&~size_t(3);
            if(depth>=64 || length+count+2>=sizeof(path)) {error="Linux DT nesting exceeds limit";return false;}
            lengths[depth++]=length;
            if(depth==1) {path[0]='/';length=1;}
            else {
                if(length>1) path[length++]='/';
                for(size_t i=0;i<count;++i) path[length++]=char(source.structure.data[name_start+i]);
            }
            path[length]=0;w.raw(source.structure.data+start,cursor-start);
            if(equal({path,length},text("/chosen"))) {
                ++chosen;w.word(3);w.word(uint32_t(o.arguments.size+1));w.word(offsets[0]);w.str(o.arguments);
                w.value(offsets[1],o.initrd_start);w.value(offsets[2],o.initrd_start+o.initrd_bytes);
                w.word(3);w.word(36);w.word(offsets[3]);w.wide(o.framebuffer);
                w.word(o.width);w.word(o.height);w.word(o.stride);w.word(o.stored_height);w.word(o.planes);w.wide(o.framebuffer_bytes);
                if(o.simple_framebuffer) {w.small(offsets[11],2);w.small(offsets[12],2);w.property(offsets[13],{});}
            }
            if(o.simple_framebuffer && begins({path,length},text("/chosen/framebuffer"))) {
                error="existing chosen framebuffer conflicts with Linux handoff";return false;
            }
            if(equal({path,length},find.path.view())) {++fb_nodes;if(o.framebuffer_no_map)w.property(offsets[4],{});}
        } else if(token==2) {
            if(o.simple_framebuffer && equal({path,length},text("/chosen"))) {
                char node[40]="framebuffer@";size_t count=12;bool started=false;
                for(int shift=60;shift>=0;shift-=4) {
                    const auto digit=unsigned((o.framebuffer>>shift)&15);
                    if(digit || started || !shift) {node[count++]="0123456789abcdef"[digit];started=true;}
                }
                node[count]=0;w.word(1);w.str({node,count});
                w.property(offsets[5],{reinterpret_cast<const uint8_t*>("simple-framebuffer"),19});
                w.word(3);w.word(16);w.word(offsets[6]);w.wide(o.framebuffer);w.wide(uint64_t(o.stride)*o.stored_height*4);
                w.small(offsets[7],o.width);w.small(offsets[8],o.height);w.small(offsets[9],o.stride*4);
                w.property(offsets[10],{reinterpret_cast<const uint8_t*>("x8r8g8b8"),9});w.word(2);
            }
            w.word(2);length=lengths[--depth];path[length]=0;
        } else if(token==3) {
            const auto size=big32(source.structure.data+cursor),index=big32(source.structure.data+cursor+4);
            cursor=(cursor+8+size+3)&~size_t(3);
            Text name=text(reinterpret_cast<const char*>(source.strings.data+index));bool skip=false;
            if(equal({path,length},text("/chosen")))
                for(unsigned i=0;i<4;++i) if(equal(name,text(names[i]))) skip=true;
            if(o.simple_framebuffer && equal({path,length},text("/chosen")))
                for(unsigned i=11;i<14;++i) if(equal(name,text(names[i]))) skip=true;
            if(o.framebuffer_no_map && equal({path,length},find.path.view()) && equal(name,text("no-map"))) skip=true;
            if(!skip && o.target_property) {
                Bytes value{source.structure.data+start+12,size};
                const auto action=o.target_property({path,length},name,value);
                if(action==LinuxPropertyAction::omit)skip=true;
                else if(action==LinuxPropertyAction::replace) {
                    if(value.size>2*1024*1024 || (value.size && !value.data)) {error="invalid target DT property";return false;}
                    w.property(index,value);skip=true;
                }
            }
            if(!skip) w.raw(source.structure.data+start,cursor-start);
        } else w.word(token);
    }
    const auto structure_size=w.used-structure_start,strings_start=w.used;
    w.raw(source.strings.data,source.strings.size);
    for(const auto name:names) {const auto s=text(name);w.raw(reinterpret_cast<const uint8_t*>(s.data),s.size+1);}
    if(!w.valid || chosen!=1 || fb_nodes!=1 || w.used>capacity) {error="Linux DT copy failed or exceeds capacity";return false;}
    Writer header{output,40};header.word(0xd00dfeed);header.word(uint32_t(w.used));header.word(uint32_t(structure_start));
    header.word(uint32_t(strings_start));header.word(40);header.word(17);header.word(16);
    header.word(big32(source.blob.data+28));header.word(uint32_t(names_size));header.word(uint32_t(structure_size));
    Fdt check{};
    if(!Fdt::open({output,w.used},check,error)) return false;
    bytes=w.used;error=nullptr;return true;
}
}
