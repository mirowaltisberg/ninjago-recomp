/* Hosts the game: one ES2 context and layer-backed framebuffer, boots the guest like Android's GameActivity,
 * then drives GameGLSurfaceView.Renderer.onDrawFrame from a 60 Hz display link and forwards touches. */
#import "GameViewController.h"

#import <OpenGLES/ES2/glext.h>

#import "GameView.h"
#import "NJPlatformIOS.h"

#include "boot.h"

@implementation GameViewController {
    EAGLContext *_ctx;
    GLuint _fbo, _color, _depth;
    GLint _w, _h;
    CADisplayLink *_link;
    BOOL _booted, _paused;
    NSMutableArray<UITouch *> *_touches; /* index = Android pointer id; NSNull marks a free slot */
}

- (void)loadView {
    self.view = [[GameView alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.view.contentScaleFactor = UIScreen.mainScreen.nativeScale;
}

- (BOOL)prefersStatusBarHidden { return YES; }
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
- (UIInterfaceOrientationMask)supportedInterfaceOrientations { return UIInterfaceOrientationMaskLandscape; }

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    if (_booted || self.view.bounds.size.width < self.view.bounds.size.height) return; /* wait for landscape */
    [self createFramebuffer];
    [self boot];
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
    _link = [CADisplayLink displayLinkWithTarget:self selector:@selector(frame:)];
    _link.preferredFrameRateRange = CAFrameRateRangeMake(60, 60, 60);
    [_link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
}

- (void)frame:(CADisplayLink *)link {
    if (_paused) return;
    [EAGLContext setCurrentContext:_ctx];
    glBindFramebuffer(GL_FRAMEBUFFER, _fbo);
    nj_frame();
    glBindRenderbuffer(GL_RENDERBUFFER, _color);
    [_ctx presentRenderbuffer:GL_RENDERBUFFER];
}

- (void)pauseGame {
    if (!_booted || _paused) return;
    _paused = YES;
    [EAGLContext setCurrentContext:_ctx];
    nj_pause();
}

- (void)resumeGame {
    if (!_booted || !_paused) return;
    _paused = NO;
    [EAGLContext setCurrentContext:_ctx];
    nj_resume();
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
    return CGPointMake(p.x * s, p.y * s);
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    if (!_booted) return;
    for (UITouch *t in touches) {
        BOOL first = [self activeCount] == 0;
        int pid = [self slotFor:t create:YES];
        CGPoint p = [self pixels:t];
        nj_touch(first ? 0 : 4, pid, (float)p.x, (float)p.y);
    }
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    if (!_booted) return;
    for (UITouch *t in touches) {
        int pid = [self slotFor:t create:NO];
        if (pid < 0) continue;
        CGPoint p = [self pixels:t];
        nj_touch(1, pid, (float)p.x, (float)p.y);
    }
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    if (!_booted) return;
    for (UITouch *t in touches) {
        int pid = [self slotFor:t create:NO];
        if (pid < 0) continue;
        CGPoint p = [self pixels:t];
        BOOL last = [self activeCount] == 1;
        nj_touch(last ? 2 : 5, pid, (float)p.x, (float)p.y);
        _touches[(NSUInteger)pid] = (id)NSNull.null;
    }
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    if (!_booted) return;
    nj_touch(3, 0, 0, 0);
    [_touches removeAllObjects];
}
@end
