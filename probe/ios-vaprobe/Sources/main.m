// Probes whether a 4 GiB PROT_NONE reservation works and whether guest-map offsets can be made writable.
#import <UIKit/UIKit.h>
#include <os/proc.h>
#include <sys/mman.h>

static NSString *RunProbe(void) {
    NSMutableString *out = [NSMutableString string];
    [out appendFormat:@"available memory: %zu MiB\n", os_proc_available_memory() >> 20];
    uint8_t *base = mmap(NULL, 1ull << 32, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (base == MAP_FAILED) {
        [out appendFormat:@"reserve 4GiB: FAILED errno=%d (%s)\n", errno, strerror(errno)];
        for (int gib = 3; gib >= 1; gib--) {
            void *p = mmap(NULL, (uint64_t)gib << 30, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
            [out appendFormat:@"reserve %dGiB: %s\n", gib, p == MAP_FAILED ? "FAILED" : "ok"];
            if (p != MAP_FAILED) { munmap(p, (uint64_t)gib << 30); break; }
        }
        return out;
    }
    [out appendFormat:@"reserve 4GiB: ok at %p\n", base];
    const uint32_t offsets[] = {0x00100000u, 0x02000000u, 0x80000000u, 0xE0000000u, 0xF0000000u, 0xFFFF0000u};
    for (size_t i = 0; i < sizeof offsets / sizeof offsets[0]; i++) {
        uint8_t *p = base + offsets[i];
        int rc = mprotect(p, 0x10000, PROT_READ | PROT_WRITE);
        if (rc == 0) { p[0] = 0x5A; p[0xFFFF] = 0xA5; }
        [out appendFormat:@"0x%08x: mprotect %s%s\n", offsets[i], rc == 0 ? "ok" : "FAILED",
                           rc == 0 ? (p[0] == 0x5A && p[0xFFFF] == 0xA5 ? ", write/read ok" : ", readback BAD") : ""];
    }
    // Touch 512 MiB of heap region to see whether committing real memory is allowed.
    uint8_t *heap = base + 0x02000000u;
    size_t big = 512u << 20;
    int rc = mprotect(heap, big, PROT_READ | PROT_WRITE);
    if (rc == 0) { for (size_t o = 0; o < big; o += 16384) heap[o] = 1; }
    [out appendFormat:@"commit 512MiB in heap: %s, available after: %zu MiB\n", rc == 0 ? "ok" : "FAILED",
                       os_proc_available_memory() >> 20];
    return out;
}

@interface SceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(strong, nonatomic) UIWindow *window;
@end

@implementation SceneDelegate
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)opts {
    NSString *result = RunProbe();
    NSLog(@"VAPROBE\n%@", result);
    NSString *docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    [result writeToFile:[docs stringByAppendingPathComponent:@"vaprobe.txt"] atomically:YES encoding:NSUTF8StringEncoding error:nil];
    self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    UIViewController *vc = [UIViewController new];
    UITextView *tv = [[UITextView alloc] initWithFrame:vc.view.bounds];
    tv.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    tv.font = [UIFont monospacedSystemFontOfSize:13 weight:UIFontWeightRegular];
    tv.text = result;
    tv.editable = NO;
    [vc.view addSubview:tv];
    self.window.rootViewController = vc;
    [self.window makeKeyAndVisible];
}
@end

@interface AppDelegate : UIResponder <UIApplicationDelegate>
@end

@implementation AppDelegate
- (UISceneConfiguration *)application:(UIApplication *)app
    configurationForConnectingSceneSession:(UISceneSession *)session
                                   options:(UISceneConnectionOptions *)opts {
    UISceneConfiguration *cfg = [[UISceneConfiguration alloc] initWithName:@"Default" sessionRole:session.role];
    cfg.delegateClass = [SceneDelegate class];
    return cfg;
}
@end

int main(int argc, char *argv[]) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class])); }
}
