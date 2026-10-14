"""Validacion del nucleo logico de Tetris (replica en Python).
Comprueba: tablas de rotacion (cada rotacion tiene 4 celdas, rotaciones
distintas, coherencia), spawn, colision, clear_lines y colapso.
"""
BOARD_W, BOARD_H = 10, 20

# --- Tablas de rotacion (igual que game.c). Cada fila = nibble, bit3=col0. ---
def P(a, b, c, d):
    return (a << 12) | (b << 8) | (c << 4) | d

piece_rot = [
    [0, 0, 0, 0],                                              # 0 vacio
    [P(0,0xF,0,0), P(0x2,0x2,0x2,0x2), P(0,0,0xF,0), P(0x4,0x4,0x4,0x4)],  # I
    [P(0xC,0xC,0,0), P(0xC,0xC,0,0), P(0xC,0xC,0,0), P(0xC,0xC,0,0)],      # O
    [P(0x4,0xE,0,0), P(0x8,0xC,0x8,0), P(0,0xE,0x4,0), P(0x4,0xC,0x4,0)],  # T
    [P(0x6,0xC,0,0), P(0x8,0xC,0x4,0), P(0,0x6,0xC,0), P(0x4,0xC,0x8,0)],  # S
    [P(0xC,0x6,0,0), P(0x4,0xC,0x8,0), P(0,0xC,0x6,0), P(0x8,0xC,0x4,0)],  # Z
    [P(0x8,0xE,0,0), P(0xC,0x8,0x8,0), P(0,0xE,0x2,0), P(0x4,0x4,0xC,0)],  # J
    [P(0x2,0xE,0,0), P(0x8,0x8,0xC,0), P(0,0xE,0x8,0), P(0xC,0x4,0x4,0)],  # L
]

NAMES = {1: "I", 2: "O", 3: "T", 4: "S", 5: "Z", 6: "J", 7: "L"}


def cells(mask):
    out = []
    for r in range(4):
        rowbits = (mask >> (12 - r * 4)) & 0x0F
        for c in range(4):
            if rowbits & (0x08 >> c):
                out.append((c, r))
    return out


def check_tables():
    ok = True
    for t in range(1, 8):
        rots = [frozenset(cells(piece_rot[t][r])) for r in range(4)]
        for r in range(4):
            n = len(rots[r])
            if n != 4:
                print(f"  ERROR pieza {NAMES[t]} rot {r}: {n} celdas (esperado 4)")
                ok = False
        # O debe ser identica en todas las rotaciones
        if t == 2 and len(set(rots)) != 1:
            print("  ERROR O: rotaciones distintas")
            ok = False
        # I/T/S/Z/J/L deben tener al menos 2 formas distintas
        if t in (1, 3, 4, 5, 6, 7) and len(set(rots)) < 2:
            print(f"  ERROR {NAMES[t]}: todas las rotaciones iguales")
            ok = False
    print("  tablas de rotacion:", "OK" if ok else "FALLO")
    return ok


# --- Tablero y operaciones ---
def new_board():
    return [[0] * BOARD_W for _ in range(BOARD_H)]


def fits(board, t, rot, px, py):
    for (c, r) in cells(piece_rot[t][rot]):
        bx, by = px + c, py + r
        if bx < 0 or bx >= BOARD_W:
            return False
        if by >= BOARD_H:
            return False
        if by < 0:
            continue
        if board[by][bx] != 0:
            return False
    return True


def lock(board, t, rot, px, py):
    for (c, r) in cells(piece_rot[t][rot]):
        bx, by = px + c, py + r
        if 0 <= bx < BOARD_W and 0 <= by < BOARD_H:
            board[by][bx] = t


def clear_lines(board):
    cleared = 0
    y = BOARD_H - 1
    while y >= 0:
        if all(board[y][x] != 0 for x in range(BOARD_W)):
            board.pop(y)
            board.insert(0, [0] * BOARD_W)
            cleared += 1
            # no decrementamos y: la fila de arriba cae aqui
        else:
            y -= 1
    return cleared


def test_spawn():
    b = new_board()
    ok = True
    for t in range(1, 8):
        # spawn en px=3, py=-1, busca la primera fila donde quepa
        px = BOARD_W // 2 - 2
        py = -1
        placed = False
        while py < 2:
            if fits(b, t, 0, px, py):
                placed = True
                break
            py += 1
        if not placed:
            print(f"  ERROR pieza {NAMES[t]} no cabe en pozo vacio")
            ok = False
    print("  spawn en pozo vacio:", "OK" if ok else "FALLO")
    return ok


def test_clear():
    b = new_board()
    ok = True
    # llena la fila 19 completa y la 18 a medias
    for x in range(BOARD_W):
        b[19][x] = 1
    for x in range(BOARD_W - 1):
        b[18][x] = 2
    n = clear_lines(b)
    if n != 1:
        print(f"  ERROR clear_lines: borro {n}, esperado 1")
        ok = False
    if any(b[19][x] == 1 for x in range(BOARD_W)):
        print("  ERROR: fila 19 no quedo limpia tras colapsar")
        ok = False
    # la fila 18 (con 9 bloques) debe estar ahora en la 19
    if b[19][0] != 2 or b[19][8] != 2 or b[19][9] != 0:
        print("  ERROR: el colapso no movio la fila 18 a la 19")
        ok = False

    # 4 lineas a la vez
    b = new_board()
    for y in range(16, 20):
        for x in range(BOARD_W):
            b[y][x] = 3
    n = clear_lines(b)
    if n != 4:
        print(f"  ERROR clear_lines 4: borro {n}")
        ok = False
    print("  clear_lines (1 y 4):", "OK" if ok else "FALLO")
    return ok


def test_wall_and_floor():
    b = new_board()
    ok = True
    # pieza no puede salirse por la izquierda
    if fits(b, 1, 0, -1, 0):
        print("  ERROR: I cabe fuera del borde izquierdo")
        ok = False
    # no puede atravesar el suelo
    if fits(b, 2, 0, 0, BOARD_H - 1):  # O ocupa 2 filas -> se sale
        print("  ERROR: O cabe atravesando el suelo")
        ok = False
    # bloque pegado a la izquierda impide movimiento a la izquierda.
    # O rot0 ocupa las columnas 0-1, filas 0-1 -> en py=5 usa filas 5-6;
    # ponemos el bloque en la fila 5, columna 0.
    b[5][0] = 1
    if fits(b, 2, 0, 0, 5):
        print("  ERROR: O se solapa con bloque en columna 0")
        ok = False
    print("  limites (paredes/suelo/solape):", "OK" if ok else "FALLO")
    return ok


if __name__ == "__main__":
    print("Validacion del nucleo de Tetris:")
    results = [check_tables(), test_spawn(), test_clear(), test_wall_and_floor()]
    print("\nRESULTADO:", "TODO OK" if all(results) else "HAY FALLOS")
