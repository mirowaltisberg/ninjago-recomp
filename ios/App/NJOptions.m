#import "NJOptions.h"

NSString *const NJOptionAllCharacters = @"allCharacters";
NSString *const NJOptionAllLevels = @"allLevels";
NSString *const NJOptionAllExtras = @"allExtras";
NSString *const NJOptionAllSuperMoves = @"allSuperMoves";
NSString *const NJOptionStudMultiplier = @"studMultiplier";
NSString *const NJOptionStudsFloor = @"studsFloor";
NSString *const NJOptionTokensFloor = @"tokensFloor";
NSString *const NJOptionInvincible = @"invincible";
NSString *const NJOptionExtraHearts = @"extraHearts";
NSString *const NJOptionRegen = @"regen";
NSString *const NJOptionDoubleDamage = @"doubleDamage";
NSString *const NJOptionSafeArea = @"safeAreaControls";
NSString *const NJOptionShowFPS = @"showFPS";
NSNotificationName const NJOptionsDidChange = @"NJOptionsDidChange";

@implementation NJOptions

/* Every character unlocked and the controls inside the safe area are what the port did before it had options. */
+ (void)registerDefaults {
    [NSUserDefaults.standardUserDefaults registerDefaults:@{NJOptionAllCharacters : @YES, NJOptionSafeArea : @YES}];
}

+ (BOOL)boolForKey:(NSString *)key { return [NSUserDefaults.standardUserDefaults boolForKey:key]; }
+ (NSInteger)integerForKey:(NSString *)key { return [NSUserDefaults.standardUserDefaults integerForKey:key]; }

+ (void)setValue:(id)value forOption:(NSString *)key {
    [NSUserDefaults.standardUserDefaults setObject:value forKey:key];
    [NSNotificationCenter.defaultCenter postNotificationName:NJOptionsDidChange object:key];
}

+ (NjCheats)cheats {
    NSInteger m = [self integerForKey:NJOptionStudMultiplier];
    return (NjCheats){
        .all_characters = [self boolForKey:NJOptionAllCharacters],
        .all_levels = [self boolForKey:NJOptionAllLevels],
        .all_extras = [self boolForKey:NJOptionAllExtras],
        .all_super_moves = [self boolForKey:NJOptionAllSuperMoves],
        .stud_multiplier = m >= NJ_STUDS_X1 && m <= NJ_STUDS_X6 ? (NjStudMultiplier)m : NJ_STUDS_X1,
        .regen_x2 = [self boolForKey:NJOptionRegen],
        .extra_hearts = [self boolForKey:NJOptionExtraHearts],
        .double_damage = [self boolForKey:NJOptionDoubleDamage],
        .invincible = [self boolForKey:NJOptionInvincible],
        .studs_floor = [self boolForKey:NJOptionStudsFloor],
        .tokens_floor = [self boolForKey:NJOptionTokensFloor],
    };
}
@end
