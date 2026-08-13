#ifndef MESH_QUAD_H
#define MESH_QUAD_H

const obj_vertex score_verts[8] = {
        {{.x = -0.5f, .y = 0.0f, .z = -0.5f}, {.x = 0.0f, .y = 1.0f, .z = 0.0f}, {.x = 0.0f, .y = 0.0f}}, 
        {{.x =  0.5f, .y = 0.0f, .z = -0.5f}, {.x = 0.0f, .y = 1.0f, .z = 0.0f}, {.x = 1.0f, .y = 0.0f}}, 
        {{.x = -0.5f, .y = 0.0f, .z =  0.5f}, {.x = 0.0f, .y = 1.0f, .z = 0.0f}, {.x = 0.0f, .y = 1.0f}}, 
        {{.x =  0.5f, .y = 0.0f, .z =  0.5f}, {.x = 0.0f, .y = 1.0f, .z = 0.0f}, {.x = 1.0f, .y = 1.0f}},
        {{.x = -0.5f, .y = 0.0f, .z = -0.5f}, {.x = 0.0f, .y = -1.0f, .z = 0.0f}, {.x = 0.0f, .y = 0.0f}}, 
        {{.x =  0.5f, .y = 0.0f, .z = -0.5f}, {.x = 0.0f, .y = -1.0f, .z = 0.0f}, {.x = 1.0f, .y = 0.0f}}, 
        {{.x = -0.5f, .y = 0.0f, .z =  0.5f}, {.x = 0.0f, .y = -1.0f, .z = 0.0f}, {.x = 0.0f, .y = 1.0f}}, 
        {{.x =  0.5f, .y = 0.0f, .z =  0.5f}, {.x = 0.0f, .y = -1.0f, .z = 0.0f}, {.x = 1.0f, .y = 1.0f}} 
};
const u16 score_indexes[12] = {
    0,2,1,
    1,2,3,
    4,5,6,
    5,7,6
};

const obj_mesh score_mesh = {
    .vertexStream = score_verts,
    .indexStream = score_indexes,
    .vertexCount = 4,
    .indexCount = 12
};

#endif 