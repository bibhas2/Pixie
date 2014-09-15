//
//  PlainTextViewController.m
//  Pixie
//
//  Created by BIBHAS BHATTACHARYA on 9/12/14.
//  Copyright (c) 2014 Vikingo Games. All rights reserved.
//

#import "PlainTextViewController.h"

@interface PlainTextViewController ()

@end

@implementation PlainTextViewController

- (void)setBuffer:(Buffer *)buff
{
    NSString *str = @"";
    
    if (buff != NULL) {
        str = bufferToString(buff);
    }
    
    [self.textView setString: str];
    [self.textView scrollRangeToVisible: NSMakeRange(0, 0)]; //Scroll to top
}

static NSString *bufferToString(Buffer *buffer) {
    return [[NSString alloc] initWithBytes:buffer->buffer
                                    length:buffer->length
                                  encoding:NSUTF8StringEncoding];
}

- (void)awakeFromNib {
    //Disable wrapping
    NSTextView *theTextView = self.textView;
    [[theTextView enclosingScrollView] setHasHorizontalScroller:YES];
    [theTextView setHorizontallyResizable:YES];
    [theTextView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [[theTextView textContainer] setContainerSize:NSMakeSize(FLT_MAX, FLT_MAX)];
    [[theTextView textContainer] setWidthTracksTextView:NO];
}
@end
