/**
 * ============================================================================
 * sound.c - Driver de sonido para el SID 6581 (compatible C64)
 * ============================================================================
 * Acceso directo a los registros del SID en $D400-$D41F.
 *
 * FORMATO DE REGISTROS (por voz, base = $D400 + 7*voz):
 *   FREQ_LO  = base+0   frecuencia, bits 7:0
 *   FREQ_HI  = base+1   frecuencia, bits 15:8
 *   PW_LO    = base+2   ancho de pulso, bits 7:0
 *   PW_HI    = base+3   ancho de pulso, bits 11:8
 *   CTRL     = base+4   bit0 gate | bit1 sync | bit2 ring | bit3 test |
 *                       bits7:4 waveform (pulse/tri/saw/noise)
 *   AD       = base+5   ataque (nibble alto) / decay (nibble bajo)
 *   SR       = base+6   sustain (nibble alto) / release (nibble bajo)
 *
 *   $D418 MODE_VOL  bits3:0 = volumen master (0-15)
 *
 * MODELO DE SONIDO: cada efecto es "percutivo" (ataque rapido + decay/release),
 * asi que en cada frame comprobamos si la nota sigue viva con un temporizador
 * y, al agotarse, bajamos el GATE (dejando que el release la apague).
 * ============================================================================
 */

#include <stdint.h>
#include "sound.h"

/* Registros del SID (escritura directa). */
#define SID_V1      0xD400
#define SID_V2      0xD407
#define SID_V3      0xD40E
#define SID_MODE_VOL (*(volatile uint8_t *)0xD418)

/* Registro de LECTURA del oscilador de la voz 3 ($D41B). Con la voz 3 en modo
 * RUIDO es el generador pseudoaleatorio del SID (LFSR de 23 bits): basta leer
 * sus bits para obtener numeros aleatorios. */
#define SID_V3_OSC  (*(volatile uint8_t *)0xD41B)

/* Formas de onda (bits 7:4 de CTRL). */
#define WF_TRI      0x10
#define WF_SAW      0x20
#define WF_PULSE    0x40
#define WF_NOISE    0x80
#define GATE        0x01

/* Duraciones (en frames) de los efectos con temporizador. */
#define TIC_FRAMES    3   /* tic al mover/rotar              */
#define DROP_FRAMES   6   /* blip al encajar/caer la pieza   */
#define LINE_FRAMES  12   /* linea completada                */
#define LEVEL_FRAMES 10   /* subir de nivel                  */
#define OVER_FRAMES  40   /* fin de partida (descenso grave) */

/* ===========================================================================
 * ESCRITURA DE REGISTROS
 * =========================================================================== */

static void sid_wr(uint16_t addr, uint8_t v) {
    *(volatile uint8_t *)addr = v;
}

static void sid_freq(uint16_t base, uint16_t f) {
    sid_wr(base + 0, (uint8_t)(f & 0xFF));
    sid_wr(base + 1, (uint8_t)(f >> 8));
}

static void sid_pw(uint16_t base, uint16_t pw) {
    sid_wr(base + 2, (uint8_t)(pw & 0xFF));
    sid_wr(base + 3, (uint8_t)(pw >> 8));
}

/* ===========================================================================
 * ESTADO (un temporizador por voz; 0 = libre)
 * =========================================================================== */
static uint8_t  t_v1;          /* mover / rotar            */
static uint8_t  t_v2;          /* encajar / linea          */
static uint8_t  t_v3;          /* nivel / game over        */
static uint16_t v2_freq;       /* frecuencia actual (para el sweep) */
static uint8_t  v2_is_line;    /* 1 = la voz 2 suena "linea" (no "drop") */
static uint16_t v3_freq;       /* para el descenso de game over     */
static uint8_t  v3_is_over;    /* 1 = la voz 3 suena game over      */

/* ===========================================================================
 * API
 * =========================================================================== */

void snd_init(void) {
    sid_wr(SID_V1 + 4, 0);
    sid_wr(SID_V2 + 4, 0);
    sid_wr(SID_V3 + 4, 0);
    sid_wr(SID_V1 + 5, 0); sid_wr(SID_V1 + 6, 0);
    sid_wr(SID_V2 + 5, 0); sid_wr(SID_V2 + 6, 0);
    sid_wr(SID_V3 + 5, 0); sid_wr(SID_V3 + 6, 0);
    SID_MODE_VOL = 0x0F;       /* filtro off + volumen master maximo */
    t_v1 = t_v2 = t_v3 = 0;
    v2_freq = 0; v2_is_line = 0;
    v3_freq = 0; v3_is_over = 0;
}

void snd_silence(void) {
    sid_wr(SID_V1 + 4, 0);
    sid_wr(SID_V2 + 4, 0);
    sid_wr(SID_V3 + 4, 0);
    t_v1 = t_v2 = t_v3 = 0;
}

