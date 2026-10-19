/* Hosts the game: one ES2 context and layer-backed framebuffer, boots the guest like Android's GameActivity,
 * then drives GameGLSurfaceView.Renderer.onDrawFrame from a 60 Hz display link and forwards touches. */
#import "GameViewController.h"

#import <OpenGLES/ES2/glext.h>

#import "GameView.h"
#import "NJOptions.h"
#import "NJPlatformIOS.h"
#import "OptionsViewController.h"

#include "boot.h"
#include "cheats.h"
#include "nj/gles.h"

@implementation GameViewController {
    EAGLContext *_ctx;
    GLuint _fbo, _color, _depth;
    GLint _w, _h;
    CADisplayLink *_link;
    BOOL _booted, _paused;
    long _frameNo;
    CFAbsoluteTime _statT0, _statBusy; /* frame-rate log */
    int _taps[32][3], _ntaps; /* NJ_TAPS developer option: frame, x, y in pixels */
    NSMutableArray<UITouch *> *_touches; /* index = Android pointer id; NSNull marks a free slot */
    UILabel *_fps;
    CFAbsoluteTime _fpsT0;
}

/* The Simulator renders OpenGL ES in software, so it draws at 1 pixel per point there (9x fewer pixels than
 * the iPhone 17's native 3x); NJ_RENDER_SCALE overrides. Touches are mapped with the same factor. */
- (void)loadView {
    self.view = [[GameView alloc] initWithFrame:UIScreen.mainScreen.bounds];
    CGFloat scale = UIScreen.mainScreen.nativeScale;
#if TARGET_OS_SIMULATOR
    scale = 1.0;
#endif
    const char *env = getenv("NJ_RENDER_SCALE");
    if (env && atof(env) > 0) scale = atof(env);
    self.view.contentScaleFactor = scale;
}

- (BOOL)prefersStatusBarHidden { return YES; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
- (UIInterfaceOrientationMask)supportedInterfaceOrientations { return UIInterfaceOrientationMaskLandscape; }

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    if (_booted || self.view.bounds.size.width < self.view.bounds.size.height) return; /* wait for landscape */
    [NJOptions registerDefaults]; /* before the first overlay scale, which reads an option */
    [self createFramebuffer];
    [self boot];
    [self addOptionsButton];
    [self applyOptions];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(applyOptions) name:NJOptionsDidChange object:nil];
    [self showCredit];
}

/* ---- options ---- */

/* A small button in the screen corner, outside the area the game draws its controls in (see updateHudScale). */
- (void)addOptionsButton {
    UIButtonConfiguration *c = [UIButtonConfiguration filledButtonConfiguration];
    c.image = [UIImage systemImageNamed:@"slider.horizontal.3"
                      withConfiguration:[UIImageSymbolConfiguration configurationWithPointSize:15 weight:UIImageSymbolWeightSemibold]];
    c.cornerStyle = UIButtonConfigurationCornerStyleCapsule;
    c.baseBackgroundColor = [UIColor colorWithWhite:0 alpha:0.45];
    c.baseForegroundColor = [UIColor colorWithWhite:1 alpha:0.9];
    c.contentInsets = NSDirectionalEdgeInsetsMake(8, 8, 8, 8);
    UIButton *b = [UIButton buttonWithConfiguration:c primaryAction:[UIAction actionWithHandler:^(UIAction *a) {
                                                        [self openOptions];
                                                    }]];
    b.accessibilityLabel = @"Options";
    b.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:b];

    _fps = [UILabel new];
    _fps.font = [UIFont monospacedDigitSystemFontOfSize:12 weight:UIFontWeightSemibold];
    _fps.textColor = [UIColor colorWithWhite:1 alpha:0.9];
    _fps.backgroundColor = [UIColor colorWithWhite:0 alpha:0.45];
    _fps.textAlignment = NSTextAlignmentCenter;
    _fps.layer.cornerRadius = 10;
    _fps.layer.masksToBounds = YES;
    _fps.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_fps];
    [NSLayoutConstraint activateConstraints:@[
        [b.centerXAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:34],
        [b.centerYAnchor constraintEqualToAnchor:self.view.topAnchor constant:34],
        [_fps.centerXAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-40],
        [_fps.centerYAnchor constraintEqualToAnchor:self.view.topAnchor constant:34],
        [_fps.widthAnchor constraintEqualToConstant:56],
        [_fps.heightAnchor constraintEqualToConstant:20],
    ]];
}

