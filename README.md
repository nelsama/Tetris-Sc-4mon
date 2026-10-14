# TETRIS — Core de Vídeo (vc) para 6502 / Tang Nano 9K

El clásico **Tetris** para el computador **6502** con el **Core de Vídeo**
(`videocore-6502-cc65`) y el **monitor 6502**. Programado en C con **cc65** y la
biblioteca `vc`.

> **Origen:** este proyecto parte de la plantilla de `snake` / `SpaceInvaders`
> (proyectos autónomos de ejemplo), pero tiene su propia lógica, configuración y
> copia de la biblioteca (`lib/vc.lib`, `config/programa.cfg`, `include/video.h`).

> 📌 **¿Vas a modificar el juego?** Lee antes
> [`doc/TIPS-Y-TRAMPA.md`](doc/TIPS-Y-TRAMPA.md): guía **general** de desarrollo en
> el Core de Vídeo (tiles, sprites, flips, paletas, texto, SID, depuración) con
> síntoma → causa → solución.

---

## Cómo jugar

Se puede jugar con **joystick Atari (DB9)** o con **teclado UART**, en paralelo.

**Joystick Atari** (puerto GPIO del FPGA):

| Dirección | Acción |
|-----------|--------|
| stick ← / → | mover la pieza a izquierda / derecha |
| stick ↓ | bajar más rápido (soft drop) |
| botón (fuego) | rotar la pieza |

**Teclado UART:**

| Tecla | Acción |
|-------|--------|
| `a` / `4` / flecha ← | mover izquierda |
| `d` / `6` / flecha → | mover derecha |
| `s` / `2` / flecha ↓ | bajar (soft drop) |
| `w` / `8` / flecha ↑ / espacio | rotar |
| `p` | pausa (alterna) |
| `q` | salir al monitor |
| `r` | (en GAME OVER) jugar de nuevo |

Al mover a los lados o rotar hay **auto-repetición (DAS)**: mantener la dirección
un instante hace que la pieza siga moviéndose.

### Reglas

- **Objetivo:** encajar los tetrominós (7 piezas) en el pozo de 10×20 celdas y
  completar líneas horizontales, que desaparecen.
- **Reparto de piezas:** bolsa de 7 barajada (cada pieza sale una vez por ciclo),
  así no hay rachas interminables sin la pieza que necesitas.
- **Puntuación:** 1/2/3/4 líneas a la vez = 40/100/300/1200 puntos, multiplicado
  por el nivel. Además, cada celda de soft drop suma 1 punto.
- **Efecto al borrar línea:** las filas completas **parpadean** (3 destellos
  blancos) antes de colapsar, como en los arcade.
- **Niveles:** cada 10 líneas sube el nivel y la gravedad acelera (mínimo ~6
  frames por caída).
- **Pieza siguiente:** se muestra en el panel `NEXT` (arriba a la derecha).
- **Derrota:** si una pieza nueva no cabe al aparecer (pozo desbordado).
- **Highscore:** se guardan las **5 mejores puntuaciones** de la sesión (en RAM),
  con las líneas hechas. Al morir se muestra la tabla con tu partida **resaltada**.
  Al salir al monitor (`jmp $8000` reinicia la RAM) la tabla se pierde.
- **Fin de partida:** se muestra `GAME OVER` y espera el **botón** (o `R`) para
  reintentar, o `Q` para salir.

---

## Diseño del juego (decisiones fijadas)

### Pozo, bloques y pieza activa = **tiles** del tilemap

El hardware dibuja con un motor de tiles + sprites (sin framebuffer). En Tetris
todo el contenido del juego cabe holgadamente como tiles:

- **Pozo (muros, bloques fijados y pieza activa) → tiles** del tilemap.
- **Panel NEXT y HUD → tiles** (bloques + fuente del sistema).

No se usan sprites: el juego es una rejilla estática y no necesita el OAM.

### La pieza activa se dibuja por diferencia

