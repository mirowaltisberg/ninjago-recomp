/* The port's options, checked through the game's own queries: each switch takes effect on the next frame and
 * switching it off restores the game's own answer. Boots the real game once (skipped without the game files). */
#include <stdlib.h>
#include <unistd.h>

#include "nj_test.h"

#include "boot.h"
#include "cheats.h"
#include "nj/backend.h"
#include "nj/guest.h"
#include "nj/heap.h"

static NjImage img;
static int booted;

static void boot_once(void) {
    if (access(NJ_GUEST_SO_PATH, R_OK) != 0 || access(NJ_GUEST_OBB_PATH, R_OK) != 0) NJ_SKIP("game files not present");
    if (booted) return;
    static char dir[] = "/tmp/nj_cheats_XXXXXX";
    NJ_CHECK(mkdtemp(dir) != NULL);
    NjBootConfig cfg = {.so_path = NJ_GUEST_SO_PATH, .obb_path = NJ_GUEST_OBB_PATH, .sandbox_dir = dir,
                        .width = 1280, .height = 720, .fixed_clock = true};
    NJ_EQ_U(nj_boot(&cfg, &img), 0);
    nj_frame();
    booted = 1;
}

/* Sets the options and runs one frame, as the app does when a switch changes. */
static void with(NjCheats c) {
    nj_cheats_set(&c);
    nj_frame();
}

static uint32_t ask(const char *fn, int argc, uint32_t a0, uint32_t a1, uint32_t a2) {
    uint32_t args[3] = {a0, a1, a2};
    uint32_t addr = nj_image_sym(&img, fn);
    NJ_CHECK(addr != 0);
    return nj_call(nj_ctx(), addr, argc, args);
}

static uint32_t sym(const char *name) {
    uint32_t a = nj_image_sym(&img, name);
    NJ_CHECK(a != 0);
    return a;
}

NJ_TEST(characters_switch_on_and_off) {
    boot_once();
    with((NjCheats){.all_characters = true});
    for (uint32_t id = 1; id < 0x82; id += 16) {
        NJ_EQ_U(ask("_Z23SaveGame_IsCharUnlockedjbb", 3, id, 0, 0) & 0xff, 1);
        NJ_EQ_U(ask("_Z26SaveGame_IsCharHubUnlockedjbb", 3, id, 0, 0) & 0xff, 1);
        NJ_EQ_U(ask("_Z21SaveGame_IsCharBoughtjbb", 3, id, 0, 0) & 0xff, 1);
    }
    with((NjCheats){0});
    NJ_EQ_U(ask("_Z23SaveGame_IsCharUnlockedjbb", 3, 0x71, 0, 0) & 0xff, 0); /* locked in a fresh save */
}

NJ_TEST(levels_switch_on_and_off) {
    boot_once();
    with((NjCheats){0});
    NJ_EQ_U(ask("_Z26UIFrontEnd_IsLevelUnlockedj", 1, 2, 0, 0) & 0xff, 0);
    with((NjCheats){.all_levels = true});
    NJ_EQ_U(ask("_Z26UIFrontEnd_IsLevelUnlockedj", 1, 2, 0, 0) & 0xff, 1);
    with((NjCheats){0});
    NJ_EQ_U(ask("_Z26UIFrontEnd_IsLevelUnlockedj", 1, 2, 0, 0) & 0xff, 0);
}

NJ_TEST(red_brick_extras_switch_on_and_off) {
    boot_once();
    with((NjCheats){.all_extras = true});
    for (uint32_t e = 0; e < 10; e++) {
        NJ_EQ_U(ask("_Z24SaveGame_IsExtraUnlockedj", 1, e, 0, 0) & 0xff, 1);
        NJ_EQ_U(ask("_Z22SaveGame_IsExtraBoughtj", 1, e, 0, 0) & 0xff, 1);
    }
    with((NjCheats){0});
    NJ_EQ_U(ask("_Z22SaveGame_IsExtraBoughtj", 1, 3, 0, 0) & 0xff, 0);
}

/* SaveGame_IsCharacterSuperMoveBought reads the character table, which loads with the first level, so this
 * checks the bit it falls back to (byte 0 bit 4) instead of calling it from the front end. */
NJ_TEST(super_moves_switch_on_and_off) {
    boot_once();
    uint32_t opts = sym("g_CheatOptions");
    with((NjCheats){.all_super_moves = true});
    NJ_EQ_U(nj_r8(opts) & 0x10, 0x10);
    with((NjCheats){0});
    NJ_EQ_U(nj_r8(opts) & 0x10, 0);
}

