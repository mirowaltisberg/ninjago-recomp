#include "nj_test.h"

#include "boot.h"

/* GameGLSurfaceView.onTouchEvent: the first finger starts a gesture, further fingers are plain downs/ups. */
NJ_TEST(touch_kinds_map_like_android) {
    NJ_EQ_STR(nj_touch_native(NJ_TOUCH_FIRST_DOWN), "nativeTouchEventGestureStart");
    NJ_EQ_STR(nj_touch_native(NJ_TOUCH_MOVE), "nativeTouchEventMove");
    NJ_EQ_STR(nj_touch_native(NJ_TOUCH_LAST_UP), "nativeTouchEventGestureEnd");
    NJ_EQ_STR(nj_touch_native(NJ_TOUCH_CANCEL), "nativeTouchEventCancelAll");
    NJ_EQ_STR(nj_touch_native(NJ_TOUCH_POINTER_DOWN), "nativeTouchEventDown");
    NJ_EQ_STR(nj_touch_native(NJ_TOUCH_POINTER_UP), "nativeTouchEventUp");
}