En cada paso solo se tocan las celdas de la pieza: primero se **borra** la
posición anterior (tile vacío) y luego se **pinta** la nueva. Mover o rotar toca
4 celdas; la gravedad, 4. Es un coste mínimo para el 6502. Al fijar la pieza se
escriben sus celdas en el tablero y, si hay líneas completas, se colapsa y se
redibuja el pozo.

### Codificación de las piezas (rotaciones)

Cada pieza es una **máscara de 4×4 bits** por rotación (4 rotaciones), guardada
como `uint16_t`. Cada fila es un nibble; **bit 3 = columna 0**. Las piezas de
3×3 (T, S, Z, J, L) se colocan en las columnas 0-2 de la caja 4×4; la I usa la
caja completa.

```
color = (plano1 << 1) | plano0   (índices 0..3, 0 = transparente)
```

El bloque se dibuja con un **bisel** (borde, relleno y brillo) usando los índices
1/2/3 del patrón. El color real lo pone la **paleta** de la celda, de modo que el
mismo patrón sirve para las 7 piezas: cada tipo usa su propia paleta/flags.

### Color y paletas

- `BG_COLOR` se programa a **negro** (entrada 15 de la paleta de fondo). El cielo
  del fondo se dibuja con tiles propios, no con BG_COLOR.
- **7 colores de pieza**: como solo hay 4 paletas de fondo (una reservada al
  fondo), se combinan **3 paletas × 3 patrones de bloque** (que colocan el
  relleno en un índice de color distinto) para obtener 7 colores únicos:
  cian, amarillo, rojo, verde, naranja, azul y magenta.
- **Paleta 3** → fondo (cielo + Kremlin): índices 1=azul medianoche, 2=dorado.
  Su índice 3 es `BG_COLOR` (entrada 15), que se deja en negro.
- El **HUD y los textos** usan la fuente del sistema (tile = ASCII), que pinta
  siempre el color 3 de la paleta de la celda.

### Fondo: cielo + skyline de torres a los costados

El fondo es un **cielo nocturno** (BG_COLOR azul medianoche + estrellas) con un
**skyline de torres** a cada lado del pozo. Las torres vienen de un Piskel de
10 frames de 8×8, convertidos a patrones de 2 planos.

Cada torre son **2 tiles de ancho**, con este reparto vertical:

```
 1| 6
 2| 7
 3| 8
 4| 9
 5|10     <- 5 y 10 = BASE (se repite hacia abajo, da el "largo")
```

- El **remate** son los pisos 1-5 (columna izquierda) y 6-10 (derecha).
- La **altura de cada torre** (5 a **10 tiles** como máximo) se define en
`towerL[]` / `towerR[]`; bajo el remate se repite la base (tiles 5 | 10) hasta el
suelo (fila 29). `draw_tower()` limita el alto a 10 tiles por seguridad.
- Se dibujan **7 torres por lado** (14 columnas), una al lado de otra, con
alturas distintas para que el skyline quede escalonado.
- **3 colores + fondo** (paleta 3): idx1 = naranja, idx2 = amarillo,
  idx3 = naranja oscuro; idx0 = transparente. El **cielo** usa la paleta 0 (fondo
  azul oscuro + estrellas), así no compite por la paleta de las torres.

### Sonido (SID 6581, compatible C64)

El juego usa el **SID** mapeado en `$D400-$D41F`. Driver en `sound.c` /
`include/sound.h`. Voces:

| Voz | Uso |
|-----|-----|
| 1 | tic al mover/rotar |
| 2 | encaje de pieza (blip grave) y línea completada (tono agudo) |
| 3 | subir de nivel y **game over** |

El volumen master se fija a máximo (`$D418 = 0x0F`, filtro desactivado).
`snd_update()` se llama una vez por frame para gestionar las envolventes.

> La **voz 3** (oscilador de ruido) también se usa como fuente de aleatoriedad
> (`snd_random()`, registro `$D41B`, "OSC3 random") para sembrar el RNG.

### Entrada: joystick Atari (DB9) + teclado UART

Driver en `joy.c` / `include/joy.h`. El joystick y el teclado funcionan **en
paralelo**; el juego solo lee un bitmask de acciones (`JOY_A_*`) en `read_input()`.