NJ_TEST(stud_multiplier_is_one_choice) {
    boot_once();
    const char *fn = "_Z26Extras_ApplyStudMultiplierj";
    with((NjCheats){.stud_multiplier = NJ_STUDS_X2});
    NJ_EQ_U(ask(fn, 1, 10, 0, 0), 20);
    with((NjCheats){.stud_multiplier = NJ_STUDS_X4});
    NJ_EQ_U(ask(fn, 1, 10, 0, 0), 40);
    with((NjCheats){.stud_multiplier = NJ_STUDS_X6});
    NJ_EQ_U(ask(fn, 1, 10, 0, 0), 60);
    with((NjCheats){0});
    NJ_EQ_U(ask(fn, 1, 10, 0, 0), 10);
}

NJ_TEST(helper_extras_switch_on_and_off) {
    boot_once();
    const char *fn = "_Z15Extras_IsActivei";
    with((NjCheats){.regen_x2 = true, .extra_hearts = true, .double_damage = true});
    NJ_EQ_U(ask(fn, 1, 1, 0, 0) & 0xff, 1);
    NJ_EQ_U(ask(fn, 1, 3, 0, 0) & 0xff, 1);
    NJ_EQ_U(ask(fn, 1, 5, 0, 0) & 0xff, 1);
    with((NjCheats){0});
    NJ_EQ_U(ask(fn, 1, 1, 0, 0) & 0xff, 0);
    NJ_EQ_U(ask(fn, 1, 3, 0, 0) & 0xff, 0);
    NJ_EQ_U(ask(fn, 1, 5, 0, 0) & 0xff, 0);
}

NJ_TEST(extras_switched_on_in_the_game_menu_stay_on) {
    boot_once();
    uint32_t active = sym("Extras_Active");
    with((NjCheats){0});
    nj_w32(active, nj_r32(active) | (1u << 3)); /* the player activates "extra hearts" in the game's own menu */
    nj_frame();
    nj_frame();
    NJ_EQ_U(nj_r32(active) & (1u << 3), 1u << 3);
    nj_w32(active, 0);
}

NJ_TEST(stud_and_token_floors) {
    boot_once();
    uint32_t save = sym("SaveGame_Data");
    nj_w64(save + NJ_SAVE_STUDS, 5);
    nj_w32(save + NJ_SAVE_DRAW_TOKENS, 0);
    with((NjCheats){0});
    NJ_EQ_U(nj_r64(save + NJ_SAVE_STUDS), 5);
    NJ_EQ_U(nj_r32(save + NJ_SAVE_DRAW_TOKENS), 0);
    with((NjCheats){.studs_floor = true, .tokens_floor = true});
    NJ_EQ_U(nj_r64(save + NJ_SAVE_STUDS), NJ_CHEAT_STUDS_FLOOR);
    NJ_EQ_U(nj_r32(save + NJ_SAVE_DRAW_TOKENS), NJ_CHEAT_TOKENS_FLOOR);
    nj_w64(save + NJ_SAVE_STUDS, 3000000); /* more than the floor stays */
    nj_frame();
    NJ_EQ_U(nj_r64(save + NJ_SAVE_STUDS), 3000000);
    with((NjCheats){0});
    nj_w64(save + NJ_SAVE_STUDS, 0);
    nj_w32(save + NJ_SAVE_DRAW_TOKENS, 0);
}

NJ_TEST(invincibility_refills_the_player) {
    boot_once();
    uint32_t player = sym("GOPlayer_Active");
    NJ_EQ_U(nj_r32(player), 0); /* no level loaded: nothing to refill, nothing must break */
    with((NjCheats){.invincible = true});

    /* a minimal player object: GO+0x7c -> character data (health at +0xd0), whose +0x164 -> max health at +0x3b8 */
    uint32_t go = nj_calloc(1, 0x100), cd = nj_calloc(1, 0x200), ext = nj_calloc(1, 0x400);
    nj_w32(go + 0x7c, cd);
    nj_w32(cd + 0x164, ext);
    nj_w16(ext + 0x3b8, 40);
    nj_w16(cd + 0xd0, 13);
    nj_w32(player, go);
    nj_cheats_apply();
    NJ_EQ_U(nj_r16(cd + 0xd0), 40);

    nj_w16(cd + 0xd0, 13);
    NjCheats off = {0};
    nj_cheats_set(&off);
    nj_cheats_apply();
    NJ_EQ_U(nj_r16(cd + 0xd0), 13);
    nj_w32(player, 0);
}
