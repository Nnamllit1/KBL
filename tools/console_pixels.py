"""Check early-console text directly in a raw multi-buffer framebuffer capture."""
import struct


def kernel_font(image,system_map):
    symbols={p[2]:int(p[0],16) for line in system_map.splitlines() if len(p:=line.split())==3}
    base=symbols['_text'];descriptor=symbols['font_vga_8x16']-base
    table=symbols['fontdata_8x16']-base
    if struct.unpack_from('<III',image,descriptor+16)!=(8,16,256):
        raise ValueError('Expected the kernel VGA 8x16 font descriptor')
    if struct.unpack_from('<I',image,table+8)[0]!=4096 or table+16+4096>len(image):
        raise ValueError('Invalid kernel font bitmap extent')
    return image[table+16:table+16+4096]


def find_console_text(pixels,font,needle,width=800,height=1340,stride=800):
    if len(font)!=4096 or not needle or len(needle)*8>width:
        raise ValueError('Invalid console text/font size')
    white=struct.pack('<I',0xffe0e0e0);black=struct.pack('<I',0xff000000)
    rows=[b''.join(white if font[ord(c)*16+y]&(0x80>>x) else black
                   for c in needle for x in range(8)) for y in range(16)]
    probe=max(range(16),key=lambda y:rows[y].count(white))
    pitch=stride*4;cursor=0
    while (position:=pixels.find(rows[probe],cursor))>=0:
        cursor=position+1;x=(position%pitch)//4;y=position//pitch-probe
        if position%4 or x+len(needle)*8>width or y<0 or y+16>height:continue
        if all(pixels[(y+n)*pitch+x*4:(y+n)*pitch+x*4+len(row)]==row for n,row in enumerate(rows)):
            return {'column':x//8,'pixel_y':y}
    return None
