//
//  Entity.h
//  TrenchBroom
//
//  Created by Kristian Duske on 28.02.11.
//  Copyright 2011 __MyCompanyName__. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import "Math.h"

@protocol Map;
@class VBOMemBlock;
@class EntityDefinition;
@class PickingHitList;

static NSString* const ClassnameKey = @"classname";
static NSString* const WorldspawnClassname = @"worldspawn";
static NSString* const OriginKey = @"origin";

@protocol Entity <NSObject>

- (NSNumber *)entityId;
- (id <Map>)map;

- (NSArray *)brushes;

- (NSString *)propertyForKey:(NSString *)key;
- (NSDictionary *)properties;

- (EntityDefinition *)entityDefinition;
- (BOOL)isWorldspawn;
- (NSString *)classname;

- (TBoundingBox *)bounds;
- (TVector3f *)center;
- (TVector3i *)origin;

- (void)pick:(TRay *)theRay hitList:(PickingHitList *)theHitList;

@end
