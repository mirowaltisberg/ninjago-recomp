/* With unlock_all_characters, the game's own checks report every character as unlocked and bought. */
#include <stdlib.h>
#include <unistd.h>

#include "nj_test.h"

#include "boot.h"
#include "nj/backend.h"
#include "nj/guest.h"

static NjImage img;

static uint32_t ask(const char *fn, uint32_t id) {
    uint32_t args[3] = {id, 0, 0};
    return nj_call(nj_ctx(), nj_image_sym(&img, fn), 3, args) & 0xff;
}

NJ_TEST(all_characters_unlocked_and_bought) {
    if (access(NJ_GUEST_SO_PATH, R_OK) != 0 || access(NJ_GUEST_OBB_PATH, R_OK) != 0) NJ_SKIP("game files not present");
    char dir[] = "/tmp/nj_unlock_XXXXXX";
    NJ_CHECK(mkdtemp(dir) != NULL);
    NjBootConfig cfg = {.so_path = NJ_GUEST_SO_PATH, .obb_path = NJ_GUEST_OBB_PATH, .sandbox_dir = dir,
                        .width = 1280, .height = 720, .fixed_clock = true, .unlock_all_characters = true};
    NJ_EQ_U(nj_boot(&cfg, &img), 0);
    nj_frame();
    nj_frame();
    for (uint32_t id = 1; id < 0x82; id += 16) {
        NJ_EQ_U(ask("_Z23SaveGame_IsCharUnlockedjbb", id), 1);
        NJ_EQ_U(ask("_Z26SaveGame_IsCharHubUnlockedjbb", id), 1);
        NJ_EQ_U(ask("_Z21SaveGame_IsCharBoughtjbb", id), 1);
    }
}
