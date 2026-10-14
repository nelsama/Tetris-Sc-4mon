"""Genera los patrones (planos p0/p1) del cielo / Kremlin de Tetris.

Convencion de indices para el fondo (paleta 3):
    idx1 = azul medianoche (cielo / sombra)
    idx2 = dorado (cupulas, estrellas, detalles)
    idx3 = transparente (0 del patron) -> se ve BG_COLOR (negro)
Cada patron es 8x8, bit7 = pixel izquierdo.
"""

def P(rows_idx):
    p0, p1 = [], []
    for r in rows_idx:
        v0 = v1 = 0
        for c, ch in enumerate(r):
            idx = int(ch)
            v0 |= (idx & 1) << (7 - c)
            v1 |= ((idx >> 1) & 1) << (7 - c)
        p0.append(v0); p1.append(v1)
    return p0, p1


NEXT = [0]


def emit(comment, rows_idx, name=None):
    if name is None:
        name = "tile_bg%d" % NEXT[0]
        NEXT[0] += 1
    p0, p1 = P(rows_idx)
    print(f"/* {comment} */")
    print("static const uint8_t %s_p0[8] = { %s };" % (name, ", ".join("0x%02X" % x for x in p0)))
    print("static const uint8_t %s_p1[8] = { %s };" % (name, ", ".join("0x%02X" % x for x in p1)))
    print()
    return name


# idx: 1=azul medianoche, 2=dorado, 0/3=transparente
# ---- Cielo con estrellas (fondo azul) ----
sky = [
    "11111111",
    "11121111",
    "11111111",
    "11211111",
    "11111111",
    "11111121",
    "11111111",
    "12111111",
]
emit("Cielo: azul medianoche con estrellas doradas", sky, "tile_sky")

# ---- Cupula de cebolla: mitad SUPERIOR (2 tiles de ancho) ----
dome_top_l = [
    "00022000",
    "00222200",
    "02211220",
    "22111122",
    "22111122",
    "22111122",
    "22111122",
    "22111122",
]
dome_top_r = [
    "00022000",
    "00222200",
    "02211220",
    "22111122",
    "22111122",
    "22111122",
    "22111122",
    "22111122",
]
emit("Cupula superior: mitad IZQUIERDA", dome_top_l, "tile_dome_top_l")
emit("Cupula superior: mitad DERECHA", dome_top_r, "tile_dome_top_r")

# ---- Cupula - mitad INFERIOR (mas estrecha, cuello) ----
dome_bot_l = [
    "22111122",
    "02211120",
    "02211120",
    "00221100",
    "00222200",
    "00222200",
    "00222200",
    "00000000",
]
dome_bot_r = [
    "22111122",
    "02211120",
    "02211120",
    "00221100",
    "00222200",
    "00222200",
    "00222200",
    "00000000",
]
emit("Cupula inferior: mitad IZQUIERDA", dome_bot_l, "tile_dome_bot_l")
emit("Cupula inferior: mitad DERECHA", dome_bot_r, "tile_dome_bot_r")

# ---- Torre: cuerpo con ventanas (2 tiles de alto) ----
tower_sup = [
    "22222222",
    "21111112",
    "21121112",
    "21121112",
    "21111112",
    "22222222",
    "21122112",
    "21122112",
]
tower_inf = [
    "21122112",
    "21122112",
    "21111112",
    "22222222",
    "21111112",
    "21111112",
    "21111112",
    "22222222",
]
emit("Torre: cuerpo superior", tower_sup, "tile_tower_sup")
emit("Torre: cuerpo inferior", tower_inf, "tile_tower_inf")

# ---- Aguja / espira con estrella arriba ----
spire = [
    "00022000",
    "00022000",
    "00022000",
    "00022000",
    "00011000",
    "00111100",
    "01111110",
    "11111111",
]
emit("Aguja de torre con punta dorada", spire, "tile_spire")

if __name__ == "__main__":
    pass