/* Byte pseudoaleatorio a partir del oscilador de ruido de la voz 3 ($D41B).
 * El LFSR del SID avanza a la frecuencia del oscilador, asi que a frecuencia
 * alta cada lectura da bits nuevos. Se pone la voz 3 en modo RUIDO SIN gate
 * (no suena) y se toman 8 bits del registro del oscilador. */
uint8_t snd_random(void) {
    uint8_t out;

    /* Ruido a la frecuencia mas alta posible, sin gate (silencioso). */
    sid_freq(SID_V3, 0xFFFF);
    sid_wr(SID_V3 + 4, WF_NOISE);

    /* El LFSR avanza solo; una lectura ya mezcla bits frescos. */
    out = SID_V3_OSC;

    /* Deja la voz 3 apagada. */
    sid_wr(SID_V3 + 4, 0);
    return out;
}

/* Tic corto al mover/rotar la pieza (voz 1). */
void snd_move(void) {
    sid_wr(SID_V1 + 5, 0x08);      /* ataque 0, decay 8 (percutivo) */
    sid_wr(SID_V1 + 6, 0x00);      /* sustain 0, release 0          */
    sid_pw(SID_V1, 0x0400);        /* pulso ~25%                    */
    sid_freq(SID_V1, 0x0700);      /* grave (~110 Hz)               */
    sid_wr(SID_V1 + 4, WF_PULSE | GATE);
    t_v1 = TIC_FRAMES;
}

/* Blip al encajar la pieza (voz 2), con sweep descendente. */
void snd_drop(void) {
    sid_wr(SID_V2 + 5, 0x04);      /* ataque 0, decay 4 */
    sid_wr(SID_V2 + 6, 0x00);
    sid_pw(SID_V2, 0x0800);        /* pulso ~50% */
    sid_freq(SID_V2, 0x5000);
    sid_wr(SID_V2 + 4, WF_PULSE | GATE);
    v2_freq    = 0x5000;
    v2_is_line = 0;
    t_v2       = DROP_FRAMES;
}

/* Linea completada: tono agudo y brillante (voz 2, triangular con sweep). */
void snd_line(void) {
    sid_wr(SID_V2 + 5, 0x06);      /* ataque 0, decay 6 */
    sid_wr(SID_V2 + 6, 0x00);
    sid_freq(SID_V2, 0x2000);
    sid_wr(SID_V2 + 4, WF_TRI | GATE);
    v2_freq    = 0x2000;
    v2_is_line = 1;
    t_v2       = LINE_FRAMES;
}

/* Subir de nivel: tono triangular corto (voz 3). */
void snd_level(void) {
    sid_wr(SID_V3 + 5, 0x09);
    sid_wr(SID_V3 + 6, 0x00);
    sid_freq(SID_V3, 0x5800);
    sid_wr(SID_V3 + 4, WF_TRI | GATE);
    v3_is_over = 0;
    t_v3       = LEVEL_FRAMES;
}

/* Fin de partida: descenso grave y largo (voz 3, pulso). */
void snd_over(void) {
    sid_wr(SID_V3 + 5, 0x0A);      /* ataque 0, decay 10 */
    sid_wr(SID_V3 + 6, 0x00);
    sid_pw(SID_V3, 0x0800);
    sid_freq(SID_V3, 0x7000);
    sid_wr(SID_V3 + 4, WF_PULSE | GATE);
    v3_freq    = 0x7000;
    v3_is_over = 1;
    t_v3       = OVER_FRAMES;
}

/* Un "tick" por frame: gestiona las notas percutivas y los sweeps. */
void snd_update(void) {
    /* Voz 1: tic de mover/rotar. */
    if (t_v1) {
        t_v1--;
        if (t_v1 == 0) sid_wr(SID_V1 + 4, 0);
    }

    /* Voz 2: encajar (sweep descendente) o linea (sweep ascendente). */
    if (t_v2) {
        t_v2--;
        if (t_v2 == 0) {
            sid_wr(SID_V2 + 4, v2_is_line ? 0 : WF_PULSE);
            v2_is_line = 0;
        } else if (v2_is_line) {
            if (v2_freq < 0x7800) { v2_freq += 0x0300; sid_freq(SID_V2, v2_freq); }
        } else {
            if (v2_freq > 0x0A00) { v2_freq -= 0x0600; sid_freq(SID_V2, v2_freq); }
        }
    }

    /* Voz 3: nivel (corto) o game over (descenso grave). */
    if (t_v3) {
        t_v3--;
        if (t_v3 == 0) {
            sid_wr(SID_V3 + 4, 0);
            v3_is_over = 0;
        } else if (v3_is_over) {
            if (v3_freq > 0x0900) { v3_freq -= 0x0280; sid_freq(SID_V3, v3_freq); }
        }
    }
}
