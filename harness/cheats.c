/* The port's options (cheats.h). The unlocks are bits the game already checks in g_CheatOptions (its own
 * SaveGame_DoCheats has no callers in the shipped build); the helpers are the game's red-brick extras, set in
 * Extras_Active the way its Extras menu does; the floors and invincibility write the game's own fields. */
#include "cheats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nj/backend.h"
#include "nj/guest.h"

/* g_CheatOptions byte 0, read by SaveGame_IsCharUnlocked/IsCharHubUnlocked/IsCharBought (bit 3),
 * SaveGame_IsCharacterSuperMove(X)Bought (bit 4), UIFrontEnd_IsLevelUnlocked (bit 5) and
 * SaveGame_IsExtraUnlocked/IsExtraBought(MP) (bit 6). Nothing in the game writes these bits. */
enum { OPT_CHARACTERS = 0x08, OPT_SUPER_MOVES = 0x10, OPT_LEVELS = 0x20, OPT_EXTRAS = 0x40 };
#define OPT_MASK (OPT_CHARACTERS | OPT_SUPER_MOVES | OPT_LEVELS | OPT_EXTRAS)

/* Extras_Active bits, by what the game's Extras_IsActive callers do with them */
enum { EX_REGEN = 1, EX_HEARTS = 3, EX_DAMAGE = 5, EX_STUDS_X2 = 7, EX_STUDS_X4 = 8, EX_STUDS_X6 = 9 };
#define EX_STUD_BITS ((1u << EX_STUDS_X2) | (1u << EX_STUDS_X4) | (1u << EX_STUDS_X6))

/* Player health: GOPlayer_Active -> GEGAMEOBJECT; +0x7c character data (GOCharacterData) with the health at
 * +0xd0; its +0x164 points at the data holding the maximum at +0x3b8 (GOCharacter_GetHealth/SetHealth). */
#define GO_CHARDATA 0x7cu
#define CD_HEALTH 0xd0u
#define CD_EXT 0x164u
#define EXT_MAX_HEALTH 0x3b8u
#define HUD_SHOWN_HEARTS 0x444u /* HudPlayerBar: hearts on screen, written by Hud_SetHeartCount */

static struct {
    uint32_t options, extras, save, player, hud_hearts, hud_bar, per_heart;
} g;
static NjCheats cur;
static uint32_t forced; /* Extras_Active bits these options switched on */
static int log_on;      /* NJ_CHEAT_LOG: report every refill and top-up on stderr */

void nj_cheats_bind(const NjImage *img) {
    g.options = nj_image_sym(img, "g_CheatOptions");
    g.extras = nj_image_sym(img, "Extras_Active");
    g.save = nj_image_sym(img, "SaveGame_Data");
    g.player = nj_image_sym(img, "GOPlayer_Active");
    g.hud_hearts = nj_image_sym(img, "_Z17Hud_SetHeartCountj");
    g.hud_bar = nj_image_sym(img, "HudPlayerBar");
    g.per_heart = nj_image_sym(img, "gdv_percentPerHeart");
    memset(&cur, 0, sizeof cur);
    forced = 0;
    log_on = getenv("NJ_CHEAT_LOG") != NULL;
}

void nj_cheats_set(const NjCheats *c) { cur = *c; }

static void apply_unlocks(void) {
    uint8_t want = (uint8_t)((cur.all_characters ? OPT_CHARACTERS : 0) | (cur.all_super_moves ? OPT_SUPER_MOVES : 0) |
                             (cur.all_levels ? OPT_LEVELS : 0) | (cur.all_extras ? OPT_EXTRAS : 0));
    nj_w8(g.options, (uint8_t)((nj_r8(g.options) & ~OPT_MASK) | want));
}

/* Bits are kept on while their option is on (a level load may clear them) and cleared once when it is switched
 * off; extras the player switched on in the game's own menu are left alone. Extras_ApplyStudMultiplier takes the
 * first active multiplier (x2, then x4, then x6), so a chosen one clears the other two. */
