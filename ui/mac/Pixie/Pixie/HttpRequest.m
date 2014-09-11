//
//  HttpRequest.m
//  Pixie
//
//  Created by BIBHAS BHATTACHARYA on 9/3/14.
//  Copyright (c) 2014 Vikingo Games. All rights reserved.
//

#import "HttpRequest.h"

@implementation HttpRequest
- (id) initWithRequest: (Request*) req {
    self = [self init];
    
    if (self != nil) {
        self.uniqueId = [NSString stringWithUTF8String:stringAsCString(req->uniqueId)];
        self.method = [NSString stringWithUTF8String:stringAsCString(req->method)];
        self.path = [NSString stringWithUTF8String:stringAsCString(req->path)];
        self.host = [NSString stringWithUTF8String:stringAsCString(req->host)];
        self.statusMessage = req->responseStatusMessage->length == 0 ? @"In progress" :
            [NSString stringWithUTF8String:stringAsCString(req->responseStatusMessage)];
        self.statusCode = [NSString stringWithUTF8String:stringAsCString(req->responseStatusCode)];
    }
    
    return self;
}
@end
