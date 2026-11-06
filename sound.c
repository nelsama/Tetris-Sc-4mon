/**
 * ============================================================================
 * sound.c - Driver de sonido para el SID 6581 (compatible C64)
 * ============================================================================
 * Acceso directo a los registros del SID en $D400-$D41F.
 *
 * FORMATO DE REGISTROS (por voz, base = $D400 + 7*voz):
 *   FREQ_LO = +0  FREQ_HI = +1  PW_LO = +2  PW_HI = +3
 *   CTRL    = +4  (gate | sync | ring | test | forma de onda en bits 7:4)
 *   AD      = +5  (attack/decay)   SR = +6  (sustain/release)
 *   $D418 MODE_VOL  bits3:0 = volumen master
 *
 * ARQUITECTURA: 3 voces, cada una con su propia secuencia de pasos que avanza
 * sola por frame (no se bloquea el juego):
 *   Voz 1 = melodia / arpegios (PULSO con PWM animado)
 *   Voz 2 = bajo / acordes     (TRIANGULO)
 *   Voz 3 = percusion          (RUIDO)  <- tambien da la aleatoriedad
 *
 * Se aprovechan capacidades del SID: ADSR por nota, forma de onda distinta por
 * voz, PWM (ancho de pulso modulado) y capas simultaneas melodia+bajo.
 * ============================================================================
 */

#include <stdint.h>
#include "sound.h"

#define SID_V1      0xD400
#define SID_V2      0xD407
#define SID_V3      0xD40E
#define SID_MODE_VOL (*(volatile uint8_t *)0xD418)
#define SID_V3_OSC  (*(volatile uint8_t *)0xD41B)

#define WF_TRI      0x10
#define WF_PULSE    0x40
#define WF_NOISE    0x80
#define GATE        0x01

/* Notas (frecuencias del SID). */
#define N_F2 0x0208
#define N_A2 0x0292
#define N_C3 0x0310
#define N_F3 0x0411
#define N_G3 0x0490
#define N_A3 0x0517
#define N_C4 0x0620
#define N_E4 0x07BA
#define N_G4 0x0930
#define N_C5 0x0C44
#define N_E5 0x0F80
#define N_G5 0x1313

/* ===========================================================================
 * TABLAS DE SECUENCIAS
 * =========================================================================== */

/* Jingle: melodia y bajo (mismo patron de duraciones). */
static const uint16_t jg_mel[]  = { N_C4, N_E4, N_G4, N_C5, N_E4, N_C4 };
static const uint16_t jg_bass[] = { N_C3, N_C3, N_G3, N_G3, N_C4, N_C4 };
static const uint8_t  jg_d[]    = { 8, 8, 8, 12, 8, 14 };

/* Game over: descendente. */
static const uint16_t go_mel[]  = { N_E4, N_C4, N_A3, N_F3 };
static const uint16_t go_bass[] = { N_C3, N_A2, N_F2, N_F2 };
static const uint8_t  go_d[]    = { 12, 12, 12, 24 };

/* Linea: arpegio ascendente + bajo. */
static const uint16_t ln_mel[]  = { N_C5, N_E5, N_G5 };
static const uint16_t ln_bass[] = { N_C4, N_G4, N_C5 };
static const uint8_t  ln_d[]    = { 2, 2, 10 };

/* Encaje: una nota grave corta (reutiliza una tabla de 1 paso). */
static const uint16_t dp_mel[]   = { N_E4 };
static const uint16_t dp_bass[]  = { N_C3 };
static const uint8_t  dp_d[]     = { 3 };

/* ===========================================================================
 * ESTADO
 * =========================================================================== */

/* Voz 1 (melodia): tabla activa, posicion y tiempo. */
static const uint16_t *m_f;
static const uint8_t  *m_d;
static uint8_t         m_len, m_idx, m_t, m_on;

/* Voz 2 (bajo). */
static const uint16_t *b_f;
static const uint8_t  *b_d;
static uint8_t b_len, b_idx, b_t, b_on;

/* Voz 3 (efectos cortos: click de mover/rotar). NO comparte con la melodia,
 * asi el aterrizaje/movimiento nunca corta una melodia en curso. */
static uint8_t e_t;      /* frames restantes del efecto */

/* PWM de la voz 1. */
static uint8_t pw_val, pw_dir;

/* --- Escritura de registros --- */
static void sid_wr(uint16_t addr, uint8_t v) {
    *(volatile uint8_t *)addr = v;
}

static void sid_freq(uint16_t base, uint16_t f) {
    sid_wr(base + 0, (uint8_t)(f & 0xFF));
    sid_wr(base + 1, (uint8_t)(f >> 8));
}

/* --- Arranque de un canal de melodia/bajo --- */
static void mel_start(const uint16_t *f, const uint8_t *d, uint8_t len) {
    m_f = f; m_d = d; m_len = len; m_idx = 0; m_t = d[0]; m_on = 1;
    sid_wr(SID_V1 + 5, 0x09);
    sid_wr(SID_V1 + 6, 0x96);
    sid_freq(SID_V1, f[0]);
    sid_wr(SID_V1 + 2, pw_val);
    sid_wr(SID_V1 + 3, 0x08);
    sid_wr(SID_V1 + 4, (uint8_t)(WF_PULSE | GATE));
}