/* The game stands still while the sheet is open (frame: skips while something is presented); fingers that were
 * on the screen are released so no control stays held. */
- (void)openOptions {
    nj_touch(NJ_TOUCH_CANCEL, 0, 0, 0);
    [_touches removeAllObjects];
    UINavigationController *nav = [[UINavigationController alloc] initWithRootViewController:[OptionsViewController new]];
    nav.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
    nav.view.tintColor = [UIColor colorWithRed:0.96 green:0.76 blue:0.22 alpha:1];
    nav.modalPresentationStyle = UIModalPresentationPageSheet;
    nav.preferredContentSize = CGSizeMake(560, 0);
    UISheetPresentationController *sheet = nav.sheetPresentationController;
    sheet.prefersEdgeAttachedInCompactHeight = YES;
    sheet.widthFollowsPreferredContentSizeWhenEdgeAttached = YES;
    sheet.prefersGrabberVisible = YES;
    [self presentViewController:nav animated:YES completion:nil];
}

- (void)applyOptions {
    NjCheats c = [NJOptions cheats];
    nj_cheats_set(&c);
    if (_w) [self updateHudScale];
    _fps.hidden = ![NJOptions boolForKey:NJOptionShowFPS];
}

/* Credit for the port, shown over the publisher intro at launch. */
- (void)showCredit {
    UILabel *label = [[UILabel alloc] init];
    label.text = @"iOS port by Miró Waltisberg";
    label.textColor = UIColor.whiteColor;
    label.font = [UIFont systemFontOfSize:15 weight:UIFontWeightSemibold];
    label.layer.shadowColor = UIColor.blackColor.CGColor;
    label.layer.shadowOpacity = 0.8;
    label.layer.shadowRadius = 3;
    label.layer.shadowOffset = CGSizeZero;
    label.alpha = 0;
    label.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:label];
    [NSLayoutConstraint activateConstraints:@[
        [label.centerXAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.centerXAnchor],
        [label.bottomAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.bottomAnchor constant:-16],
    ]];
    [UIView animateWithDuration:0.6 delay:0.3 options:0 animations:^{ label.alpha = 1; } completion:^(BOOL done) {
        [UIView animateWithDuration:1.0 delay:4.0 options:0 animations:^{ label.alpha = 0; }
                         completion:^(BOOL d) { [label removeFromSuperview]; }];
    }];
}

/* The game anchors its controls to the screen edges; shrink the in-game overlay towards the centre just enough
 * to clear the safe area (rounded corners, camera cutout, home indicator). NJ_HUD_SCALE overrides. */
- (void)updateHudScale {
    UIEdgeInsets in = self.view.safeAreaInsets;
    CGSize sz = self.view.bounds.size;
    const CGFloat own_margin = 8; /* the game's controls keep about this much (points) from the edge */
    CGFloat sx = 1 - 2 * MAX(MAX(in.left, in.right) - own_margin, 0) / sz.width;
    CGFloat sy = 1 - 2 * MAX(MAX(in.top, in.bottom) - own_margin, 0) / sz.height;
    CGFloat s = [NJOptions boolForKey:NJOptionSafeArea] ? MAX(MIN(sx, sy), 0.8) : 1.0;
    const char *env = getenv("NJ_HUD_SCALE");
    if (env && atof(env) > 0) s = atof(env);
    nj_gles_set_hud_scale((float)s, _w, _h);
    NSLog(@"[ninjago] overlay scale %.3f (safe area %.0f/%.0f/%.0f/%.0f pt)", s, in.top, in.left, in.bottom, in.right);
}

- (void)viewSafeAreaInsetsDidChange {
    [super viewSafeAreaInsetsDidChange];
    if (_w) [self updateHudScale];
}

- (void)createFramebuffer {
    _ctx = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
    [EAGLContext setCurrentContext:_ctx];
    glGenFramebuffers(1, &_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, _fbo);
    glGenRenderbuffers(1, &_color);
    glBindRenderbuffer(GL_RENDERBUFFER, _color);
    [_ctx renderbufferStorage:GL_RENDERBUFFER fromDrawable:(CAEAGLLayer *)self.view.layer];
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, _color);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &_w);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &_h);
    glGenRenderbuffers(1, &_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, _depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8_OES, _w, _h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, _depth);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, _depth);
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    NSLog(@"[ninjago] framebuffer %dx%d status 0x%x", _w, _h, st);
    [self updateHudScale];
    nj_ios_set_main_context(_ctx, _fbo, _w, _h);
}

