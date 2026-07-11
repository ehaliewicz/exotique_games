#include "exotique.h"

#define WIDTH 1280
#define HEIGHT 736
const int kScreenWidth = WIDTH;
const int kScreenHeight = HEIGHT;

#define TILE_SIZE 32
#define TILES_WIDE (WIDTH/TILE_SIZE)
#define TILES_HIGH (HEIGHT/TILE_SIZE)

f32 zbuf[WIDTH*HEIGHT];


typedef struct {
    f32 x,y;
} vert2f;
typedef struct {
    f32 x,y,z;
} vert3f;
typedef struct {
    i32 x,y;
} vert2i;

typedef struct {
    float m[4][4];
} matrix;


typedef struct {
    vert3f position;
    vert3f rotation;
    vert3f scale;
} transform;

#define M_PI (3.141592)
#define M_PI_2 (M_PI/2.)
#define M_PI_M_2 (M_PI*2.0)

int compare_f32(double f1, double f2) {
    double precision = 0.00000000000000000001;
    if ((f1 - precision) < f2) {
        return -1;
    } else if ((f1 + precision) > f2) {
        return 1;
    } else {
        return 0;
    }
}

double cos(double x)
{
    while (x > M_PI)
        x -= 2.0 * M_PI;

    while (x < -M_PI)
        x += 2.0 * M_PI;

    int negate = 0;

    if (x > M_PI / 2)
    {
        x = M_PI - x;
        negate = 1;
    }
    else if (x < -M_PI / 2)
    {
        x = -M_PI - x;
        negate = 1;
    }

    double result =
        1.0 - (x*x/2.0) *
        (1.0 - (x*x/12.0) *
        (1.0 - (x*x/30.0)));

    return negate ? -result : result;
}


double sin(double x){
    return cos(x-M_PI_2);
}

f32 cosf(f32 x) {
    return (f32)cos((double)x);
}

f32 sinf(f32 x) {
    return (f32)sin((double)x);
}

int f32s_equal(f32 a, f32 b) {
    f32 df = a-b;
    if(df < 0.0f) { df = -df; }
    return (df <= 0.0001f);
}

f32 fabsf(f32 f) {
    if(f < 0.0f) { return -f; }
    return f;
}


f32 lerp(f32 a, f32 b, f32 mix) {
    return a + ((b-a) * mix);
}


f32 fast_atan2(f32 y, f32 x) {
    const f32 PI_4 = (f32)M_PI * 0.25f;
    const f32 PI_3_4 = (f32)M_PI * 0.75f;

    f32 abs_y = fabsf(y) + 1e-10f;
    f32 angle;

    if (x >= 0.0f)
    {
        f32 r = (x - abs_y) / (x + abs_y);
        angle = PI_4 - PI_4 * r;
    }
    else
    {
        f32 r = (x + abs_y) / (abs_y - x);
        angle = PI_3_4 - PI_4 * r;
    }

    return (y < 0.0f) ? -angle : angle;
}

static inline f32 fast_inv_sqrt( f32 number )
{
	long i;
	f32 x2, y;
	const f32 threehalfs = 1.5F;

	x2 = number * 0.5F;
	y  = number;
	i  = * ( long * ) &y;      
	i  = 0x5f3759df - ( i >> 1 );
	y  = * ( f32 * ) &i;
	y  = y * ( threehalfs - ( x2 * y * y ) ); 

	return y;
}

static inline f32 my_sqrt(f32 i) {
    return 1.0f / fast_inv_sqrt(i);
}


