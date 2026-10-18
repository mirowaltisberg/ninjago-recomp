/* GL contexts in one sharegroup (the engine's loader thread gets its own), the layer FBO standing in for
 * Android's default framebuffer, and a RemoteIO audio unit pulling the software mixer. */
#import "NJPlatformIOS.h"

#import <AVFAudio/AVFAudio.h>
#import <AudioToolbox/AudioToolbox.h>

#include "nj/audio.h"
#include "nj/platform.h"

static EAGLContext *main_ctx;
static GLuint main_fbo;
static int surface_w, surface_h;
static NSMutableDictionary<NSNumber *, EAGLContext *> *contexts;
static int next_id = 2;

void nj_ios_set_main_context(EAGLContext *ctx, GLuint framebuffer, int width, int height) {
    @synchronized([EAGLContext class]) {
        if (!contexts) contexts = [NSMutableDictionary dictionary];
        main_ctx = ctx;
        main_fbo = framebuffer;
        surface_w = width;
        surface_h = height;
        contexts[@1] = ctx;
    }
}

int nj_platform_create_context(int share_id) {
    @synchronized([EAGLContext class]) {
        EAGLContext *base = contexts[@(share_id)] ?: main_ctx;
        EAGLContext *c = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2 sharegroup:base.sharegroup];
        if (!c) return 0;
        int id = next_id++;
        contexts[@(id)] = c;
        return id;
    }
}

int nj_platform_make_current(int ctx_id) {
    EAGLContext *c = nil;
    @synchronized([EAGLContext class]) { c = ctx_id ? contexts[@(ctx_id)] : nil; }
    if (ctx_id && !c) return 0;
    return [EAGLContext setCurrentContext:c] ? 1 : 0;
}

/* Framebuffer objects are not shared between contexts, so only the main context maps 0 to the layer FBO. */
unsigned nj_platform_default_framebuffer(void) { return [EAGLContext currentContext] == main_ctx ? main_fbo : 0; }

void nj_platform_surface_size(int *w, int *h) {
    *w = surface_w;
    *h = surface_h;
}

static OSStatus render(void *ref, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *ts, UInt32 bus,
                       UInt32 frames, AudioBufferList *io) {
    (void)ref; (void)flags; (void)ts; (void)bus;
    nj_audio_mix((float *)io->mBuffers[0].mData, frames);
    return noErr;
}

int nj_audio_output_start(void) {
    static dispatch_once_t once;
    static int rc = -1;
    dispatch_once(&once, ^{
        AVAudioSession *session = [AVAudioSession sharedInstance];
        [session setCategory:AVAudioSessionCategoryPlayback error:nil];
        [session setActive:YES error:nil];
        AudioComponentDescription d = {kAudioUnitType_Output, kAudioUnitSubType_RemoteIO, kAudioUnitManufacturer_Apple, 0, 0};
        AudioComponent comp = AudioComponentFindNext(NULL, &d);
        AudioUnit unit;
        if (!comp || AudioComponentInstanceNew(comp, &unit) != noErr) return;
        AudioStreamBasicDescription f = {0};
        f.mSampleRate = NJ_AUDIO_RATE;
        f.mFormatID = kAudioFormatLinearPCM;
        f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
        f.mChannelsPerFrame = 2;
        f.mBitsPerChannel = 32;
        f.mFramesPerPacket = 1;
        f.mBytesPerFrame = 8;
        f.mBytesPerPacket = 8;
        AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f);
        AURenderCallbackStruct cb = {render, NULL};
        AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb);
        if (AudioUnitInitialize(unit) == noErr && AudioOutputUnitStart(unit) == noErr) rc = 0;
    });
    return rc;
}
