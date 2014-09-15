//
//  BinaryDataViewController.m
//  Pixie
//
//  Created by BIBHAS BHATTACHARYA on 9/12/14.
//  Copyright (c) 2014 Vikingo Games. All rights reserved.
//

#import "BinaryDataViewController.h"
#import "Persistence.h"

@interface BinaryDataViewController ()

@end

@implementation BinaryDataViewController

- (id)initWithProxyServer: (ProxyServer *) p
{
    self = [super initWithNibName:@"BinaryDataViewController" bundle:nil];
    if (self) {
        proxy = p;
    }
    return self;
}

- (void) setHeaderBuffer: (Buffer*) hBuff bodyBuffer: (Buffer*) bBuff {
    bodyBuffer = bBuff;
    headerBuffer = hBuff;
    
    if (headerBuffer != NULL) {
        [self.textView setString:[
                    [NSString alloc] initWithBytes:headerBuffer->buffer
                                            length:headerBuffer->length
                                          encoding:NSUTF8StringEncoding]];
        [self.textView scrollRangeToVisible: NSMakeRange(0, 0)]; //Scroll to top
    } else {
        [self.textView setString:@""];
    }
    [self.saveBtn setEnabled: bodyBuffer != NULL];
}
- (IBAction)saveBody:(id)sender {
    NSSavePanel * savePanel = [NSSavePanel savePanel];

    [savePanel beginSheetModalForWindow:[[NSApplication sharedApplication] mainWindow]
                      completionHandler:^(NSInteger result){
        if (result == NSFileHandlingPanelOKButton) {
            NSURL* url = [savePanel URL];
            proxyServerSaveBuffer(proxy,
                    [url fileSystemRepresentation], bodyBuffer);
        }
    }];
}
@end
