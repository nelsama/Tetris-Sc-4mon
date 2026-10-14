"""Renderiza los 10 tiles de torre + una torre compuesta, con los colores reales,
para comparar con la referencia del usuario. Salida: PPM."""
import re

def rgb444(r,g,b): return (r*17,g*17,b*17)
# pal3
PAL3 = {0:(0,0,0xCC), 1:rgb444(0xD,0x6,0x0), 2:rgb444(0xF,0xC,0x0), 3:rgb444(0x8,0x3,0x0)}

src=open('game.c').read()
def get(n):
    m=re.search(r'uint8_t\s+'+re.escape(n)+r'\[8\]\s*=\s*\{([^}]*)\}', src)
    return [int(x,16) for x in re.findall(r'0x([0-9A-Fa-f]+)', m.group(1))]

names=[('torre%d'%(i),'torre%d'%(i+5)) for i in range(1,6)]

# Imagen: 5 filas x 2 columnas de tiles de 8x8 -> 16 x 40 px, zoom x8
Z=8
cols=2; rows=5
img=[['BG']*(8*Z*cols) for _ in range(8*Z*rows)]

def draw_tile(n, ox, oy):
    p0=get('torre%d_p0'%n); p1=get('torre%d_p1'%n)
    for r in range(8):
        for c in range(8):
            b=7-c
            idx=((p1[r]>>b)&1)<<1 | ((p0[r]>>b)&1)
            col=PAL3[idx] if idx!=0 else (0,0,0xCC)
            for zy in range(Z):
                for zx in range(Z):
                    img[oy+r*Z+zy][ox+c*Z+zx]=col

for i,(l,rr) in enumerate(names):
    draw_tile(i+1, 0, i*8*Z)
    draw_tile(i+6, 8*Z, i*8*Z)

W=8*Z*cols; H=8*Z*rows
with open('tools/ref_tiles.ppm','w') as f:
    f.write('P3\n%d %d\n255\n'%(W,H))
    for row in img:
        f.write(' '.join('%d %d %d'%px for px in row)+'\n')
print('Escrito tools/ref_tiles.ppm (%dx%d)'%(W,H))
print('Colores: idx0=azulBG idx1=naranja idx2=amarillo idx3=naranjaOscuro')