static void apply_extras(void) {
    static const uint8_t stud_bit[] = {0, EX_STUDS_X2, EX_STUDS_X4, EX_STUDS_X6};
    uint32_t want = (cur.regen_x2 ? 1u << EX_REGEN : 0) | (cur.extra_hearts ? 1u << EX_HEARTS : 0) |
                    (cur.double_damage ? 1u << EX_DAMAGE : 0);
    if (cur.stud_multiplier > NJ_STUDS_X1 && cur.stud_multiplier <= NJ_STUDS_X6)
        want |= 1u << stud_bit[cur.stud_multiplier];
    uint32_t v = nj_r32(g.extras) & ~(forced & ~want);
    if (want & EX_STUD_BITS) v &= ~(EX_STUD_BITS & ~want);
    nj_w32(g.extras, v | want);
    forced = want;
}

static void apply_floors(void) {
    uint64_t studs = nj_r64(g.save + NJ_SAVE_STUDS);
    uint32_t tokens = nj_r32(g.save + NJ_SAVE_DRAW_TOKENS);
    if (cur.studs_floor && studs < NJ_CHEAT_STUDS_FLOOR) {
        nj_w64(g.save + NJ_SAVE_STUDS, NJ_CHEAT_STUDS_FLOOR);
        if (log_on) fprintf(stderr, "[cheats] studs %llu -> %u\n", (unsigned long long)studs, NJ_CHEAT_STUDS_FLOOR);
    }
    if (cur.tokens_floor && tokens < NJ_CHEAT_TOKENS_FLOOR) {
        nj_w32(g.save + NJ_SAVE_DRAW_TOKENS, NJ_CHEAT_TOKENS_FLOOR);
        if (log_on) fprintf(stderr, "[cheats] prize tokens %u -> %u\n", tokens, NJ_CHEAT_TOKENS_FLOOR);
    }
}

uint32_t nj_cheats_hearts(uint32_t health, uint32_t per_heart) {
    if (!per_heart) return 0;
    uint32_t n = health / per_heart + (health % per_heart != 0);
    return n < 5 ? n : 5;
}

static int readable(uint32_t ga, uint32_t size) { return ga >= 0x1000 && nj_mem_is_mapped(ga, size); }

/* Health lost since the last frame comes back in full. The hearts display is only updated through
 * Hud_SetHeartCount, which stops every heart's animation and restarts them only for a changed count, so it is
 * called only when the hearts on screen differ from the full count. A player at 0 is already dying: the game's
 * own respawn takes over. */
static void refill_player(int refill) {
    static uint32_t seen_go, seen_state; /* NJ_CHEAT_LOG: report the player object and health when they change */
    uint32_t go = nj_r32(g.player);
    uint32_t cd = readable(go, GO_CHARDATA + 4) ? nj_r32(go + GO_CHARDATA) : 0;
    uint32_t ext = readable(cd, CD_EXT + 4) ? nj_r32(cd + CD_EXT) : 0;
    if (!readable(ext, EXT_MAX_HEALTH + 2)) {
        if (log_on && go != seen_go) fprintf(stderr, "[cheats] player %08x (character data %08x): no health yet\n", go, cd);
        seen_go = go;
        return;
    }
    uint16_t max = nj_r16(ext + EXT_MAX_HEALTH), hp = nj_r16(cd + CD_HEALTH);
    if (log_on && (go != seen_go || seen_state != ((uint32_t)hp << 16 | max)))
        fprintf(stderr, "[cheats] player %08x health %u of %u\n", go, hp, max);
    seen_go = go;
    seen_state = (uint32_t)hp << 16 | max;
    if (!refill || hp == 0 || hp >= max) return;
    nj_w16(cd + CD_HEALTH, max);
    if (log_on) fprintf(stderr, "[cheats] player health %u -> %u\n", hp, max);
    if (!g.hud_hearts || !g.hud_bar || !g.per_heart) return;
    uint32_t arg = max;
    if (nj_cheats_hearts(max, nj_r32(g.per_heart)) != nj_r8(g.hud_bar + HUD_SHOWN_HEARTS))
        nj_call(nj_ctx(), g.hud_hearts, 1, &arg);
}

void nj_cheats_apply(void) {
    if (g.options) apply_unlocks();
    if (g.extras) apply_extras();
    if (g.save) apply_floors();
    if (g.player && (cur.invincible || log_on)) refill_player(cur.invincible); /* the log also watches without it */
}
