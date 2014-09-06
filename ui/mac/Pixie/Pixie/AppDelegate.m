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
    self->proxyServer->persistenceEnabled = TRUE;
    
    [[self.enableTraceMenuItem menu] setAutoenablesItems:NO];
     
    proxySetTrace(1);
    [self.enableTraceMenuItem setState:NSOnState];
    
    proxyServerStartInBackground(self->proxyServer);    
    [self.startServerMenuItem setEnabled:FALSE];
    
    NSScrollView *textScrollView = [self.rawResponseText enclosingScrollView];
    NSTextContainer *textContainer = [self.rawResponseText textContainer];
    
    [textScrollView setHasHorizontalScroller:YES];
    [textContainer setContainerSize:NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX)];
    [textContainer setWidthTracksTextView: NO];
    [self.rawResponseText setHorizontallyResizable: YES];
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

- (void) showRequestDetails: (HttpRequest*) req {
    NSString *filePath, *contents;
    NSError *error;
    
    //Get the request file name
    filePath = [NSString stringWithFormat:@"%s/%@.req",
                stringAsCString(self->proxyServer->persistenceFolder),
                req.uniqueId];
    contents = [NSString stringWithContentsOfFile:filePath encoding: NSUTF8StringEncoding error:&error];
    if (error == NULL) {
        [self.rawRequestText setString:contents];
    } else {
        NSLog(@"Failed to load file: %@", filePath);
        [self.rawRequestText setString:@""];
    }

    //Get the response
    filePath = [NSString stringWithFormat:@"%s/%@.res",
                stringAsCString(self->proxyServer->persistenceFolder),
                req.uniqueId];
    contents = [NSString stringWithContentsOfFile:filePath encoding: NSUTF8StringEncoding error:&error];
    if (error == NULL) {
        [self.rawResponseText setString:contents];
    } else {
        NSLog(@"Failed to load file: %@", filePath);
        [self.rawResponseText setString: @""];
    }
}

- (void)tableViewSelectionDidChange:(NSNotification *) notification {
    long pos = [self.requestTableView selectedRow];
    
    if (pos < 0) {
        return;
    }
    
    HttpRequest *req = [self.requestList objectAtIndex:pos];
    
    [self showRequestDetails: req];
}

- (IBAction)stopServer:(id)sender {
    proxyServerStop(self->proxyServer);
    [self.startServerMenuItem setEnabled:TRUE];
    [self.stopServerMenuItem setEnabled:FALSE];
}

- (IBAction)startServer:(id)sender {
    proxyServerStartInBackground(self->proxyServer);
    [self.startServerMenuItem setEnabled:FALSE];
    [self.stopServerMenuItem setEnabled:TRUE];
}

- (IBAction)enableTrace:(id)sender {
    int newState = [self.enableTraceMenuItem state] == NSOnState ? NSOffState : NSOnState;
    
    proxySetTrace(newState == NSOnState);
    
    [self.enableTraceMenuItem setState:newState];
    
}
@end