static void bass_start(const uint16_t *f, const uint8_t *d, uint8_t len) {
    b_f = f; b_d = d; b_len = len; b_idx = 0; b_t = d[0]; b_on = 1;
    sid_wr(SID_V2 + 5, 0x09);
    sid_wr(SID_V2 + 6, 0xA9);
    sid_freq(SID_V2, f[0]);
    sid_wr(SID_V2 + 4, (uint8_t)(WF_TRI | GATE));
}

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
    m_on = b_on = 0;
    m_len = m_idx = m_t = 0;
    b_len = b_idx = b_t = 0;
    e_t = 0;
    pw_val = 0x80; pw_dir = 1;
}

void snd_silence(void) {
    sid_wr(SID_V1 + 4, 0);
    sid_wr(SID_V2 + 4, 0);
    sid_wr(SID_V3 + 4, 0);
    m_on = b_on = 0;
    e_t = 0;
}

uint8_t snd_random(void) {
    uint8_t out;
    sid_freq(SID_V3, 0xFFFF);
    sid_wr(SID_V3 + 4, WF_NOISE);
    out = SID_V3_OSC;
    sid_wr(SID_V3 + 4, 0);
    return out;
}

/* Jingle: melodia (pulso) + bajo (triangulo). */
void snd_jingle(void) {
    mel_start(jg_mel, jg_d, 6);
    bass_start(jg_bass, jg_d, 6);
}

/* Click seco en la voz 3 (mover, rotar o tocar el suelo). 'freq' y 'pw' fijan
 * el timbre: agudo/fino para el movimiento, grave/redondo para el contacto. */
static void click3(uint16_t freq, uint8_t pw, uint8_t frames) {
    sid_wr(SID_V3 + 5, 0x08);           /* attack 0, decay 8: sin silbido */
    sid_wr(SID_V3 + 6, 0x00);           /* sustain 0, release 0 */
    sid_wr(SID_V3 + 2, 0x00);
    sid_wr(SID_V3 + 3, pw);
    sid_freq(SID_V3, freq);
    sid_wr(SID_V3 + 4, WF_PULSE | GATE);
    e_t = frames;
}

/* Click fino al mover/rotar (VOZ 3: no pisa la melodia ni el bajo). */
void snd_move(void) {
    click3(0x1200, 0x04, 2);
}

/* Contacto con el suelo: grave y con cuerpo, para que se perciba el golpe. */
void snd_land(void) {
    click3(0x0400, 0x08, 4);
}

/* Encaje: nota grave corta + bajo. Suena AUNQUE haya melodia (no la corta,
 * porque usa las voces 1 y 2 solo si estan libres; si no, solo el bajo). */
void snd_drop(void) {
    if (!m_on) mel_start(dp_mel, dp_d, 1);
    if (!b_on) bass_start(dp_bass, dp_d, 1);
}

/* Linea completada / subida de nivel: arpegio ascendente + bajo. */
void snd_line(void) {
    mel_start(ln_mel, ln_d, 3);
    bass_start(ln_bass, ln_d, 3);
}
void snd_level(void) {
    snd_line();
}

/* Game over: melodia descendente + bajo grave. */
void snd_over(void) {
    mel_start(go_mel, go_d, 4);
    bass_start(go_bass, go_d, 4);
}

/* ===========================================================================
 * TICK POR FRAME
 * =========================================================================== */

void snd_update(void) {
    /* Efecto corto de la voz 3 (click de mover/rotar). */
    if (e_t) {
        e_t--;
        if (e_t == 0) sid_wr(SID_V3 + 4, 0);
    }

    /* PWM de la melodia mientras suena: el timbre "respira". */
    if (m_on) {
        if (pw_dir) {
            pw_val += 2;
            if (pw_val >= 0xB0) pw_dir = 0;
        } else {
            pw_val -= 2;
            if (pw_val <= 0x50) pw_dir = 1;
        }
    }

    /* Voz 1: melodia. */
    if (m_on) {
        if (m_t) m_t--;
        if (m_t == 0) {
            m_idx++;
            if (m_idx >= m_len) {
                m_on = 0;
                sid_wr(SID_V1 + 4, WF_PULSE);
            } else {
                m_t = m_d[m_idx];
                sid_freq(SID_V1, m_f[m_idx]);
                sid_wr(SID_V1 + 2, pw_val);
                sid_wr(SID_V1 + 3, 0x08);
                sid_wr(SID_V1 + 4, WF_PULSE | GATE);
            }
        }
    }

    /* Voz 2: bajo. */
    if (b_on) {
        if (b_t) b_t--;
        if (b_t == 0) {
            b_idx++;
            if (b_idx >= b_len) {
                b_on = 0;
                sid_wr(SID_V2 + 4, WF_TRI);
            } else {
                b_t = b_d[b_idx];
                sid_freq(SID_V2, b_f[b_idx]);
                sid_wr(SID_V2 + 4, WF_TRI | GATE);
            }
        }
    }
}
