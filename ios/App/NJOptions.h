/* The port's options, stored in NSUserDefaults and handed to the game (harness/cheats.h) on every change. */
#import <Foundation/Foundation.h>

#include "cheats.h"

/* Option keys; the BOOL ones are switches, NJOptionStudMultiplier is 0 (off), 1 (x2), 2 (x4), 3 (x6). */
extern NSString *const NJOptionAllCharacters, *const NJOptionAllLevels, *const NJOptionAllExtras,
    *const NJOptionAllSuperMoves, *const NJOptionStudMultiplier, *const NJOptionStudsFloor, *const NJOptionTokensFloor,
    *const NJOptionInvincible, *const NJOptionExtraHearts, *const NJOptionRegen, *const NJOptionDoubleDamage,
    *const NJOptionSafeArea, *const NJOptionShowFPS;

/* Posted after any option changed (object: the key). */
extern NSNotificationName const NJOptionsDidChange;

@interface NJOptions : NSObject
+ (void)registerDefaults;
+ (BOOL)boolForKey:(NSString *)key;
+ (NSInteger)integerForKey:(NSString *)key;
+ (void)setValue:(id)value forOption:(NSString *)key;
/* The game-side options as currently stored. */
+ (NjCheats)cheats;
@end
