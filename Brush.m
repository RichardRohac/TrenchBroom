//
//  Brush.m
//  TrenchBroom
//
//  Created by Kristian Duske on 30.01.10.
//  Copyright 2010 __MyCompanyName__. All rights reserved.
//

#import "Brush.h"
#import "Entity.h"
#import "IdGenerator.h"
#import "Vector3f.h"
#import "Vector3i.h"
#import "Face.h"
#import "HalfSpace3D.h"
#import "VertexData.h"
#import "BoundingBox.h"
#import "Ray3D.h"
#import "PickingHit.h"

NSString* const BrushFaceAdded              = @"BrushFaceAdded";
NSString* const BrushFaceRemoved            = @"BrushFaceRemoved";
NSString* const BrushFaceGeometryChanged    = @"BrushFaceGeometryChanged";
NSString* const BrushFacePropertiesChanged  = @"BrushFacePropertiesChanged";
NSString* const BrushFaceKey                = @"BrushFace";

@implementation Brush

- (id)init {
    if (self = [super init]) {
        brushId = [[IdGenerator sharedGenerator] getId];
        faces = [[NSMutableArray alloc] init];
        vertexData = [[VertexData alloc] init];
        
        flatColor[0] = (rand() % 255) / 255.0f;
        flatColor[1] = (rand() % 255) / 255.0f;
        flatColor[2] = (rand() % 255) / 255.0f;
    }
    
    return self;
}

- (id)initInEntity:(Entity *)theEntity {
    if (theEntity == nil)
        [NSException raise:NSInvalidArgumentException format:@"entity must not be nil"];
    
    if (self = [self init]) {
        entity = theEntity; // do not retain
    }
    
    return self;
}

- (VertexData *)vertexData {
    if (vertexData == nil) {
        NSMutableArray* droppedFaces = nil;
        vertexData = [[VertexData alloc] initWithFaces:faces droppedFaces:&droppedFaces];
        if (droppedFaces != nil)
            [faces removeObjectsInArray:droppedFaces];
    }
    
    return vertexData;
}

- (Face *)createFaceWithPoint1:(Vector3i *)point1 point2:(Vector3i *)point2 point3:(Vector3i *)point3 texture:(NSString *)texture {
    Face* face = [[Face alloc] initInBrush:self point1:point1 point2:point2 point3:point3 texture:texture];

    NSMutableArray* droppedFaces = nil;
    if (![[self vertexData] cutWithFace:face droppedFaces:&droppedFaces]) {
        NSLog(@"Brush %@ was cut away by face %@", self, face);
        [face release];
        return nil;
    }

    if (droppedFaces != nil) {
        NSEnumerator* droppedFacesEn = [droppedFaces objectEnumerator];
        Face* droppedFace;
        while ((droppedFace = [droppedFacesEn nextObject])) {
            NSLog(@"Face %@ was cut away by face %@", droppedFace, face);
            [faces removeObject:droppedFace];
        }
    }

    NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
    [center addObserver:self selector:@selector(faceGeometryChanged:) name:FaceGeometryChanged object:face];
    [center addObserver:self selector:@selector(facePropertiesChanged:) name:FacePropertiesChanged object:face];
    
    [faces addObject:face];

    if ([self postNotifications])
        [center postNotificationName:BrushFaceAdded
                              object:self 
                            userInfo:[NSDictionary dictionaryWithObject:face forKey:BrushFaceKey]];
    
    return [face autorelease];
}

- (void)faceGeometryChanged:(NSNotification *)notification {
    [vertexData release];
    vertexData = nil;
    
    if ([self postNotifications]) {
        NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
        [center postNotificationName:BrushFaceGeometryChanged 
                              object:self 
                            userInfo:[NSDictionary dictionaryWithObject:[notification object] forKey:BrushFaceKey]];
    }
}

- (void)facePropertiesChanged:(NSNotification *)notification {
    if ([self postNotifications]) {
        NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
        [center postNotificationName:BrushFacePropertiesChanged 
                              object:self 
                            userInfo:[NSDictionary dictionaryWithObject:[notification object] forKey:BrushFaceKey]];
    }
}

- (Entity *)entity {
    return entity;
}

- (NSNumber *)brushId {
    return brushId;
}
         
- (NSArray *)faces {
    return faces;
}

- (NSArray *)verticesForFace:(Face *)face {
    if (face == nil)
        [NSException raise:NSInvalidArgumentException format:@"face must not be nil"];

    return [vertexData verticesForFace:face];
}

- (int)edgeCount {
    return [vertexData edgeCount];
}

- (NSArray *)verticesForWireframe {
    return [vertexData verticesForWireframe];
}

- (float *)flatColor {
    return flatColor;
}

- (BoundingBox *)bounds {
    return [vertexData bounds];
}

- (PickingHit *)pickFace:(Ray3D *)theRay; {
    return [vertexData pickFace:theRay];
}

- (BOOL)postNotifications {
    return [entity postNotifications];
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [vertexData release];
    [faces release];
    [super dealloc];
}

@end
