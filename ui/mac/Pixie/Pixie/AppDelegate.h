//
//  AppDelegate.h
//  Pixie
//
//  Created by BIBHAS BHATTACHARYA on 9/3/14.
//  Copyright (c) 2014 Vikingo Games. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import "Proxy.h"
#import "Persistence.h"
#import "PlainTextViewController.h"
#import "ArrayPairViewController.h"
#import "BinaryDataViewController.h"

@interface AppDelegate : NSObject <NSApplicationDelegate, NSTableViewDataSource, NSTableViewDelegate> {
    ProxyServer *proxyServer;
    RequestRecord *requestRecord;
    ResponseRecord *responseRecord;
}
@property (weak) IBOutlet NSTabViewItem *rawRequestTab;
@property (weak) IBOutlet NSTabViewItem *rawResponseTab;
@property (weak) IBOutlet NSTabViewItem *requestParamTab;

@property (strong, nonatomic) NSMutableArray *requestList;
@property (assign) IBOutlet NSWindow *window;
@property (weak) IBOutlet NSTableView *requestTableView;
@property (weak) IBOutlet NSMenuItem *stopServerMenuItem;
@property (weak) IBOutlet NSMenuItem *startServerMenuItem;
@property (weak) IBOutlet NSMenuItem *enableTraceMenuItem;
@property (strong) PlainTextViewController *rawReqTextCtrl;
@property (strong) PlainTextViewController *rawResTextCtrl;
@property (strong) ArrayPairViewController *requestParamCtrl;
@property (strong) BinaryDataViewController *rawResBinCtrl;

- (IBAction)stopServer:(id)sender;
- (IBAction)startServer:(id)sender;
- (IBAction)enableTrace:(id)sender;
- (IBAction)deleteRequest:(id)sender;
- (IBAction)deleteAllRequests:(id)sender;

@end
