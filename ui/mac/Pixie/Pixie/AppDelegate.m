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
    
    NSLog(@"Request started: %@", r.uniqueId);

    [d performSelectorOnMainThread:@selector(addRequest:) withObject:r waitUntilDone:FALSE];
}

static void on_end_request(ProxyServer *p, Request *req) {
    AppDelegate *d = (AppDelegate *)[NSApp delegate];
    
    HttpRequest *r = [[HttpRequest alloc] initWithRequest:req];
    
    NSLog(@"Request ended: %@", r.uniqueId);
    
    [d performSelectorOnMainThread:@selector(updateRequest:) withObject:r waitUntilDone:FALSE];
}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
    self.requestList = [[NSMutableArray alloc] initWithCapacity:512];
    
    self->proxyServer = newProxyServer(9090);
    self->proxyServer->onRequestHeaderParsed = on_request_header_parsed;
    self->proxyServer->onEndRequest = on_end_request;
    self->proxyServer->persistenceEnabled = TRUE;
    
    self->requestRecord = newRequestRecord();
    self->responseRecord = newResponseRecord();
    
    //Manually update menu item states
    [[self.enableTraceMenuItem menu] setAutoenablesItems:NO];
     
    proxySetTrace(1);
    [self.enableTraceMenuItem setState:NSOnState];
    
    proxyServerStartInBackground(self->proxyServer);    
    [self.startServerMenuItem setEnabled:FALSE];
    
    self.rawReqTextCtrl = [[PlainTextViewController alloc] init];
    self.rawRequestTab.view = self.rawReqTextCtrl.view;
    
    self.rawResTextCtrl = [[PlainTextViewController alloc] init];
    self.rawResponseTab.view = self.rawResTextCtrl.view;
 }

- (void)applicationWillTerminate:(NSNotification *)notification {
    proxyServerStop(self->proxyServer);
    deleteRequestRecord(self->requestRecord);
    deleteResponseRecord(self->responseRecord);
    deleteProxyServer(self->proxyServer);
}
- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView {
    return self.requestList.count;
}

- (void) updateRequest: (HttpRequest*) r {
    //Search for the request by ID. Do this backwords for
    //faster search since only newer requests are updated.
    for (NSUInteger i = self.requestList.count; i > 0L; --i) {
        HttpRequest* item = [self.requestList objectAtIndex:i - 1];
        
        if ([item.uniqueId compare:r.uniqueId] == NSOrderedSame) {
            //Update the properties that might have changed.
            item.statusCode = r.statusCode;
            item.statusMessage = r.statusMessage;
            
            break; //End search
        }
    }
    [self.requestTableView reloadData];
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
    } else if ([columnIdentifer isEqualToString:@"STATUS"]) {
        returnValue = req.statusMessage;
    }
    
    return returnValue;
}

/*
 * Convert UTF-8 buffer to NSString
 */
static NSString *bufferToString(Buffer *buffer) {
    return [[NSString alloc] initWithBytes:buffer->buffer
                                    length:buffer->length
                                  encoding:NSUTF8StringEncoding];
}

- (void) showRequestDetails: (HttpRequest*) req {
    const char *uniqueId = [req.uniqueId cStringUsingEncoding:NSUTF8StringEncoding];
    int status = proxyServerLoadRequest(self->proxyServer,
                                        uniqueId,
                                        self->requestRecord);
    if (status < 0) {
        NSLog(@"Failed to load request.");
    }
    status = proxyServerLoadResponse(self->proxyServer,
                                     uniqueId,
                                     self->responseRecord);
    if (status < 0) {
        NSLog(@"Failed to load response.");
    }

    //Show the request
    [self.rawReqTextCtrl setBuffer: &(self->requestRecord->map)];

    //Show the response
    [self.rawResTextCtrl setBuffer: &(self->responseRecord->map)];
    
    //Free up resources that we don't need.
    proxyServerResetRecords(self->proxyServer, self->requestRecord, self->responseRecord);
}

- (void)tableViewSelectionDidChange:(NSNotification *) notification {
    long pos = [self.requestTableView selectedRow];
    
    if (pos < 0) {
        return;
    }
    
    HttpRequest *req = [self.requestList objectAtIndex:pos];
    
    [self showRequestDetails:req];
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

- (IBAction)deleteRequest:(id)sender {
    long pos = [self.requestTableView selectedRow];
    
    if (pos < 0) {
        return;
    }
    
    HttpRequest *req = [self.requestList objectAtIndex:pos];
    const char *uniqueId = [req.uniqueId cStringUsingEncoding:NSUTF8StringEncoding];
    proxyServerDeleteRecord(self->proxyServer, uniqueId);
    [self.requestList removeObjectAtIndex:pos];
    
    [self.requestTableView reloadData];

    //Clear request details
    [self clearRequestDetails];
}

- (IBAction)deleteAllRequests:(id)sender {
    //Delete all requests. Do this backwords for
    //faster deletion.
    for (NSUInteger i = self.requestList.count; i > 0L; --i) {
        HttpRequest* item = [self.requestList objectAtIndex:i - 1];
        const char *uniqueId = [item.uniqueId cStringUsingEncoding:NSUTF8StringEncoding];
        proxyServerDeleteRecord(self->proxyServer, uniqueId);
    }
    [self.requestList removeAllObjects];
    [self.requestTableView reloadData];
    
    //Clear request details
    [self clearRequestDetails];
}

- (void) clearRequestDetails {
    [self.rawReqTextCtrl setBuffer: NULL];
    [self.rawResTextCtrl setBuffer: NULL];
}
@end
