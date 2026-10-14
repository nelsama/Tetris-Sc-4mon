/**
 * ============================================================================
 * game.c - TETRIS para el Core de Video (vc) / Monitor 6502
 * ============================================================================
 * El clasico Tetris sobre una rejilla de 10x20 celdas (cada celda = 1 tile de
 * 8x8). Los tetrominos caen; el jugador los mueve y rota para completar lineas
 * horizontales, que desaparecen.
 *
 * TECNICA DE DIBUJO
 *   El pozo (muros, bloques fijados y la pieza activa) son TILES del tilemap.
 *   La pieza activa se dibuja encima del fondo y, al cambiar de celda, se borra
 *   su posicion anterior y se pinta la nueva (2-8 celdas por paso): muy barato
 *   para el 6502. El panel "NEXT" y el HUD tambien son tiles (bloques / fuente).
 *
 * CONTROLES (joystick Atari + teclado UART, en paralelo):
 *   Izquierda/derecha = mover,  Abajo = bajar (soft drop),  Boton = rotar.
 *   Teclado:  a/4 = izquierda,  d/6 = derecha,  s/2 = abajo,  w/8/espacio = rotar,
 *             p = pausa,  q = salir.  En GAME OVER: r = reiniciar, q = salir.
 * ============================================================================
 */

#include <stdint.h>
#include "video.h"
#include "romapi.h"
#include "sound.h"
#include "joy.h"

/* ===========================================================================
 * GEOMETRIA DEL TABLERO
 * ===========================================================================
 * Pozo de 10x20 celdas. Se reserva el HUD en la parte superior (filas 0-3) y el
 * panel lateral derecho (columnas 27-39) para NEXT/puntos/nivel/lineas.
 * El pozo queda centrado a la izquierda: columnas 15..24, filas 5..24.
 * =========================================================================== */
#define BOARD_W      10                  /* celdas de ancho (clasico)         */
#define BOARD_H      20                  /* celdas de alto  (clasico)         */
#define BOARD_X0     15                  /* columna del pozo (borde izq.)     */
#define BOARD_Y0     6                   /* fila del pozo (borde sup.)        */

/* ===========================================================================
 * TILES
 * ===========================================================================
 * La fuente ASCII ocupa $20-$7F: los dibujos propios van fuera de ese rango.
 *   $01..$07 = bloque relleno, uno por tipo de pieza (indice de color = tipo).
 *   $08      = bloque del muro / marco del pozo.
 * El "hueco" del pozo usa el tile transparente (0).
 * =========================================================================== */
#define TILE_BLANK   0x00                /* transparente (fondo)              */
#define TILE_BLOCK   0x01                /* base de los 7 bloques ($01..$07)  */
#define TILE_WALL    0x08                /* muro / marco                      */
#define TILE_FLASH   0x0A                /* bloque solido para el flash de linea */

/* Tiles del FONDO (skyline del Kremlin). Fuera de $20-$7F (fuente). */
#define TILE_SKY         0x09   /* cielo azul opaco (liso)                */

/* Torres del fondo (2 tiles de ancho por "piso"). Izquierda 1..5, derecha 6..10.
 * Los tiles 5 y 10 ("cuerpo") se repiten hacia abajo para dar altura. */
#define TILE_T1  0x12
#define TILE_T2  0x13
#define TILE_T3  0x14
#define TILE_T4  0x15
#define TILE_T5  0x16   /* cuerpo (repetible) */
#define TILE_T6  0x17
#define TILE_T7  0x18
#define TILE_T8  0x19
#define TILE_T9  0x1A
#define TILE_T10 0x1B   /* cuerpo (repetible) */

/* Indices de bloques por tipo de pieza (1..7). El tile = TILE_BLOCK-1 + tipo,
 * es decir $01..$07. El 0 se reserva para "vacio". */
#define BLK_I  1
#define BLK_O  2
#define BLK_T  3
#define BLK_S  4
#define BLK_Z  5
#define BLK_J  6
#define BLK_L  7

/* ===========================================================================
 * PATRONES DE BLOQUE (2 planos, 8x8). color = (plano1 << 1) | plano0   (0..3)
 * ===========================================================================
 * El bisel de un bloque tiene 3 zonas: BORDE (marco exterior), RELLENO (cuerpo)
 * y BRILLO (esquina superior-izquierda). Como el color real lo pone la PALETA,
 * para sacar 7 colores distintos reutilizamos TRES patrones que colocan el
 * RELLENO en un indice de color distinto (1, 2 o 3). Combinados con las 3 paletas
 * de pieza dan 7 colores base diferentes.
 *   patron A: relleno=1, brillo=2, borde=3
 *   patron B: relleno=2, brillo=1, borde=3
 *   patron C: relleno=3, brillo=1, borde=2
 * idx 0 = transparente (no se usa en el bloque).
 * =========================================================================== */

/* Patron A: relleno=1, brillo=2, borde=3 */
static const uint8_t blkA_p0[8] = { 0xFF, 0x9F, 0x9F, 0x9F, 0xFF, 0xFF, 0xFF, 0xFF };
static const uint8_t blkA_p1[8] = { 0xFF, 0xE1, 0xE1, 0xE1, 0x81, 0x81, 0x81, 0xFF };

/* Patron B: relleno=2, brillo=1, borde=3 */
static const uint8_t blkB_p0[8] = { 0xFF, 0xE1, 0xE1, 0xE1, 0x81, 0x81, 0x81, 0xFF };
static const uint8_t blkB_p1[8] = { 0xFF, 0x9F, 0x9F, 0x9F, 0xFF, 0xFF, 0xFF, 0xFF };

/* Patron C: relleno=3, brillo=1, borde=2 */
static const uint8_t blkC_p0[8] = { 0x00, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x00 };
static const uint8_t blkC_p1[8] = { 0xFF, 0x9F, 0x9F, 0x9F, 0xFF, 0xFF, 0xFF, 0xFF };

/* Muro / marco: bloque solido (index 3 = color 3 de la paleta, gris). */
static const uint8_t wall_p0[8] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

/* ---------------------------------------------------------------------------
 * FONDO: skyline del Kremlin (catedral de San Basilio) para decorar el marco.
 * Paleta 3 (cielo/kremlin): idx1 = azul medianoche, idx2 = dorado, idx3 = fondo.
 * --------------------------------------------------------------------------- */

