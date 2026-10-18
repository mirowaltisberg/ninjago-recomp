/* CPU backend contract; implemented by backend/unicorn (macOS dev/oracle) and backend/aot (product).
 * Exactly one backend is linked into each executable. */
#ifndef NJ_BACKEND_H
#define NJ_BACKEND_H

#include <stdint.h>

#include "nj/guest.h"
#include "nj/loader.h"

/* Start the backend. Every guest region must already be mapped (image, heap, fixed regions) and
 * overrides resolved; afterwards guest memory is frozen. 0 on success. */
int nj_backend_init(void);
/* Optional: image used for symbolised diagnostics. */
void nj_backend_set_image(const NjImage *img);

/* Guest context of the calling host thread at the current call depth (created on first use with
 * its own 1 MiB guest stack). Inside an HLE function this is the context that function received. */
NjCtx *nj_ctx(void);

/* Call guest function fn (Thumb bit honoured) with AAPCS arguments: words 0-3 in r0-r3, the rest on
 * the guest stack below c's sp. lr = NJ_RETURN_TRAP. Returns r0; c->r[0] and c->r[1] receive the
 * results, every other field of c is unchanged. Re-entrant: HLE functions may call it. */
uint32_t nj_call(NjCtx *c, uint32_t fn, int argc, const uint32_t *argv);

/* Run fn(arg) on a new host thread with its own guest stack; on_exit(ret, ud) runs on that thread
 * after fn returns. 0 on success. */
int nj_spawn_thread(uint32_t start_fn, uint32_t arg, void (*on_exit)(uint32_t ret, void *ud), void *ud);

#endif
