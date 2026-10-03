// Looking with the touch controls' right stick. Halo's own stick look has a
// response curve and an acceleration ramp built for a physical stick; a thumb
// on glass reaches full tilt at once, so turning went from barely moving to
// fast. While the touch controls are up, the right stick instead turns the
// view at a rate set here (port/linux/src/xinput_sdl.c adds it like mouse
// motion), with the speed, curve, invert and aim assist the player picks in
// the settings sheet (the gear button). Settings persist in NSUserDefaults.

#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import "HaloTouchControls.h"

HaloTouchControls *ios_touch_controls(void);   // ios_touch.m

#pragma mark - Settings

static NSString *const kLookSpeed = @"look.speed";
static NSString *const kLookCurve = @"look.curve";
static NSString *const kLookInvert = @"look.invert";
static NSString *const kAimAssist = @"look.aimAssist";

static const float kDefaultSpeed = 1.0f;     // multiplies the turn rates below
static const float kDefaultCurve = 1.5f;     // 1 = linear; higher = finer near center
static const float kYawRate = 3.4f;          // radians/s at full tilt, speed 1 (~195 deg/s)
static const float kPitchRate = 2.2f;        // radians/s at full tilt, speed 1
static const float kDeadZone = 0.06f;

static struct {
    BOOL loaded;
    float speed;
    float curve;
    BOOL invert;
    BOOL aimAssist;
} settings;

static void settings_load(void)
{
    NSUserDefaults *d = NSUserDefaults.standardUserDefaults;
    [d registerDefaults:@{ kLookSpeed: @(kDefaultSpeed), kLookCurve: @(kDefaultCurve),
                           kLookInvert: @NO, kAimAssist: @YES }];
    settings.speed = [d floatForKey:kLookSpeed];
    settings.curve = [d floatForKey:kLookCurve];
    settings.invert = [d boolForKey:kLookInvert];
    settings.aimAssist = [d boolForKey:kAimAssist];
    settings.loaded = YES;
}

#pragma mark - The game's side (xinput_sdl.c)

int ios_touch_look_active(void)
{
    return ios_touch_controls().isActive;
}

int ios_touch_aim_assist(void)
{
    if (!settings.loaded) settings_load();
    return settings.aimAssist;
}

// radians to turn since the last call; FALSE when the stick is centered
int ios_touch_look(float *yaw, float *pitch)
{
    static CFTimeInterval last;
    CFTimeInterval now = CACurrentMediaTime();
    float dt = last ? (float)(now - last) : 0.0f;

    last = now;
    *yaw = 0.0f;
    *pitch = 0.0f;
    if (!settings.loaded) settings_load();
    if (dt <= 0.0f || dt > 0.1f) dt = 1.0f / 30.0f;   // (a gap: one tick's worth)

    HaloTouchControls *controls = ios_touch_controls();
    GCExtendedGamepad *pad = controls.isActive ? controls.controller.extendedGamepad : nil;
    if (!pad) return 0;

    float x = pad.rightThumbstick.xAxis.value;
    float y = pad.rightThumbstick.yAxis.value;
    float magnitude = sqrtf(x * x + y * y);
    if (magnitude <= kDeadZone) return 0;

    // the shaped tilt, applied along the stick's direction
    float tilt = fminf((magnitude - kDeadZone) / (1.0f - kDeadZone), 1.0f);
    float shaped = powf(tilt, settings.curve) / magnitude;
    *yaw = -x * shaped * kYawRate * settings.speed * dt;
    *pitch = (settings.invert ? -y : y) * shaped * kPitchRate * settings.speed * dt;
    return 1;
}

#pragma mark - Settings sheet

@interface HaloLookSettingsViewController : UIViewController
@end

@implementation HaloLookSettingsViewController {
    UISlider *_speed;
    UISlider *_curve;
    UILabel *_speedValue;
    UILabel *_curveValue;
    UISwitch *_invert;
    UISwitch *_aimAssist;
}

static UILabel *label(NSString *text, UIFont *font, UIColor *color)
{
    UILabel *l = [[UILabel alloc] init];
    l.text = text;
    l.font = font;
    l.textColor = color;
    return l;
}

- (UIView *)sliderRow:(NSString *)title slider:(UISlider *)slider value:(UILabel *)value
{
    UILabel *name = label(title, [UIFont systemFontOfSize:17 weight:UIFontWeightSemibold], UIColor.labelColor);
    value.font = [UIFont monospacedDigitSystemFontOfSize:15 weight:UIFontWeightRegular];
    value.textColor = UIColor.secondaryLabelColor;
    UIStackView *top = [[UIStackView alloc] initWithArrangedSubviews:@[ name, value ]];
    top.distribution = UIStackViewDistributionEqualSpacing;
    UIStackView *row = [[UIStackView alloc] initWithArrangedSubviews:@[ top, slider ]];
    row.axis = UILayoutConstraintAxisVertical;
    row.spacing = 6;
    return row;
}

