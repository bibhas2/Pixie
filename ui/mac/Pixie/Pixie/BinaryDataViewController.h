//
//  BinaryDataViewController.h
//  Pixie
//
//  Created by BIBHAS BHATTACHARYA on 9/12/14.
//  Copyright (c) 2014 Vikingo Games. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import "Proxy.h"

@interface BinaryDataViewController : NSViewController {
    ProxyServer *proxy;
    Buffer *headerBuffer;
    Buffer *bodyBuffer;
}

@property (strong) IBOutlet NSTextView *textView;
- (id)initWithProxyServer: (ProxyServer *) p;
- (IBAction)saveBody:(id)sender;
- (void) setHeaderBuffer: (Buffer*) hBuff bodyBuffer: (Buffer*) bBuff;
@end
