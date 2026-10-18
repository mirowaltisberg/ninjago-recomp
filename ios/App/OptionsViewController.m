#import "OptionsViewController.h"

#import "NJOptions.h"

/* Gold of the game's own menu frames. */
static UIColor *Gold(void) { return [UIColor colorWithRed:0.96 green:0.76 blue:0.22 alpha:1]; }

@interface OptionRow : NSObject
@property (copy) NSString *key, *title, *detail, *symbol;
@property (copy) NSArray<NSString *> *choices; /* a segmented control instead of a switch */
@end
@implementation OptionRow
+ (instancetype)key:(NSString *)k title:(NSString *)t detail:(NSString *)d symbol:(NSString *)s {
    OptionRow *r = [OptionRow new];
    r.key = k;
    r.title = t;
    r.detail = d;
    r.symbol = s;
    return r;
}
@end

@interface OptionSection : NSObject
@property (copy) NSString *title, *footer;
@property (copy) NSArray<OptionRow *> *rows;
@end
@implementation OptionSection
+ (instancetype)title:(NSString *)t footer:(NSString *)f rows:(NSArray<OptionRow *> *)rows {
    OptionSection *s = [OptionSection new];
    s.title = t;
    s.footer = f;
    s.rows = rows;
    return s;
}
@end

@implementation OptionsViewController {
    NSArray<OptionSection *> *_sections;
}

- (instancetype)init {
    if (!(self = [super initWithStyle:UITableViewStyleInsetGrouped])) return nil;
    OptionRow *multiplier = [OptionRow key:NJOptionStudMultiplier title:@"Stud multiplier"
                                    detail:@"For every stud you collect" symbol:@"circle.circle.fill"];
    multiplier.choices = @[ @"Off", @"×2", @"×4", @"×6" ];
    NSString *version = [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"] ?: @"1.0";
    _sections = @[
        [OptionSection title:@"Unlock"
                      footer:@"These don't change your save: switch one off and the game shows your own progress "
                             @"again. Menus pick up a change the next time they open."
                        rows:@[
                            [OptionRow key:NJOptionAllCharacters title:@"All characters"
                                    detail:@"Every ninja and villain, unlocked and bought" symbol:@"person.3.fill"],
                            [OptionRow key:NJOptionAllLevels title:@"All levels"
                                    detail:@"Every tournament level in the level select" symbol:@"map.fill"],
                            [OptionRow key:NJOptionAllSuperMoves title:@"All super moves"
                                    detail:@"Every character's special attacks" symbol:@"bolt.fill"],
                            [OptionRow key:NJOptionAllExtras title:@"All red-brick extras"
                                    detail:@"Switch them on in the game's Extras menu" symbol:@"square.stack.3d.up.fill"],
                        ]],
        [OptionSection title:@"Studs"
                      footer:@"Top-ups are stored with your game the next time it saves."
                        rows:@[
                            multiplier,
                            [OptionRow key:NJOptionStudsFloor title:@"Never run out of studs"
                                    detail:@"Tops your studs up to 1,000,000" symbol:@"infinity"],
                            [OptionRow key:NJOptionTokensFloor title:@"Unlimited prize draws"
                                    detail:@"Always 10 tokens for the prize draw" symbol:@"gift.fill"],
                        ]],
        [OptionSection title:@"Easier fights"
                      footer:@"Handy for getting back into the game. Extra hearts apply from the next level you start."
                        rows:@[
                            [OptionRow key:NJOptionInvincible title:@"Invincible"
                                    detail:@"Lost health comes straight back" symbol:@"shield.fill"],
                            [OptionRow key:NJOptionExtraHearts title:@"Extra hearts"
                                    detail:@"Start each level with more hearts" symbol:@"heart.fill"],
                            [OptionRow key:NJOptionRegen title:@"Faster healing"
                                    detail:@"Health comes back twice as fast" symbol:@"cross.case.fill"],
                            [OptionRow key:NJOptionDoubleDamage title:@"Double damage"
                                    detail:@"Your attacks hit twice as hard" symbol:@"flame.fill"],
                        ]],
        [OptionSection title:@"Display"
                      footer:[NSString stringWithFormat:
                                  @"iOS port by Miró Waltisberg · version %@\nLEGO Ninjago Tournament 1.05.2 for "
                                  @"Android, statically recompiled from 32-bit ARM to native arm64. Not affiliated "
                                  @"with the LEGO Group or Warner Bros. Games.",
                                  version]
                        rows:@[
                            [OptionRow key:NJOptionSafeArea title:@"Controls inside the safe area"
                                    detail:@"Keeps buttons clear of the rounded corners" symbol:@"rectangle.inset.filled"],
                            [OptionRow key:NJOptionShowFPS title:@"Show frame rate" detail:nil symbol:@"speedometer"],
                        ]],
    ];
    return self;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"Options";
    self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone
                                                                                           target:self
                                                                                           action:@selector(done)];
    self.tableView.allowsSelection = NO;
}

- (void)done { [self dismissViewControllerAnimated:YES completion:nil]; }

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tv { return (NSInteger)_sections.count; }
- (NSInteger)tableView:(UITableView *)tv numberOfRowsInSection:(NSInteger)s { return (NSInteger)_sections[s].rows.count; }
- (NSString *)tableView:(UITableView *)tv titleForHeaderInSection:(NSInteger)s { return _sections[s].title; }
- (NSString *)tableView:(UITableView *)tv titleForFooterInSection:(NSInteger)s { return _sections[s].footer; }

- (UITableViewCell *)tableView:(UITableView *)tv cellForRowAtIndexPath:(NSIndexPath *)ip {
    OptionRow *row = _sections[ip.section].rows[ip.row];
    UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil];
    UIListContentConfiguration *c = [UIListContentConfiguration subtitleCellConfiguration];
    c.text = row.title;
    c.secondaryText = row.detail;
    c.secondaryTextProperties.color = UIColor.secondaryLabelColor;
    c.image = [UIImage systemImageNamed:row.symbol];
    c.imageProperties.tintColor = Gold();
    cell.contentConfiguration = c;
    if (row.choices) {
        UISegmentedControl *seg = [[UISegmentedControl alloc] initWithItems:row.choices];
        seg.selectedSegmentIndex = [NJOptions integerForKey:row.key];
        seg.selectedSegmentTintColor = Gold();
        [seg setTitleTextAttributes:@{NSForegroundColorAttributeName : UIColor.blackColor} forState:UIControlStateSelected];
        [seg addAction:[UIAction actionWithHandler:^(UIAction *a) {
                 [NJOptions setValue:@(((UISegmentedControl *)a.sender).selectedSegmentIndex) forOption:row.key];
             }]
            forControlEvents:UIControlEventValueChanged];
        cell.accessoryView = seg;
        [seg sizeToFit];
    } else {
        UISwitch *sw = [UISwitch new];
        sw.on = [NJOptions boolForKey:row.key];
        sw.onTintColor = Gold();
        [sw addAction:[UIAction actionWithHandler:^(UIAction *a) {
                [NJOptions setValue:@(((UISwitch *)a.sender).on) forOption:row.key];
            }]
            forControlEvents:UIControlEventValueChanged];
        cell.accessoryView = sw;
    }
    return cell;
}
@end
