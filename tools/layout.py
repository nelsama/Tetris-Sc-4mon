"""Simula el layout (40x30 celdas) con el skyline de torres (max 10 tiles alto)."""
W, H = 40, 30
BOARD_X0, BOARD_Y0 = 15, 5
FLOOR = 29

towerL = [10, 6, 9, 7]
towerR = [7, 9, 6, 10]
towerL_col = [0, 4, 8, 12]
towerR_col = [26, 30, 34, 38]

g = [['.'] * W for _ in range(H)]

def draw_tower(col, alto):
    alto = max(5, min(10, alto))
    top = FLOOR + 1 - alto
    for i in range(5):          # remate (5 celdas)
        if top+i < H:
            g[top+i][col] = 'T'
            if col+1 < W: g[top+i][col+1] = 'R'
    y = top+5
    while y <= FLOOR:           # base repetida
        g[y][col] = 'L'
        if col+1 < W: g[y][col+1] = 'D'
        y += 1

for t in range(4):
    draw_tower(towerL_col[t], towerL[t])
    draw_tower(towerR_col[t], towerR[t])

def draw_well():
    for by in range(20):
        for bx in range(10):
            g[BOARD_Y0+by][BOARD_X0+bx] = 'o'
    for i in range(21):
        g[BOARD_Y0+i][BOARD_X0-1] = '|'
        g[BOARD_Y0+i][BOARD_X0+10] = '|'
    for i in range(10):
        g[BOARD_Y0-1][BOARD_X0+i] = '-'
        g[BOARD_Y0+20][BOARD_X0+i] = '-'

def put(col,row,s):
    for i,ch in enumerate(s):
        if col+i<W and row<H: g[row][col+i]=ch

draw_well()
put(15,1,"T E T R I S")
put(29,3,"NEXT")
put(29,10,"PTS"); put(33,10,"00000")
put(29,12,"LINEAS"); put(36,12,"000")
put(29,14,"NIVEL"); put(36,14,"01")
put(29,16,"MEJOR"); put(34,16,"00000")
put(0,2,"WASD:mover BOTON/W:rotar P:pausa Q:salir")

print("   " + "".join(str(i%10) for i in range(W)))
for y in range(H):
    print("%2d "%y + "".join(g[y]))
print()
print("Alturas (tiles): izq", towerL, " der", towerR, " (max 10)")
print("Columnas: izq", towerL_col, " der", towerR_col)
print("T/R=remate  L/D=base repetida  o=pozo  |-=marco  .=cielo")
