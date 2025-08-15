//
//  PPSSPPAboutViewController.m
//  PPSSPP
//
//  Created by Serena on 23/04/2023.
//

#import <Cocoa/Cocoa.h> // AppKit ftw
#import "PPSSPPAboutViewController.h"

#if !__has_feature(objc_arc)
#error Caveman detected (warning emoji) please enable arc with -fobjc-arc
#endif

extern const char *PPSSPP_GIT_VERSION;

@implementation PPSSPPAboutViewController
- (void)loadView {
    self.view = [[NSView alloc] init];
    [self.view setFrameSize:CGSizeMake(500, 180)];
}

- (void)viewDidLoad {
    [super viewDidLoad];
    
    NSImageView *imageView = [[NSImageView alloc] init];
    imageView.translatesAutoresizingMaskIntoConstraints = NO;
    imageView.image = [NSImage imageNamed:@"ppsspp.icns"];;
    [self.view addSubview:imageView];
    
    NSTextField *titleLabel = [NSTextField labelWithString:@"PPSSPP"];
    titleLabel.translatesAutoresizingMaskIntoConstraints = NO;
    titleLabel.font = [NSFont systemFontOfSize:38];
    [self.view addSubview:titleLabel];
    
    NSTextField *versionLabel = [NSTextField labelWithString:@(PPSSPP_GIT_VERSION)];
    versionLabel.translatesAutoresizingMaskIntoConstraints = NO;
    versionLabel.textColor = [NSColor secondaryLabelColor];
    versionLabel.selectable = YES; /* in case someone wants to copy the version */
    [self.view addSubview:versionLabel];
    
    NSTextField *descriptionLabel = [NSTextField wrappingLabelWithString:@"A PSP emulator for Android, Windows, Mac and Linux, written in C++"];
    descriptionLabel.translatesAutoresizingMaskIntoConstraints = NO;
    
    if (@available(macOS 11.0, *)) {
        descriptionLabel.font = [NSFont preferredFontForTextStyle:NSFontTextStyleSubheadline options:@{}];
    } else {
        descriptionLabel.font = [NSFont systemFontOfSize:10];
    }
    
    descriptionLabel.textColor = [NSColor colorWithRed:0.60 green:0.60 blue:0.60 alpha:1.0];
    [self.view addSubview:descriptionLabel];
    
    NSButton *sourceCodeButton = [NSButton buttonWithTitle:@"Source Code" target:self action:@selector(sourceCodeButtonTapped)];
    sourceCodeButton.bezelStyle = NSBezelStyleRounded;
    
    NSButton *discordButton = [NSButton buttonWithTitle:@"Join Discord" target:self action: @selector(joinDiscord)];
    discordButton.bezelStyle = NSBezelStyleRounded;
    
    NSStackView *stackView = [NSStackView stackViewWithViews:@[discordButton, sourceCodeButton]];
    stackView.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:stackView];
    
    [NSLayoutConstraint activateConstraints:@[
        [imageView.centerYAnchor constraintEqualToAnchor:self.view.centerYAnchor],
        [imageView.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:40],
        
        [titleLabel.leadingAnchor constraintEqualToAnchor:imageView.trailingAnchor constant:15],
        [titleLabel.centerYAnchor constraintEqualToAnchor:imageView.topAnchor constant:25],
        
        [versionLabel.leadingAnchor constraintEqualToAnchor:titleLabel.leadingAnchor],
        [versionLabel.centerYAnchor constraintEqualToAnchor:titleLabel.bottomAnchor constant:5],
        
        [descriptionLabel.leadingAnchor constraintEqualToAnchor:versionLabel.leadingAnchor],
        [descriptionLabel.trailingAnchor constraintLessThanOrEqualToAnchor: self.view.trailingAnchor],
        [descriptionLabel.centerYAnchor constraintEqualToAnchor:versionLabel.bottomAnchor constant:20],
        
        [stackView.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-26.4],
        [stackView.centerYAnchor constraintEqualToAnchor:self.view.bottomAnchor constant:-25]
    ]];
}

-(void)sourceCodeButtonTapped {
    [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"https://github.com/hrydgard/ppsspp"]];
}

-(void)joinDiscord {
    [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"https://discord.gg/5NJB6dD"]];
}

@end
