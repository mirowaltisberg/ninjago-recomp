#import "AppDelegate.h"

#import "SceneDelegate.h"

@implementation AppDelegate
- (UISceneConfiguration *)application:(UIApplication *)app
    configurationForConnectingSceneSession:(UISceneSession *)session
                                   options:(UISceneConnectionOptions *)opts {
    UISceneConfiguration *cfg = [[UISceneConfiguration alloc] initWithName:@"Game" sessionRole:session.role];
    cfg.delegateClass = [SceneDelegate class];
    return cfg;
}
@end