static const char *env_or(const char *name, NSString *fallback) {
    const char *v = getenv(name);
    return v && *v ? v : fallback.UTF8String;
}

- (void)boot {
    _booted = YES;
    _touches = [NSMutableArray array];
    NSString *docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    NSString *bundle = NSBundle.mainBundle.resourcePath;
    static NjBootConfig cfg;
    cfg.so_path = strdup(env_or("NJ_SO", [bundle stringByAppendingPathComponent:@"libLEGO_M1.so"]));
    cfg.obb_path = strdup(env_or("NJ_OBB", [bundle stringByAppendingPathComponent:@"main.1057.com.lego.ninjago.toe.obb"]));
    /* NJ_SANDBOX (developer testing): start from a fresh save in the temporary folder instead of the player's;
     * NJ_SAVE_SEED names a save file in the temporary folder to start from instead (only with NJ_SANDBOX, so the
     * player's own save is never overwritten) */
    const char *test_sandbox = getenv("NJ_SANDBOX");
    if (test_sandbox && *test_sandbox) {
        NSFileManager *fm = NSFileManager.defaultManager;
        docs = [NSTemporaryDirectory() stringByAppendingPathComponent:@(test_sandbox)];
        [fm removeItemAtPath:docs error:nil];
        const char *seed = getenv("NJ_SAVE_SEED");
        if (seed && *seed) {
            NSString *files = [docs stringByAppendingPathComponent:@"files"];
            [fm createDirectoryAtPath:files withIntermediateDirectories:YES attributes:nil error:nil];
            NSError *err = nil;
            if (![fm copyItemAtPath:[NSTemporaryDirectory() stringByAppendingPathComponent:@(seed)]
                             toPath:[files stringByAppendingPathComponent:@"savegame.dat"] error:&err])
                NSLog(@"[ninjago] NJ_SAVE_SEED %s: %@", seed, err.localizedDescription);
        }
    }
    cfg.sandbox_dir = strdup(docs.UTF8String);
    cfg.width = _w;
    cfg.height = _h;
    cfg.trace_path = getenv("NJ_TRACE");
    cfg.device_model = "iPhone18,3";
    static NjImage img;
    CFAbsoluteTime t0 = CFAbsoluteTimeGetCurrent();
    if (nj_boot(&cfg, &img) != 0) {
        NSLog(@"[ninjago] boot failed (so=%s obb=%s)", cfg.so_path, cfg.obb_path);
        return;
    }
    NSLog(@"[ninjago] boot done in %.2f s", CFAbsoluteTimeGetCurrent() - t0);
    const char *taps = getenv("NJ_TAPS");
    for (const char *p = taps; p && *p && _ntaps < 32;) {
        if (sscanf(p, "%d,%d,%d", &_taps[_ntaps][0], &_taps[_ntaps][1], &_taps[_ntaps][2]) == 3) _ntaps++;
        p = strchr(p, ';');
        if (p) p++;
    }
    /* NJ_KEEP_AWAKE (developer testing): no auto-lock while an automated run plays without touches; the game itself,
     * like the Android original, lets the phone lock */
    if (getenv("NJ_KEEP_AWAKE")) UIApplication.sharedApplication.idleTimerDisabled = YES;
    _link = [CADisplayLink displayLinkWithTarget:self selector:@selector(frame:)];
    _link.preferredFrameRateRange = CAFrameRateRangeMake(60, 60, 60);
    [_link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
}

- (void)frame:(CADisplayLink *)link {
    if (_paused || self.presentedViewController) return;
    [EAGLContext setCurrentContext:_ctx];
    for (int i = 0; i < _ntaps; i++) {
        if (_frameNo == _taps[i][0]) nj_touch(NJ_TOUCH_FIRST_DOWN, 0, (float)_taps[i][1], (float)_taps[i][2]);
        if (_frameNo == _taps[i][0] + 4) nj_touch(NJ_TOUCH_LAST_UP, 0, (float)_taps[i][1], (float)_taps[i][2]);
    }
    _frameNo++;
    CFAbsoluteTime t0 = CFAbsoluteTimeGetCurrent();
    glBindFramebuffer(GL_FRAMEBUFFER, _fbo);
    nj_gles_frame_begin();
    nj_frame();
    glBindRenderbuffer(GL_RENDERBUFFER, _color);
    [_ctx presentRenderbuffer:GL_RENDERBUFFER];
    CFAbsoluteTime t1 = CFAbsoluteTimeGetCurrent();
    _statBusy += t1 - t0;
    if (!_statT0) _statT0 = t1;
    if (_frameNo % 30 == 0 && !_fps.hidden) {
        if (_fpsT0) _fps.text = [NSString stringWithFormat:@"%.0f fps", 30 / (t1 - _fpsT0)];
        _fpsT0 = t1;
    }
    if (_frameNo % 300 == 0) {
        NSLog(@"[ninjago] %.1f fps, %.1f ms per frame (%dx%d)", 300 / (t1 - _statT0), _statBusy / 300 * 1000, _w, _h);
        _statT0 = t1;
        _statBusy = 0;
    }
}

- (void)pauseGame {
    if (!_booted || _paused) return;
    _paused = YES;
    NSLog(@"[ninjago] paused (app in the background)");
    [EAGLContext setCurrentContext:_ctx];
    nj_pause();
}

- (void)resumeGame {
    if (!_booted || !_paused) return;
    _paused = NO;
    NSLog(@"[ninjago] resumed");
    [EAGLContext setCurrentContext:_ctx];
    nj_resume();
    nj_ios_audio_resume();
}

/* ---- touches with Android pointer-id semantics ---- */

- (int)slotFor:(UITouch *)t create:(BOOL)create {
    NSUInteger i = [_touches indexOfObjectIdenticalTo:t];
    if (i != NSNotFound) return (int)i;
    if (!create) return -1;
    for (NSUInteger k = 0; k < _touches.count; k++)
        if (_touches[k] == (id)NSNull.null) { _touches[k] = t; return (int)k; }
    [_touches addObject:t];
    return (int)_touches.count - 1;
}

- (NSUInteger)activeCount {
    NSUInteger n = 0;
    for (id t in _touches) if (t != NSNull.null) n++;
    return n;
}

- (CGPoint)pixels:(UITouch *)t {
    CGPoint p = [t locationInView:self.view];
    CGFloat s = self.view.contentScaleFactor;
    p = CGPointMake(p.x * s, p.y * s);
    if (nj_gles_hud_scaled()) { /* the overlay was drawn scaled towards the centre: map back into game pixels */
        CGFloat k = nj_gles_hud_scale_factor(), cx = _w / 2.0, cy = _h / 2.0;
        p = CGPointMake(cx + (p.x - cx) / k, cy + (p.y - cy) / k);
    }
    return p;
}

/* Android's order (GameGLSurfaceView.onTouchEvent): first finger -> gesture start; a further finger -> down,
 * then a move for every finger; any movement -> a move for every finger; a finger lifting while others stay ->
 * a move for every finger, then up; the last finger -> gesture end. */
- (void)moveAll {
    for (NSUInteger k = 0; k < _touches.count; k++) {
        UITouch *t = _touches[k];
        if ((id)t == NSNull.null) continue;
        CGPoint p = [self pixels:t];
        nj_touch(NJ_TOUCH_MOVE, (int)k, (float)p.x, (float)p.y);
    }
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    if (!_booted) return;
    for (UITouch *t in touches) {
        BOOL first = [self activeCount] == 0;
        int pid = [self slotFor:t create:YES];
        CGPoint p = [self pixels:t];
        nj_touch(first ? NJ_TOUCH_FIRST_DOWN : NJ_TOUCH_POINTER_DOWN, pid, (float)p.x, (float)p.y);
        if (getenv("NJ_TOUCH_LOG")) NSLog(@"[ninjago] touch %s %d at %.0f,%.0f (overlay scaled %d)", first ? "first" : "pointer", pid, p.x, p.y, nj_gles_hud_scaled());
        if (!first) [self moveAll];
    }
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    if (!_booted) return;
    [self moveAll];
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    if (!_booted) return;
    for (UITouch *t in touches) {
        int pid = [self slotFor:t create:NO];
        if (pid < 0) continue;
        CGPoint p = [self pixels:t];
        if ([self activeCount] == 1) {
            nj_touch(NJ_TOUCH_LAST_UP, pid, (float)p.x, (float)p.y);
        } else {
            [self moveAll];
            nj_touch(NJ_TOUCH_POINTER_UP, pid, (float)p.x, (float)p.y);
        }
        _touches[(NSUInteger)pid] = (id)NSNull.null;
    }
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    if (!_booted) return;
    nj_touch(NJ_TOUCH_CANCEL, 0, 0, 0);
    [_touches removeAllObjects];
}
@end
