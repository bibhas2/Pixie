//
//  AppDelegate.h
//  Pixie
//
//  Created by BIBHAS BHATTACHARYA on 9/3/14.
//  Copyright (c) 2014 Vikingo Games. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import "Proxy.h"

@interface AppDelegate : NSObject <NSApplicationDelegate, NSTableViewDataSource> {
    ProxyServer *proxyServer;
}

@property (strong, nonatomic) NSMutableArray *requestList;
@property (assign) IBOutlet NSWindow *window;
@property (weak) IBOutlet NSTableView *requestTableView;
@property (unsafe_unretained) IBOutlet NSTextView *rawRequestText;
@property (unsafe_unretained) IBOutlet NSTextView *rawResponseText;

@end
