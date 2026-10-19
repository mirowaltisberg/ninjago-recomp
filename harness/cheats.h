/* The port's options, built on the game's own switches (see docs/options.md for how each was found).
 * Everything is re-applied once per frame, so a switch takes effect on the next frame, and switching it off
 * restores the game's own behaviour. Only the stud and token floors change the save itself. */
#ifndef NJ_CHEATS_H
#define NJ_CHEATS_H

#include <stdbool.h>
#include <stdint.h>

#include "nj/loader.h"

typedef enum { NJ_STUDS_X1, NJ_STUDS_X2, NJ_STUDS_X4, NJ_STUDS_X6 } NjStudMultiplier;

typedef struct {
    bool all_characters;  /* every character unlocked and bought (g_CheatOptions byte 0 bit 3) */
    bool all_levels;      /* every level unlocked in the level select (bit 5) */
    bool all_extras;      /* every red-brick extra collected and bought, for the game's own Extras menu (bit 6) */
    bool all_super_moves; /* every character's super moves bought (bit 4) */
    NjStudMultiplier stud_multiplier; /* red-brick extras 7, 8, 9 */
    bool regen_x2;        /* red-brick extra 1: health comes back twice as fast */
    bool extra_hearts;    /* red-brick extra 3: more hearts at the start of a level */
    bool double_damage;   /* red-brick extra 5: the player's hits do twice the damage */
    bool invincible;      /* the player's health is refilled after every hit */
    bool studs_floor;     /* studs never below NJ_CHEAT_STUDS_FLOOR */
    bool tokens_floor;    /* prize-draw tokens never below NJ_CHEAT_TOKENS_FLOOR */
} NjCheats;

#define NJ_CHEAT_STUDS_FLOOR 1000000u
#define NJ_CHEAT_TOKENS_FLOOR 10u
/* SAVEGAMEDATA fields (offsets from SaveGame_Data), as SaveGame_DoCheats and the prize picker use them */
#define NJ_SAVE_STUDS 0x78u        /* uint64 */
#define NJ_SAVE_DRAW_TOKENS 0x4a8u /* uint32 */

/* Resolve the game's symbols; called by nj_boot. Resets the options to all off. */
void nj_cheats_bind(const NjImage *img);
/* Replace the current options (copied). */
void nj_cheats_set(const NjCheats *c);
/* Apply the options to the game's state; nj_frame calls this before every frame. */
void nj_cheats_apply(void);
/* Hearts the game's display shows for this health (Hud_SetHeartCount: ceil(health / per heart), at most 5). */
uint32_t nj_cheats_hearts(uint32_t health, uint32_t per_heart);

#endif
