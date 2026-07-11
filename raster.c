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

#define MAX_GLOBAL_TRIS 1000000
typedef struct {
    vert3f proj_v0, proj_v1, proj_v2; /* z is still rotated world space */
    //vert3f world_v0, world_v1, world_v2;
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

#include "mesh_mahjong.h"
#include "palette_mahjong.h"
#include "texture_green_dragon.h"
#include "texture_red_dragon.h"
#include "texture_white_dragon.h"
#include "texture_east.h"
#include "texture_two_pin.h"

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

#define num_vertexes (sizeof(vertexes)/sizeof(obj_vertex))
#define num_indexes (sizeof(indexes)/sizeof(u32))
obj_mesh tile_mesh = {
    .vertexStream = vertexes,
    .indexStream = indexes,
    .vertexCount = num_vertexes,
    .indexCount = num_indexes
};


#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define CLAMP(a, mi, ma) MIN(MAX(a, mi), ma)

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


vert3f rotate_xy(vert3f v, f32 cx, f32 sx, f32 cy, f32 sy) {
    // X rotation
    f32 y1 = cx * v.y - sx * v.z;
    f32 z1 = sx * v.y + cx * v.z;
    f32 x1 = v.x;
    //z1 = v.z;
    //y1 = v.y;

    // Y rotation
    vert3f r;

    r.x = cy * x1 + sy * z1;
    r.y = y1;
    r.z = -sy * x1 + cy * z1;

    return r;
}

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


const f32 FAR_Z = 256.0f;
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
    const i32 DX01 = X0 - X1;
    const i32 DY01 = Y0 - Y1;
    const i32 DX20 = X2 - X0;
    const i32 DY20 = Y2 - Y0;
    //
    // Triangle area
    //
    i32 area =
        (DX01 * DY20 -
        DY01 * DX20);
    return (area <= 0);
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

const f32 PI = 3.14159265358979323846f;
f32 fast_atan2(f32 y, f32 x) {
    const f32 PI_4 = PI * 0.25f;
    const f32 PI_3_4 = PI * 0.75f;

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

static inline f32 faster_asin(f32 x) {
    f32 x2 = x * x;
    return x * (1.5707963f + x2 * (-0.2145988f + x2 * 0.0889789f));
}

f32 fast_asin(f32 x) {

    f32 ax = fabsf(x);

    // Polynomial approximation
    f32 a = 
        -0.0187293f * ax +
         0.0742610f;

    a = a * ax - 0.2121144f;
    a = a * ax + 1.5707288f;

    a *= my_sqrt(1.0f - ax);

    return (x >= 0.0f)
        ? PI * 0.5f - a
        : a - PI * 0.5f;
}


f32 lerp(f32 a, f32 b, f32 mix) {
    return a + ((b-a) * mix);
}

f32 rotY = 3.14159f;
f32 rotX = 0.0f;
f32 camera_distance = 2.5f;

int smooth_shading_enabled = 1;

#define NUM_BASE_COLORS 5
#define NUM_SHADES 63
u8 light_remap_table[NUM_SHADES][NUM_BASE_COLORS];

vert3f light = {
    1.0f,
    1.0f,
    -.5f
};

f32 perturb_norm[25] = {
    // BLK BLK
    0.0f,
    // BLK GRN
    0.0f,
    // BLK GOLD
    0.0f,
    // BLK WHITE
    0.0f,
    // BLK RED
    0.0f,
    // GRN BLK
    0.0f,
    // GRN GRN
    0.0f,
    // GRN GOLD
    0.0f,
    // GRN WHITE
    1.0f,
    // GRN RED
    0.0f,
    // GOLD BLK
    0.0f,
    // GOLD GRN
    0.0f,
    // GOLD GOLD
    0.0f,
    // GOLD WHITE
    0.0f,
    // GOLD RED
    0.0f,
    // WHITE BLK
    0.0f,
    // WHITE GRN
    0.0f,
    // WHITE GOLD
    0.0f,
    // WHITE WHITE
    0.0f,
    // WHITE RED
    0.0f,
    // RED BLK
    0.0f,
    // RED GRN
    0.0f,
    // RED GOLD
    0.0f,
    // RED WHITE
    1.0f,
    // RED RED
    0.0f,

};

// returns 1 if drew a pixel, 0 otherwise
int triangle_v2(
    ExotiqueInterface *ei,
    f32 *zbuffer,
    vert3f *v0, vert3f *v1, vert3f *v2,
    f32 iz0, f32 iz1, f32 iz2,
    vert2f *uv0, vert2f *uv1, vert2f *uv2,
    vert3f *nv0, vert3f *nv1, vert3f *nv2,
    f32 c0,
    f32 c1,
    f32 c2, const u8* texture,
    i32 start_x, i32 end_x,
    i32 start_y, i32 end_y) {
    
    int drew_pixel = 0;
    // swap first two vertexes
    vert3f *tmp = v0;
    v0 = v1; v1 = tmp;
    // swap first two normals
    tmp = nv0;
    nv0 = nv1; nv1 = tmp;
    // swap first two world positions
    //tmp = wv0;
    //wv0 = wv1; wv1 = tmp;
    // swap first two uvs
    vert2f *tmpuv = uv0;
    uv0 = uv1; uv1 = tmpuv;

    f32 tmpflt = iz0;
    iz0 = iz1;
    iz1 = tmpflt;
    
    // swap first two colors
    f32 tmpc = c0;
    c0 = c1; c1 = tmpc;
    //(void)c0;(void)c1;
    //(void)c2;

    //iz1 = 1.0f/v1->z;
    //iz2 = 1.0f/v2->z;

    //f32 v0u = uv0->x;
    //f32 v1u = uv1->x;
    //f32 v2u = uv2->x;
    //f32 v0v = uv0->y;
    //f32 v1v = uv1->y;
    //f32 v2v = uv2->y;



    f32 v0u_over_z = uv0->x * iz0;
    f32 v0v_over_z = uv0->y * iz0;

    f32 v1u_over_z = uv1->x * iz1;
    f32 v1v_over_z = uv1->y * iz1;

    f32 v2u_over_z = uv2->x * iz2;
    f32 v2v_over_z = uv2->y * iz2;

    vert3f n0_over_z = scale_vert3(*nv0, iz0);
    vert3f n1_over_z = scale_vert3(*nv1, iz1);
    vert3f n2_over_z = scale_vert3(*nv2, iz2);



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
    const i32 DX01 = X0 - X1;
    const i32 DY01 = Y0 - Y1;
    const i32 DX12 = X1 - X2;
    const i32 DY12 = Y1 - Y2;
    const i32 DX20 = X2 - X0;
    const i32 DY20 = Y2 - Y0;
    //
    // Triangle area
    //
    i32 area =
        (DX01 * DY20 -
        DY01 * DX20);
    if (area <= 0) {
        return drew_pixel;
    }
    
    //
    // Bounding box
    //
    i32 minx = MIN(X0, MIN(X1, X2));
    i32 maxx = MAX(X0, MAX(X1, X2));
    i32 miny = MIN(Y0, MIN(Y1, Y2));
    i32 maxy = MAX(Y0, MAX(Y1, Y2));
    minx = CLAMP((minx + 15) >> 4, start_x, end_x);
    maxx = CLAMP((maxx + 15) >> 4, start_x, end_x);
    miny = CLAMP((miny + 15) >> 4, start_y, end_y);
    maxy = CLAMP((maxy + 15) >> 4, start_y, end_y);
    //
    // Edge constants
    //
    i32 E01 = DY01 * X0 - DX01 * Y0;
    i32 E12 = DY12 * X1 - DX12 * Y1;
    i32 E20 = DY20 * X2 - DX20 * Y2;

    // copies for coverage only
    i32 C01 = E01;
    i32 C12 = E12;
    i32 C20 = E20;
    //
    // Top-left rule
    //
    if (DY01 < 0 || (DY01 == 0 && DX01 > 0))
        C01++;
    if (DY12 < 0 || (DY12 == 0 && DX12 > 0))
        C12++;
    if (DY20 < 0 || (DY20 == 0 && DX20 > 0))
        C20++;
    //
    // Start at pixel center
    //
    i32 startX = minx << 4;
    i32 startY = miny << 4;
    i32 CY01 = C01 + DX01 * startY - DY01 * startX;
    i32 CY12 = C12 + DX12 * startY - DY12 * startX;
    i32 CY20 = C20 + DX20 * startY - DY20 * startX;
    i32 EY01 = E01 + DX01 * startY - DY01 * startX;
    i32 EY12 = E12 + DX12 * startY - DY12 * startX;
    i32 EY20 = E20 + DX20 * startY - DY20 * startX;
    //
    // Attribute interpolation
    //
    for (i32 y = miny; y < maxy; y++)
    {
        i32 CX01 = CY01;
        i32 CX12 = CY12;
        i32 CX20 = CY20;
        i32 EX01 = EY01;
        i32 EX12 = EY12;
        i32 EX20 = EY20;
        u8 *row = &ei->screen[y * kScreenWidth + minx];
        f32 *zbuf_row = &zbuffer[y * kScreenWidth + minx];
        for (i32 x = minx; x < maxx; x++) {
            if (!((CX01 | CX12 | CX20)>>31)) { // }>= 0 && CX12 >= 0 && CX20 >= 0)
                //
                // Barycentric weights.
                //
                // Note: because all edge values are scaled
                // equally, these are valid ratios.
                //
                f32 recip_area = 1.0f / (f32)area;
                i32 w0 = EX12;
                i32 w1 = EX20;
                i32 w2 = EX01;
                f32 fw0 = (f32)w0;
                f32 fw1 = (f32)w1;
                f32 fw2 = (f32)w2;
                f32 inv_z =
                    (fw0 * iz0 + fw1 * iz1 +  fw2 * iz2) * recip_area;
                //f32 z = 1.0f / inv_z;
                float ambient = 0.08f;

                
                if(inv_z >= zbuf_row[x - minx]) {
                    f32 z = 1.0f / inv_z;
                    f32 diffuse;
                    u8 pal_idx;
                        
                    f32 u_over_z = 
                        ((f32)w0 * v0u_over_z + (f32) w1 * v1u_over_z + (f32)w2 * v2u_over_z) * recip_area;
                    f32 v_over_z = 
                        ((f32)w0 * v0v_over_z + (f32) w1 * v1v_over_z + (f32)w2 * v2v_over_z) * recip_area;
                    f32 u = (u_over_z * z);
                    f32 v = (v_over_z * z);
                    
                    i32 int_u = (i32)fast_floor(u * (f32)TEXTURE_WIDTH);// & 1023;
                    i32 int_v = (i32)fast_floor(v * (f32)TEXTURE_HEIGHT);// & 1023;
                    int_u &= (TEXTURE_WIDTH-1);
                    int_v &= (TEXTURE_HEIGHT-1);
                    i32 int_u_right = int_u+1;
                    int_u_right &= (TEXTURE_WIDTH-1);
                    u8 tex_pal_idx = texture[((TEXTURE_HEIGHT-1)-int_v)*TEXTURE_WIDTH+int_u];
                    //u8 right_tex_idx = texture[((TEXTURE_HEIGHT-1)-int_v)*TEXTURE_WIDTH+int_u_right];

                    if(smooth_shading_enabled) {

                        // nv0,nv1,nv2 are the normals at each vertex
                        // w0/w1/w2 are barycentric weights scaled by triangle area

                        // calculate normal/z

                        // norm_over_z = (n0/z * w0 + n1/z * w1 + n2/z * w2) / triangle_area
                        vert3f interp_norm_over_z = scale_vert3(
                                                add_vert3(add_vert3(scale_vert3(n0_over_z, fw0),
                                                                    scale_vert3(n1_over_z, fw1)), 
                                                           scale_vert3(n2_over_z, fw2)), 
                                              recip_area);
                        
                        // multiply by z to get normal 
                        vert3f interp_norm = scale_vert3(interp_norm_over_z, z);

                        // hacky normal mapping/perturbation
                        //f32 perturb = perturb_norm[(tex_pal_idx*5)+right_tex_idx];
                        //interp_norm.x += perturb;
                        //interp_norm.x -= perturb;

                        interp_norm = normalize(interp_norm);

                        //interp_norm 

                        float hemi = interp_norm.y * 0.5f + 0.5f;

                        ambient = lerp(0.1f, 0.3f, hemi);
                        

                        diffuse = dot(normalize(interp_norm), light);
                        
                    } else {
                        // no smooth shading, interpolate brightness calculated at each vertex
                        // c0/c1/c2 are brightness values
                        diffuse =
                            ((f32)w0 * c0 +
                            (f32)w1 * c1 +
                            (f32)w2 * c2) / (f32)area;
                        
                    }

                    f32 brightness = ambient + diffuse * (1.0f - ambient);
                    brightness = CLAMP(brightness, 0.0f, 1.0f);
                    u8 quantized_brightness = (u8)(brightness * (NUM_SHADES-1));


                    
                    pal_idx = light_remap_table[quantized_brightness][tex_pal_idx];

                    row[x - minx] = pal_idx;
                    zbuf_row[x - minx] = inv_z;
                    drew_pixel = 1;
                }
            }
            CX01 -= DY01 << 4;
            CX12 -= DY12 << 4;
            CX20 -= DY20 << 4;
            EX01 -= DY01 << 4;
            EX12 -= DY12 << 4;
            EX20 -= DY20 << 4;
        }
        CY01 += DX01 << 4;
        CY12 += DX12 << 4;
        CY20 += DX20 << 4;
        EY01 += DX01 << 4;
        EY12 += DX12 << 4;
        EY20 += DX20 << 4;
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

double cos(double x) {
    if( x < 0.0) {
        x = -x;
    }

    if (0 <= compare_f32(x,M_PI_M_2)) {
        do {
            x -= M_PI_M_2;
        } while(0 <= compare_f32(x,M_PI_M_2));
    }

    if ((0 <= compare_f32(x, M_PI)) && (-1 == compare_f32(x, M_PI_M_2))) {
        x -= M_PI;
        return ((-1)*(1.0 - (x*x/2.0)*( 1.0 - (x*x/12.0) * ( 1.0 - (x*x/30.0) * (1.0 - (x*x/56.0 )*(1.0 - (x*x/90.0)*(1.0 - (x*x/132.0)*(1.0 - (x*x/182.0)))))))));
    }
    return 1.0 - (x*x/2.0)*( 1.0 - (x*x/12.0) * ( 1.0 - (x*x/30.0) * (1.0 - (x*x/56.0 )*(1.0 - (x*x/90.0)*(1.0 - (x*x/132.0)*(1.0 - (x*x/182.0)))))));
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


typedef enum {
    NORMAL,
    TILE_DRAW,
    HI_Z_DRAW,
    NUM_DRAW_MODES
} draw_modes;
int last_x_pushed = 0, last_y_pushed = 0;
int hi_z_enabled = 1;
draw_modes draw_mode = 0;
void game_update(ExotiqueInterface* ei) {
    light = normalize(light);

    if(ei->input->left) {
        rotY += 0.006f;
    } else if (ei->input->right) {
        rotY -= 0.006f;
    } else {
        //rotY += 0.001f;
    }
    if(ei->input->up) {
        rotX += 0.008f;
    } else if (ei->input->down) {
        rotX -= 0.008f;
    } else {
        //rotX += 0.0002f;
    }
    if(ei->input->a) {
        camera_distance -= 0.02f;
    } else if (ei->input->b) {
        camera_distance += 0.02f;
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
    if(!last_y_pushed) {
        if(ei->input->y) { smooth_shading_enabled = !smooth_shading_enabled; }
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
    obj_mesh* mesh; u8 texture;
    f32 pos_x, pos_y, pos_z;
    f32 angle_x, angle_y;
    f32 trans_centroid_z;
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

vert3f transform_coord(vert3f v, f32 off_x, f32 off_y, f32 off_z, f32 sin_x, f32 sin_y, f32 cos_x, f32 cos_y) {
    v.x += off_x;
    v.y += off_y;
    v.z += off_z;
    vert3f r = rotate_xy(v, cos_x, sin_x, cos_y, sin_y);
    r.z += camera_distance;
    return r;

}

// 
int is_bounding_box_hi_z_occluded_or_offscreen(mesh_draw_call* m, bbox* box) {
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
    f32 sx = sinf(m->angle_x);
    f32 sy = sinf(m->angle_y);
    f32 cx = cosf(m->angle_x);
    f32 cy = cosf(m->angle_y);
    for(int i = 0; i < 8; i++) {
        vert3f r = transform_coord(verts[i], m->pos_x, m->pos_y, m->pos_z, sx, sy, cx, cy);
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
        return 1;
    }

    if(min_z > FAR_Z) {
        // FAR CLIPPED
        return 1;
    }

    if(max_z < NEAR_Z) {
        // NEAR CLIPPED
        return 1;
    }

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

    return 1;
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

void game_load(ExotiqueInterface* ei) {
    int i;
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
                ei->palette[base+i] = (byte_r<<24)|(byte_g<<16)|(byte_b<<8)|0xFF;
                light_remap_table[shade][i] = (u8)(base+i);
            }
        }
        //(i<<24)|(i<<16)|(i<<8)|i;
        //ei->palette[i] = (i<<24)|(i<<16)|(i<<8)|i;
    }
    init_tiles();

    tile_bbox = get_mesh_bbox(&tile_mesh);
}

typedef enum {
    WHITE_DRAGON,
    RED_DRAGON,
    GREEN_DRAGON,
    EAST,
    TWO_PIN,
    NUM_TILES
} tile_type;

const u8* textures[NUM_TILES] = {
    texture_white_dragon,
    texture_red_dragon,
    texture_green_dragon,
    texture_east,
    texture_two_pin
};


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
            &global_tri_buffer[global_tri_idx].proj_v0,
            &global_tri_buffer[global_tri_idx].proj_v1,
            &global_tri_buffer[global_tri_idx].proj_v2,
            global_tri_buffer[global_tri_idx].inv_z0,
            global_tri_buffer[global_tri_idx].inv_z1,
            global_tri_buffer[global_tri_idx].inv_z2,
            &global_tri_buffer[global_tri_idx].uv0,
            &global_tri_buffer[global_tri_idx].uv1,
            &global_tri_buffer[global_tri_idx].uv2,
            &global_tri_buffer[global_tri_idx].nv0,
            &global_tri_buffer[global_tri_idx].nv1,
            &global_tri_buffer[global_tri_idx].nv2,

            global_tri_buffer[global_tri_idx].c0,
            global_tri_buffer[global_tri_idx].c1,
            global_tri_buffer[global_tri_idx].c2,
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



        if(triangle_backfacing(v0, v1, v2)) {
            // do not submit backfacing triangles
            return;
        }

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
    //obj_mesh *m, f32 angleY, f32 angleX, f32 base_off_x, f32 base_off_y, f32 base_off_z, 
    //u8 texture
) {
    obj_mesh *m = mdc->mesh;
    f32 angleY = mdc->angle_y;
    f32 angleX = mdc->angle_x;
    f32 base_off_x = mdc->pos_x;
    f32 base_off_y = mdc->pos_y;
    f32 base_off_z = mdc->pos_z;
    u8 texture = mdc->texture;

    meshes_transformed += 1;

    f32 cy = cosf(angleY);
    f32 sy = sinf(angleY);
    f32 cx = cosf(angleX);
    f32 sx = sinf(angleX);

    // super coarse rasterization




    for (int i = 0; i < m->indexCount; i += 3) {

        obj_vertex ov0 = m->vertexStream[m->indexStream[i + 0]];
        obj_vertex ov1 = m->vertexStream[m->indexStream[i + 1]];
        obj_vertex ov2 = m->vertexStream[m->indexStream[i + 2]];

        vert3f v0 = ov0.pos;
        vert3f v1 = ov1.pos;
        vert3f v2 = ov2.pos;
        //v0.x += base_off_x; v1.x += base_off_x; v2.x += base_off_x;
        //v0.y += base_off_y; v1.y += base_off_y; v2.y += base_off_y;
        //v0.z += base_off_z; v1.z += base_off_z; v2.z += base_off_z;

        //
        // Hardcoded model transform.
        // Push the whole mesh in front of the camera.
        //

        //vert3f r0 = rotate_xy(v0, cx, sx, cy, sy); 
        /*{
            cy * v0.x + sy * v0.z,
            v0.y,
           -sy * v0.x + cy * v0.z
        }; */

        //vert3f r1 = rotate_xy(v1, cx, sx, cy, sy);
        /* {
            cy * v1.x + sy * v1.z,
            v1.y,
           -sy * v1.x + cy * v1.z
        }; */

        //vert3f r2 = rotate_xy(v2, cx, sx, cy, sy); 
        /*{
            cy * v2.x + sy * v2.z,
            v2.y,
           -sy * v2.x + cy * v2.z
        };*/

        // Move in front of the camera.
        //r0.z += camera_distance;
        //r1.z += camera_distance;
        //r2.z += camera_distance;

        vert3f r0 = transform_coord(v0, base_off_x, base_off_y, base_off_z, sx, sy, cx, cy);
        vert3f r1 = transform_coord(v1, base_off_x, base_off_y, base_off_z, sx, sy, cx, cy);
        vert3f r2 = transform_coord(v2, base_off_x, base_off_y, base_off_z, sx, sy, cx, cy);

        //
        // Reject triangles behind the camera.
        //

        if (r0.z <= NEAR_Z || r1.z <= NEAR_Z || r2.z <= NEAR_Z || r0.z >= FAR_Z || r1.z >= FAR_Z || r2.z >= FAR_Z) {
            continue;
        }
        triangles_transformed += 1;

        vert3f n0 = ov0.norm;
        vert3f n1 = ov1.norm;
        vert3f n2 = ov2.norm;

        // Rotate normals (don't translate)

        vert3f rn0 = rotate_xy(n0, cx, sx, cy, sy);
        /* = {
            cy*n0.x + sy*n0.z,
            n0.y,
            -sy*n0.x + cy*n0.z
        };*/

        vert3f rn1 = rotate_xy(n1, cx, sx, cy, sy);
        /* = {
            cy*n1.x + sy*n1.z,
            n1.y,
            -sy*n1.x + cy*n1.z
        };*/

        vert3f rn2 = rotate_xy(n2, cx, sx, cy, sy);
        /* = {
            cy*n2.x + sy*n2.z,
            n2.y,
            -sy*n2.x + cy*n2.z
        };*/

        f32 l0 = dot(normalize(rn0), light);
        f32 l1 = dot(normalize(rn1), light);
        f32 l2 = dot(normalize(rn2), light);

        //if (l0 < 0) l0 = 0;
        //if (l1 < 0) l1 = 0;
        //if (l2 < 0) l2 = 0;
        //float hemi_c0 = rn0.y * 0.5f + 0.5f;
        //float ambient_c0 = lerp(0.1f, 0.3f, hemi_c0);
        //float hemi_c0 = rn0.y * 0.5f + 0.5f;
        //float ambient_c0 = lerp(0.1f, 0.3f, hemi_c0);
        //float hemi_c0 = rn0.y * 0.5f + 0.5f;
        //float ambient_c0 = lerp(0.1f, 0.3f, hemi_c0);
        //l0 *= (1.0f - ambient_c0);
        //l0 += ambient_c0;
        f32 c0 = CLAMP(l0, 0.0f, 1.0f);
        f32 c1 = CLAMP(l1, 0.0f, 1.0f);
        f32 c2 = CLAMP(l2, 0.0f, 1.0f);

        //u8 c0 = (u8)CLAMP(l0 * (f32)(NUM_SHADES-1), 5.0f, (f32)(NUM_SHADES-1));
        //u8 c1 = (u8)CLAMP(l1 * (f32)(NUM_SHADES-1), 5.0f, (f32)(NUM_SHADES-1));
        //u8 c2 = (u8)CLAMP(l2 * (f32)(NUM_SHADES-1), 5.0f, (f32)(NUM_SHADES-1));
        //
        // Perspective projection.
        //



        vert3f s0 = project_coord(r0);
        vert3f s1 = project_coord(r1);
        vert3f s2 = project_coord(r2);
        //{
        //    camx + focal * r0.x / r0.z,
        //    camy - focal * r0.y / r0.z,
        //    r0.z
        //};

        //vert3f s1 = {
        //    camx + focal * r1.x / r1.z,
        //    camy - focal * r1.y / r1.z,
        //    r1.z
        //};

        //vert3f s2 = {
        //    camx + focal * r2.x / r2.z,
        //    camy - focal * r2.y / r2.z,
        //    r2.z
        //};

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

    for(int i = 0; i < num_meshes; i++) {
        vert3f model_pos = (vert3f){list[i].pos_x, list[i].pos_y, list[i].pos_z};
        vert3f trans_pos = transform_coord(model_pos, 0.0f, 0.0f, 0.0f, 
            sinf(list[i].angle_x), sinf(list[i].angle_y),
            cosf(list[i].angle_x), cosf(list[i].angle_y)
        );
        list[i].trans_centroid_z = trans_pos.z;
    }
    
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
    exotique_printf("sorted\n");
    
}

void submit_draw_calls(ExotiqueInterface *ei, mesh_draw_call *list, int num_meshes) {
    int occluded_meshes = 0;
    for(int i = 0; i < num_meshes; i++) {
        clear_tile_bins();
        if(hi_z_enabled) {
            rebuild_hi_z();
            if(is_bounding_box_hi_z_occluded_or_offscreen(&list[i], &tile_bbox)) {
                occluded_meshes++;
                //exotique_printf("MESH IS OCCLUDED!!!\n");
                continue;
            }
        }
        submit_mesh_draw_call(&list[i]);
        draw_tiles(ei, zbuf);
    }
    exotique_printf("%i/%i meshes occluded\n", occluded_meshes, num_meshes);

    //exotique_printf("----------------DONE-------------------------\n");
}


static u64 last_frame_ticks = 0;
void game_draw(ExotiqueInterface* ei) {
    u64 cur_frame_ticks = ei->ticks;

    exotique_printf("%llu ms\n", cur_frame_ticks - last_frame_ticks);
    last_frame_ticks = cur_frame_ticks;
    //u8 color = 0;
    for(int y = 0; y < kScreenHeight; y+=16) {
        for(int x = 0; x < kScreenWidth; x+=16) {
            u8 col = light_remap_table[15][GREEN];//color++;
            for(int yy = y; yy < y+16; yy++) {
                for(int xx = x; xx < x+16; xx++) {
                    zbuf[yy*kScreenWidth+xx] = 1.0f/FAR_Z;
                    ei->screen[yy*kScreenWidth+xx] = col;
                }
            }
            //light_remap_table[NUM_SHADES/2][GREEN];
        }
    }
    if(hi_z_enabled) {
        reset_tile_hi_z();
    }

    int num_tiles_to_draw = 28;
    tile_type tiles_to_draw[14] = {
        WHITE_DRAGON, WHITE_DRAGON, WHITE_DRAGON,
        RED_DRAGON, RED_DRAGON, RED_DRAGON,
        GREEN_DRAGON, GREEN_DRAGON, GREEN_DRAGON,
        EAST, EAST, EAST, TWO_PIN, TWO_PIN,
    };
    mesh_draw_call draw_calls[28];
    total_triangles = 0;
    f32 start_x = -17.5f;
    for(int i = 0; i < num_tiles_to_draw/2; i++) {
        draw_calls[i*2].mesh = &tile_mesh;
        draw_calls[i*2].texture = tiles_to_draw[i];
        draw_calls[i*2].pos_x = start_x + (2.5f*(f32)i);
        draw_calls[i*2].pos_y = 0.0f;
        draw_calls[i*2].pos_z = 0.0f;
        draw_calls[i*2].angle_x = rotX;
        draw_calls[i*2].angle_y = rotY;

        draw_calls[i*2+1].mesh = &tile_mesh;
        draw_calls[i*2+1].texture = tiles_to_draw[i];
        draw_calls[i*2+1].pos_x = start_x + (2.5f*(f32)i);
        draw_calls[i*2+1].pos_y = 0.0f;
        draw_calls[i*2+1].pos_z = -2.5f;
        draw_calls[i*2+1].angle_x = rotX;
        draw_calls[i*2+1].angle_y = rotY;

    }

    sort_draw_calls_near_to_far(draw_calls, num_tiles_to_draw);
    submit_draw_calls(ei, draw_calls, num_tiles_to_draw);
    //for(int i = 0; i < num_tiles_to_draw; i++) {
    //    draw_calls[i].pos_z -= 2.5f;
    //}
    //submit_draw_calls(ei, draw_calls, num_tiles_to_draw);


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


    exotique_printf("smooth shading %i\n", smooth_shading_enabled);
    //exotique_printf("meshes %i\n", meshes_transformed);
    //exotique_printf("triangles transformed %i\n", triangles_transformed);
    //exotique_printf("triangles hi z culled %i\n", triangles_hi_z_culled);
    //exotique_printf("triangles rasterized %i\n", triangles_rasterized);
    exotique_printf("hi_z_enabled %i\n", hi_z_enabled);
    meshes_transformed = 0;
    triangles_transformed = 0;
    triangles_rasterized = 0;
    triangles_hi_z_culled = 0;
}