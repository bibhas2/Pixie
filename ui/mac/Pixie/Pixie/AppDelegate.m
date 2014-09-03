//
//  AppDelegate.m
//  Pixie
//
//  Created by BIBHAS BHATTACHARYA on 9/3/14.
//  Copyright (c) 2014 Vikingo Games. All rights reserved.
//

#import "AppDelegate.h"
#import "HttpRequest.h"

@implementation AppDelegate

static void on_request_header_parsed(ProxyServer *p, Request *req) {
    AppDelegate *d = (AppDelegate *)[NSApp delegate];

    HttpRequest *r = [[HttpRequest alloc] initWithRequest:req];
    
    [d performSelectorOnMainThread:@selector(addRequest:) withObject:r waitUntilDone:FALSE];
}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
    self.requestList = [[NSMutableArray alloc] initWithCapacity:512];
    
    self->proxyServer = newProxyServer(9090);
    self->proxyServer->onRequestHeaderParsed = on_request_header_parsed;
    
    proxySetTrace(1);
    proxyServerStartInBackground(self->proxyServer);
}

- (void)applicationWillTerminate:(NSNotification *)notification {
    proxyServerStop(self->proxyServer);
    //Wait for the server to stop before freeing memory
    //deleteProxyServer(self->proxyServer);
}
- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView {
    return self.requestList.count;
}

- (void) addRequest: (HttpRequest*) r {
    [self.requestList addObject:r];
    [self.requestTableView reloadData];
}

- (id)tableView:(NSTableView *)aTableView objectValueForTableColumn:(NSTableColumn *)col row:(NSInteger)rowIndex
{
    // The return value is typed as (id) because it will return a string in most cases.
    id returnValue = nil;
    
    // The column identifier string is the easiest way to identify a table column.
    NSString *columnIdentifer = [col identifier];
    
    // Get the name at the specified row in namesArray
    HttpRequest *req = [self.requestList objectAtIndex:rowIndex];
    
    
    // Compare each column identifier and set the return value to
    // the Person field value appropriate for the column.
    if ([columnIdentifer isEqualToString:@"UNIQUE_ID"]) {
        returnValue = req.uniqueId;
    } else if ([columnIdentifer isEqualToString:@"METHOD"]) {
        returnValue = req.method;
    } else if ([columnIdentifer isEqualToString:@"HOST"]) {
        returnValue = req.host;
    } else if ([columnIdentifer isEqualToString:@"PATH"]) {
        returnValue = req.path;
    }
    
    return returnValue;
}

@end