- (UIView *)switchRow:(NSString *)title detail:(NSString *)detail control:(UISwitch *)control
{
    UILabel *name = label(title, [UIFont systemFontOfSize:17 weight:UIFontWeightSemibold], UIColor.labelColor);
    UILabel *sub = label(detail, [UIFont systemFontOfSize:13], UIColor.secondaryLabelColor);
    sub.numberOfLines = 0;
    UIStackView *text = [[UIStackView alloc] initWithArrangedSubviews:@[ name, sub ]];
    text.axis = UILayoutConstraintAxisVertical;
    text.spacing = 2;
    UIStackView *row = [[UIStackView alloc] initWithArrangedSubviews:@[ text, control ]];
    row.alignment = UIStackViewAlignmentCenter;
    row.spacing = 12;
    return row;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    if (!settings.loaded) settings_load();
    self.view.backgroundColor = UIColor.systemBackgroundColor;
    self.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;

    _speed = [[UISlider alloc] init];
    _speed.minimumValue = 0.25f;
    _speed.maximumValue = 3.0f;
    _speed.value = settings.speed;
    _speedValue = [[UILabel alloc] init];
    [_speed addTarget:self action:@selector(changed) forControlEvents:UIControlEventValueChanged];

    _curve = [[UISlider alloc] init];
    _curve.minimumValue = 1.0f;
    _curve.maximumValue = 3.0f;
    _curve.value = settings.curve;
    _curveValue = [[UILabel alloc] init];
    [_curve addTarget:self action:@selector(changed) forControlEvents:UIControlEventValueChanged];

    _invert = [[UISwitch alloc] init];
    _invert.on = settings.invert;
    [_invert addTarget:self action:@selector(changed) forControlEvents:UIControlEventValueChanged];
    _aimAssist = [[UISwitch alloc] init];
    _aimAssist.on = settings.aimAssist;
    [_aimAssist addTarget:self action:@selector(changed) forControlEvents:UIControlEventValueChanged];

    UILabel *title = label(@"Touch controls", [UIFont systemFontOfSize:22 weight:UIFontWeightBold], UIColor.labelColor);
    UIButton *reset = [UIButton buttonWithType:UIButtonTypeSystem];
    [reset setTitle:@"Reset" forState:UIControlStateNormal];
    [reset addTarget:self action:@selector(reset) forControlEvents:UIControlEventTouchUpInside];
    UIButton *done = [UIButton buttonWithType:UIButtonTypeSystem];
    [done setTitle:@"Done" forState:UIControlStateNormal];
    done.titleLabel.font = [UIFont systemFontOfSize:17 weight:UIFontWeightSemibold];
    [done addTarget:self action:@selector(done) forControlEvents:UIControlEventTouchUpInside];
    UIStackView *buttons = [[UIStackView alloc] initWithArrangedSubviews:@[ reset, done ]];
    buttons.spacing = 20;
    UIStackView *header = [[UIStackView alloc] initWithArrangedSubviews:@[ title, buttons ]];
    header.distribution = UIStackViewDistributionEqualSpacing;
    header.alignment = UIStackViewAlignmentCenter;

    UIStackView *stack = [[UIStackView alloc] initWithArrangedSubviews:@[
        header,
        [self sliderRow:@"Look speed" slider:_speed value:_speedValue],
        [self sliderRow:@"Look curve" slider:_curve value:_curveValue],
        [self switchRow:@"Invert look" detail:@"Push up to look down." control:_invert],
        [self switchRow:@"Aim assist" detail:@"Halo's controller aim help: the view slows over targets."
                control:_aimAssist],
    ]];
    stack.axis = UILayoutConstraintAxisVertical;
    stack.spacing = 22;
    stack.translatesAutoresizingMaskIntoConstraints = NO;

    UIScrollView *scroll = [[UIScrollView alloc] init];
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    [scroll addSubview:stack];
    [self.view addSubview:scroll];
    UILayoutGuide *safe = self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [scroll.topAnchor constraintEqualToAnchor:safe.topAnchor],
        [scroll.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor],
        [scroll.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor],
        [scroll.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor],
        [stack.topAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.topAnchor constant:20],
        [stack.bottomAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.bottomAnchor constant:-20],
        [stack.leadingAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.leadingAnchor constant:24],
        [stack.trailingAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.trailingAnchor constant:-24],
    ]];
    [self showValues];
}

- (void)showValues
{
    _speedValue.text = [NSString stringWithFormat:@"%.2fx", _speed.value];
    float c = _curve.value;
    _curveValue.text = c < 1.15f ? @"linear" : [NSString stringWithFormat:@"%.1f  %@", c, c < 2.0f ? @"balanced" : @"precise"];
}

- (void)changed
{
    settings.speed = _speed.value;
    settings.curve = _curve.value;
    settings.invert = _invert.on;
    settings.aimAssist = _aimAssist.on;
    NSUserDefaults *d = NSUserDefaults.standardUserDefaults;
    [d setFloat:settings.speed forKey:kLookSpeed];
    [d setFloat:settings.curve forKey:kLookCurve];
    [d setBool:settings.invert forKey:kLookInvert];
    [d setBool:settings.aimAssist forKey:kAimAssist];
    [self showValues];
}

- (void)reset
{
    _speed.value = kDefaultSpeed;
    _curve.value = kDefaultCurve;
    _invert.on = NO;
    _aimAssist.on = YES;
    [self changed];
}

- (void)done
{
    [self dismissViewControllerAnimated:YES completion:nil];
}

@end

void ios_look_present_settings(UIViewController *presenter)
{
    if (!presenter || presenter.presentedViewController) return;
    HaloLookSettingsViewController *vc = [[HaloLookSettingsViewController alloc] init];
    vc.modalPresentationStyle = UIModalPresentationFormSheet;
    [presenter presentViewController:vc animated:YES completion:nil];
}
