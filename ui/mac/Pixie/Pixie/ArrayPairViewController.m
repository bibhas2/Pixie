//
//  ArrayPairViewController.m
//  Pixie
//
//  Created by BIBHAS BHATTACHARYA on 9/12/14.
//  Copyright (c) 2014 Vikingo Games. All rights reserved.
//

#import "ArrayPairViewController.h"

@interface ArrayPairViewController ()

@end

@implementation ArrayPairViewController

- (id)init
{
    self = [super initWithNibName:@"ArrayPairViewController" bundle:nil];
    if (self) {
        // Initialization code here.
        self.names = [[NSMutableArray alloc] init];
        self.values = [[NSMutableArray alloc] init];
    }
    return self;
}

static NSString *stringToString(String *str) {
    return [[NSString alloc] initWithBytes:str->buffer
                                    length:str->length
                                  encoding:NSUTF8StringEncoding];
}

- (void) setNames: (Array*) nameList values: (Array*) valueList {
    [self.names removeAllObjects];
    [self.values removeAllObjects];
    
    if (nameList == NULL || valueList == NULL) {
        //We are clearing data
        [self.tableView reloadData];

        return;
    }
    
    for (size_t i = 0; i < nameList->length; ++i) {
        NSString *name = stringToString(arrayGet(nameList, i));
        NSString *value = stringToString(arrayGet(valueList, i));
        
        [self.names addObject:name];
        [self.values addObject:value];
    }
    [self.tableView reloadData];
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView {
    return self.names.count;
}

- (id)tableView:(NSTableView *)aTableView objectValueForTableColumn:(NSTableColumn *)col row:(NSInteger)rowIndex
{
    // The return value is typed as (id) because it will return a string in most cases.
    id returnValue = nil;
    
    // The column identifier string is the easiest way to identify a table column.
    NSString *columnIdentifer = [col identifier];
    
    // Compare each column identifier and set the return value to
    // the Person field value appropriate for the column.
    if ([columnIdentifer isEqualToString:@"NAME"]) {
        returnValue = [self.names objectAtIndex:rowIndex];
    } else if ([columnIdentifer isEqualToString:@"VALUE"]) {
        returnValue = [self.values objectAtIndex:rowIndex];
    }
    
    return returnValue;
}
@end
