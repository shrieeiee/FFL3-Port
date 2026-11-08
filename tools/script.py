import sys
d=open(sys.argv[1],'rb').read()
o=0xc000+0x31c5; dic=[]
for i in range(0x100-0x4e):
    n=d[o]; dic.append(d[o+1:o+1+n]); o+=1+n
P={0xff:' ',0xac:'.',0xaf:',',0xae:'!',0xad:'?',0xf2:'.',0xf0:'"',0xef:'"'}
def glyph(b):
    if 0xba<=b<=0xd3: return chr(65+b-0xba)
    if 0xd4<=b<=0xed: return chr(97+b-0xd4)
    if 0xb0<=b<=0xb9: return chr(48+b-0xb0)
    return P.get(b, f'<{b:02x}>')
def dec(bs, depth=0):
    s=''
    for b in bs:
        if b>=0x4e and b<0xb0 and depth<3: s+=dec(dic[b-0x4e], depth+1)
        elif b<0x4e: s+={0x06:'\n',0x00:'<END>\n'}.get(b, f'{{{b:02x}}}')
        else: s+=glyph(b)
    return s
start=int(sys.argv[2],16); n=int(sys.argv[3],16)
print(dec(d[start:start+n]))
