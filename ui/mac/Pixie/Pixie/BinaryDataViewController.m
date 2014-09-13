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
    } else {
        [self.textView setString:@""];
    }
}
- (IBAction)saveBody:(id)sender {
    NSSavePanel * savePanel = [NSSavePanel savePanel];
    // Restrict the file type to whatever you like
    //[savePanel setAllowedFileTypes:@[@"txt"]];
    // Set the starting directory
    //[savePanel setDirectoryURL:someURL];
    // Perform other setup
    // Use a completion handler -- this is a block which takes one argument
    // which corresponds to the button that was clicked
    [savePanel beginSheetModalForWindow:[[NSApplication sharedApplication] mainWindow]
                      completionHandler:^(NSInteger result){
        if (result == NSFileHandlingPanelOKButton) {
            NSURL* url = [savePanel URL];
            //NSLog(@"%s", [url fileSystemRepresentation]);
            proxyServerSaveBuffer(proxy,
                    [url fileSystemRepresentation], bodyBuffer);
        }
    }];
}
@end
