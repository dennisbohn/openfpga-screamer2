/*
 * The SDRAM the track image is built in (docs/soc.md), and what the viewer knows about the image there. The viewer builds a race's track in the 7 MB window at 0x1210_0000 (the
 * game's containers are read above it, the build's scratch above those); the options room (s2room.h) builds its room in the same window, from the same containers' place.
 */
#pragma once
#include <stdint.h>
#include "hw.h"
#include "race.h"
#include "s2teams.h"

#define TEX_BASE    (SDRAM_BASE + 0x2100000)                                          /* the image, up to 7 MB */
#define TEX_FILE    ((const volatile uint8_t *)(uintptr_t)TEX_BASE)
#define TEX_MAX     0x700000u
#define GFX_BASE    (SDRAM_BASE + 0x2800000)                                          /* GFX/MAPnH01.DAT, GFX/MENU<t>1.DAT */
#define GFX_MEM     ((uint8_t *)(uintptr_t)GFX_BASE)
#define GFX_MAX     0x400000u
#define CARS_MEM    ((uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2C00000))                  /* the place for the cars' ZIP entries (s2teams.h) */
#define CARS_MAX    0x100000u
#define DATA_MEM    ((uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2D00000))                  /* DATA/MAPn.DAT */
#define DATA_MAX    0x40000u
#define ENV_MEM     ((uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2D40000))                  /* GFX/ENVPn01.DAT (up to 20 KB) */
#define ENV_MAX     0x20000u
#define BLOB_MEM    ((uint8_t *)(uintptr_t)(SDRAM_BASE + 0x2D60000))                  /* the grid's cars, unpacked */
#define BLOB_ROOM   (RACE_CARS * S2_CAR_ROOM)                                         /* 0xE0000 */
#define SCRATCH_BASE    (SDRAM_BASE + 0x2D60000 + BLOB_ROOM)
#define SCRATCH_MEM ((uint8_t *)(uintptr_t)SCRATCH_BASE)
#define SCRATCH_MAX (PROG_BASE - (SDRAM_BASE + 0x2D60000u + BLOB_ROOM))

/* What the image in the window holds: so that time attack does not build it again. valid is 0 whenever something else was built there (the options room clears it). */
struct viewer_image { int valid, track, cars; struct s2_pick pick[RACE_CARS]; };
extern struct viewer_image loaded;
