//
//  VertexData2.h
//  TrenchBroom
//
//  Created by Kristian Duske on 25.01.11.
//  Copyright 2011 __MyCompanyName__. All rights reserved.
//

#import <Cocoa/Cocoa.h>

typedef enum {
    BM_KEEP,
    BM_DROP,
    BM_SPLIT
} EBrushMark;

@class Face;
@class BoundingBox;
@class Ray3D;
@class PickingHit;

@interface VertexData : NSObject {
    @private
    NSMutableArray* vertices;
    NSMutableArray* edges;
    NSMutableArray* sides;
    NSMutableArray* sideToFace;
    NSMutableDictionary* faceToSide;
    BoundingBox* bounds;
}

- (id)initWithFaces:(NSArray *)faces droppedFaces:(NSMutableArray **)droppedFaces;

- (BOOL)cutWithFace:(Face *)face droppedFaces:(NSMutableArray **)droppedFaces;
- (NSArray *)verticesForFace:(Face *)face;
- (NSArray *)verticesForWireframe;
- (int)edgeCount;
- (BoundingBox *)bounds;
- (PickingHit *)pickFace:(Ray3D *)theRay;
@end
