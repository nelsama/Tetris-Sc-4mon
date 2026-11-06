/**
 * ============================================================================
 * sound.h - Driver de sonido para el SID 6581 (compatible C64)
 * ============================================================================
 * El SID esta mapeado en $D400-$D41F, igual que en el Commodore 64.
 *
 * Mapa de registros por voz (offset 0, 7, 14):
 *   +0 FREQ_LO      +1 FREQ_HI      +2 PW_LO    +3 PW_HI
 *   +4 CTRL (gate/sync/ring/test/waveform)
 *   +5 ATTACK/DECAY (+6 SUSTAIN/RELEASE)
 * $D418 = MODE/VOL (bits 3:0 = volumen master)
 *
 * ARQUITECTURA (3 voces con secuencias independientes, sin bloqueos):
 *   Voz 1: melodia y arpegios -> PULSO con PWM animado.
 *   Voz 2: bajo / acordes     -> TRIANGULO.
 *   Voz 3: solo aleatoriedad ($D41B, oscilador de ruido).
 *
 * Cada efecto es una pequena melodia de 1-6 notas que avanza sola desde
 * snd_update(). El juego no se detiene esperando al sonido.
 * ============================================================================
 */

#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

/* Inicializa el SID: silencia las 3 voces, volumen master al maximo. */
void snd_init(void);

/* Efectos (no bloqueantes: disparan la secuencia y suenan solos).
 * Todos llevan dos capas: melodia (pulso animado) + bajo (triangulo). */
void snd_move(void);      /* click corto al mover/rotar la pieza   */
void snd_land(void);      /* click grave al tocar el suelo        */
void snd_drop(void);      /* golpe al fijar (encajar) la pieza    */
void snd_line(void);      /* arpegio ascendente al completar linea */
void snd_level(void);     /* arpegio corto al subir de nivel       */
void snd_over(void);      /* melodia descendente de fin de partida */

/* Melodia de introduccion (melodia + bajo). */
void snd_jingle(void);

/* Apaga todas las voces (al salir o cambiar de estado). */
void snd_silence(void);

/* Byte pseudoaleatorio del SID (oscilador de ruido de la voz 3, $D41B). */
uint8_t snd_random(void);

/* Tick por frame: avanza las secuencias y el PWM. */
void snd_update(void);

#endif /* SOUND_H */