/* Cielo: transparente (idx0) -> se ve BG_COLOR = azul (pal3/idx3). */
static const uint8_t tile_sky_p0[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t tile_sky_p1[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

/* ---------------------------------------------------------------------------
 * TORRES (2 tiles de ancho). Convertidas de los frames Piskel "Torres".
 * Paleta 3: idx1 = azul (se funde con el cielo), idx2 = dorado, idx0 = transp.
 * Layout vertical de una torre (izquierda 1-5 | derecha 6-10):
 *    1| 6
 *    2| 7
 *    3| 8
 *    4| 9
 *    5|10   <- 5 y 10 se repiten hacia abajo (cuerpo)
 * --------------------------------------------------------------------------- */
static const uint8_t torre1_p0[8]  = { 0x00, 0x02, 0x03, 0x01, 0x00, 0x00, 0x00, 0x01 };
static const uint8_t torre1_p1[8]  = { 0x01, 0x03, 0x07, 0x07, 0x03, 0x01, 0x03, 0x07 };
static const uint8_t torre2_p0[8]  = { 0x03, 0x07, 0x00, 0x03, 0x07, 0x07, 0x07, 0x07 };
static const uint8_t torre2_p1[8]  = { 0x0F, 0x1F, 0x1F, 0x0C, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t torre3_p0[8]  = { 0x07, 0x1F, 0x17, 0x14, 0x14, 0x1C, 0x1F, 0x07 };
static const uint8_t torre3_p1[8]  = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18 };
static const uint8_t torre4_p0[8]  = { 0x00, 0x33, 0x4E, 0x44, 0x7F, 0x0F, 0x70, 0x7F };
static const uint8_t torre4_p1[8]  = { 0x1F, 0x0C, 0x00, 0x00, 0x00, 0x70, 0x0F, 0x00 };
static const uint8_t torre5_p0[8]  = { 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F };
static const uint8_t torre5_p1[8]  = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t torre6_p0[8]  = { 0x00, 0x80, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t torre6_p1[8]  = { 0x00, 0x80, 0xC0, 0xC0, 0x80, 0x00, 0x80, 0xC0 };
static const uint8_t torre7_p0[8]  = { 0x80, 0xC0, 0x00, 0x80, 0xC0, 0xC0, 0xC0, 0xC0 };
static const uint8_t torre7_p1[8]  = { 0xE0, 0xF0, 0xF0, 0x60, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t torre8_p0[8]  = { 0xC0, 0xF0, 0xD0, 0x50, 0x50, 0x70, 0xF0, 0xC0 };
static const uint8_t torre8_p1[8]  = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30 };
static const uint8_t torre9_p0[8]  = { 0x00, 0x98, 0xE4, 0x44, 0xFC, 0xE0, 0x1C, 0xFC };
static const uint8_t torre9_p1[8]  = { 0xF0, 0x60, 0x00, 0x00, 0x00, 0x1C, 0xE0, 0x00 };
static const uint8_t torre10_p0[8] = { 0xFC, 0xFC, 0xFC, 0xFC, 0xFC, 0xFC, 0xFC, 0xFC };
static const uint8_t torre10_p1[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

/* ===========================================================================
 * PALETAS
 * ===========================================================================
 * El color de una celda lo determina la PALETA (el patron solo guarda indices
 * 0..3). Usamos 3 paletas de pieza con 3 colores utiles cada una (indices 1..3):
 *   PAL0: 1=cian   2=azul     3=gris (UI)
 *   PAL1: 1=amarillo 2=naranja 3=blanco
 *   PAL2: 1=verde  2=rojo     3=magenta
 * Combinando cada paleta con el patron de bloque (A/B/C, que pone el RELLENO en
 * un indice distinto) obtenemos 7 colores de relleno UNICOS para las 7 piezas.
 * La paleta 3 reserva su color 3 para BG_COLOR (entrada 15) = AZUL del fondo.
 * =========================================================================== */
#define PAL0        VC_BGPAL_0     /* TORRES: marron / naranja / amarillo      */
#define PAL1        VC_BGPAL_1     /* UI + piezas: cian / azul / gris(UI)      */
#define PAL2        VC_BGPAL_2     /* piezas: amarillo / naranja / blanco      */
#define PAL3        VC_BGPAL_3     /* piezas: verde / rojo (idx3 = BG_COLOR azul) */

/* La UI (marco, HUD, textos) usa la paleta 1, cuyo color 3 es gris oscuro. */
#define PAL_UI      PAL1
/* La opcion SELECCIONADA del menu usa pal2 (idx3 = blanco brillante). */
#define PAL_SEL     PAL2

/* Paleta de las torres del fondo. */
#define PAL_TORRE   PAL0

/* Paleta del flash al borrar linea (usa pal2, cuyo idx3 = blanco). */
#define PAL_FLASH   PAL2

/* Seleccion de patron de bloque por tipo de pieza (0=A, 1=B, 2=C). */
#define PAT_A  0
#define PAT_B  1
#define PAT_C  2
static const uint8_t type_pat[8] = {
    PAT_A,   /* 0 = vacio (no usado) */
    PAT_A,   /* I -> pal1 idx1 = cian     */
    PAT_A,   /* O -> pal2 idx1 = amarillo */
    PAT_B,   /* T -> pal2 idx2 = naranja  */
    PAT_A,   /* S -> pal3 idx1 = verde    */
    PAT_C,   /* Z -> pal2 idx3 = blanco   */
    PAT_B,   /* J -> pal1 idx2 = azul     */
    PAT_B    /* L -> pal3 idx2 = rojo     */
};

/* Paleta de fondo por tipo de pieza. */
static const uint8_t type_pal[8] = {
    PAL1,    /* 0 = vacio */
    PAL1,    /* I */
    PAL2,    /* O */
    PAL2,    /* T */
    PAL3,    /* S */
    PAL2,    /* Z */
    PAL1,    /* J */
    PAL3     /* L */
};

/* ===========================================================================
 * PIEZAS (tetrominos)
 * ===========================================================================
 * Cada pieza es una mascara de 4x4 bits (fila 0 = arriba, bit 3 = columna 0).
 * Se guardan las 4 rotaciones. La caja es 4x4 para todas; las piezas de 3x3
 * colocan su contenido en la esquina superior-izquierda y la rotacion tambien
 * es sobre la caja 4x4 (para I y O se ajusta).
 *
 * Codificacion: 4 nibbles, fila 0 en el nibble alto. Se usa uint16_t (16 bits).
 *   bit 15 = (fila 0, col 0) ... bit 0 = (fila 3, col 3)
 * =========================================================================== */

/* Helper en tiempo de compilacion: nibbles de arriba a abajo.
 * El cast a uint16_t va ANTES de los shifts para evitar el overflow de un
 * literal int de 16 bits con signo (p.ej. 0xF << 12). */
#define P(a,b,c,d) ((uint16_t)(((uint16_t)(a) << 12) | ((uint16_t)(b) << 8) | \
                               ((uint16_t)(c) << 4) | (uint16_t)(d)))

/* Cada fila es un nibble: bit3 = columna 0, bit0 = columna 3.
 * Las piezas de 3x3 se colocan en las columnas 0..2 (bits 3..1) de la caja 4x4.
 * La I usa la caja completa (fila/columna 0..3). */
static const uint16_t piece_rot[8][4] = {
    { 0, 0, 0, 0 },                                      /* 0: vacio       */
    /* 1 I */
    { P(0,0xF,0,0), P(0x2,0x2,0x2,0x2), P(0,0,0xF,0), P(0x4,0x4,0x4,0x4) },
    /* 2 O */
    { P(0xC,0xC,0,0), P(0xC,0xC,0,0), P(0xC,0xC,0,0), P(0xC,0xC,0,0) },
    /* 3 T */
    { P(0x4,0xE,0,0), P(0x8,0xC,0x8,0), P(0,0xE,0x4,0), P(0x4,0xC,0x4,0) },
    /* 4 S */
    { P(0x6,0xC,0,0), P(0x8,0xC,0x4,0), P(0,0x6,0xC,0), P(0x4,0xC,0x8,0) },
    /* 5 Z */
    { P(0xC,0x6,0,0), P(0x4,0xC,0x8,0), P(0,0xC,0x6,0), P(0x8,0xC,0x4,0) },
    /* 6 J */
    { P(0x8,0xE,0,0), P(0xC,0x8,0x8,0), P(0,0xE,0x2,0), P(0x4,0x4,0xC,0) },
    /* 7 L */
    { P(0x2,0xE,0,0), P(0x8,0x8,0xC,0), P(0,0xE,0x8,0), P(0xC,0x4,0x4,0) }
};


/* ===========================================================================
 * ESTADO DEL JUEGO
 * =========================================================================== */
static uint8_t  board[BOARD_H * BOARD_W];   /* fila 0 = arriba; 0 = vacio = tipo */

static uint8_t  cur_type;       /* tipo de la pieza activa (1..7)             */
static uint8_t  cur_rot;        /* rotacion actual (0..3)                     */
static int8_t   cur_x;          /* columna de la caja 4x4 dentro del pozo     */
static int8_t   cur_y;          /* fila de la caja 4x4 dentro del pozo        */
static uint8_t  next_type;      /* tipo de la siguiente pieza                 */
static uint8_t  piece_bag[7];   /* bolsa de 7 (reparto equilibrado)           */
static uint8_t  bag_idx;

static uint16_t score;
static uint16_t lines;
static uint8_t  level;
static uint8_t  game_over;
static uint8_t  quit_flag;
static uint8_t  paused;
static uint8_t  dead;           /* ya perdio (pausa en curso)                 */
static uint8_t  difficulty;     /* 0=Facil, 1=Normal, 2=Dificil               */

#define DIFF_COUNT  3
/* Parametros de velocidad por dificultad:
 *   base   = frames por caida en el nivel 1 (mas = mas lento)
 *   step   = cuanto se reduce por nivel
 *   minv   = frames minimos (velocidad tope)
 * F=Facil, N=Normal, D=Dificil. */
static const uint8_t diff_base[DIFF_COUNT] = { 70, 45, 24 };
static const uint8_t diff_step[DIFF_COUNT] = {  2,  3,  4 };
static const uint8_t diff_min [DIFF_COUNT] = { 20, 10,  4 };
/* Nombres para el menu (devueltos por funcion, sin array de punteros). */
static const char *diff_label(uint8_t i) {
    if (i == 0) return "FACIL";
    if (i == 1) return "NORMAL";
    return "DIFICIL";
}

/* Temporizacion (en frames). */
static uint8_t  drop_tick;      /* contador hasta la siguiente caida           */
static uint8_t  lock_delay;     /* frames con la pieza apoyada (para ajustar)  */
static uint8_t  was_grounded;   /* 1 = la pieza estaba apoyada el frame anterior */
static uint8_t  lock_resets;    /* reinicios del lock permitidos (al mover/rotar) */

/* Lock delay: al apoyarse, la pieza espera LOCK_DELAY frames antes de fijarse.
 * Si el jugador MUEVE o ROTA mientras esta apoyada, el contador se reinicia
 * (hasta LOCK_MAX_RESETS veces), para poder deslizarla a un hueco. */
#define LOCK_DELAY       30
#define LOCK_MAX_RESETS  15

/* Mejores puntuaciones de la sesion (top 5), en orden descendente. */
#define HS_COUNT  5
static uint16_t high_scores[HS_COUNT];
static uint16_t high_lines[HS_COUNT];
static uint8_t  hs_last_idx;

static uint16_t rng_state;

/* ===========================================================================
 * ALEATORIO (xorshift de 16 bits)
 * =========================================================================== */
static uint8_t rng_next(void) {
    rng_state ^= (uint16_t)(rng_state << 7);
    rng_state ^= (uint16_t)(rng_state >> 9);
    rng_state ^= (uint16_t)(rng_state << 8);
    return (uint8_t)(rng_state & 0xFF);
}

static uint8_t rng_boot_count;
static void rng_seed(void) {
    uint32_t t = rom_get_micros();
    uint8_t  s0 = snd_random();
    uint8_t  s1 = snd_random();

    rng_boot_count++;
    rng_state  = (uint16_t)((uint16_t)s0 << 8) | s1;
    rng_state ^= (uint16_t)t;
    rng_state ^= (uint16_t)(t >> 16) << 8;
    rng_state ^= (uint16_t)rng_boot_count * 0x9E37u;

    if (rng_state == 0) rng_state = 0xA5A5;
}

/* Reparto de piezas por "bolsa de 7": se barajan los 7 tipos y se sacan en
 * orden; asi no hay rachas largas sin la pieza que necesitas. */
static void bag_refill(void) {
    uint8_t i;
    for (i = 0; i < 7; i++) piece_bag[i] = (uint8_t)(i + 1);
    /* Fisher-Yates con el RNG. */
    for (i = 7; i > 1; i--) {
        uint8_t j = (uint8_t)(rng_next() % i);
        uint8_t tmp = piece_bag[i - 1];
        piece_bag[i - 1] = piece_bag[j];
        piece_bag[j] = tmp;
    }
    bag_idx = 0;
}

static uint8_t bag_next(void) {
    if (bag_idx >= 7) bag_refill();
    return piece_bag[bag_idx++];
}

/* ===========================================================================
 * TEXTO (fuente del sistema: tile = ASCII, tinta = color 3 de la paleta)
 * =========================================================================== */
static void put_str_pal(uint8_t col, uint8_t row, const char *s, uint8_t pal) {
    while (*s && col < VC_SCREEN_COLS) {
        vc_put_cell(col, row, (uint8_t)*s);
        vc_set_cell_attr(col, row, pal, 0);
        col++;
        s++;
    }
}

static void put_str_at(uint8_t col, uint8_t row, const char *s) {
    put_str_pal(col, row, s, PAL_UI);
}

static void clear_str_at(uint8_t col, uint8_t row, uint8_t len) {
    while (len-- && col < VC_SCREEN_COLS) {
        vc_put_cell(col, row, VC_CHAR_SPACE);
        vc_set_cell_attr(col, row, PAL_UI, 0);
        col++;
    }
}

static void put_u16_at(uint8_t col, uint8_t row, uint16_t v, uint8_t digits) {
    char buf[6];
    uint8_t i;
    for (i = 0; i < digits; i++) {
        buf[digits - 1 - i] = (char)('0' + (v % 10));
        v = (uint16_t)(v / 10);
    }
    buf[digits] = 0;
    put_str_at(col, row, buf);
}

static void put_u16_pal(uint8_t col, uint8_t row, uint16_t v, uint8_t digits,
                        uint8_t pal) {
    char buf[6];
    uint8_t i;
    for (i = 0; i < digits; i++) {
        buf[digits - 1 - i] = (char)('0' + (v % 10));
        v = (uint16_t)(v / 10);
    }
    buf[digits] = 0;
    put_str_pal(col, row, buf, pal);
}

/* ---------------------------------------------------------------------------
 * ACCESO AL TABLERO
 * ------------------------------------------------------------------------- */
#define CELL(x, y)  ((uint8_t)((y) * BOARD_W + (x)))

/* Celda vacia del pozo: transparente -> BG_COLOR (azul), igual que el cielo. */
#define WELL_TILE   TILE_BLANK
#define WELL_PAL    PAL_UI

/* Pinta una celda del pozo con un tile y paleta (sin flags). */
static void paint(uint8_t col, uint8_t row, uint8_t tile, uint8_t pal) {
    vc_put_cell(col, row, tile);
    vc_set_cell_attr(col, row, pal, 0);
}

/* Dibuja una celda del tablero con el color de su tipo (o vacia opaca). */
static void draw_board_cell(uint8_t bx, uint8_t by) {
    uint8_t t = board[CELL(bx, by)];
    uint8_t tile = (uint8_t)(t ? (TILE_BLOCK - 1 + t) : WELL_TILE);
    uint8_t pal  = (uint8_t)(t ? type_pal[t] : WELL_PAL);
    paint((uint8_t)(BOARD_X0 + bx), (uint8_t)(BOARD_Y0 + by), tile, pal);
}

/* ===========================================================================
 * PIEZA ACTIVA
 * ===========================================================================
 * Comprueba si la pieza (tipo, rot) colocada en la caja en (px,py) cabe en el
 * tablero (sin solaparse con bloques fijados ni salirse por abajo/lados).
 * =========================================================================== */
static uint8_t piece_fits(uint8_t type, uint8_t rot, int8_t px, int8_t py) {
    uint16_t mask = piece_rot[type][rot];
    uint8_t r, c;
    for (r = 0; r < 4; r++) {
        uint8_t rowbits = (uint8_t)((mask >> (12 - r * 4)) & 0x0F);
        if (rowbits == 0) continue;
        for (c = 0; c < 4; c++) {
            if (rowbits & (0x08 >> c)) {
                int8_t bx = (int8_t)(px + c);
                int8_t by = (int8_t)(py + r);
                if (bx < 0 || bx >= BOARD_W) return 0;
                if (by >= BOARD_H) return 0;
                if (by < 0) continue;   /* por encima del techo: permitido */
                if (board[CELL((uint8_t)bx, (uint8_t)by)] != 0) return 0;
            }
        }
    }
    return 1;
}

/* Dibuja (o borra con tile vacio) la pieza activa en la posicion (px,py).
 * erase = 1 -> borra (pinta vacio); 0 -> pinta los bloques. */
static void piece_paint(uint8_t type, uint8_t rot, int8_t px, int8_t py,
                        uint8_t erase) {
    uint16_t mask = piece_rot[type][rot];
    uint8_t r, c;
    uint8_t tile = erase ? WELL_TILE : (uint8_t)(TILE_BLOCK - 1 + type);
    uint8_t pal  = erase ? WELL_PAL : type_pal[type];

    for (r = 0; r < 4; r++) {
        uint8_t rowbits = (uint8_t)((mask >> (12 - r * 4)) & 0x0F);
        if (rowbits == 0) continue;
        for (c = 0; c < 4; c++) {
            if (rowbits & (0x08 >> c)) {
                int8_t bx = (int8_t)(px + c);
                int8_t by = (int8_t)(py + r);
                if (bx < 0 || bx >= BOARD_W || by < 0 || by >= BOARD_H) continue;
                paint((uint8_t)(BOARD_X0 + bx), (uint8_t)(BOARD_Y0 + by),
                      tile, pal);
            }
        }
    }
}

/* ===========================================================================
 * SPAWN DE PIEZA
 * =========================================================================== */
/* Coloca la pieza 'type' arriba-centrada. Devuelve 0 si NO cabe (game over). */
static uint8_t spawn_piece(uint8_t type) {
    cur_type = type;
    cur_rot  = 0;
    cur_x    = (int8_t)(BOARD_W / 2 - 2);   /* caja 4x4 centrada */
    cur_y    = -1;                          /* empieza justo encima del techo */

    /* Busca la primera fila donde quepa (permite spawn parcial arriba). */
    while (cur_y < 2) {
        if (piece_fits(cur_type, cur_rot, cur_x, cur_y)) return 1;
        cur_y++;
    }
    return 0;
}

/* Nueva pieza a partir de la bolsa; actualiza la cola NEXT. */
static uint8_t piece_new(void) {
    uint8_t t = next_type;
    next_type = bag_next();
    return spawn_piece(t);
}

/* ===========================================================================
 * FIJAR PIEZA / LINEAS
 * =========================================================================== */
/* Escribe la pieza activa en el tablero y redibuja sus celdas. */
static void lock_piece(void) {
    uint16_t mask = piece_rot[cur_type][cur_rot];
    uint8_t r, c;
    for (r = 0; r < 4; r++) {
        uint8_t rowbits = (uint8_t)((mask >> (12 - r * 4)) & 0x0F);
        if (rowbits == 0) continue;
        for (c = 0; c < 4; c++) {
            if (rowbits & (0x08 >> c)) {
                int8_t bx = (int8_t)(cur_x + c);
                int8_t by = (int8_t)(cur_y + r);
                if (bx < 0 || bx >= BOARD_W || by < 0 || by >= BOARD_H) continue;
                board[CELL((uint8_t)bx, (uint8_t)by)] = cur_type;
                draw_board_cell((uint8_t)bx, (uint8_t)by);
            }
        }
    }
}

/* Comprueba si la fila by esta completa. */
static uint8_t row_full(uint8_t by) {
    uint8_t x;
    for (x = 0; x < BOARD_W; x++) {
        if (board[CELL(x, by)] == 0) return 0;
    }
    return 1;
}

/* Elimina las filas completas, colapsa hacia abajo y redibuja. Devuelve el
 * numero de lineas eliminadas. */
static uint8_t clear_lines(void) {
    uint8_t cleared = 0;
    int8_t y;

    for (y = (int8_t)(BOARD_H - 1); y >= 0; y--) {
        if (row_full((uint8_t)y)) {
            int8_t yy;
            cleared++;
            /* Colapsa: mueve todas las filas de arriba una posicion abajo. */
            for (yy = y; yy > 0; yy--) {
                uint8_t x;
                for (x = 0; x < BOARD_W; x++) {
                    board[CELL(x, (uint8_t)yy)] = board[CELL(x, (uint8_t)(yy - 1))];
                }
            }
            /* La fila 0 queda vacia. */
            {
                uint8_t x;
                for (x = 0; x < BOARD_W; x++) board[CELL(x, 0)] = 0;
            }
            /* Repite la misma fila y (ahora contiene lo de arriba). */
            y++;
        }
    }

    if (cleared) {
        /* Redibuja todo el pozo (las lineas pueden afectar a muchas filas). */
        uint8_t by;
        for (by = 0; by < BOARD_H; by++) {
            uint8_t x;
            for (x = 0; x < BOARD_W; x++) draw_board_cell(x, by);
        }
    }
    return cleared;
}

/* ===========================================================================
 * ANIMACION DE LINEAS
 * ===========================================================================
 * Antes de colapsar, las filas completas PARPADEAN unos frames (estilo arcade).
 * Se dibuja la fila completa en blanco (paleta UI) y se alterna con su color
 * normal. Usa su propio VBLANK (bloquea el juego ~12 frames, como los arcade).
 * =========================================================================== */

/* Marca las filas completas en un bitmask (bit y = fila y completa). */
static uint16_t full_mask;

/* Pinta TODA la fila by de blanco solido (flash al borrar linea). */
static void fill_row(uint8_t by, uint8_t pal) {
    uint8_t x;
    for (x = 0; x < BOARD_W; x++) {
        paint((uint8_t)(BOARD_X0 + x), (uint8_t)(BOARD_Y0 + by), TILE_FLASH, pal);
    }
}

/* Redibuja la fila by con los colores reales del tablero. */
static void redraw_row(uint8_t by) {
    uint8_t x;
    for (x = 0; x < BOARD_W; x++) draw_board_cell(x, by);
}

/* Marca las filas completas en full_mask y devuelve cuantas hay. */
static uint8_t mark_full_rows(void) {
    uint8_t n = 0, y;
    full_mask = 0;
    for (y = 0; y < BOARD_H; y++) {
        if (row_full(y)) { full_mask |= (uint16_t)(1u << y); n++; }
    }
    return n;
}

/* Animacion de parpadeo de las filas marcadas en full_mask (3 destellos). */
static void flash_lines(void) {
    uint8_t f, y;

    for (f = 0; f < 6; f++) {          /* 6 medias-fases = 3 destellos */
        vc_wait_vblank();
        for (y = 0; y < BOARD_H; y++) {
            if (!(full_mask & (uint16_t)(1u << y))) continue;
            if (f & 1) redraw_row(y);          /* color real */
            else       fill_row(y, PAL_FLASH); /* flash blanco */
        }
        vc_wait_vblank_end();
    }
}

/* Skyline de torres. Cada torre es de 2 tiles de ancho (col izq = pisos 1-5,
 * col der = pisos 6-10) y se SEPARAN entre si con columnas de cielo, para que se
 * vean como pilares independientes sobre el fondo azul.
 * Altura EN TILES (5 = solo el remate; max 10) de cada torre. */
static const uint8_t towerL[4] = { 10, 6, 9, 7 };
static const uint8_t towerR[4] = { 7, 9, 6, 10 };
/* Columna (izquierda) de cada torre, dejando huecos de cielo entre ellas. */
static const uint8_t towerL_col[4] = { 0, 4, 8, 12 };
static const uint8_t towerR_col[4] = { 26, 30, 34, 38 };

#define TOWER_MAX_H 10   /* altura maxima de una torre (tiles) */
#define FLOOR_ROW   29   /* fila del suelo (ultima de la pantalla) */

/* Dibuja una torre de 2 tiles de ancho, de 'alto' tiles (5..10). La columna
 * 'col' usa los pisos 1-5 y 'col+1' los pisos 6-10; bajo el piso 5 repite la base. */
static void draw_tower(uint8_t col, uint8_t alto) {
    static const uint8_t pisoL[5] = { TILE_T1, TILE_T2, TILE_T3, TILE_T4, TILE_T5 };
    static const uint8_t pisoR[5] = { TILE_T6, TILE_T7, TILE_T8, TILE_T9, TILE_T10 };
    uint8_t i, y, top;

    if (alto > TOWER_MAX_H) alto = TOWER_MAX_H;
    if (alto < 5) alto = 5;                 /* el remate ocupa 5 tiles        */
    top = (uint8_t)(FLOOR_ROW + 1 - alto);   /* fila superior del remate      */

    for (i = 0; i < 5; i++) {
        paint(col,        (uint8_t)(top + i), pisoL[i], PAL_TORRE);
        paint((uint8_t)(col + 1), (uint8_t)(top + i), pisoR[i], PAL_TORRE);
    }
    for (y = (uint8_t)(top + 5); y <= FLOOR_ROW; y++) {
        paint(col,                y, TILE_T5,  PAL_TORRE);
        paint((uint8_t)(col + 1), y, TILE_T10, PAL_TORRE);
    }
}

static void draw_background(void) {
    uint8_t x, y, t;

    /* 1) Cielo: transparente (se ve BG_COLOR azul). */
    for (y = 0; y < VC_SCREEN_ROWS; y++) {
        for (x = 0; x < VC_SCREEN_COLS; x++) {
            paint(x, y, TILE_BLANK, PAL_UI);
        }
    }

    /* 2) Skyline: 4 torres por lado, separadas por cielo. */
    for (t = 0; t < 4; t++) {
        draw_tower(towerL_col[t], towerL[t]);
        draw_tower(towerR_col[t], towerR[t]);
    }
}

/* ===========================================================================
 * DIBUJO DEL ENTORNO
 * =========================================================================== */
/* Muro / marco del pozo: borde izquierdo, derecho, suelo y techo. */
static void draw_frame(void) {
    uint8_t i;

    /* Columnas del borde (izquierda y derecha), una fila por encima del techo
     * y hasta el suelo. */
    for (i = 0; i <= BOARD_H; i++) {
        paint((uint8_t)(BOARD_X0 - 1), (uint8_t)(BOARD_Y0 + i), TILE_WALL, PAL_UI);
        paint((uint8_t)(BOARD_X0 + BOARD_W), (uint8_t)(BOARD_Y0 + i), TILE_WALL, PAL_UI);
    }
    /* Techo y suelo horizontales. */
    for (i = 0; i < BOARD_W; i++) {
        paint((uint8_t)(BOARD_X0 + i), (uint8_t)(BOARD_Y0 - 1), TILE_WALL, PAL_UI);
        paint((uint8_t)(BOARD_X0 + i), (uint8_t)(BOARD_Y0 + BOARD_H), TILE_WALL, PAL_UI);
    }
}

/* Limpia el interior del pozo (celdas de juego). */
static void clear_well(void) {
    uint8_t x, y;
    for (y = 0; y < BOARD_H; y++) {
        for (x = 0; x < BOARD_W; x++) {
            board[CELL(x, y)] = 0;
            paint((uint8_t)(BOARD_X0 + x), (uint8_t)(BOARD_Y0 + y), WELL_TILE, WELL_PAL);
        }
    }
}

/* Dibuja el panel NEXT: caja de 4x4 celdas con la pieza 'type' centrada. */
static void draw_next(uint8_t type) {
    uint8_t x, y;
    const uint8_t nx = 29;   /* columna del panel NEXT */
    const uint8_t ny = 6;    /* fila del panel NEXT    */

    /* Limpia la caja 4x4. */
    for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++)
            paint((uint8_t)(nx + x), (uint8_t)(ny + y), TILE_BLANK, PAL_UI);

    /* Pinta la pieza con su rotacion 0 en la caja (sin offset de tablero). */
    {
        uint16_t mask = piece_rot[type][0];
        uint8_t r, c;
        uint8_t tile = (uint8_t)(TILE_BLOCK - 1 + type);
        uint8_t pal  = type_pal[type];
        for (r = 0; r < 4; r++) {
            uint8_t rowbits = (uint8_t)((mask >> (12 - r * 4)) & 0x0F);
            if (rowbits == 0) continue;
            for (c = 0; c < 4; c++) {
                if (rowbits & (0x08 >> c)) {
                    paint((uint8_t)(nx + c), (uint8_t)(ny + r), tile, pal);
                }
            }
        }
    }
}

/* HUD de la derecha: etiqueta en col 27, valores alineados a la derecha.
 * Se mantiene dentro de las columnas seguras (27..38) y filas 1..28. */
static void hud_draw(void) {
    /* Titulo arriba. */
    put_str_pal(15, 1, "T E T R I S", PAL_UI);

    /* Panel NEXT (bajado para no pegarlo a las instrucciones). */
    put_str_at(29, 5, "NEXT");
    draw_next(next_type);

    put_str_at(29, 12, "PTS");
    put_u16_at(33, 12, score, 5);

    put_str_at(29, 14, "LINEAS");
    put_u16_at(36, 14, lines, 3);

    put_str_at(29, 16, "NIVEL");
    put_u16_at(36, 16, level, 2);

    put_str_at(29, 18, "MEJOR");
    put_u16_at(34, 18, high_scores[0], 5);

    put_str_at(0, 2, "WASD:mover BOTON/W:rotar P:pausa Q:salir");
}

/* Refresca solo los numeros del HUD. */
static void hud_update(void) {
    uint8_t rec = (uint8_t)(score > high_scores[0]);

    put_u16_at(33, 12, score, 5);
    put_u16_at(36, 14, lines, 3);
    put_u16_at(36, 16, level, 2);
    put_u16_pal(34, 18, (rec ? score : high_scores[0]), 5,
                rec ? PAL1 : PAL_UI);
}

/* ===========================================================================
 * HIGHSCORES (top 5 en RAM)
 * =========================================================================== */
static void hs_reset(void) {
    uint8_t i;
    for (i = 0; i < HS_COUNT; i++) {
        high_scores[i] = 0;
        high_lines[i]  = 0;
    }
    hs_last_idx = 0xFF;
}

static void hs_insert(uint16_t v, uint16_t ln) {
    uint8_t i, pos;

    hs_last_idx = 0xFF;
    if (v == 0) return;

    for (pos = 0; pos < HS_COUNT; pos++) {
        if (v > high_scores[pos]) break;
    }
    if (pos >= HS_COUNT) return;

    for (i = (uint8_t)(HS_COUNT - 1); i > pos; i--) {
        high_scores[i] = high_scores[i - 1];
        high_lines[i]  = high_lines[i - 1];
    }
    high_scores[pos] = v;
    high_lines[pos]  = ln;
    hs_last_idx = pos;
}

static void hs_draw(uint8_t col, uint8_t row) {
    uint8_t i;

    put_str_pal(col, row, "MEJORES:", PAL_UI);
    put_str_pal((uint8_t)(col + 3), (uint8_t)(row + 1), "PTS", PAL_UI);
    put_str_pal((uint8_t)(col + 9), (uint8_t)(row + 1), "LIN", PAL_UI);
    for (i = 0; i < HS_COUNT; i++) {
        uint8_t r = (uint8_t)(row + 2 + i);
        uint8_t pal = (i == hs_last_idx) ? PAL1 : PAL_UI;

        char buf[3];
        buf[0] = (char)('1' + i);
        buf[1] = '.';
        buf[2] = 0;
        put_str_pal(col, r, buf, pal);
        put_u16_pal((uint8_t)(col + 3), r, high_scores[i], 5, pal);
        put_u16_pal((uint8_t)(col + 9), r, high_lines[i], 3, pal);
    }
}

/* ===========================================================================
 * ENTRADA
 * ===========================================================================
 * Los movimientos horizontales y el giro son DISCRETOS (una accion por pulsacion)
 * con auto-repeticion: tras mantener la direccion un tiempo de retardo (DAS), se
 * repite cada pocos frames. El descenso (abajo) es continuo. El teclado UART
 * funciona en paralelo al joystick.
 * =========================================================================== */
#define DAS_DELAY   12   /* frames de retardo antes de auto-repetir la direccion */
#define DAS_REPEAT   4   /* frames entre repeticiones una vez arrancada        */

/* Teclas que no distinguen mayus/minus (flechas ANSI del terminal). */
#define KEY_UP    0x10
#define KEY_DOWN  0x11
#define KEY_LEFT  0x12
#define KEY_RIGHT 0x13

static uint8_t prev_joy;    /* estado previo del joystick (para el flanco)    */
static uint8_t pause_shown; /* 1 = el aviso PAUSA esta dibujado en pantalla   */

/* Estado del auto-repeat de la direccion horizontal. */
static uint8_t das_dir;     /* 0 = ninguna, 1 = izquierda, 2 = derecha        */
static uint8_t das_timer;

/* Acciones pendientes que el game loop aplica en el tick. */
static uint8_t act_left, act_right, act_rot, act_drop;

static void read_input(void) {
    char c;
    uint8_t j = joy_read();
    uint8_t held = 0;   /* 1 = hay direccion horizontal mantenida          */

    /* --- Joystick --- */
    {
        uint8_t jp = (uint8_t)(j & (uint8_t)~prev_joy);   /* nuevo = pulsado ahora */

        /* Flanco: primer movimiento inmediato. */
        if (jp & JOY_A_LEFT)  { act_left = 1;  das_dir = 1; das_timer = DAS_DELAY; }
        if (jp & JOY_A_RIGHT) { act_right = 1; das_dir = 2; das_timer = DAS_DELAY; }
        if (jp & JOY_A_FIRE)  act_rot = 1;
        if (j & JOY_A_DOWN)   act_drop = 1;               /* continuo */

        /* Direccion mantenida -> auto-repeat. */
        if (j & (JOY_A_LEFT | JOY_A_RIGHT)) held = 1;
        prev_joy = j;
    }

    /* --- Teclado UART --- */
    while (rom_uart_rx_ready()) {
        c = rom_uart_getc();
        switch (c) {
            case 'a': case 'A': case '4': case KEY_LEFT:
                act_left = 1; das_dir = 1; das_timer = DAS_DELAY; held = 1; break;
            case 'd': case 'D': case '6': case KEY_RIGHT:
                act_right = 1; das_dir = 2; das_timer = DAS_DELAY; held = 1; break;
            case 's': case 'S': case '2': case KEY_DOWN: act_drop = 1;  break;
            case 'w': case 'W': case '8': case ' ': case KEY_UP: act_rot = 1; break;
            case 'p': case 'P': paused ^= 1;             break;
            case 'q': case 'Q': quit_flag = 1;           break;
            default: break;
        }
    }

    /* Auto-repeat de la direccion mantenida. */
    if (held && das_dir) {
        if (das_timer) das_timer--;
        else {
            if (das_dir == 1) act_left = 1; else act_right = 1;
            das_timer = DAS_REPEAT;
        }
    } else {
        das_dir = 0;
        das_timer = 0;
    }
}

/* ===========================================================================
 * SETUP DE VIDEO
 * =========================================================================== */
static void setup_video(void) {
    vc_wait_ready();
    vc_wait_vblank();
    vc_clear_vram();

    /* Paleta 0 (TORRES): 1=marron claro, 2=naranja claro, 3=amarillo. */
    vc_pal_set_bg(PAL0, 1, VC_RGB444(0xA, 0x5, 0x0));     /* marron claro  */
    vc_pal_set_bg(PAL0, 2, VC_RGB444(0xF, 0x8, 0x2));     /* naranja claro */
    vc_pal_set_bg(PAL0, 3, VC_RGB444(0xF, 0xC, 0x1));     /* amarillo      */

    /* Paleta 1 (UI + piezas): 1=cian, 2=azul, 3=gris (UI). */
    vc_pal_set_bg(PAL1, 1, VC_RGB444(0x0, 0xA, 0xF));     /* cian       */
    vc_pal_set_bg(PAL1, 2, VC_RGB444(0x0, 0x0, 0xA));     /* azul       */
    vc_pal_set_bg(PAL1, 3, VC_RGB444(0x8, 0x8, 0x8));     /* gris (UI)  */

    /* Paleta 2 (piezas): 1=amarillo, 2=naranja, 3=blanco. */
    vc_pal_set_bg(PAL2, 1, VC_RGB444(0xF, 0xE, 0x0));     /* amarillo   */
    vc_pal_set_bg(PAL2, 2, VC_RGB444(0xF, 0x8, 0x0));     /* naranja    */
    vc_pal_set_bg(PAL2, 3, VC_RGB444(0xF, 0xF, 0xF));     /* blanco     */

    /* Paleta 3 (piezas + FONDO): 1=verde, 2=rojo. Su idx3 = BG_COLOR: lo ponemos
     * a AZUL, que es el FONDO de la pantalla (el cielo es un tile transparente). */
    vc_pal_set_bg(PAL3, 1, VC_RGB444(0x0, 0xD, 0x2));     /* verde      */
    vc_pal_set_bg(PAL3, 2, VC_RGB444(0xE, 0x0, 0x0));     /* rojo       */
    vc_pal_set_bg(PAL3, 3, VC_RGB444(0x0, 0x0, 0xC));     /* AZUL = BG_COLOR */

    /* Patrones: cada tipo de pieza usa uno de los 3 patrones de bloque (A/B/C),
     * que colocan el RELLENO en un indice distinto. Se cargan en los 7 tiles de
     * pieza ($01..$07) segun type_pat. El color final lo da la PALETA (type_pal). */
    {
        uint8_t t;
        for (t = 1; t <= 7; t++) {
            uint8_t p = type_pat[t];
            const uint8_t *p0 = (p == PAT_A) ? blkA_p0 : (p == PAT_B) ? blkB_p0 : blkC_p0;
            const uint8_t *p1 = (p == PAT_A) ? blkA_p1 : (p == PAT_B) ? blkB_p1 : blkC_p1;
            vc_load_bg_pattern((uint8_t)(TILE_BLOCK - 1 + t), p0, p1);
        }
    }
    vc_load_bg_pattern(TILE_WALL, wall_p0, wall_p0);

    /* Tile solido para el flash de linea (todo idx3). */
    vc_load_bg_pattern(TILE_FLASH, wall_p0, wall_p0);

    /* Patrones del fondo: cielo + torres. */
    vc_load_bg_pattern(TILE_SKY, tile_sky_p0, tile_sky_p1);

    /* Torres del fondo (izquierda 1-5, derecha 6-10). */
    vc_load_bg_pattern(TILE_T1,  torre1_p0,  torre1_p1);
    vc_load_bg_pattern(TILE_T2,  torre2_p0,  torre2_p1);
    vc_load_bg_pattern(TILE_T3,  torre3_p0,  torre3_p1);
    vc_load_bg_pattern(TILE_T4,  torre4_p0,  torre4_p1);
    vc_load_bg_pattern(TILE_T5,  torre5_p0,  torre5_p1);
    vc_load_bg_pattern(TILE_T6,  torre6_p0,  torre6_p1);
    vc_load_bg_pattern(TILE_T7,  torre7_p0,  torre7_p1);
    vc_load_bg_pattern(TILE_T8,  torre8_p0,  torre8_p1);
    vc_load_bg_pattern(TILE_T9,  torre9_p0,  torre9_p1);
    vc_load_bg_pattern(TILE_T10, torre10_p0, torre10_p1);

    /* Asegura un patron transparente en el tile 0. */
    {
        static const uint8_t zero8[8] = { 0,0,0,0,0,0,0,0 };
        vc_load_bg_pattern(TILE_BLANK, zero8, zero8);
    }

    /* Raster fijo: sin bandas. */
    vc_set_raster(0xFF, 0xFF);
    vc_set_band2_scroll(0, 0);
    vc_set_band3_scroll(0, 0);
}

/* ===========================================================================
 * RESET / INICIO DE PARTIDA
 * =========================================================================== */
static void game_reset(void) {
    score = 0;
    lines = 0;
    level = 1;
    game_over = 0;
    dead = 0;
    paused = 0;
    hs_last_idx = 0xFF;

    draw_background();
    clear_well();
    draw_frame();

    bag_refill();
    next_type = bag_next();

    /* Primera pieza. Si no cabe (pozo vacio siempre cabe), game over. */
    if (!piece_new()) game_over = 1;

    drop_tick  = 0;
    lock_delay = 0;
    was_grounded = 0;
    lock_resets = 0;

    hud_draw();
}

/* Frames entre caidas segun el nivel (menos = mas rapido). */
static uint8_t gravity_frames(void) {
    /* Base, aceleracion y minimo segun dificultad. */
    int16_t base = (int16_t)diff_base[difficulty];
    int16_t step = (int16_t)diff_step[difficulty];
    int16_t minv = (int16_t)diff_min[difficulty];
    int16_t f = (int16_t)(base - (int16_t)(level - 1) * step);
    if (f < minv) f = minv;
    return (uint8_t)f;
}

/* ===========================================================================
 * APLICAR UNA ACCION (mover/rotar/bajar)
 * =========================================================================== */
/* Intenta mover la pieza en dx (columna). Devuelve 1 si se movio. */
static uint8_t try_move(int8_t dx) {
    if (piece_fits(cur_type, cur_rot, (int8_t)(cur_x + dx), cur_y)) {
        piece_paint(cur_type, cur_rot, cur_x, cur_y, 1);   /* borra */
        cur_x = (int8_t)(cur_x + dx);
        piece_paint(cur_type, cur_rot, cur_x, cur_y, 0);   /* pinta */
        return 1;
    }
    return 0;
}

/* Intenta rotar. Si no cabe, prueba wall-kick simple (+-1 columna). */
static uint8_t try_rotate(void) {
    uint8_t nr = (uint8_t)((cur_rot + 1) & 3);
    int8_t k;

    if (piece_fits(cur_type, nr, cur_x, cur_y)) {
        piece_paint(cur_type, cur_rot, cur_x, cur_y, 1);
        cur_rot = nr;
        piece_paint(cur_type, cur_rot, cur_x, cur_y, 0);
        return 1;
    }
    /* Wall kick: prueba desplazar 1 a la derecha y 1 a la izquierda. */
    for (k = 1; k >= -1; k -= 2) {
        if (piece_fits(cur_type, nr, (int8_t)(cur_x + k), cur_y)) {
            piece_paint(cur_type, cur_rot, cur_x, cur_y, 1);
            cur_rot = nr;
            cur_x = (int8_t)(cur_x + k);
            piece_paint(cur_type, cur_rot, cur_x, cur_y, 0);
            return 1;
        }
    }
    return 0;
}

/* Baja la pieza una celda. Devuelve 1 si se movio, 0 si ya esta apoyada. */
static uint8_t try_down(void) {
    if (piece_fits(cur_type, cur_rot, cur_x, (int8_t)(cur_y + 1))) {
        piece_paint(cur_type, cur_rot, cur_x, cur_y, 1);
        cur_y = (int8_t)(cur_y + 1);
        piece_paint(cur_type, cur_rot, cur_x, cur_y, 0);
        return 1;
    }
    return 0;
}

/* Puntua las lineas borradas (x nivel) y actualiza nivel. */
static void score_lines(uint8_t n) {
    static const uint16_t base[5] = { 0, 40, 100, 300, 1200 };
    uint16_t add;

    if (n == 0) return;
    if (n > 4) n = 4;
    add = (uint16_t)(base[n] * level);
    score = (uint16_t)(score + add);
    lines = (uint16_t)(lines + n);

    /* Sube de nivel cada 10 lineas. */
    {
        uint8_t nl = (uint8_t)(1 + (lines / 10));
        if (nl > level) {
            level = nl;
            snd_level();
        }
    }
}

/* Coloca la pieza (lock), borra lineas, actualiza HUD y saca la siguiente. */
static void lock_and_next(void) {
    uint8_t n;

    lock_piece();

    /* Si hay lineas completas: parpadean y luego colapsan. */
    if (mark_full_rows()) {
        snd_line();
        flash_lines();
    }
    n = clear_lines();
    if (n) score_lines(n);
    hud_update();

    /* Saca la pieza nueva de la cola (piece_new actualiza next_type) y luego
     * dibuja el panel NEXT con el NUEVO next_type. */
    if (!piece_new()) {
        game_over = 1;
        dead = 1;
        snd_over();
    }
    draw_next(next_type);
    drop_tick   = 0;
    lock_delay  = 0;
    lock_resets = 0;
}

/* ===========================================================================
 * PARTIDA
 * ===========================================================================
 * Juega una partida completa. Devuelve 1 si perdio (Game Over), 0 si salio 'q'.
 * =========================================================================== */

/* Reinicia el lock delay si aun quedan reseteos (al mover/rotar apoyada). */
static void reset_lock(void) {
    if (lock_resets < LOCK_MAX_RESETS) {
        lock_delay = 0;
        lock_resets++;
    }
}

static uint8_t play_game(void) {
    quit_flag = 0;
    act_left = act_right = act_rot = act_drop = 0;
    prev_joy = 0;
    das_dir = das_timer = 0;
    pause_shown = 0;

    snd_init();
    rng_seed();

    /* El dibujo del fondo entero (cielo+skyline+pozo) es una operacion masiva
     * de VRAM: se hace FUERA del VBLANK (pantalla de transicion estatica), y el
     * HUD/pieza se refrescan ya dentro. */
    game_reset();

    vc_wait_vblank();
    piece_paint(cur_type, cur_rot, cur_x, cur_y, 0);
    vc_wait_vblank_end();

    while (!quit_flag && !game_over) {
        uint8_t grounded;

        vc_wait_vblank();

        /* Siempre leemos la entrada (para poder despausar o salir); las
         * acciones de juego se ignoran mientras esta en pausa. */
        read_input();

        if (paused) {
            /* Aviso de pausa (se dibuja solo una vez por frame). */
            if (!pause_shown) {
                put_str_pal(17, 2, "PAUSA", PAL2);
                pause_shown = 1;
            }
            /* Descarta acciones de juego acumuladas. */
            act_left = act_right = act_rot = act_drop = 0;
            drop_tick = 0;
            snd_update();
            vc_wait_vblank_end();
            continue;
        }
        if (pause_shown) {
            clear_str_at(17, 2, 5);
            pause_shown = 0;
        }

        /* La pieza esta apoyada si no puede bajar (se calcula ANTES de procesar
         * el input, para que el movimiento sepa si reiniciar el lock delay). */
        grounded = (uint8_t)(!piece_fits(cur_type, cur_rot, cur_x,
                                         (int8_t)(cur_y + 1)));

        /* Movimiento horizontal (discreto: un movimiento por accion).
         * Si la pieza esta apoyada, mover REINICIA el lock delay (permite
         * deslizarla a un hueco antes de que se fije). */
        if (act_left)  { if (try_move(-1)) { snd_move(); if (grounded) reset_lock(); } act_left = 0; }
        if (act_right) { if (try_move(1))  { snd_move(); if (grounded) reset_lock(); } act_right = 0; }

        if (act_rot) {
            if (try_rotate()) { snd_move(); if (grounded) reset_lock(); }
            act_rot = 0;
        }

        /* Recalcula si sigue apoyada tras el movimiento. */
        grounded = (uint8_t)(!piece_fits(cur_type, cur_rot, cur_x,
                                         (int8_t)(cur_y + 1)));

        /* Golpe de aterrizaje: suena EN CUANTO la pieza toca el suelo (flanco
         * 0->1), no al fijarla. Asi el sonido coincide con lo que se ve. */
        if (grounded && !was_grounded) snd_drop();
        was_grounded = grounded;

        if (act_drop) {
            /* Soft drop: baja mas rapido mientras se mantiene. */
            if (try_down()) {
                drop_tick = 0;
                score = (uint16_t)(score + 1);   /* punto por celda bajada */
            }
            act_drop = 0;
        }

        if (grounded) {
            /* Pieza apoyada: espera LOCK_DELAY frames antes de fijarla. Mover o
             * rotar reinicia el contador (reset_lock), para poder ajustarla. */
            lock_delay++;
        } else {
            lock_delay = 0;
            lock_resets = 0;        /* al despegar, se recargan los reseteos */
            /* Gravedad. */
            drop_tick++;
            if (drop_tick >= gravity_frames()) {
                drop_tick = 0;
                try_down();
            }
        }

        hud_update();
        snd_update();
        vc_wait_vblank_end();

        /* El lock (con su animacion de lineas) se hace FUERA del VBLANK, porque
         * la animacion usa su propio vc_wait_vblank() (no se puede anidar). */
        if (grounded && lock_delay >= LOCK_DELAY) {
            lock_and_next();
            was_grounded = 0;   /* la nueva pieza aun no esta apoyada */
        }
    }

    /* Pausa de gracia al morir: deja ver el pozo lleno unos instantes. */
    if (dead && !quit_flag) {
        uint8_t i;
        put_str_pal(16, 3, "GAME OVER", PAL2);
        for (i = 0; i < 30 && !quit_flag; i++) {
            vc_wait_vblank();
            snd_update();
            vc_wait_vblank_end();
        }
    }

    return quit_flag ? 0 : 1;
}

/* ===========================================================================
 * PANTALLA DE TITULO
 * =========================================================================== */

/* Resultado de menu_input(). */
#define MR_NONE   0
#define MR_START  1
#define MR_QUIT   2
#define MR_LEFT   3
#define MR_RIGHT  4

static void draw_diff_menu(uint8_t sel, uint8_t on);

/* Lee la entrada del menu de dificultad (joystick + teclado). Devuelve:
 * MR_LEFT/MR_RIGHT para cambiar la dificultad, MR_START para empezar, MR_QUIT
 * para salir. Actualiza 'difficulty' al cambiar. */
static uint8_t menu_input(void) {
    char c;
    uint8_t j = joy_read();
    uint8_t res = MR_NONE;
    static uint8_t prev = 0;
    uint8_t jp = (uint8_t)(j & (uint8_t)~prev);

    if (jp & JOY_A_LEFT)  res = MR_LEFT;
    if (jp & JOY_A_RIGHT) res = MR_RIGHT;
    if (jp & JOY_A_FIRE)  res = MR_START;
    prev = j;

    while (rom_uart_rx_ready()) {
        c = rom_uart_getc();
        switch (c) {
            case 'a': case 'A': case '4': res = MR_LEFT;  break;
            case 'd': case 'D': case '6': res = MR_RIGHT; break;
            case ' ': case '\r': case '\n': case 'e': case 'E': res = MR_START; break;
            case 'q': case 'Q': res = MR_QUIT; break;
            default: break;
        }
    }

    /* Aplica el cambio de dificultad y redibuja el menu. */
    if (res == MR_LEFT) {
        if (difficulty > 0) difficulty--;
        draw_diff_menu(difficulty, 1);
        res = MR_NONE;
    } else if (res == MR_RIGHT) {
        if (difficulty < DIFF_COUNT - 1) difficulty++;
        draw_diff_menu(difficulty, 1);
        res = MR_NONE;
    }
    return res;
}

/* Dibuja las 3 opciones de dificultad en una fila; 'sel' va resaltada. */
static void draw_diff_menu(uint8_t sel, uint8_t on) {
    uint8_t i;
    /* Columna inicial de cada opcion (centradas con separacion). */
    const uint8_t col[DIFF_COUNT] = { 8, 16, 25 };
    /* Longitud de cada nombre. */
    const uint8_t len[DIFF_COUNT] = { 5, 6, 7 };

    /* Etiqueta. */
    put_str_pal(15, 15, "DIFICULTAD", PAL_UI);

    for (i = 0; i < DIFF_COUNT; i++) {
        /* Borra la zona de la opcion (con sitio para las flechas). */
        clear_str_at((uint8_t)(col[i] - 1), 17, 9);
        if (i == sel) {
            /* Resaltada: color vivo (parpadea) + flechas a los lados. */
            uint8_t p = on ? PAL_SEL : PAL_UI;
            if (on) put_str_pal((uint8_t)(col[i] - 1), 17, ">", p);
            put_str_pal(col[i], 17, diff_label(i), p);
            if (on) put_str_pal((uint8_t)(col[i] + len[i]), 17, "<", p);
        } else {
            put_str_pal(col[i], 17, diff_label(i), PAL_UI);
        }
    }
}
/* Dibuja las 3 opciones de dificultad en una fila; 'sel' va resaltada. */
/* Pantalla de titulo. Devuelve 1 = jugar, 0 = salir. */
static uint8_t title_screen(void) {
    /* Dibujo de transicion (fondo + textos): fuera del VBLANK. */
    draw_background();

    put_str_pal(14, 4, "T E T R I S", PAL1);            /* 11 chars -> 14 */
    put_str_pal(5, 10, "IZQ/DER: mover   ABAJO: bajar", PAL_UI); /* 29 -> 5 */
    put_str_pal(9, 11, "BOTON/W/ESPACIO: rotar", PAL_UI);        /* 22 -> 9 */

    /* Menu de dificultad. */
    draw_diff_menu(difficulty, 1);

    /* Espera a que se SUELTE el boton/direccion antes de leer el menu (evita
     * que el boton que disparo el game over inicie la partida de golpe). */
    while (joy_read() & (JOY_A_FIRE | JOY_A_LEFT | JOY_A_RIGHT)) {
        vc_wait_vblank();
        vc_wait_vblank_end();
    }
    while (rom_uart_rx_ready()) (void)rom_uart_getc();

    {
        uint8_t  shown = 0xFF;
        uint16_t hold  = 0;
        uint8_t  sel_shown = 0xFF;   /* estado dibujado del parpadeo de seleccion */

        for (;;) {
            uint8_t rd;
            uint8_t on = (uint8_t)((hold / 40) & 1);
            uint8_t sel_on = (uint8_t)((hold / 16) & 1);   /* parpadeo del menu */

            vc_wait_vblank();

            /* Parpadeo del aviso de inicio. */
            if (on != shown) {
                if (on) {
                    clear_str_at(11, 21, 18);
                } else {
                    put_str_pal(11, 21, "BOTON PARA EMPEZAR", PAL1);
                }
                shown = on;
            }
            /* Parpadeo de la opcion seleccionada. */
            if (sel_on != sel_shown) {
                draw_diff_menu(difficulty, sel_on);
                sel_shown = sel_on;
            }
            hold++;
            vc_wait_vblank_end();

            /* Menu: elegir dificultad + empezar / salir. */
            rd = menu_input();
            if (rd == MR_START) return 1;
            if (rd == MR_QUIT)  return 0;
        }
    }
}

/* ===========================================================================
 * DESPEDIDA
 * =========================================================================== */
static void bye_screen(void) {
    uint8_t i;

    /* Dibujo de transicion (fuera del VBLANK). */
    draw_background();
    put_str_pal(11, 13, "GRACIAS POR JUGAR", PAL_UI);
    put_str_pal(16, 15, "TETRIS 6502", PAL1);

    for (i = 0; i < 80; i++) {
        vc_wait_vblank();
        vc_wait_vblank_end();
    }
}

/* ===========================================================================
 * MAIN
 * =========================================================================== */
int main(void) {
    rom_uart_puts("\r\nTETRIS - Core de Video\r\n");
    rom_uart_puts("joy/WASD = mover, boton/W = rotar, q = salir.\r\n");

    setup_video();
    snd_init();
    joy_init();
    hs_reset();
    difficulty = 1;     /* dificultad por defecto: NORMAL */

    if (!title_screen()) {
        bye_screen();
        snd_silence();
        vc_wait_vblank();
        vc_clear_oam();
        vc_set_scroll_x(0);
        vc_set_scroll_y(0);
        rom_uart_puts("\r\nSaliendo al monitor...\r\n");
        return 0;
    }

    for (;;) {
        if (!play_game()) break;    /* salio con Q durante la partida */

        /* Fin de partida (GAME OVER). */
        snd_silence();
        hs_insert(score, lines);

        /* Panel de fin de partida: dibujo de transicion (fuera del VBLANK). */
        draw_background();

        put_str_pal(15, 3, "GAME OVER", PAL2);   /* 9 chars -> 15 */
        put_str_at(13, 6, "PUNTOS:");
        put_u16_at(21, 6, score, 5);
        put_str_at(13, 8, "LINEAS:");
        put_u16_at(21, 8, lines, 3);

        hs_draw(13, 10);

        put_str_at(9, 18, "BOTON/R = menu");
        put_str_at(9, 19, "Q = salir");

        rom_uart_puts("\r\nGAME OVER - BOTON/R = volver al menu, Q = salir\r\n");

        {
            uint8_t d;
            for (d = 0; d < 20; d++) {
                vc_wait_vblank();
                vc_wait_vblank_end();
            }
        }

        /* Espera: BOTON/R vuelve al MENU inicial (elegir dificultad), Q sale. */
        for (;;) {
            char c;

            if (joy_read() & JOY_A_FIRE) break;

            if (!rom_uart_rx_ready()) {
                vc_wait_vblank();
                vc_wait_vblank_end();
                continue;
            }
            c = rom_uart_getc();
            if (c == 'r' || c == 'R') break;
            if (c == 'q' || c == 'Q') goto salir;
        }

        /* Vuelve al menu de titulo (permite elegir dificultad otra vez). */
        if (!title_screen()) goto salir;
    }

salir:
    bye_screen();
    snd_silence();
    vc_wait_vblank();
    vc_clear_oam();
    vc_set_scroll_x(0);
    vc_set_scroll_y(0);
    rom_uart_puts("\r\nSaliendo al monitor...\r\n");
    return 0;
}
