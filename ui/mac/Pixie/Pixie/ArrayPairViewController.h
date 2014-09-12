//
//  ArrayPairViewController.h
//  Pixie
//
//  Created by BIBHAS BHATTACHARYA on 9/12/14.
//  Copyright (c) 2014 Vikingo Games. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import "Proxy.h"

@interface ArrayPairViewController : NSViewController <NSTableViewDataSource>
@property (strong) IBOutlet NSTableView *tableView;
@property (strong) NSMutableArray *names;
@property (strong) NSMutableArray *values;
- (void) setNames: (Array*) names values: (Array*) values;
@end
