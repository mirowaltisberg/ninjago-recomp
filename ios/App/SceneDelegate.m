#import "SceneDelegate.h"

#import "GameViewController.h"

@implementation SceneDelegate
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)opts {
    self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    self.window.rootViewController = [GameViewController new];
    [self.window makeKeyAndVisible];
}

- (void)sceneWillResignActive:(UIScene *)scene {
    [(GameViewController *)self.window.rootViewController pauseGame];
}

- (void)sceneDidBecomeActive:(UIScene *)scene {
    [(GameViewController *)self.window.rootViewController resumeGame];
}
@end
