//
//  PlainTextViewController.h
//  Pixie
//
//  Created by BIBHAS BHATTACHARYA on 9/12/14.
//  Copyright (c) 2014 Vikingo Games. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import "Proxy.h"

@interface PlainTextViewController : NSViewController
@property (strong) IBOutlet NSTextView *textView;
- (void) setBuffer: (Buffer*) buff;
@end