static inline f32 dot(vert3f a, vert3f b)
{
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

static inline vert3f normalize(vert3f v)
{
    f32 len = my_sqrt(dot(v,v));
    f32 recip_len = 1.0f / len;

    vert3f out = {
        v.x * recip_len,
        v.y * recip_len,
        v.z * recip_len
    };

    return out;
}


matrix mat_mul_mat(const matrix *a, const matrix *b) { 
    matrix r; 
    for (int i = 0; i < 4; ++i) { 
        for (int j = 0; j < 4; ++j) { 
         r.m[i][j] = (a->m[i][0] * b->m[0][j] + 
                      a->m[i][1] * b->m[1][j] + 
                      a->m[i][2] * b->m[2][j] + 
                      a->m[i][3] * b->m[3][j]);
        }
    } 
    return r; 
}

matrix transform_to_matrix(const transform *t) { 
    float sx = sinf(t->rotation.x); 
    float cx = cosf(t->rotation.x); 
    float sy = sinf(t->rotation.y);
    float cy = cosf(t->rotation.y); 
    matrix r = {0}; 
    r.m[0][0] = cy * t->scale.x; 
    r.m[0][1] = sy * sx * t->scale.y; 
    r.m[0][2] = sy * cx * t->scale.z; 
    r.m[0][3] = t->position.x; 
    r.m[1][0] = 0.0f; 
    r.m[1][1] = cx * t->scale.y; 
    r.m[1][2] = -sx * t->scale.z; 
    r.m[1][3] = t->position.y; 
    r.m[2][0] = -sy * t->scale.x; 
    r.m[2][1] = cy * sx * t->scale.y; 
    r.m[2][2] = cy * cx * t->scale.z; 
    r.m[2][3] = t->position.z; 
    r.m[3][0] = 0.0f; 
    r.m[3][1] = 0.0f; r.m[3][2] = 0.0f; 
    r.m[3][3] = 1.0f; 
    return r; 
}

matrix translation_matrix(f32 x, f32 y, f32 z) { 
    matrix r = {0}; 
    r.m[0][0] = 1.0f; 
    r.m[1][1] = 1.0f; 
    r.m[2][2] = 1.0f;
    r.m[3][3] = 1.0f; 
    r.m[0][3] = x; 
    r.m[1][3] = y;
    r.m[2][3] = z; 
    return r; 
} 

matrix rotation_x_matrix(f32 angle) { 
    f32 s = sinf(angle); 
    f32 c = cosf(angle); 
    matrix r = {0}; 
    r.m[0][0] = 1.0f; 
    r.m[1][1] = c; 
    r.m[1][2] = -s; 
    r.m[2][1] = s; 
    r.m[2][2] = c; 
    r.m[3][3] = 1.0f; 
    return r; 
} 

matrix rotation_y_matrix(f32 angle) { 
    f32 s = sinf(angle); 
    f32 c = cosf(angle); 
    matrix r = {0}; 
    r.m[0][0] = c; 
    r.m[0][2] = s; 
    r.m[1][1] = 1.0f; 
    r.m[2][0] = -s; 
    r.m[2][2] = c; 
    r.m[3][3] = 1.0f; 
    return r; 
}

matrix transform_to_view_matrix(const transform *cam)
{
    matrix t = translation_matrix(
        -cam->position.x,
        -cam->position.y,
        -cam->position.z);

    matrix rx = rotation_x_matrix(-cam->rotation.x);
    matrix ry = rotation_y_matrix(-cam->rotation.y);

    matrix r = mat_mul_mat(&rx, &ry);

    return mat_mul_mat(&r, &t);
}


vert3f mat_mul_vert3(const matrix *m, const vert3f *v) {
    vert3f r;

    r.x =
        m->m[0][0] * v->x +
        m->m[0][1] * v->y +
        m->m[0][2] * v->z +
        m->m[0][3];

    r.y =
        m->m[1][0] * v->x +
        m->m[1][1] * v->y +
        m->m[1][2] * v->z +
        m->m[1][3];

    r.z =
        m->m[2][0] * v->x +
        m->m[2][1] * v->y +
        m->m[2][2] * v->z +
        m->m[2][3];

    return r;
}

vert3f mat_mul_normal(const matrix *m, const vert3f *n) {
    vert3f r;
    r.x =
        m->m[0][0] * n->x +
        m->m[0][1] * n->y +
        m->m[0][2] * n->z;

    r.y =
        m->m[1][0] * n->x +
        m->m[1][1] * n->y +
        m->m[1][2] * n->z;

    r.z =
        m->m[2][0] * n->x +
        m->m[2][1] * n->y +
        m->m[2][2] * n->z;

    return normalize(r);
}




#define MAX_GLOBAL_TRIS 1000000
typedef struct {
    vert3f proj_v0, proj_v1, proj_v2;
    f32 c0, c1, c2;
    vert2f uv0, uv1, uv2;
    vert3f nv0, nv1, nv2;
    u8 tex;
    f32 inv_z0, inv_z1, inv_z2;
} transformed_tri;

#define MAX_TILE_TRIS 36000
typedef struct {
    i32 start_x; i32 start_y;
    u32 num_triangles;
    u32 tri_indexes[MAX_TILE_TRIS]; // up to 2048 triangles per tile
    f32 max_z, min_z;
    u8 z_dirty;
} tile;

tile tiles[TILES_WIDE*TILES_HIGH];

transformed_tri global_tri_buffer[MAX_GLOBAL_TRIS]; /* up to one million? */


typedef struct {
    vert3f pos;
    vert3f norm;
    vert2f uv;
} obj_vertex;

#define TEXTURE_WIDTH 512
#define TEXTURE_HEIGHT 512

#include "mesh_tile.h"
#include "mesh_board.h"
#include "palette_mahjong.h"
#include "palette_background.h"

#define BLACK 0
#define GREEN 1
#define GOLD 2
#define WHITE 3
#define RED 4

typedef struct {
    obj_vertex *vertexStream;
    u32 *indexStream; 

    int vertexCount;
    int indexCount;
} obj_mesh;

obj_mesh tile_mesh = {
    .vertexStream = tile_vertexes,
    .indexStream = tile_indexes,
    .vertexCount = (sizeof(tile_vertexes)/sizeof(obj_vertex)),
    .indexCount = (sizeof(tile_indexes)/sizeof(u32))
};

obj_mesh board_mesh = {
    .vertexStream = board_vertexes,
    .indexStream = board_indexes,
    .vertexCount = (sizeof(board_vertexes)/sizeof(obj_vertex)),
    .indexCount = (sizeof(board_indexes)/sizeof(u32))
};



#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define CLAMP(a, mi, ma) MIN(MAX(a, mi), ma)



f32 orient2d(vert2f *a, vert2f *b, vert2f *c) {
    return ((f32)b->x-(f32)a->x)*((f32)c->y-(f32)a->y) - ((f32)b->y-(f32)a->y)*((f32)c->x-(f32)a->x);
}

static inline int isTopLeft(vert3f *a, vert3f *b) {
    /* a, b are the two endpoints of a directed edge, walked in the
       triangle's winding order (e.g. called as isTopLeft(v1, v2)
       for the edge v1->v2) */
    i32 dx = (i32)(b->x - a->x);
    i32 dy = (i32)(b->y - a->y);
    return (dy == 0 && dx > 0) ||   // top edge
           (dy > 0);                // left edge (points downward, y+ is down)
}

static inline vert3f scale_vert3(vert3f a, f32 b) {
    return (vert3f){.x = a.x*b, .y = a.y*b, .z = a.z*b};
}
static inline vert3f add_vert3(vert3f a, vert3f b) {
    return (vert3f){.x = a.x+b.x, .y = a.y+b.y, .z = a.z+b.z};
}

static inline int fast_floor(f32 x)
{
    int i = (int)x;
    return i - (i > x);
}


static inline int fast_ceil(f32 x)
{
    int i = (int)x;
    return i + (i < x);
}


const f32 FAR_Z = 512.0f;
const f32 NEAR_Z = 0.1f;

int triangle_backfacing(
    vert3f *v0,
    vert3f *v1,
    vert3f *v2) {
    vert3f *tmp = v0;
    v0 = v1; v1 = tmp;


    // 28.4 fixed point
    const i32 X0 = (i32)(v0->x * 16.0f);
    const i32 Y0 = (i32)(v0->y * 16.0f);
    const i32 X1 = (i32)(v1->x * 16.0f);
    const i32 Y1 = (i32)(v1->y * 16.0f);
    const i32 X2 = (i32)(v2->x * 16.0f);
    const i32 Y2 = (i32)(v2->y * 16.0f);
    //
    // Edge deltas
    //
    const i32 dx01 = X0 - X1;
    const i32 dy01 = Y0 - Y1;
    const i32 dx20 = X2 - X0;
    const i32 dy20 = Y2 - Y0;
    //
    // Triangle area
    //
    i32 area =
        (dx01 * dy20 -
        dy01 * dx20);
    return (area <= 0);
}

f32 camera_rot_y = 3.14159f;
f32 camera_rot_x = 0.20f;
f32 camera_radius = 40.0f;

#define NUM_BASE_COLORS 5
#define NUM_SHADES 32
u8 light_remap_table[NUM_SHADES][NUM_BASE_COLORS];

vert3f light = {
    1.0f,
    1.0f,
    -.5f
};

// returns 1 if drew a pixel, 0 otherwise
int triangle_v2(
    ExotiqueInterface *ei,
    f32 *zbuffer,
    transformed_tri* tri_attributes,
    const u8* texture,
    i32 start_x, i32 end_x,
    i32 start_y, i32 end_y) {

    // swap everything for first two vertexes (actual vertex positions and attributes)
    
    f32 iz0 = tri_attributes->inv_z1;
    f32 iz1 = tri_attributes->inv_z0;
    f32 iz2 = tri_attributes->inv_z2;
    vert3f v0 = tri_attributes->proj_v1;
    vert3f v1 = tri_attributes->proj_v0;
    vert3f v2 = tri_attributes->proj_v2;
    vert2f uv0 = tri_attributes->uv1;
    vert2f uv1 = tri_attributes->uv0;
    vert2f uv2 = tri_attributes->uv2;
    f32 c0 = tri_attributes->c1;
    f32 c1 = tri_attributes->c0;
    f32 c2 = tri_attributes->c2;


    int drew_pixel = 0;


    f32 v0u_over_z = uv0.x * iz0;
    f32 v0v_over_z = uv0.y * iz0;

    f32 v1u_over_z = uv1.x * iz1;
    f32 v1v_over_z = uv1.y * iz1;

    f32 v2u_over_z = uv2.x * iz2;
    f32 v2v_over_z = uv2.y* iz2;


    // 28.4 fixed point
    const i32 x0 = (i32)(v0.x * 16.0f);
    const i32 y0 = (i32)(v0.y * 16.0f);
    const i32 x1 = (i32)(v1.x * 16.0f);
    const i32 y1 = (i32)(v1.y * 16.0f);
    const i32 x2 = (i32)(v2.x * 16.0f);
    const i32 y2 = (i32)(v2.y * 16.0f);
    //
    // Edge deltas
    //
    const i32 dx01 = x0 - x1;
    const i32 dy01 = y0 - y1;
    const i32 dx12 = x1 - x2;
    const i32 dy12 = y1 - y2;
    const i32 dx20 = x2 - x0;
    const i32 dy20 = y2 - y0;
    //
    // Triangle area
    //
    i32 area = (dx01 * dy20 - dy01 * dx20);

    
    // bounding box of triangle (not so good for larger triangles)
    i32 minx = MIN(x0, MIN(x1, x2));
    i32 maxx = MAX(x0, MAX(x1, x2));
    i32 miny = MIN(y0, MIN(y1, y2));
    i32 maxy = MAX(y0, MAX(y1, y2));
    minx = CLAMP((minx + 15) >> 4, start_x, end_x);
    maxx = CLAMP((maxx + 15) >> 4, start_x, end_x);
    miny = CLAMP((miny + 15) >> 4, start_y, end_y);
    maxy = CLAMP((maxy + 15) >> 4, start_y, end_y);

    // edge constants, used for incremental edge coverage calculation
    i32 e01 = dy01 * x0 - dx01 * y0;
    i32 e12 = dy12 * x1 - dx12 * y1;
    i32 e20 = dy20 * x2 - dx20 * y2;

    // copy the edge constants into separate variables, so that the fill rule nudge below doesn't affect attribute interpolation (although it would be minor)
    i32 c01 = e01;
    i32 c12 = e12;
    i32 c20 = e20;

    // top left fill rule
    // ensure that sample positions on a left or top edge are nudged over (to be covered).
    if (dy01 < 0 || (dy01 == 0 && dx01 > 0)) {
        c01++;
    }
    if (dy12 < 0 || (dy12 == 0 && dx12 > 0)) {
        c12++;
    }
    if (dy20 < 0 || (dy20 == 0 && dx20 > 0)) {
        c20++;
    }

    i32 startX = minx << 4;
    i32 startY = miny << 4;
    i32 cy01 = c01 + dx01 * startY - dy01 * startX;
    i32 cy12 = c12 + dx12 * startY - dy12 * startX;
    i32 cy20 = c20 + dx20 * startY - dy20 * startX;
    i32 ey01 = e01 + dx01 * startY - dy01 * startX;
    i32 ey12 = e12 + dx12 * startY - dy12 * startX;
    i32 ey20 = e20 + dx20 * startY - dy20 * startX;
    //
    // Attribute interpolation
    //
    for (i32 y = miny; y < maxy; y++)
    {
        i32 cx01 = cy01;
        i32 cx12 = cy12;
        i32 cx20 = cy20;
        i32 ex01 = ey01;
        i32 ex12 = ey12;
        i32 ex20 = ey20;
        u8 *row = &ei->screen[y * kScreenWidth + minx];
        f32 *zbuf_row = &zbuffer[y * kScreenWidth + minx];
        for (i32 x = minx; x < maxx; x++) {
            if (!((cx01 | cx12 | cx20)>>31)) { // check the sign bit

                // barycentric weights weights (scaled by area)
                f32 recip_area = 1.0f / (f32)area;
                f32 w0 = (f32)ex12;
                f32 w1 = (f32)ex20;
                f32 w2 = (f32)ex01;
                f32 inv_z = (w0 * iz0 + w1 * iz1 +  w2 * iz2) * recip_area;
                
                if(inv_z >= zbuf_row[x - minx]) {
                    f32 z = 1.0f / inv_z;
                        
                    f32 u_over_z = (w0 * v0u_over_z + w1 * v1u_over_z + w2 * v2u_over_z) * recip_area;
                    f32 v_over_z = (w0 * v0v_over_z + w1 * v1v_over_z + w2 * v2v_over_z) * recip_area;
                    f32 u = (u_over_z * z);
                    f32 v = (v_over_z * z);
                    
                    i32 int_u = (i32)fast_floor(u * (f32)TEXTURE_WIDTH);// & 1023;
                    i32 int_v = (i32)fast_floor(v * (f32)TEXTURE_HEIGHT);// & 1023;
                    int_u &= (TEXTURE_WIDTH-1);
                    int_v &= (TEXTURE_HEIGHT-1);
                    u8 tex_pal_idx = texture[((TEXTURE_HEIGHT-1)-int_v)*TEXTURE_WIDTH+int_u];

                    // linearly interpolate brightness calculated at each vertex
                    // c0/c1/c2 are brightness values
                    f32 diffuse =(w0 * c0 + w1 * c1 + w2 * c2) / (f32)area;

                    diffuse = CLAMP(diffuse, 0.0f, 1.0f);
                    u8 quantized_brightness = (u8)(diffuse * (NUM_SHADES-1));


                    
                    u8 pal_idx = light_remap_table[quantized_brightness][tex_pal_idx];

                    row[x - minx] = pal_idx;
                    zbuf_row[x - minx] = inv_z;
                    drew_pixel = 1;
                }
            }
            // step horizontal edge coverage
            cx01 -= dy01 << 4;
            cx12 -= dy12 << 4;
            cx20 -= dy20 << 4;
            ex01 -= dy01 << 4;
            ex12 -= dy12 << 4;
            ex20 -= dy20 << 4;
        }
        // step vertical edge coverage
        cy01 += dx01 << 4;
        cy12 += dx12 << 4;
        cy20 += dx20 << 4;
        ey01 += dx01 << 4;
        ey12 += dx12 << 4;
        ey20 += dx20 << 4;
    }
    return drew_pixel;
}



void triangle_old(
    ExotiqueInterface* ei, f32* zbuffer, 
    vert3f *v0, vert3f *v1, vert3f *v2, 
    u8 v0_color, u8 v1_color, u8 v2_color,
    i32 start_x, i32 end_x,
    i32 start_y, i32 end_y
) {
    int y,x;
    u8* screen_ptr;
    (void)zbuffer;
    /* 4-bit subpixel precision, with a top left edge fill rule */
    i32 subStep = 16;
    i32 subMask = subStep - 1;
    i32 subShift = 4;

    i32 y0 = (i32)fast_floor(v0->y*(f32)subStep);
    i32 y1 = (i32)fast_floor(v1->y*(f32)subStep);
    i32 y2 = (i32)fast_floor(v2->y*(f32)subStep);

    i32 x0 = (i32)fast_floor(v0->x*(f32)subStep);
    i32 x1 = (i32)fast_floor(v1->x*(f32)subStep);
    i32 x2 = (i32)fast_floor(v2->x*(f32)subStep);

    i32 fixed_area = ((x1-x0)*(y2-y0) - (y1-y0)*(x2-x0))>>subShift; /* scale down so it's just scaled by subStep rather than subStep*subStep */
    if(fixed_area <= 0) {
        return;
    }
    /* i32 recip_fixed_area = (1<<23) / fixed_area; */


    i32 min_x = MAX(start_x, (i32)MIN(x0, MIN(x1, x2)));
    i32 max_x = MIN(end_x*subStep-1,  (i32)MAX(x0, MAX(x1, x2)));
    i32 min_y = MAX(start_y, (i32)MIN(y0, MIN(y1, y2)));
    i32 max_y = MIN(end_y*subStep-1,  (i32)MAX(y0, MAX(y1, y2)));

    i32 v2x_min_v1x = x2-x1;
    i32 v0x_min_v2x = x0-x2;
    i32 v1x_min_v0x = x1-x0;
    i32 v2y_min_v1y = y2-y1;
    i32 v0y_min_v2y = y0-y2;
    i32 v1y_min_v0y = y1-y0;

    i32 bias0 = isTopLeft(v1, v2) ? 0 : -1;
    i32 bias1 = isTopLeft(v2, v0) ? 0 : -1;
    i32 bias2 = isTopLeft(v0, v1) ? 0 : -1;

    i32 y_portion_w0, y_portion_w1, y_portion_w2;
    i32 init_x_portion_w0, init_x_portion_w1, init_x_portion_w2;
    

    min_x = min_x & ~subMask;                 // floor
    max_x = (max_x + subMask) & ~subMask;     // ceil
    min_y = min_y & ~subMask;                 // floor
    max_y = (max_y + subMask) & ~subMask;     // ceil

    y_portion_w0 = (v2x_min_v1x*((min_y - y1)))>>subShift;
    y_portion_w1 = (v0x_min_v2x*((min_y - y2)))>>subShift;
    y_portion_w2 = (v1x_min_v0x*((min_y - y0)))>>subShift;

    init_x_portion_w0 = (v2y_min_v1y * ((min_x - x1)))>>subShift;
    init_x_portion_w1 = (v0y_min_v2y * ((min_x - x2)))>>subShift;
    init_x_portion_w2 = (v1y_min_v0y * ((min_x - x0)))>>subShift;


    for(y = min_y; y <= max_y; y += subStep) {
        u8 *row_ptr = &ei->screen[(y>>subShift)*kScreenWidth + (min_x>>subShift)];
        //f32 *zbuf_row = &zbuf[(y>>subShift)*kScreenWidth + (min_x>>subShift)];
        i32 x_portion_w0 = init_x_portion_w0;
        i32 x_portion_w1 = init_x_portion_w1;
        i32 x_portion_w2 = init_x_portion_w2;

        i32 w0 = bias0 + y_portion_w0 - x_portion_w0;
        i32 w1 = bias1 + y_portion_w1 - x_portion_w1;
        i32 w2 = bias2 + y_portion_w2 - x_portion_w2;
        u32 next_sign = ((u32)(w0|w1|w2))>>31;
        i32 w0_color = w0 * v0_color;
        i32 w1_color = w1 * v1_color;
        i32 w2_color = w2 * v2_color;

        for(x = min_x; x <= max_x; x += subStep) {


            if(!next_sign) { /* (w0|w1|w2) >= 0) { */
                //f32 prev_z = *zbuf_row;
                //if(prev_z < FAR_Z) {
                //}
                //*zbuf_row = 0.0f;
                i32 index = (( w0_color + w1_color + w2_color) / fixed_area);
                *row_ptr = (u8)index;
                /*ei->screen[(y>>subShift)*kScreenWidth+(x>>subShift)] = (u8)index;*/ /* (u8)((v0_color + v1_color + v2_color) / 3);*/
            }
            w0 -= v2y_min_v1y;
            w1 -= v0y_min_v2y;
            w2 -= v1y_min_v0y;
            next_sign = ((u32)(w0|w1|w2))>>31;
            w0_color -= v2y_min_v1y * v0_color;
            w1_color -= v0y_min_v2y * v1_color;
            w2_color -= v1y_min_v0y * v2_color;

            row_ptr++;
            //zbuf_row++;

        }
        y_portion_w0 += v2x_min_v1x;
        y_portion_w1 += v0x_min_v2x;
        y_portion_w2 += v1x_min_v0x;
        screen_ptr += kScreenWidth;
    }
}




typedef enum {
    NORMAL,
    TILE_DRAW,
    HI_Z_DRAW,
    NUM_DRAW_MODES
} draw_modes;
int last_x_pushed = 0, last_y_pushed = 0;
int hi_z_enabled = 1;
draw_modes draw_mode = 0;

transform cam_view_trans;


vert3f orbit_camera_position(float yaw, float pitch, float radius)
{
    vert3f p;

    float cp = cosf(pitch);
    float sp = sinf(pitch);

    float cy = cosf(yaw);
    float sy = sinf(yaw);

    p.x = sy * cp * radius;
    p.y = sp * radius;
    p.z = cy * cp * radius;

    return p;
}

void look_at_yx(transform *cam, vert3f position, vert3f target)
{
    float dx = target.x - position.x;
    float dy = target.y - position.y;
    float dz = target.z - position.z;

    cam->rotation.y = fast_atan2(dx, dz);

    float horizontal = my_sqrt(dx * dx + dz * dz);

    cam->rotation.x = -fast_atan2(dy, horizontal);

    cam->position = position;
}

void game_update(ExotiqueInterface* ei) {

    vert3f cam_pos = orbit_camera_position(camera_rot_y, camera_rot_x, camera_radius);
    look_at_yx(&cam_view_trans, cam_pos, (vert3f){0.0f,0.0f,0.0f});

    vert3f forward = {0, 0, -1};

    matrix rx = rotation_x_matrix(cam_view_trans.rotation.x);
    matrix ry = rotation_y_matrix(cam_view_trans.rotation.y);
    matrix rot = mat_mul_mat(&ry, &rx); // inverse of your view rotation

    light = normalize(mat_mul_vert3(&rot, &forward));

    if(ei->input->left) {
        camera_rot_y += 0.006f;
    } else if (ei->input->right) {
        camera_rot_y -= 0.006f;
    }

    if(ei->input->up) {
        camera_rot_x += 0.008f;
    } else if (ei->input->down) {
        camera_rot_x -= 0.008f;
    }
    camera_rot_x = CLAMP(camera_rot_x, -0.25f, 0.75f);


    if(ei->input->a) {
        camera_radius -= 0.02f;
    } else if (ei->input->b) {
        camera_radius += 0.02f;
    }
    if(!last_x_pushed) {
        if(ei->input->x) { 
            draw_mode++;
            if(draw_mode >= NUM_DRAW_MODES) {
                draw_mode = 0;
                hi_z_enabled = !hi_z_enabled;
            } 
        }
    }
    last_x_pushed = ei->input->x;
    last_y_pushed = ei->input->y;
    
}

u16 lfsr16(u16 *state)
{
    u16 bit = ((*state >> 14) ^ (*state >> 11) ^ (*state >> 7) ^ (*state >> 3)) & 1;
    *state = (u16)(*state << 1) | bit;
    return *state;
}

static u16 _rand_state = 0xA519;
u16 rand_word() {
    return lfsr16(&_rand_state);
}


void init_tiles() {
    for(int y = 0; y < TILES_HIGH; y++) {
        for(int x = 0; x < TILES_WIDE; x++) {
            tiles[y*TILES_WIDE+x].num_triangles = 0;
            tiles[y*TILES_WIDE+x].start_y = y * TILE_SIZE;
            tiles[y*TILES_WIDE+x].start_x = x * TILE_SIZE;
            tiles[y*TILES_WIDE+x].max_z = 1.0f/FAR_Z;
            tiles[y*TILES_WIDE+x].min_z = 1.0f/FAR_Z;
            tiles[y*TILES_WIDE+x].z_dirty = 0;
        }
    }
}

static u32 total_triangles;
void clear_tile_bins() {
    for(int y = 0; y < TILES_HIGH; y++) {
        for(int x = 0; x < TILES_WIDE; x++) {
            tiles[y*TILES_WIDE+x].num_triangles = 0;
        }
    }
}

void reset_tile_hi_z() {
    for(int y = 0; y < TILES_HIGH; y++) {
        for(int x = 0; x < TILES_WIDE; x++) {

            tiles[y*TILES_WIDE+x].max_z = 1.0f/FAR_Z;
            tiles[y*TILES_WIDE+x].min_z = 1.0f/FAR_Z;
            tiles[y*TILES_WIDE+x].z_dirty = 0;
        }
    }
}


void rebuild_hi_z() {
    for(int y = 0; y < TILES_HIGH; y++) {
        for(int x = 0; x < TILES_WIDE; x++) {
            int start_x = tiles[y*TILES_WIDE+x].start_x;
            int start_y = tiles[y*TILES_WIDE+x].start_y;
            if(tiles[y*TILES_WIDE+x].z_dirty == 0) {
                continue;
            }

            f32 furthest_inv_z = 1.0f;
            f32 closest_inv_z = 0.0f;
            for(int py = start_y; py < start_y+TILE_SIZE; py++) {
                for(int px = start_x; px < start_x+TILE_SIZE; px++) {
                    f32 pix_inv_z = zbuf[py*kScreenWidth+px];
                    if(pix_inv_z < furthest_inv_z) {
                        furthest_inv_z = pix_inv_z;
                    }
                    if(pix_inv_z > closest_inv_z) {
                        closest_inv_z = pix_inv_z;
                    }
                }
            }
            tiles[y*TILES_WIDE+x].max_z = furthest_inv_z;
            tiles[y*TILES_WIDE+x].min_z = closest_inv_z;
            tiles[y*TILES_WIDE+x].z_dirty = 0;
        }
    }
}


typedef struct {
    f32 min_x, max_x;
    f32 min_y, max_y;
    f32 min_z, max_z;
} bbox;

typedef struct {
    obj_mesh* mesh; 
    u8 texture;
    matrix model_to_view;
    matrix model_to_world;
    u8 shader;
} mesh_draw_call;



const f32 focal = 500.0f;
const f32 camx = (f32)(WIDTH/2.0f);
const f32 camy = (f32)(HEIGHT/2.0f);


vert3f project_coord(vert3f r) {
    return (vert3f){
            camx + focal * r.x / r.z,
            camy - focal * r.y / r.z,
            r.z
    };
}


typedef enum {
    ON_SCREEN,
    NEAR_CLIPPED,
    FAR_CLIPPED,
    OFF_SCREEN
} clip_res;


clip_res clip_bounding_box(mesh_draw_call* m, bbox* box) {
    vert3f verts[8] = {
        {.x = box->min_x, .y = box->min_y, .z = box->min_z},
        {.x = box->max_x, .y = box->min_y, .z = box->min_z},
        {.x = box->min_x, .y = box->max_y, .z = box->min_z},
        {.x = box->max_x, .y = box->max_y, .z = box->min_z},
        {.x = box->min_x, .y = box->min_y, .z = box->max_z},
        {.x = box->max_x, .y = box->min_y, .z = box->max_z},
        {.x = box->min_x, .y = box->max_y, .z = box->max_z},
        {.x = box->max_x, .y = box->max_y, .z = box->max_z}
    };
    
    for(int i = 0; i < 8; i++) {
        vert3f r = mat_mul_vert3(&m->model_to_view, &verts[i]);
        verts[i] = project_coord(r);
    }

    f32 min_x = verts[0].x;
    f32 max_x = verts[0].x;
    f32 min_y = verts[0].y;
    f32 max_y = verts[0].y;
    f32 min_z = verts[0].z;
    f32 max_z = verts[0].z;

    for(int i = 0; i < 8; i++) {
        min_x = MIN(min_x, verts[i].x);
        max_x = MAX(max_x, verts[i].x);
        min_y = MIN(min_y, verts[i].y);
        max_y = MAX(max_y, verts[i].y);
        min_z = MIN(min_z, verts[i].z);
        max_z = MAX(max_z, verts[i].z);
    }

    if(min_y >= HEIGHT || min_x >= WIDTH || max_x <= 0 || max_y <= 0) {
        // OFF SCREEN!
        return OFF_SCREEN;
    }

    if(min_z > FAR_Z) {
        // FAR CLIPPED
        return FAR_CLIPPED;
    }

    if(max_z < NEAR_Z) {
        // NEAR CLIPPED
        return NEAR_CLIPPED;
    }

    /*
    int i_min_x = fast_floor(min_x);
    int i_max_x = fast_ceil(max_x);
    int i_min_y = fast_floor(min_y);
    int i_max_y = fast_ceil(max_y);

    int tile_start_x = CLAMP(i_min_x / TILE_SIZE, 0, TILES_WIDE-1);
    int tile_end_x = CLAMP((i_max_x / TILE_SIZE), 0, TILES_WIDE-1);
    int tile_start_y = CLAMP(i_min_y / TILE_SIZE, 0, TILES_HIGH-1);
    int tile_end_y = CLAMP((i_max_y / TILE_SIZE), 0, TILES_HIGH-1);

    // invert the closest z 
    f32 closest_inv_z = 1.0f / min_z;

    for(int y = tile_start_y; y <= tile_end_y; y++) {
        for(int x = tile_start_x; x <= tile_end_x; x++) {
            // if it is too close to reject for ANY tile, return 0;
            if(closest_inv_z >= tiles[y*TILES_WIDE+x].max_z) {
                return 0;
            }
        }
    }
    */

    return ON_SCREEN;
}


bbox tile_bbox;

bbox get_mesh_bbox(obj_mesh *m) {
    bbox mesh_bbox;
    mesh_bbox.min_x = m->vertexStream[0].pos.x;
    mesh_bbox.min_y = m->vertexStream[0].pos.y;
    mesh_bbox.min_z = m->vertexStream[0].pos.z;
    mesh_bbox.max_x = m->vertexStream[0].pos.x;
    mesh_bbox.max_y = m->vertexStream[0].pos.y;
    mesh_bbox.max_z = m->vertexStream[0].pos.z;

    for(int v = 0; v < m->vertexCount; v++) {
        mesh_bbox.min_x = MIN(mesh_bbox.min_x, m->vertexStream[v].pos.x);
        mesh_bbox.min_y = MIN(mesh_bbox.min_y, m->vertexStream[v].pos.y);
        mesh_bbox.min_z = MIN(mesh_bbox.min_z, m->vertexStream[v].pos.z);
        mesh_bbox.max_x = MAX(mesh_bbox.max_x, m->vertexStream[v].pos.x);
        mesh_bbox.max_y = MAX(mesh_bbox.max_y, m->vertexStream[v].pos.y);
        mesh_bbox.max_z = MAX(mesh_bbox.max_z, m->vertexStream[v].pos.x);
    }
    return mesh_bbox;
}


static u32 rotl(const u32 x, i32 k)
{
  return (x << k) | (x >> (32 - k));
}

/* Completely arbitrary seeds */
static u32 s[4] = {0x27cb588d, 0x096379a9, 0xe81f5914, 0x2ee1c98c};

u32 nextrand(void)
{
  const u32 result = rotl(s[0] + s[3], 7) + s[0];

  const u32 t = s[1] << 9;

  s[2] ^= s[0];
  s[3] ^= s[1];
  s[1] ^= s[2];
  s[0] ^= s[3];

  s[2] ^= t;

  s[3] = rotl(s[3], 11);

  return result;
}


#include "texture_background.h"
#include "texture_green_dragon.h"
#include "texture_red_dragon.h"
#include "texture_white_dragon.h"
#include "texture_north.h"
#include "texture_east.h"
#include "texture_two_pin.h"
#include "texture_seven_man.h"
#include "texture_two_sou.h"
#include "texture_board.h"

typedef enum {
    WHITE_DRAGON,
    RED_DRAGON,
    GREEN_DRAGON,
    NORTH,
    EAST,
    TWO_PIN,
    SEVEN_MAN,
    TWO_SOU,
    NUM_TILES,
    BOARD
} tile_type;

const u8* textures[NUM_TILES+2] = {
    texture_white_dragon,
    texture_red_dragon,
    texture_green_dragon,
    texture_north,
    texture_east,
    texture_two_pin,
    texture_seven_man,
    texture_two_sou,
    texture_board,
    texture_board
};

typedef enum {
    LIT_TEXTURE,
    UNLIT_TEXTURE
} shader;




typedef struct {
    int num_closed_tiles;
    tile_type tiles[13];
    int num_open_tiles;
    int selected_tile_idx;
} hand;

typedef struct {
    int rem;
    tile_type tiles[136];
    int split_distance;

} dead_wall;

typedef struct {
    dead_wall deadwall;
    hand north_hand;
    hand east_hand;
    hand south_hand;
    hand west_hand;
} board;

board game_board;

// 17 tiles
//int split = [
//]

dead_wall init_deadwall() {
    dead_wall d;
    d.rem = 136;
    //int dice_total = (nextrand()%6)+(nextrand()%6);
    //int split_distance =
    d.split_distance = 0;

    for(int i = 0; i < 136; i++) {
        d.tiles[i] = nextrand()%NUM_TILES;
    }
    return d;
}

hand init_hand() {
    hand h;
    h.num_closed_tiles = 0;
    h.num_open_tiles = 0;
    return h;
}

hand init_random_hand() {
    hand h;
    h.num_open_tiles = (int)(nextrand()&1) ? 3 : 0; // 13-h.num_closed_tiles;
    h.num_closed_tiles = 13-h.num_open_tiles;
    
    for(int i = 0; i < 13; i++) {
        h.tiles[i] = nextrand()%NUM_TILES;
    }

    h.selected_tile_idx = (int)nextrand()%(h.num_closed_tiles);
    return h;
}

#


void game_load(ExotiqueInterface* ei) {
    int i;
    int last_used_pal_idx = 0;

    for(int shade = 0; shade < NUM_SHADES; shade++) {
        int base = 1 + shade*(NUM_BASE_COLORS-1);
        f32 scale = lerp(1.0f/(f32)NUM_SHADES, (f32)NUM_SHADES/(f32)NUM_SHADES, (f32)shade/(f32)NUM_SHADES);
        for(i = 0; i < NUM_BASE_COLORS; i++) {
            if(i == 0) {
                ei->palette[0] = 0x000000FF;
                light_remap_table[shade][i] = 0;
            } else {
                //int base = i*32;
                u32 rgba = (palette[i]<<8)|0xFF; 
                u8 br = (u8)(rgba >> 24);
                u8 bg = (u8)(rgba >> 16);
                u8 bb = (u8)(rgba >> 8);
                f32 r = ((f32)br)/255.0f;
                f32 g = ((f32)bg)/255.0f;
                f32 b = ((f32)bb)/255.0f;
                // shade=0 => 1/32
                // shade31 => 32/32

                f32 scaled_r = r * scale;
                f32 scaled_g = g * scale;
                f32 scaled_b = b * scale;
                u32 byte_r = (u32)CLAMP(scaled_r*255.0f, 0.0f, 255.0f);
                u32 byte_g = (u32)CLAMP(scaled_g*255.0f, 0.0f, 255.0f);
                u32 byte_b = (u32)CLAMP(scaled_b*255.0f, 0.0f, 255.0f);
                last_used_pal_idx = base+i;
                ei->palette[base+i] = (byte_r<<24)|(byte_g<<16)|(byte_b<<8)|0xFF;
                exotique_printf("%i\n", base+i);
                light_remap_table[shade][i] = (u8)(base+i);
            }
        }
        //(i<<24)|(i<<16)|(i<<8)|i;
        //ei->palette[i] = (i<<24)|(i<<16)|(i<<8)|i;
    }
    int num_bkgd_pal_entries = sizeof(palette_background)/sizeof(u32);

    for(i = 0; i < num_bkgd_pal_entries; i++) {
        u32 pal_entry = (palette_background[i]<<8)|0xFF;
        ei->palette[last_used_pal_idx+i+1] = pal_entry;
    }
    for(i = 0; i < BACKGROUND_TEX_HEIGHT*BACKGROUND_TEX_WIDTH; i++) {
        texture_background[i] = (u8) (texture_background[i] + (last_used_pal_idx+1));
    }
    init_tiles();

    tile_bbox = get_mesh_bbox(&tile_mesh);

    game_board.deadwall = init_deadwall();
    game_board.north_hand = init_random_hand();
    game_board.east_hand = init_random_hand();
    game_board.south_hand = init_random_hand();
    game_board.west_hand = init_random_hand();
    game_board.deadwall.rem -= (13*4);
}


void draw_tile(ExotiqueInterface *ei, f32 *zbuffer, tile* t) {
    (void)ei; (void)zbuffer;
    u32 i = 1;
    u32 num_tris = t->num_triangles;
    /*
    while(i < num_tris) {
        u32 j = i;
        while(j > 0 && z_greater(t->tri_indexes[j-1], t->tri_indexes[j])) {
            u32 tmp = t->tri_indexes[j-1];
            t->tri_indexes[j-1] = t->tri_indexes[j];
            t->tri_indexes[j] = tmp;
            j--;

        }
        i++;
    }
    */
    for(i = 0; i < num_tris; i++) {
        u32 global_tri_idx = t->tri_indexes[i];

        //t->z_dirty = t->z_dirty || 
        int drew_pix = triangle_v2(
            ei,
            zbuffer,
            &global_tri_buffer[global_tri_idx],
            textures[global_tri_buffer[global_tri_idx].tex],
            t->start_x, t->start_x+TILE_SIZE,
            t->start_y, t->start_y+TILE_SIZE
        );
        t->z_dirty = t->z_dirty || drew_pix;
    }
    if(draw_mode == TILE_DRAW) {
        for(int cnt = 0; cnt < TILE_SIZE; cnt++) {
            ei->screen[(t->start_y+cnt)*kScreenWidth + t->start_x] = 0;
            ei->screen[(t->start_y+cnt)*kScreenWidth + t->start_x+TILE_SIZE] = 0;
            ei->screen[t->start_y*kScreenWidth + t->start_x+cnt] = 0;
            ei->screen[(t->start_y+TILE_SIZE)*kScreenWidth + t->start_x+cnt] = 0;
        }
    }
}


void draw_tiles(ExotiqueInterface *ei, f32 *zbuffer) {
    //u64 tot_tris_in_tiles = 0;
    //(void)ei;
    //(void)zbuffer;
    //u32 max_tris_in_a_tile = 0;
    for(int y = 0; y < TILES_HIGH; y++) {
        for(int x = 0; x < TILES_WIDE; x++) {

            u32 num_tris = tiles[y*TILES_WIDE+x].num_triangles;
            //exotique_printf("tile %i,%i => tris: %i\n", x, y, num_tris);
            //tot_tris_in_tiles += num_tris;
            //max_tris_in_a_tile = MAX(num_tris, max_tris_in_a_tile);
            
            if(num_tris) {
                draw_tile(ei, zbuffer, &tiles[y*TILES_WIDE+x]);
            }
        }
    }
    //exotique_printf("avg tris per tile %f, total tris %i\n", (double)tot_tris_in_tiles / (double)(TILES_WIDE*TILES_HIGH), total_triangles);
    //exotique_printf("max tris in a tile %i\n", max_tris_in_a_tile);
}


int triangles_rasterized, triangles_hi_z_culled;
void bin_triangle(
    vert3f *v0,
    vert3f *v1,
    vert3f *v2,
    vert2f *v0_uv,
    vert2f *v1_uv,
    vert2f *v2_uv,
    vert3f *v0_norm,
    vert3f *v1_norm,
    vert3f *v2_norm,
    f32 c0,
    f32 c1,
    f32 c2,
    u8 texture) {

        if(total_triangles == MAX_GLOBAL_TRIS) {
            return;
        }

        f32 inv_z0 = 1.0f/v0->z;
        f32 inv_z1 = 1.0f/v1->z;
        f32 inv_z2 = 1.0f/v2->z;
        f32 closest_inv_z = MAX(inv_z0, MAX(inv_z1, inv_z2));




        f32 minx = MIN(v0->x, MIN(v1->x, v2->x));
        f32 maxx = MAX(v0->x, MAX(v1->x, v2->x));
        f32 miny = MIN(v0->y, MIN(v1->y, v2->y));
        f32 maxy = MAX(v0->y, MAX(v1->y, v2->y));

        i32 startX = CLAMP((int)fast_floor(minx), 0, WIDTH-1);
        i32 endX   = CLAMP((int)fast_ceil(maxx), 0, WIDTH-1);

        i32 startY = CLAMP((int)fast_floor(miny), 0, HEIGHT-1);
        i32 endY   = CLAMP((int)fast_ceil(maxy), 0, HEIGHT-1);

        int tile_start_x = startX / TILE_SIZE;
        int tile_start_y = startY / TILE_SIZE;
        int tile_end_x = endX / TILE_SIZE;
        int tile_end_y = endY / TILE_SIZE;

        int rasterized_at_least_once = 0;
        for(int y = tile_start_y; y <= tile_end_y; y++) {
            for(int x = tile_start_x; x <= tile_end_x; x++) {
                u32 num_tris_in_tile = tiles[y*TILES_WIDE+x].num_triangles;
                if(num_tris_in_tile == MAX_TILE_TRIS) {
                    continue;
                }
                // if our closest inv_z is further than this, it's HI-Z culled baby
                f32 this_tiles_furthest_inv_z = tiles[y*TILES_WIDE+x].max_z;
                // but since it's inv_z, the value gets smaller as it gets further away
                if(hi_z_enabled && closest_inv_z < this_tiles_furthest_inv_z) {
                    triangles_hi_z_culled++;
                    continue;
                }
                //triangles_rasterized++;
                rasterized_at_least_once = 1;


                tiles[y*TILES_WIDE+x].tri_indexes[num_tris_in_tile++] = total_triangles;
                tiles[y*TILES_WIDE+x].num_triangles = num_tris_in_tile;
            }
        }
        triangles_rasterized += rasterized_at_least_once;
        global_tri_buffer[total_triangles].proj_v0 = *v0;
        global_tri_buffer[total_triangles].proj_v1 = *v1;
        global_tri_buffer[total_triangles].proj_v2 = *v2;
        global_tri_buffer[total_triangles].c0 = c0;
        global_tri_buffer[total_triangles].c1 = c1;
        global_tri_buffer[total_triangles].c2 = c2;
        global_tri_buffer[total_triangles].uv0 = *v0_uv;
        global_tri_buffer[total_triangles].uv1 = *v1_uv;
        global_tri_buffer[total_triangles].uv2 = *v2_uv;
        global_tri_buffer[total_triangles].nv0 = *v0_norm;
        global_tri_buffer[total_triangles].nv1 = *v1_norm;
        global_tri_buffer[total_triangles].nv2 = *v2_norm;
        global_tri_buffer[total_triangles].inv_z0 = inv_z0;
        global_tri_buffer[total_triangles].inv_z1 = inv_z1;
        global_tri_buffer[total_triangles].inv_z2 = inv_z2;
        global_tri_buffer[total_triangles++].tex = texture;
}

int meshes_transformed = 0;
int triangles_transformed = 0;


void submit_mesh_draw_call(
    mesh_draw_call* mdc
) {
    obj_mesh *m = mdc->mesh;
    matrix *model_to_view = &mdc->model_to_view;
    matrix *model_to_world = &mdc->model_to_world;
    u8 texture = mdc->texture;

    meshes_transformed += 1;


    for (int i = 0; i < m->indexCount; i += 3) {

        obj_vertex ov0 = m->vertexStream[m->indexStream[i + 0]];
        obj_vertex ov1 = m->vertexStream[m->indexStream[i + 1]];
        obj_vertex ov2 = m->vertexStream[m->indexStream[i + 2]];

        vert3f v0 = ov0.pos;
        vert3f v1 = ov1.pos;
        vert3f v2 = ov2.pos;


        // Move in front of the camera.


        vert3f r0 = mat_mul_vert3(model_to_view, &v0);
        vert3f r1 = mat_mul_vert3(model_to_view, &v1);
        vert3f r2 = mat_mul_vert3(model_to_view, &v2);

        //
        // Reject triangles behind the camera.
        //

        if (r0.z <= NEAR_Z || r1.z <= NEAR_Z || r2.z <= NEAR_Z || r0.z >= FAR_Z || r1.z >= FAR_Z || r2.z >= FAR_Z) {
            continue;
        }

        vert3f s0 = project_coord(r0);
        vert3f s1 = project_coord(r1);
        vert3f s2 = project_coord(r2);
        if(triangle_backfacing(&s0, &s1, &s2)) {
            // do not submit backfacing triangles
            continue;
        }

        triangles_transformed += 1;

        vert3f n0 = ov0.norm;
        vert3f n1 = ov1.norm;
        vert3f n2 = ov2.norm;

        // Rotate normals into world space (not view)
        vert3f rn0 = mat_mul_normal(model_to_world, &n0);
        vert3f rn1 = mat_mul_normal(model_to_world, &n1);
        vert3f rn2 = mat_mul_normal(model_to_world, &n2);

        vert3f avg_norm = normalize(scale_vert3(add_vert3(add_vert3(n0, n1), n2), 1.0f/3.0f));
        f32 l0 = dot(normalize(rn0), light);
        f32 l1 = dot(normalize(rn1), light);
        f32 l2 = dot(normalize(rn2), light);

        
        float hemi = avg_norm.y * 0.5f + 0.5f;

        float ambient = lerp(0.15f, 0.35f, hemi);

        l0 = ambient + l0 * (1.0f - ambient);
        l1 = ambient + l1 * (1.0f - ambient);
        l2 = ambient + l2 * (1.0f - ambient);


        f32 c0 = CLAMP(l0, 0.0f, 1.0f);
        f32 c1 = CLAMP(l1, 0.0f, 1.0f);
        f32 c2 = CLAMP(l2, 0.0f, 1.0f);

        //
        // Perspective projection.
        //





        //
        // Draw.
        //
        bin_triangle(
            //ei,
            &s0, &s1, &s2,
            &ov0.uv, &ov1.uv, &ov2.uv,
            &rn0, &rn1, &rn2,
            c0,
            c1,
            c2,
            texture
        );
    }
}

void sort_draw_calls_near_to_far(mesh_draw_call *list, int num_meshes) {
    (void)list; (void)num_meshes;

    //for(int i = 0; i < num_meshes; i++) {
    //    vert3f model_pos = (vert3f){list[i].pos_x, list[i].pos_y, list[i].pos_z};
    //    vert3f trans_pos = transform_coord(model_pos, 0.0f, 0.0f, 0.0f, 
    //        sinf(list[i].angle_x), sinf(list[i].angle_y),
    //        cosf(list[i].angle_x), cosf(list[i].angle_y)
    //    );
    //    list[i].trans_centroid_z = trans_pos.z;
    //}

    /*
    int gaps[] = {701, 301, 132, 57, 23, 10, 4, 1};
    int num_gaps = sizeof(gaps)/sizeof(gaps[0]);
    for(int gi = 0; gi < num_gaps; gi++) {
        int gap = gaps[gi];
        for (int i = gap; i < num_meshes; ++i) {
            // save a[i] in temp and make a hole at position i
            mesh_draw_call temp = list[i];

            int j;
            // shift earlier gap-sorted elements up until the correct location for a[i] is found
            for (j = i; (j >= gap) && (list[j - gap].trans_centroid_z > temp.trans_centroid_z); j -= gap)
            {
                list[j] = list[j - gap];
            }
            // put temp (the original a[i]) in its correct location
            list[j] = temp;
        }
    }
    */
    exotique_printf("sorted\n");
    
}

typedef enum {
    NO_FRUSTUM_CULL,
    FRUSTUM_CULL
} culling_mode;

void submit_draw_calls(mesh_draw_call *list, int num_meshes, culling_mode frustum_cull_mode) {
    int meshes_clipped = 0;
    for(int i = 0; i < num_meshes; i++) {
        if(frustum_cull_mode == FRUSTUM_CULL) {
            clip_res clipped = clip_bounding_box(&list[i], &tile_bbox);
            if(clipped == FAR_CLIPPED || clipped == NEAR_CLIPPED || clipped == OFF_SCREEN) {
                meshes_clipped++;
                continue;
            }
        }
        submit_mesh_draw_call(&list[i]);
    }
    //exotique_printf("%i/%i meshes clipped\n", meshes_clipped, num_meshes);

    //exotique_printf("----------------DONE-------------------------\n");
}





transform identity_transform(void) {
    return (transform){
        .scale = {1.0f,1.0f,1.0f},
        .position = {0.0f,0.0f,0.0f},
        .rotation = {0.0f,0.0f,0.0f}
    };
}

int frame;


void draw_hand(hand* h, matrix* hand_to_view_matrix, matrix* hand_to_world_matrix) {

    mesh_draw_call draw_calls[13];

    f32 whole_hand_width = (f32)(13 * 2);
    f32 half = whole_hand_width / 2.0f;
    f32 position = half;


    int draw_idx = 0;

    for(int i = 0; i < h->num_closed_tiles + h->num_open_tiles; i++) {
        int is_open = (i >= h->num_closed_tiles);

        transform tile_trans = identity_transform();
        tile_trans.position.x = position;
        tile_trans.rotation.x = is_open ? 0.0f : 1.57f; // rotate back towards player
        tile_trans.position.y = is_open ? 0.0f : (h->selected_tile_idx == i ? 1.47f : 0.455f);
        matrix tile_mat = transform_to_matrix(&tile_trans);

        matrix tile_to_view_matrix = mat_mul_mat(hand_to_view_matrix, &tile_mat);
        matrix tile_to_world_matrix = mat_mul_mat(hand_to_world_matrix, &tile_mat);

        draw_calls[draw_idx].mesh = &tile_mesh;
        draw_calls[draw_idx].texture = h->tiles[i];
        draw_calls[draw_idx].model_to_view = tile_to_view_matrix;
        draw_calls[draw_idx].model_to_world = tile_to_world_matrix;
        position -= 2.0f;
        draw_idx++;
    }
    
    submit_draw_calls(draw_calls, draw_idx, FRUSTUM_CULL);
}

void draw_deadwall(dead_wall* d, matrix* view_mat) {

    f32 offset_x[4] = {16.0f, 0.0f, -16.0f, 0.0f};
    f32 offset_z[4] = {0.0f, 16.0f, 0.0, -16.0f};
    f32 rot_y[4] = {(f32)M_PI * 0.5f, 0.0f, -(f32)M_PI * 0.5f, (f32)M_PI};

    const f32 wall_tile_spacing = 1.61f;

    f32 wall_length = (f32)(17 * wall_tile_spacing);
    f32 half = wall_length / 2.0f;

    mesh_draw_call draw_calls[34*4];

    int draw_idx = 0;

    int inc = 1;
    for(int i = 0; i < 4; i++) {
        f32 position = half;


        transform wall_trans = identity_transform();
        wall_trans.position.x = offset_x[i];
        wall_trans.position.z = offset_z[i];
        wall_trans.rotation.y = rot_y[i];

        matrix wall_matrix = transform_to_matrix(&wall_trans);
        matrix wall_view_matrix = mat_mul_mat(view_mat, &wall_matrix);



        for(int j = 0; j < 34; j += inc) {
            int tot_tile_idx = i*34 + j;
            f32 this_row_position = position;
            position -= wall_tile_spacing;

            if(tot_tile_idx >= d->rem) {
                break;
            }

            tile_type bot_tile = d->tiles[tot_tile_idx];

            transform bot_tile_trans = identity_transform();
            bot_tile_trans.position.x = this_row_position;
            bot_tile_trans.rotation.x = (f32)M_PI;

            matrix bot_tile_mat = transform_to_matrix(&bot_tile_trans);
            matrix bot_tile_to_view_matrix = mat_mul_mat(&wall_view_matrix, &bot_tile_mat);
            matrix bot_tile_to_world_matrix = mat_mul_mat(&wall_matrix, &bot_tile_mat);

            draw_calls[draw_idx].mesh = &tile_mesh;
            draw_calls[draw_idx].model_to_view = bot_tile_to_view_matrix;
            draw_calls[draw_idx].model_to_world = bot_tile_to_world_matrix;
            draw_calls[draw_idx++].texture = bot_tile;

            if(inc == 1) {
                
                if(tot_tile_idx == 1) {
                    inc = 2;
                }
                continue;
            }

            if(tot_tile_idx+1 >= d->rem) {
                break;
            }
            
            tile_type top_tile = d->tiles[tot_tile_idx+1];
            transform top_tile_trans = identity_transform();
            top_tile_trans.rotation.x = tot_tile_idx+1 == 6 ? 0.0f : (f32)M_PI;
            top_tile_trans.position.x = this_row_position;
            top_tile_trans.position.y = 1.40f;
            matrix top_tile_mat = transform_to_matrix(&top_tile_trans);
            matrix top_tile_to_view_matrix = mat_mul_mat(&wall_view_matrix, &top_tile_mat);
            matrix top_tile_to_world_matrix = mat_mul_mat(&wall_matrix, &top_tile_mat);

            draw_calls[draw_idx].mesh = &tile_mesh;
            draw_calls[draw_idx].model_to_view = top_tile_to_view_matrix;
            draw_calls[draw_idx].model_to_world = top_tile_to_world_matrix;
            draw_calls[draw_idx++].texture = top_tile;

        }
    }

    submit_draw_calls(draw_calls, draw_idx, FRUSTUM_CULL);
}


void draw_board(ExotiqueInterface *ei, board* b, matrix* view_mat) {
    transform t_east = identity_transform(); t_east.rotation.y = (f32)M_PI * 0.5f; t_east.position.x = 28.0f; // push 10 units to the right
    transform t_south = identity_transform(); t_south.rotation.y = 0; t_south.position.z = 28.0f; // push 10 units down
    transform t_west = identity_transform(); t_west.rotation.y = -(f32)M_PI * 0.5f; t_west.position.x = -28.0f; // push 10 units left
    transform t_north = identity_transform(); t_north.rotation.y = (f32)M_PI; t_north.position.z = -28.0f;


    matrix east_matrix = transform_to_matrix(&t_east);
    matrix south_matrix = transform_to_matrix(&t_south);
    matrix west_matrix = transform_to_matrix(&t_west);
    matrix north_matrix = transform_to_matrix(&t_north);

    matrix east_view_matrix = mat_mul_mat(view_mat, &east_matrix);
    matrix south_view_matrix = mat_mul_mat(view_mat, &south_matrix);
    matrix west_view_matrix = mat_mul_mat(view_mat, &west_matrix);
    matrix north_view_matrix = mat_mul_mat(view_mat, &north_matrix);

    draw_hand(&b->east_hand, &east_view_matrix, &east_matrix);
    draw_hand(&b->south_hand, &south_view_matrix, &south_matrix);
    draw_hand(&b->west_hand, &west_view_matrix, &west_matrix);
    draw_hand(&b->north_hand, &north_view_matrix, &north_matrix);

    draw_deadwall(&b->deadwall, view_mat);


    mesh_draw_call draw_board_call;
    transform board_transform = identity_transform();
    board_transform.position.y = -0.73f;
    board_transform.scale.x = 32.0f;
    board_transform.scale.z = 32.0f;
    matrix board_matrix = transform_to_matrix(&board_transform);
    matrix board_to_view_matrix = mat_mul_mat(view_mat, &board_matrix);

    draw_board_call.mesh = &board_mesh;
    draw_board_call.model_to_view = board_to_view_matrix;
    draw_board_call.model_to_world = board_matrix;
    draw_board_call.texture = BOARD;

    submit_draw_calls(&draw_board_call, 1, NO_FRUSTUM_CULL);

    draw_tiles(ei, zbuf);
}





static u64 last_frame_ticks = 0;
void game_draw(ExotiqueInterface* ei) {
    frame++;
    u64 cur_frame_ticks = ei->ticks;

    exotique_printf("%llu ms\n", cur_frame_ticks - last_frame_ticks);
    last_frame_ticks = cur_frame_ticks;
    //u8 color = 0;
    for(int y = 0; y < kScreenHeight; y+=16) {
        for(int x = 0; x < kScreenWidth; x+=16) {
            //u8 col = light_remap_table[15][GREEN];//color++;

            for(int yy = y; yy < y+16; yy++) {
                f32 y_portion = (f32)yy / (f32)HEIGHT;
                int tex_y_coord = (int)(y_portion * (f32)BACKGROUND_TEX_HEIGHT);
                for(int xx = x; xx < x+16; xx++) {
                    f32 x_portion = (f32)xx/(f32)WIDTH;
                    int tex_x_coord = (int)(x_portion * (f32)BACKGROUND_TEX_WIDTH);
                    zbuf[yy*kScreenWidth+xx] = 1.0f/FAR_Z;
                    
                    ei->screen[yy*kScreenWidth+xx] = texture_background[tex_y_coord*BACKGROUND_TEX_WIDTH+tex_x_coord];

                }
            }
        }
    }
    if(hi_z_enabled) {
        reset_tile_hi_z();
    }




    matrix view_mat = transform_to_view_matrix(&cam_view_trans);


    total_triangles = 0;
    clear_tile_bins();
    draw_board(ei, &game_board, &view_mat);


    if(draw_mode == HI_Z_DRAW) {
        for(int y = 0; y < TILES_HIGH; y++) {
            for(int x = 0; x < TILES_WIDE; x++) {
                tile t = tiles[y*TILES_WIDE+x];
                int t_x = t.start_x;
                int t_y = t.start_y;
                f32 normalized = (1.0f/t.max_z)/FAR_Z;
                //float normalized =
                //    (t.max_z - 1.0f/FAR_Z) /
                //    (1.0f/NEAR_Z - 1.0f/FAR_Z);
                normalized = CLAMP(normalized, 0.0f, 1.0f);

                f32 quantized_depth = lerp(0.0f, (f32)NUM_SHADES, 1.0f-normalized);
                u8 brightness = (u8)CLAMP(quantized_depth, 0.0f, (f32)(NUM_SHADES-1));
                u8 idx = light_remap_table[brightness][WHITE];

                for(int py = 0; py < TILE_SIZE; py++) {
                    for(int px = 0; px < TILE_SIZE; px++) {
                        ei->screen[(t_y+py)*kScreenWidth + (t_x+px)] = idx;
                    }
                }


            }
        }
    }


    //exotique_printf("meshes %i\n", meshes_transformed);
    //exotique_printf("triangles transformed %i\n", triangles_transformed);
    //exotique_printf("triangles hi z culled %i\n", triangles_hi_z_culled);
    //exotique_printf("triangles rasterized %i\n", triangles_rasterized);
    //exotique_printf("hi_z_enabled %i\n", hi_z_enabled);
    meshes_transformed = 0;
    triangles_transformed = 0;
    triangles_rasterized = 0;
    triangles_hi_z_culled = 0;
}