| Recurso | Valor |
|---|---|
| Puerto GPIO | Puerto 1 |
| Datos | `$C000` (lectura = entradas) |
| Config por bit | `$C002` (0 = salida, 1 = entrada) |
| Bits del joystick | 3-7 (los 0-2 los ocupa el TM1638) |

Señal → bit: right = bit 3 (`0x08`), left = 4 (`0x10`), down = 5 (`0x20`),
up = 6 (`0x40`), fire = 7 (`0x80`). El joystick Atari es **activo por nivel bajo**
(`JOY_ACTIVE_LOW`).

> ⚠️ `$C002` se escribe siempre con **read-modify-write** (preserva los bits 0-2 del
> TM1638); si se pisa con un valor fijo, el TM1638 deja de funcionar. Igual al leer:
> se enmascara con `JOY_MASK` (`0xF8`).

### Presupuesto de recursos

| Recurso | Límite | Uso previsto |
|---|---|---|
| Sprites (OAM) | 32 | **0** |
| Patrones de fondo | 256 (fuente en `$20`-`$7F`) | 3 bloques ($01-$07) + muro + 8 del fondo + fuente |
| Patrones de sprite | 64 | 0 |
| Tablero | 10×20 = 200 celdas | array en RAM (200 B) |
| RAM | `$0800-$3DFF` (~13.5 KB) | holgado (~13 KB de binario) |

---

## Compilar

```bash
make CC65_HOME=D:/cc65
```

Genera `output/game.bin`. Cárgalo en el monitor:

```
LOAD GAME 0800
R 0800
```

> **Nota sobre `CC65_HOME`:** en este entorno cc65 es un ejecutable de Windows
> (`D:/cc65`). Si compilas desde **Git Bash / cmd**, usa `CC65_HOME=D:/cc65`.
> Desde **WSL** no se pueden lanzar los `.exe`, así que compila desde el shell
> nativo de Windows.

> **Versión de la API:** el juego está alineado con la biblioteca `vc`
> correspondiente al **Manual del Core v2.7**. En esa versión las constantes de
> paleta usan **nombres neutros** (`VC_BGPAL_0..3`, `VC_SPPAL_0..3`) y
> `VC_TINTA(paleta)`.

### Validación del núcleo lógico

`tools/test_core.py` replica en Python las tablas de rotación y la lógica de
colisión/limpieza de líneas, y comprueba propiedades (4 celdas por rotación,
spawn, muros, colapso). Sirve para detectar errores en las tablas sin necesidad
de ejecutar el binario:

```bash
python tools/test_core.py
```

---

## Estructura

```
Tetris/
├── makefile            build autónomo (ROOT = .)
├── game.c              lógica del juego
├── sound.c             driver SID
├── joy.c               driver del joystick Atari (GPIO)
├── startup.s           arranque cc65 -> jmp $8000 (monitor)
├── include/
│   ├── video.h         API del Core de Vídeo (copia de la lib)
│   ├── sound.h         API del driver de sonido
│   ├── joy.h           API del driver del joystick
│   └── romapi.h        ROM API del monitor
├── config/
│   └── programa.cfg    config del linker (copia de la lib)
├── doc/
│   ├── VIDEO-LIB.md    manual de la biblioteca
│   ├── TIPS-Y-TRAMPA.md  lecciones y trampas (para empezar un juego nuevo)
│   └── CORE-BUG-sprite-right-edge.md
├── tools/
│   ├── test_core.py    validación del núcleo lógico (Python)
│   ├── gen_bg.py       generador del cielo (fondo)
│   ├── gen_torres.py   conversor de los frames Piskel de torres
│   └── layout.py       simulador del layout de pantalla (ASCII)
├── build/              objetos
└── output/
    ├── game.bin        binario final
    ├── game.map        mapa de memoria
    └── vc.lib          copia de la biblioteca
```

### Actualizar la biblioteca

```bash
cp ../videocore-6502-cc65/output/vc.lib   lib/vc.lib
cp ../videocore-6502-cc65/src/video.h     include/video.h
cp ../videocore-6502-cc65/config/programa.cfg config/programa.cfg
```
