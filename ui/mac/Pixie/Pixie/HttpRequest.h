//
//  HttpRequest.h
//  Pixie
//
//  Created by BIBHAS BHATTACHARYA on 9/3/14.
//  Copyright (c) 2014 Vikingo Games. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "Proxy.h"

@interface HttpRequest : NSObject
@property (strong, nonatomic) NSString* uniqueId;
@property (strong, nonatomic) NSString* method;
@property (strong, nonatomic) NSString* host;
@property (strong, nonatomic) NSString* path;
@property (strong, nonatomic) NSString* status;

- (id) initWithRequest: (Request*) req;
@end
