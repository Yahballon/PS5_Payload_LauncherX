#!/usr/bin/env python3
"""Generate original console icon and neon background, PS5 runtime loads PNG."""
from PIL import Image, ImageDraw, ImageFilter
from math import sin, cos
from pathlib import Path
import random
random.seed(4)
out=Path('app/assets')
out.mkdir(parents=True,exist_ok=True)
W,H=1920,1080
bg=Image.new('RGB',(W,H))
p=bg.load()
for y in range(H):
    for x in range(W):
        light=max(0.0,1-abs(x-1750)/2500)
        p[x,y]=(int(1+3*light),int(7+8*y/H+2*light),int(20+13*y/H+12*light))

def curve_points(start,end,bend=0,n=100):
    x0,y0=start;x1,y1=end
    return [(x0+(x1-x0)*t, y0+(y1-y0)*t+bend*sin(t*3.14159)) for t in [j/n for j in range(n+1)]]

ribbons=[((-280,100),(1270,-120),-180,150),((530,250),(1180,-150),110,95),((1530,-150),(2030,155),125,100),((-190,720),(1990,995),180,78),((-120,868),(1950,1010),-220,100),((1860,690),(1290,1110),-140,112)]
overlay=Image.new('RGBA',(W,H),(0,0,0,0));d=ImageDraw.Draw(overlay)
for st,end,bend,width in ribbons:
    upper=curve_points(st,end,bend)
    lower=curve_points((st[0],st[1]+width),(end[0],end[1]+width),bend-30)
    poly=upper+lower[::-1]
    d.polygon(poly,fill=(0,44,138,102))
    d.line(upper,fill=(5,118,255,173),width=4,joint='curve')
    d.line(curve_points((st[0],st[1]+width*.25),(end[0],end[1]+width*.25),bend),fill=(0,79,225,64),width=17)
glow=overlay.filter(ImageFilter.GaussianBlur(20))
bg=Image.alpha_composite(bg.convert('RGBA'),glow)
bg=Image.alpha_composite(bg,overlay)
stars=Image.new('RGBA',(W,H),(0,0,0,0));sd=ImageDraw.Draw(stars)
for j in range(310):
    x=random.randrange(W);y=random.randrange(240,H)
    if 150<x<1750 and y<680:continue
    r=random.choice([1,1,1,2]);sd.ellipse((x-r,y-r,x+r,y+r),fill=(5,101,255,random.randrange(18,85)))
bg=Image.alpha_composite(bg,stars).convert('RGB')
bg.save(out/'launcher-background.png',optimize=True)

im=Image.new('RGBA',(512,512),(2,11,27,255))
ld=ImageDraw.Draw(im)
for i in range(24):
    z=20+i*3
    ld.rounded_rectangle((z,z,512-z,512-z),radius=max(6,105-i*2),outline=(0,max(30,148-i*4),255,max(40,190-i*5)),width=2)
ld.rounded_rectangle((53,53,459,459),radius=88,fill=(1,30,70,255),outline=(16,153,255,255),width=12)
ld.polygon([(132,162),(235,112),(235,341),(132,391)],fill=(18,143,242))
ld.polygon([(251,117),(378,189),(378,309),(251,388),(251,325),(313,290),(313,213),(251,178)],fill=(237,247,255))
ld.polygon([(181,220),(223,197),(223,262),(181,286)],fill=(1,29,66))
ld.line([(108,401),(225,466),(377,387)],fill=(15,143,253),width=7)
Path('app/sce_sys').mkdir(parents=True,exist_ok=True)
im.convert('RGB').save('app/sce_sys/icon0.png',optimize=True)
