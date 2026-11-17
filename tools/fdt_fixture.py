"""Small FDT fixture editor for VM tests, not a device flashing tool."""
from dataclasses import dataclass,field
import struct


@dataclass
class Node:
    name: str
    props: dict=field(default_factory=dict)
    children: list=field(default_factory=list)
    def child(self,name):
        return next((n for n in self.children if n.name==name),None)


def decode(data):
    magic,total,pos,strings,reserve,version,last,boot,ns,nstruct=struct.unpack_from('>10I',data)
    assert magic==0xd00dfeed and total<=len(data) and version>=17
    root=None;stack=[];reservations=[]
    while True:
        address,size=struct.unpack_from('>QQ',data,reserve);reserve+=16
        if not address and not size:break
        reservations.append((address,size))
    while True:
        token=struct.unpack_from('>I',data,pos)[0];pos+=4
        if token==1:
            end=data.index(0,pos);node=Node(data[pos:end].decode());pos=(end+4)&~3
            if stack:stack[-1].children.append(node)
            else:root=node
            stack.append(node)
        elif token==2:stack.pop()
        elif token==3:
            size,name=struct.unpack_from('>II',data,pos);pos+=8
            key=data[strings+name:data.index(0,strings+name)].decode()
            assert key not in stack[-1].props
            stack[-1].props[key]=data[pos:pos+size];pos=(pos+size+3)&~3
        elif token==9:break
        else:assert token==4
    assert root is not None and not stack
    return root,reservations,boot


def encode(root,reservations=(),boot=0):
    names=bytearray();indices={}
    def word(n):return struct.pack('>I',n)
    def padded(b):return b+b'\0'*(-len(b)%4)
    def node(n):
        out=word(1)+padded(n.name.encode()+b'\0')
        for name,value in n.props.items():
            if name not in indices:
                indices[name]=len(names);names.extend(name.encode()+b'\0')
            out+=word(3)+word(len(value))+word(indices[name])+padded(value)
        return out+b''.join(node(c) for c in n.children)+word(2)
    body=node(root)+word(9)
    reserved=b''.join(struct.pack('>QQ',*r) for r in list(reservations)+[(0,0)])
    structure=40+len(reserved);strings=structure+len(body);total=strings+len(names)
    return struct.pack('>10I',0xd00dfeed,total,structure,strings,40,17,16,boot,len(names),len(body))+reserved+body+names


def graft_controls(root):
    from probe_smoke import test_dtb
    fake,_,_=decode(test_dtb(controls=True))
    root.props['model']=fake.props['model']
    chosen=root.child('chosen')
    if chosen is None:chosen=Node('chosen');root.children.append(chosen)
    chosen.props.update(fake.child('chosen').props)
    soc=root.child('soc')
    if soc is None:soc=Node('soc');root.children.append(soc)
    soc.children.extend(fake.child('soc').children)
    reserved=root.child('reserved-memory')
    if reserved is None:
        reserved=Node('reserved-memory',{'#address-cells':struct.pack('>I',2),
            '#size-cells':struct.pack('>I',2),'ranges':b''});root.children.append(reserved)
    reserved.children.append(Node('kbl-test-framebuffer',{'reg':struct.pack('>QQ',0x50000000,0xc50000)}))
