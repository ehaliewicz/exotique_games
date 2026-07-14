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

matrix mat_inverse_affine(const matrix *a) {
    float m00 = a->m[0][0], m01 = a->m[0][1], m02 = a->m[0][2];
    float m10 = a->m[1][0], m11 = a->m[1][1], m12 = a->m[1][2];
    float m20 = a->m[2][0], m21 = a->m[2][1], m22 = a->m[2][2];

    float det = m00 * (m11 * m22 - m12 * m21)
              - m01 * (m10 * m22 - m12 * m20)
              + m02 * (m10 * m21 - m11 * m20);

    // det == 0 means non-invertible (e.g. a zero scale somewhere in the chain)
    float invDet = 1.0f / det;

    matrix r = {0};

    r.m[0][0] = (m11 * m22 - m12 * m21) * invDet;
    r.m[0][1] = (m02 * m21 - m01 * m22) * invDet;
    r.m[0][2] = (m01 * m12 - m02 * m11) * invDet;

    r.m[1][0] = (m12 * m20 - m10 * m22) * invDet;
    r.m[1][1] = (m00 * m22 - m02 * m20) * invDet;
    r.m[1][2] = (m02 * m10 - m00 * m12) * invDet;

    r.m[2][0] = (m10 * m21 - m11 * m20) * invDet;
    r.m[2][1] = (m01 * m20 - m00 * m21) * invDet;
    r.m[2][2] = (m00 * m11 - m01 * m10) * invDet;

    // t' = -R^-1 * t
    float tx = a->m[0][3], ty = a->m[1][3], tz = a->m[2][3];
    r.m[0][3] = -(r.m[0][0] * tx + r.m[0][1] * ty + r.m[0][2] * tz);
    r.m[1][3] = -(r.m[1][0] * tx + r.m[1][1] * ty + r.m[1][2] * tz);
    r.m[2][3] = -(r.m[2][0] * tx + r.m[2][1] * ty + r.m[2][2] * tz);

    r.m[3][0] = 0.0f; r.m[3][1] = 0.0f; r.m[3][2] = 0.0f; r.m[3][3] = 1.0f;
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



typedef struct {
    int active_edges;
    i32 cvals[3];
    i32 dxvals[3];
    i32 dyvals[3];

    //i32 c0, c1, c2;
    //i32 dx0, dy0, dx1, dy1, dx2, dy2;

    //i32 c01, c12, c20;
    //i32 dx01, dy01, dx12, dy12, dx20, dy20;
} edge_prep;

#define MAX_GLOBAL_TRIS 1000000
typedef struct {
    vert3f proj_v0, proj_v1, proj_v2;
    f32 c0, c1, c2;
    vert2f uv0, uv1, uv2;
    vert3f nv0, nv1, nv2;
    u8 tex;
    f32 inv_z0, inv_z1, inv_z2;
    edge_prep edges;
} transformed_tri;

#define MAX_TILE_TRIS 8192
typedef struct {
    i32 start_x; i32 start_y;
    u32 num_triangles, num_trivial_triangles;
    u32 tri_indexes[MAX_TILE_TRIS]; // up to 2048 triangles per tile
    u32 trivial_tri_indexes[MAX_TILE_TRIS];
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

#include "mesh_mahjong_tile.h"
#include "mesh_board.h"
#include "palette_mahjong.h"
#include "palette_background.h"

#define BLACK 0
#define GREEN 1
#define GOLD 2
#define WHITE 3
#define RED 4
#define BLUE 5

typedef struct {
    obj_vertex *vertexStream;
    u32 *indexStream; 

    int vertexCount;
    int indexCount;
} obj_mesh;

obj_mesh tile_mesh = {
    .vertexStream = mahjong_tile_vertexes,
    .indexStream = mahjong_tile_indexes,
    .vertexCount = (sizeof(mahjong_tile_vertexes)/sizeof(obj_vertex)),
    .indexCount = (sizeof(mahjong_tile_indexes)/sizeof(u32))
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

#define NUM_BASE_COLORS 6
#define NUM_SHADES 25
u8 light_remap_table[NUM_SHADES][NUM_BASE_COLORS];

vert3f light = {
    1.0f,
    1.0f,
    -.5f
};

typedef struct {
    u8 palette[4]; // up to 4 colors in per-image palette.  these are indexes of the global palette (2 are shared)
    u8 *compressed_packets;
} compressed_texture;

typedef enum {
    UNCOMPRESSED,
    COMPRESSED
} compress_type;

typedef struct {
    compressed_texture* comp_tex_ptr;
    u8* texels;
    int width;
    int height;
    compress_type compressed;
} texture;


typedef float f32_vec __attribute__((vector_size(16)));
typedef int i32_vec __attribute__((vector_size(16)));



i32_vec broadcast_i32_vec(i32 a) {
    i32_vec res;
    for(int i = 0; i < 4; i++) { res[i] = a; }
    return res;
}

i32_vec i32_vec_select(i32_vec mask, i32_vec a, i32_vec b) {
    // if mask[i] ? b[i] : a[i];
    i32_vec res;
    for(int i = 0; i < 4; i++) {
        res[i] = mask[i] ? a[i] : b[i];
    }
    return res;
}

f32_vec f32_vec_select(i32_vec mask, f32_vec a, f32_vec b) {
    // if mask[i] ? b[i] : a[i];
    //f32_vec res = mask ? a : b;
    //return res;
    f32_vec res;
    for(int i = 0; i < 4; i++) {
        res[i] = mask[i] ? a[i] : b[i];
    }
    return res;
}

f32_vec broadcast_f32_vec(f32 a) {
    f32_vec res;
    for(int i = 0; i < 4; i++) { res[i] = a; }
    return res;
}

f32_vec i32_vec_convert_f32(i32_vec a) {
    f32_vec res;
    for(int i = 0; i < 4; i++) { res[i] = (f32)a[i]; }
    return res;
}

i32_vec init_i32_vec(i32 a, i32 b, i32 c, i32 d) {
    i32_vec res;
    res[0] = a;
    res[1] = b;
    res[2] = c;
    res[3] = d;
    return res;
}

f32_vec init_f32_vec(f32 a, f32 b, f32 c, f32 d) {
    f32_vec res;
    res[0] = a;
    res[1] = b;
    res[2] = c;
    res[3] = d;
    return res;
}

i32_vec i32_vec_add(i32_vec a, i32_vec b) {
    i32_vec res = a+b;

    //for(int i = 0; i < 4; i++) {
    //    res[i] = a[i] + b[i];
    //}
    return res;
}

f32_vec f32_vec_add(f32_vec a, f32_vec b) {
    f32_vec res;
    for(int i = 0; i < 4; i++) {
        res[i] = a[i] + b[i];
    }
    return res;
}


u8 i32_vec_extract_low_bits(i32_vec a) {
    u8 res = 0;
    for(int i = 0; i < 4; i++) {
        res |= (u8)((a[i]&1)<<i);
    }
    return res;
}

i32_vec f32_vec_gte(f32_vec a, f32_vec b) {
    i32_vec res;
    for(int i = 0; i < 4; i++) {
        res[i] = (a[i] >= b[i]) ? 1 : 0;
    }
    return res;
}

i32_vec i32_vec_gte(i32_vec a, i32_vec b) {
    i32_vec res;
    for(int i = 0; i < 4; i++) {
        res[i] = (a[i] >= b[i]) ? 1 : 0;
    }
    return res;
}

i32_vec f32_vec_convert_i32(f32_vec a) {
    i32_vec res;
    res[0] = (i32)a[0];
    res[1] = (i32)a[1];
    res[2] = (i32)a[2];
    res[3] = (i32)a[3];
    return res;
}

i32_vec f32_vec_floor(f32_vec a) {
    i32_vec res;
    
    res[0] = fast_floor(a[0]);
    res[1] = fast_floor(a[1]);
    res[2] = fast_floor(a[2]);
    res[3] = fast_floor(a[3]);
    return res;
}

int fast_log2(float x)
{
    return (int)((*(u32*)&x) >> 23) - 127;
}

f32 absf(f32 a) { 
    if(a < 0.0f) { return -a; }
    return a;
}

i32_vec parallel_pixel_shader(
    f32_vec z,
    f32_vec w0, f32_vec w1, f32_vec w2, f32 v0u_over_z, f32 v0v_over_z, f32 v1u_over_z, f32 v1v_over_z, f32 v2u_over_z, f32 v2v_over_z, f32 recip_area, u8* texels, int tex_width, int tex_height, 
    u8* lit_pal_ptr
) {
    (void)tex_height;
    f32_vec u_over_z = (w0 * v0u_over_z + w1 * v1u_over_z + w2 * v2u_over_z) * recip_area;
    f32_vec v_over_z = (w0 * v0v_over_z + w1 * v1v_over_z + w2 * v2v_over_z) * recip_area;
    f32_vec u = (u_over_z * z);
    f32_vec v = (v_over_z * z);


    //f32 dudx = u[1] - u[0];
    //f32 dudy = u[2] - u[0];
    //f32 dvdx = v[1] - v[0];
    //f32 dvdy = v[2] - v[0];

    //f32 rho = MAX(absf(dudx), MAX(absf(dvdx), MAX(absf(dudy), absf(dvdy))));

    //f32 scaled_rho = rho*(f32)tex_height;

    //int mip = (int)scaled_rho;

    //int tex_scale = tex_width >> mip;

    
    i32_vec int_u = f32_vec_floor(u * (f32)tex_width); // (i32)fast_floor(u * (f32)tex_width);// & 1023;
    i32_vec int_v = f32_vec_floor(v * (f32)tex_width);// & 1023;

    int_u &= (tex_width-1);
    int_v &= (tex_width-1);

    i32_vec scaled_v = tex_width * ((tex_width-1)-int_v);
    i32_vec uv = scaled_v + int_u;
    i32_vec tex_pal_idx;
    tex_pal_idx[0] = texels[uv[0]];
    tex_pal_idx[1] = texels[uv[1]];
    tex_pal_idx[2] = texels[uv[2]];
    tex_pal_idx[3] = texels[uv[3]];

    //texels[((tex_height-1)-int_v)*tex_width+int_u];

    i32_vec res;
    res[0] = lit_pal_ptr[tex_pal_idx[0]];
    res[1] = lit_pal_ptr[tex_pal_idx[1]];
    res[2] = lit_pal_ptr[tex_pal_idx[2]];
    res[3] = lit_pal_ptr[tex_pal_idx[3]];
    return res;
}

u8 pixel_shader(
    f32 z,
    f32 w0, f32 w1, f32 w2, f32 v0u_over_z, f32 v0v_over_z, f32 v1u_over_z, f32 v1v_over_z, f32 v2u_over_z, f32 v2v_over_z, f32 recip_area, u8* texels, int tex_width, int tex_height, 
    u8* lit_pal_ptr
) {
    
    //f32 z = 1.0f / inv_z;
                        
    f32 u_over_z = (w0 * v0u_over_z + w1 * v1u_over_z + w2 * v2u_over_z) * recip_area;
    f32 v_over_z = (w0 * v0v_over_z + w1 * v1v_over_z + w2 * v2v_over_z) * recip_area;
    f32 u = (u_over_z * z);
    f32 v = (v_over_z * z);
    
    i32 int_u = (i32)fast_floor(u * (f32)tex_width);// & 1023;
    i32 int_v = (i32)fast_floor(v * (f32)tex_height);// & 1023;
    int_u &= (tex_width-1);
    int_v &= (tex_height-1);
    u8 tex_pal_idx = texels[((tex_height-1)-int_v)*tex_width+int_u];

    // linearly interpolate brightness calculated at each vertex
    // c0/c1/c2 are brightness values
    //f32 diffuse = (w0 * c0 + w1 * c1 + w2 * c2) * recip_area;

    //diffuse = CLAMP(diffuse, 0.0f, 1.0f);
    //u8 quantized_brightness = (u8)(diffuse * (NUM_SHADES-1));
    //u8 pal_idx = light_remap_table[quantized_brightness][tex_pal_idx];
    //u8 tex_pal_idx = 3;
    return lit_pal_ptr[tex_pal_idx];// light_remap_table[16][WHITE];

    //*color_ptr = pal_idx;
    //*zbuf_ptr = inv_z;
}



// returns 1 if drew a pixel, 0 otherwise
int triangle(
    ExotiqueInterface *ei,
    f32 *zbuffer,
    transformed_tri* tri_attributes,
    texture* tex,
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
    f32 avg_c = (c0+c1+c2)/3.0f;
    
    //f32 diffuse = (w0 * c0 + w1 * c1 + w2 * c2) * recip_area;

    f32 diffuse = CLAMP(avg_c, 0.0f, 1.0f);
    u8 quantized_brightness = (u8)(diffuse * (NUM_SHADES-1));
    u8* lit_pal_ptr = light_remap_table[quantized_brightness];



    int drew_pixel = 0;

    f32 v0u_over_z = uv0.x * iz0;
    f32 v0v_over_z = uv0.y * iz0;

    f32 v1u_over_z = uv1.x * iz1;
    f32 v1v_over_z = uv1.y * iz1;

    f32 v2u_over_z = uv2.x * iz2;
    f32 v2v_over_z = uv2.y* iz2;

    int tex_width = tex->width;
    int tex_height = tex->height;
    u8* texels = tex->texels;

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

    i32 dx01 = x0 - x1;
    i32 dy01 = y0 - y1;
    i32 dx12 = x1 - x2;
    i32 dy12 = y1 - y2;
    i32 dx20 = x2 - x0;
    i32 dy20 = y2 - y0;
    //
    // Triangle area
    //


    i32 area = (dx01 * dy20 - dy01 * dx20);

    // barycentric weights weights (scaled by area)
    f32 recip_area = 1.0f / (f32)area;

    
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
    dy01 <<= 4;
    dy12 <<= 4;
    dy20 <<= 4;
    dx01 <<= 4;
    dx12 <<= 4;
    dx20 <<= 4;


    for (i32 y = miny; y < maxy; y++) {
        i32 cx01 = cy01;
        i32 cx12 = cy12;
        i32 cx20 = cy20;
        i32 ex01 = ey01;
        i32 ex12 = ey12;
        i32 ex20 = ey20;

        u8 *row = &ei->screen[y * kScreenWidth + minx];
        f32 *zbuf_row = &zbuffer[y * kScreenWidth + minx];

        for (i32 x = minx; x < maxx; x++) {
            f32 zbuf_val = *zbuf_row;
            if (!((cx01|cx12|cx20)>>31)) { // check the sign bit
                f32 w0 = (f32)ex12;
                f32 w1 = (f32)ex20;
                f32 w2 = (f32)ex01;
                f32 inv_z = (w0 * iz0 + w1 * iz1 +  w2 * iz2) * recip_area;
                f32 z = 1.0f / inv_z;

                if(inv_z > zbuf_val) {
                    u8 pal_idx = pixel_shader(
                        z,
                        w0, w1, w2, v0u_over_z, v0v_over_z, v1u_over_z, v1v_over_z, v2u_over_z, v2v_over_z, recip_area, 
                        texels, tex_width, tex_height, 
                        lit_pal_ptr
                    );
                    *row = pal_idx;
                    *zbuf_row = inv_z;
                    drew_pixel = 1;
                }
            }
            zbuf_row++;
            row++;
            // step horizontal edge coverage
            cx01 -= dy01;
            cx12 -= dy12;
            cx20 -= dy20;
            ex01 -= dy01;
            ex12 -= dy12;
            ex20 -= dy20;
        }
        // step vertical edge coverage
        cy01 += dx01;
        cy12 += dx12;
        cy20 += dx20;
        ey01 += dx01;
        ey12 += dx12;
        ey20 += dx20;
    }
    return drew_pixel;
}

int trivial_triangle(
    ExotiqueInterface *ei,
    f32 *zbuffer,
    transformed_tri* tri_attributes,
    texture* tex,
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
    f32 avg_c = (c0+c1+c2)/3.0f;
    
    //f32 diffuse = (w0 * c0 + w1 * c1 + w2 * c2) * recip_area;

    f32 diffuse = CLAMP(avg_c, 0.0f, 1.0f);
    u8 quantized_brightness = (u8)(diffuse * (NUM_SHADES-1));
    u8* lit_pal_ptr = light_remap_table[quantized_brightness];



    const i32 x0 = (i32)(v0.x * 16.0f);
    const i32 y0 = (i32)(v0.y * 16.0f);
    const i32 x1 = (i32)(v1.x * 16.0f);
    const i32 y1 = (i32)(v1.y * 16.0f);
    const i32 x2 = (i32)(v2.x * 16.0f);
    const i32 y2 = (i32)(v2.y * 16.0f);

    int drew_pixel = 0;

    f32 v0u_over_z = uv0.x * iz0;
    f32 v0v_over_z = uv0.y * iz0;

    f32 v1u_over_z = uv1.x * iz1;
    f32 v1v_over_z = uv1.y * iz1;

    f32 v2u_over_z = uv2.x * iz2;
    f32 v2v_over_z = uv2.y* iz2;

    int tex_width = tex->width;
    int tex_height = tex->height;
    u8* texels = tex->texels;

    
    //
    // Edge deltas
    //

    //const i32 dx01 = tri_edges.dxvals[0];
    //const i32 dx12 = tri_edges.dxvals[1];
    //const i32 dx20 = tri_edges.dxvals[2];
    //const i32 dy01 = tri_edges.dyvals[0];
    //const i32 dy12 = tri_edges.dyvals[1];
    //const i32 dy20 = tri_edges.dyvals[2];

    i32 dx01 = x0 - x1; // 28.4
    i32 dy01 = y0 - y1;
    i32 dx12 = x1 - x2;
    i32 dy12 = y1 - y2;
    i32 dx20 = x2 - x0;
    i32 dy20 = y2 - y0;
    //
    // Triangle area
    //

    i32 area = (dx01 * dy20 - dy01 * dx20); // 24.8

    // barycentric weights weights (scaled by area)
    f32 recip_area = 1.0f / (f32)area;

    
    i32 minx = start_x;
    i32 maxx = end_x;
    i32 miny = start_y;
    i32 maxy = end_y;

    // edge constants, used for incremental edge coverage calculation

    i32 e01 = dy01 * x0 - dx01 * y0;
    i32 e12 = dy12 * x1 - dx12 * y1;
    i32 e20 = dy20 * x2 - dx20 * y2;


    i32 startX = minx << 4;
    i32 startY = miny << 4;
    i32 ey01 = e01 + dx01 * startY - dy01 * startX;
    i32 ey12 = e12 + dx12 * startY - dy12 * startX;
    i32 ey20 = e20 + dx20 * startY - dy20 * startX;

    dy01 <<= 4;
    dy12 <<= 4;
    dy20 <<= 4;
    dx01 <<= 4;
    dx12 <<= 4;
    dx20 <<= 4;


    for (i32 y = miny; y < maxy; y++) {
        i32 ex01 = ey01;
        i32 ex12 = ey12;
        i32 ex20 = ey20;

        u8 *row = &ei->screen[y * kScreenWidth + minx];
        f32 *zbuf_row = &zbuffer[y * kScreenWidth + minx];

        for (i32 x = minx; x < maxx; x++) {
            f32 zbuf_val = *zbuf_row;

            f32 w0 = (f32)ex12;
            f32 w1 = (f32)ex20;
            f32 w2 = (f32)ex01;
            f32 inv_z = (w0 * iz0 + w1 * iz1 +  w2 * iz2) * recip_area;
            f32 z = 1.0f / inv_z;
            u8 pal_idx = pixel_shader(
                z,
                w0, w1, w2, v0u_over_z, v0v_over_z, v1u_over_z, v1v_over_z, v2u_over_z, v2v_over_z, recip_area, 
                texels, tex_width, tex_height, 
                lit_pal_ptr
            );
            if(inv_z >= zbuf_val) {
                *row = pal_idx;
                *zbuf_row = inv_z;
                drew_pixel = 1;
            }
            zbuf_row++;
            row++;
            // step horizontal edge coverage
            ex01 -= dy01;
            ex12 -= dy12;
            ex20 -= dy20;



        }
        // step vertical edge coverage
        ey01 += dx01;
        ey12 += dx12;
        ey20 += dx20;
    }
    return drew_pixel;
}



int triangle_block(
    ExotiqueInterface *ei,
    f32 *zbuffer,
    transformed_tri* tri_attributes,
    texture* tex,
    i32 start_x, i32 end_x,
    i32 start_y, i32 end_y
) {

    // swap everything for first two vertexes (actual vertex positions and attributes)
    (void)tex;
    f32 iz0 = tri_attributes->inv_z1;
    f32 iz1 = tri_attributes->inv_z0;
    f32 iz2 = tri_attributes->inv_z2;
    f32_vec iz0_vec = broadcast_f32_vec(iz0);
    f32_vec iz1_vec = broadcast_f32_vec(iz1);
    f32_vec iz2_vec = broadcast_f32_vec(iz2);

    vert3f v0 = tri_attributes->proj_v1;
    vert3f v1 = tri_attributes->proj_v0;
    vert3f v2 = tri_attributes->proj_v2;
    vert2f uv0 = tri_attributes->uv1;
    vert2f uv1 = tri_attributes->uv0;
    vert2f uv2 = tri_attributes->uv2;
    f32 c0 = tri_attributes->c1;
    f32 c1 = tri_attributes->c0;
    f32 c2 = tri_attributes->c2;
    f32 avg_c = (c0+c1+c2)/3.0f;
    
    //f32 diffuse = (w0 * c0 + w1 * c1 + w2 * c2) * recip_area;

    f32 diffuse = CLAMP(avg_c, 0.0f, 1.0f);
    u8 quantized_brightness = (u8)(diffuse * (NUM_SHADES-1));
    u8* lit_pal_ptr = light_remap_table[quantized_brightness];



    int drew_pixel = 0;

    f32 v0u_over_z = uv0.x * iz0;
    f32 v0v_over_z = uv0.y * iz0;

    f32 v1u_over_z = uv1.x * iz1;
    f32 v1v_over_z = uv1.y * iz1;

    f32 v2u_over_z = uv2.x * iz2;
    f32 v2v_over_z = uv2.y* iz2;

    int tex_width = tex->width;
    int tex_height = tex->height;
    u8* texels = tex->texels;

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

    const i32_vec dy01_shifted_vec = broadcast_i32_vec(dy01<<5);
    const i32_vec dy12_shifted_vec = broadcast_i32_vec(dy12<<5);
    const i32_vec dy20_shifted_vec = broadcast_i32_vec(dy20<<5);
    const i32_vec dx01_shifted_vec = broadcast_i32_vec(dx01<<5);
    const i32_vec dx12_shifted_vec = broadcast_i32_vec(dx12<<5);
    const i32_vec dx20_shifted_vec = broadcast_i32_vec(dx20<<5); // shift by 4 for subpixel, but double again because we're using 2x2 quad blocks now 


    i32 area = (dx01 * dy20 - dy01 * dx20);
    // barycentric weights weights (scaled by area)
    f32 recip_area = 1.0f / (f32)area;
    f32_vec recip_area_vec = broadcast_f32_vec(recip_area);

    
    // bounding box of triangle (not so good for larger triangles)
    i32 minx = MIN(x0, MIN(x1, x2));
    i32 maxx = MAX(x0, MAX(x1, x2));
    i32 miny = MIN(y0, MIN(y1, y2)); 
    i32 maxy = MAX(y0, MAX(y1, y2));
    // align minx, miny
    //minx &= ~0x10;
    //miny &= ~0x10;
    minx = CLAMP((minx + 15) >> 4, start_x, end_x) & ~1; // mask off low bit to align to 2 pixels
    maxx = CLAMP((maxx + 15) >> 4, start_x, end_x);
    miny = CLAMP((miny + 15) >> 4, start_y, end_y) & ~1; // mask off low bit to align to 2 pixels
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
    #define FIXED_ONE_PX 16

    i32_vec cy01_vec = init_i32_vec(
        c01 + dx01 * startY - dy01 * startX,
        c01 + dx01 * startY - dy01 * (startX+FIXED_ONE_PX),
        c01 + dx01 * (startY+FIXED_ONE_PX) - dy01 * startX,
        c01 + dx01 * (startY+FIXED_ONE_PX) - dy01 * (startX+FIXED_ONE_PX)
    );
    i32_vec cy12_vec = init_i32_vec(
        c12 + dx12 * startY - dy12 * startX,
        c12 + dx12 * startY - dy12 * (startX+FIXED_ONE_PX),
        c12 + dx12 * (startY+FIXED_ONE_PX) - dy12 * startX,
        c12 + dx12 * (startY+FIXED_ONE_PX) - dy12 * (startX+FIXED_ONE_PX)
    );
    i32_vec cy20_vec = init_i32_vec(
        c20 + dx20 * startY - dy20 * startX,
        c20 + dx20 * startY - dy20 * (startX+FIXED_ONE_PX),
        c20 + dx20 * (startY+FIXED_ONE_PX) - dy20 * startX,
        c20 + dx20 * (startY+FIXED_ONE_PX) - dy20 * (startX+FIXED_ONE_PX)
    );
    i32_vec ey01_vec = init_i32_vec(
        e01 + dx01 * startY - dy01 * startX,
        e01 + dx01 * startY - dy01 * (startX+FIXED_ONE_PX),
        e01 + dx01 * (startY+FIXED_ONE_PX) - dy01 * startX,
        e01 + dx01 * (startY+FIXED_ONE_PX) - dy01 * (startX+FIXED_ONE_PX)
    );
    i32_vec ey12_vec = init_i32_vec(
        e12 + dx12 * startY - dy12 * startX,
        e12 + dx12 * startY - dy12 * (startX+FIXED_ONE_PX),
        e12 + dx12 * (startY+FIXED_ONE_PX) - dy12 * startX,
        e12 + dx12 * (startY+FIXED_ONE_PX) - dy12 * (startX+FIXED_ONE_PX)
    );
    i32_vec ey20_vec = init_i32_vec(
        e20 + dx20 * startY - dy20 * startX,
        e20 + dx20 * startY - dy20 * (startX+FIXED_ONE_PX),
        e20 + dx20 * (startY+FIXED_ONE_PX) - dy20 * startX,
        e20 + dx20 * (startY+FIXED_ONE_PX) - dy20 * (startX+FIXED_ONE_PX)
    );



    //const i32_vec i32_zero_vec = broadcast_i32_vec(0);
    for (i32 y = miny; y < maxy; y += 2)
    {
        i32_vec cx01_vec = cy01_vec;
        i32_vec cx12_vec = cy12_vec;
        i32_vec cx20_vec = cy20_vec;
        i32_vec ex01_vec = ey01_vec;
        i32_vec ex12_vec = ey12_vec;
        i32_vec ex20_vec = ey20_vec;

        u8 *row = &ei->screen[y * kScreenWidth + minx];
        u8 *next_row = &ei->screen[(y+1) * kScreenWidth + minx];

        f32 *zbuf_row = &zbuffer[y * kScreenWidth + minx];
        f32 *zbuf_next_row = &zbuffer[(y+1) * kScreenWidth + minx];


        for (i32 x = 0; x < (maxx-minx); x += 2) {
            i32_vec covered_vec = ~((cx01_vec|cx12_vec|cx20_vec)>>31);


            u8 coverage_mask = i32_vec_extract_low_bits(covered_vec);
            //if(coverage_mask == 0xF) {
            //} else 
            if(coverage_mask != 0x0) {
                // skip completely uncovered quads
                f32_vec zbuf_val_vec = init_f32_vec(
                    zbuf_row[0], zbuf_row[1],
                    zbuf_next_row[0], zbuf_next_row[1]
                );

                f32_vec w0_vec = i32_vec_convert_f32(ex12_vec);
                f32_vec w1_vec = i32_vec_convert_f32(ex20_vec);
                f32_vec w2_vec = i32_vec_convert_f32(ex01_vec);
                f32_vec inv_z_vec = ((w0_vec * iz0_vec) + 
                                    (w1_vec * iz1_vec) +
                                    (w2_vec * iz2_vec));
                inv_z_vec = (inv_z_vec * recip_area_vec);

                i32_vec unoccluded = inv_z_vec >= zbuf_val_vec;
                i32_vec in_tri_and_unoccluded = unoccluded & covered_vec;

                u8 mask = i32_vec_extract_low_bits(in_tri_and_unoccluded);
                if(mask != 0) {
                    drew_pixel = 1;

                    i32_vec cbuf_val_vec = init_i32_vec(
                        row[0], row[1],
                        next_row[0], next_row[1]
                    );

                    f32_vec z_vec = 1.0f / inv_z_vec;

                    i32_vec this_tri_color_vec = parallel_pixel_shader(
                        z_vec,
                        w0_vec, w1_vec, w2_vec, v0u_over_z, v0v_over_z, v1u_over_z, v1v_over_z, v2u_over_z, v2v_over_z,
                        recip_area, texels, tex_width, tex_height, 
                        lit_pal_ptr
                    );
                    
                    
                    i32_vec new_color_vec = i32_vec_select(in_tri_and_unoccluded, this_tri_color_vec, cbuf_val_vec);

                    f32_vec new_zbuf_vec = f32_vec_select(in_tri_and_unoccluded, inv_z_vec, zbuf_val_vec);

                    row[0] = (u8)new_color_vec[0];
                    row[1] = (u8)new_color_vec[1];
                    next_row[0] = (u8)new_color_vec[2];
                    next_row[1] = (u8)new_color_vec[3];

                    zbuf_row[0] = new_zbuf_vec[0];
                    zbuf_row[1] = new_zbuf_vec[1];
                    zbuf_next_row[0] = new_zbuf_vec[2];
                    zbuf_next_row[1] = new_zbuf_vec[3];
                }

            }

            // step horizontal edge coverage
            cx01_vec -= dy01_shifted_vec;
            cx12_vec -= dy12_shifted_vec;
            cx20_vec -= dy20_shifted_vec;
            ex01_vec -= dy01_shifted_vec;
            ex12_vec -= dy12_shifted_vec;
            ex20_vec -= dy20_shifted_vec;
            row += 2;
            next_row += 2;
            zbuf_row += 2;
            zbuf_next_row += 2;

        }
        // step vertical edge coverage
        
        cy01_vec += dx01_shifted_vec;
        cy12_vec += dx12_shifted_vec;
        cy20_vec += dx20_shifted_vec;
        ey01_vec += dx01_shifted_vec;
        ey12_vec += dx12_shifted_vec;
        ey20_vec += dx20_shifted_vec;
    }
    return drew_pixel;
}

typedef enum {
    NORMAL,
    TILE_DRAW,
    HI_Z_DRAW,
    NUM_DRAW_MODES
} draw_modes;
int last_x_pushed = 0, last_y_pushed = 0;
int hi_z_enabled = 1, trivial_reject_enabled = 1;
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
            tiles[y*TILES_WIDE+x].num_trivial_triangles = 0;
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
            tiles[y*TILES_WIDE+x].num_trivial_triangles = 0;
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
        mesh_bbox.max_z = MAX(mesh_bbox.max_z, m->vertexStream[v].pos.z);
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

u32 roll_die() {
    return 1+(nextrand()%6);
}

#include "texture_background.h"
#include "texture_green_dragon.h"
#include "texture_red_dragon.h"
#include "texture_white_dragon.h"
#include "texture_north.h"
#include "texture_east.h"
#include "texture_south.h"
#include "texture_west.h"
#include "texture_two_pin.h"
#include "texture_eight_pin.h"
#include "texture_five_man.h"
#include "texture_seven_man.h"
#include "texture_two_sou.h"
#include "texture_board.h"


typedef enum {
    WHITE_DRAGON,
    RED_DRAGON,
    GREEN_DRAGON,
    NORTH,
    EAST,
    SOUTH,
    WEST,
    TWO_PIN,
    EIGHT_PIN,
    FIVE_MAN,
    SEVEN_MAN,
    TWO_SOU,
    NUM_TILES,
    BOARD
} tile_type;


u8 texture_buffer[34][512*512];
texture textures[NUM_TILES+2] = {
    {
        &comp_tex_white_dragon, 0, 512, 512, COMPRESSED
    },
    {
        &comp_tex_red_dragon, 0, 512, 512, COMPRESSED
    },
    {
        &comp_tex_green_dragon, 0, 512, 512, COMPRESSED
    },
    {
        &comp_tex_north, 0, 512, 512, COMPRESSED
    },
    {
        &comp_tex_east, 0, 512, 512, COMPRESSED
    },
    {
        &comp_tex_south, 0, 512, 512, COMPRESSED
    },
    {
        &comp_tex_west, 0, 512, 512, COMPRESSED
    },
    {
        &comp_tex_two_pin, 0, 512, 512, COMPRESSED
    },
    {
        &comp_tex_eight_pin, 0, 512, 512, COMPRESSED
    },
    {
        &comp_tex_five_man, 0, 512, 512, COMPRESSED
    },
    {
        &comp_tex_seven_man, 0, 512, 512, COMPRESSED
    },
    {
        &comp_tex_two_sou, 0, 512, 512, COMPRESSED
    },
    {
        &comp_tex_green_dragon, texture_board, 1, 1, UNCOMPRESSED
    },
    {
        &comp_tex_green_dragon, texture_board, 1, 1, UNCOMPRESSED
    },
};

void decompress_texture(compressed_texture* comp_tex, u8* dst, int num_total_bytes) {
    u8 *packets = comp_tex->compressed_packets;
    u8 *local_palette = comp_tex->palette;
    int packet_idx = 0;
    int dst_idx = 0;
    while(dst_idx < num_total_bytes) {
        u8 packet = packets[packet_idx++];
        u8 global_pal_idx;
        int length;
        if(packet&1) {
            u8 bit = (u8)((packet>>1)&1);
            global_pal_idx = local_palette[bit];
            length = (packet>>2)+1;
        } else {
            // non-white packet
            global_pal_idx = WHITE;
            length = (packet>>1)+1;
        }
        for(int i = 0; i < length; i++) {
            dst[dst_idx++] = global_pal_idx;
        }
    }
}

void decompress_textures() {

    for(int i = 0; i < NUM_TILES; i++) {


        //textures[i].comp_tex_ptr = &comp_tex_east;
        u8* tex_buf = texture_buffer[i];
        textures[i].texels = tex_buf;
        textures[i].width = 512;
        textures[i].height = 512;

        decompress_texture(textures[i].comp_tex_ptr, tex_buf, 512*512);
        for(int y = 0; y < 512; y++) {
            tex_buf[511*512+511] = BLUE; //(y >= 504) ? GOLD : (y >= 496) ? WHITE : BLACK;
        }
    }
    textures[NUM_TILES].texels = texture_board;
    textures[NUM_TILES].width = 1;
    textures[NUM_TILES].height = 1;
    textures[NUM_TILES+1].texels = texture_board;
    textures[NUM_TILES+1].width = 1;
    textures[NUM_TILES+1].height = 1;

}

typedef enum {
    LIT_TEXTURE,
    UNLIT_TEXTURE
} shader;


typedef struct {
    int num_closed_tiles;
    tile_type tiles[14];
    u32 deal_frame_for_tiles[14];
    int wall_index_for_tiles[14];
    int num_open_tiles;
    int selected_tile_idx;
} hand;

#define TILES_IN_DECK 136
#define TILE_FALL_FRAMES 36
#define TILE_SPAWN_POS_Y 20.0f
#define TILE_DEAL_FRAMES 32
typedef struct {
    int rem;
    u32 tile_fall_frame[TILES_IN_DECK];
    tile_type tiles[TILES_IN_DECK];
    int split_distance;
} wall;

typedef struct {
    wall board_wall;
    hand north_hand;
    hand east_hand;
    hand south_hand;
    hand west_hand;
} board;

board game_board;

// 17 tiles
//int split = [
//]

wall init_empty_wall() {
    wall d;
    d.rem = 0;
    //int dice_total = (nextrand()%6)+(nextrand()%6);
    //int split_distance =
    d.split_distance = -1;

    for(int i = 0; i < 136; i++) {
        //d.tiles[i] = nextrand()%NUM_TILES;
    }
    return d;
}

hand init_hand() {
    hand h;
    h.num_closed_tiles = 0;
    h.num_open_tiles = 0;
    return h;
}

hand init_empty_hand() {
    hand h;
    h.num_open_tiles = 0; 
    h.num_closed_tiles = 0;

    h.selected_tile_idx = -1;
    return h;
}



typedef enum {
    INITIAL_SHUFFLE_AND_SETUP,
    DEALING,
    IN_GAME,
    NUM_GAME_STATES
} game_state;

game_state cur_game_state;

u32 frame = 0;
int deal_steps = 0;
void clear_shuffle() {

}

void reset_game(ExotiqueInterface *ei) {
    s[0] = ei->ticks;
    cur_game_state = INITIAL_SHUFFLE_AND_SETUP;
    frame = 0;
    deal_steps = 0;
    clear_shuffle();
    
    game_board.board_wall.rem = 0;
    game_board.north_hand.num_closed_tiles = 0;

    game_board.board_wall = init_empty_wall();
    game_board.north_hand = init_empty_hand();
    game_board.east_hand = init_empty_hand();
    game_board.south_hand = init_empty_hand();
    game_board.west_hand = init_empty_hand();
}

void game_load(ExotiqueInterface* ei) {
    reset_game(ei);

    int i;
    int last_used_pal_idx = 0;

    // load lighting palette into the first 1+(NUM_SHADES)*(NUM_BASE_COLORS-1) palette slots.
    // one slot is used for black, the others get NUM_SHADES variants.
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
                light_remap_table[shade][i] = (u8)(base+i);
            }
        }
    }
    int num_bkgd_pal_entries = sizeof(palette_background)/sizeof(u32);

    for(i = 0; i < num_bkgd_pal_entries; i++) {
        u32 pal_entry = (palette_background[i]<<8)|0xFF;
        ei->palette[last_used_pal_idx+i+1] = pal_entry;
    }
    for(i = 0; i < BACKGROUND_TEX_HEIGHT*BACKGROUND_TEX_WIDTH; i++) {
        texture_background[i] = (u8) (texture_background[i] + (last_used_pal_idx+1));
    }
    decompress_textures();

    init_tiles();

    tile_bbox = get_mesh_bbox(&tile_mesh);

    //game_board.deadwall.rem -= (13*4);
}

//#define NUM_SHUFFLE_FRAMES 136*2
void step_shuffle_and_setup(u32 cur_frame) {
    if((cur_frame&3) != 3) {
        return;
    }
    if(game_board.board_wall.rem == TILES_IN_DECK) {
        if(game_board.board_wall.tile_fall_frame[game_board.board_wall.rem-1]+TILE_FALL_FRAMES <= cur_frame) {
            game_board.board_wall.split_distance = (int)(roll_die() + roll_die());
            cur_game_state = DEALING;
        }
    } else {
        game_board.board_wall.tile_fall_frame[game_board.board_wall.rem] = cur_frame;
        game_board.board_wall.tiles[game_board.board_wall.rem++] = nextrand()%NUM_TILES;
    }
}

void step_deal(u32 cur_frame) {
    // start with east
    if((cur_frame & 31) != 0) {
        return;
    }
    hand* hands_in_order[4] = {
        &game_board.east_hand, 
        &game_board.south_hand, 
        &game_board.north_hand, 
        &game_board.west_hand
    };

    if(deal_steps < 12) {
        int hand_index = deal_steps & 0x3;
        int cur_num_tiles = hands_in_order[hand_index]->num_closed_tiles;
        for(int i = 0; i < 4; i++) {
            hands_in_order[hand_index]->deal_frame_for_tiles[cur_num_tiles] = cur_frame;
            hands_in_order[hand_index]->wall_index_for_tiles[cur_num_tiles] = --game_board.board_wall.rem;
            hands_in_order[hand_index]->tiles[cur_num_tiles++] = game_board.board_wall.tiles[game_board.board_wall.rem];
        }
        hands_in_order[hand_index]->num_closed_tiles = cur_num_tiles;
    } else if (deal_steps < 16) {
        int hand_index = deal_steps & 0x3;
        int cur_num_tiles = hands_in_order[hand_index]->num_closed_tiles;
        hands_in_order[hand_index]->deal_frame_for_tiles[cur_num_tiles] = cur_frame;
        hands_in_order[hand_index]->wall_index_for_tiles[cur_num_tiles] = game_board.board_wall.rem;
        hands_in_order[hand_index]->tiles[cur_num_tiles++] = game_board.board_wall.tiles[--game_board.board_wall.rem];
        hands_in_order[hand_index]->num_closed_tiles = cur_num_tiles;
    } else {
        int hand_index = deal_steps & 0x3;
        int cur_num_tiles = hands_in_order[hand_index]->num_closed_tiles;
        hands_in_order[hand_index]->deal_frame_for_tiles[cur_num_tiles] = cur_frame;
        hands_in_order[hand_index]->wall_index_for_tiles[cur_num_tiles] = --game_board.board_wall.rem;
        hands_in_order[hand_index]->tiles[cur_num_tiles++] = game_board.board_wall.tiles[game_board.board_wall.rem];
        hands_in_order[hand_index]->num_closed_tiles = cur_num_tiles;
    }
    deal_steps++;
    if(deal_steps == 17) {
        cur_game_state = IN_GAME;
    }
}



void game_update(ExotiqueInterface* ei) {
    if(ei->input->select) {
        reset_game(ei);
    }
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
    //if(!last_y_pushed && ei->input->y) {
    //    trivial_reject_enabled = !trivial_reject_enabled;
    //}
    
    switch(cur_game_state) {
        case INITIAL_SHUFFLE_AND_SETUP:
            step_shuffle_and_setup(frame);
            break;
        case DEALING:
            //if(!last_y_pushed && ei->input->y) {
                step_deal(frame);
            //}
            break;
        case IN_GAME:
            break;
        default:
        case NUM_GAME_STATES:
            break;
    }
    frame++;
    last_x_pushed = ei->input->x;
    last_y_pushed = ei->input->y;
}


void draw_tile(ExotiqueInterface *ei, f32 *zbuffer, tile* t) {
    (void)ei; (void)zbuffer;
    u32 i;
    u32 num_tris = t->num_triangles;
    u32 num_trivial_tris = t->num_trivial_triangles;
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
   
    int drew_pix = 0;
    for(i = 0; i < num_trivial_tris; i++) {
        u32 global_tri_idx = t->trivial_tri_indexes[i];
        drew_pix = trivial_triangle(
            ei,
            zbuffer,
            &global_tri_buffer[global_tri_idx],
            &textures[global_tri_buffer[global_tri_idx].tex],
            t->start_x, t->start_x+TILE_SIZE,
            t->start_y, t->start_y+TILE_SIZE
        );
    }

    for(i = 0; i < num_tris; i++) {
        u32 global_tri_idx = t->tri_indexes[i];
        drew_pix = triangle(
            ei,
            zbuffer,
            &global_tri_buffer[global_tri_idx],
            &textures[global_tri_buffer[global_tri_idx].tex],
            t->start_x, t->start_x+TILE_SIZE,
            t->start_y, t->start_y+TILE_SIZE
        );
    }   
    t->z_dirty = t->z_dirty || drew_pix;

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

            //u32 num_tris = tiles[y*TILES_WIDE+x].num_triangles;
            //exotique_printf("tile %i,%i => tris: %i\n", x, y, num_tris);
            //tot_tris_in_tiles += num_tris;
            //max_tris_in_a_tile = MAX(num_tris, max_tris_in_a_tile);
            
            //if(num_tris) {
                draw_tile(ei, zbuffer, &tiles[y*TILES_WIDE+x]);
            //}
        }
    }
    //exotique_printf("avg tris per tile %f, total tris %i\n", (double)tot_tris_in_tiles / (double)(TILES_WIDE*TILES_HIGH), total_triangles);
    //exotique_printf("max tris in a tile %i\n", max_tris_in_a_tile);
}


f32 orient2d(vert3f *a, vert3f *b, vert2f *c) {
    return ((f32)b->x-(f32)a->x)*((f32)c->y-(f32)a->y) - ((f32)b->y-(f32)a->y)*((f32)c->x-(f32)a->x);
}


edge_prep calc_edges(vert3f *v1, vert3f *v0, vert3f *v2) {
    const i32 x0 = (i32)(v0->x * 16.0f);
    const i32 y0 = (i32)(v0->y * 16.0f);
    const i32 x1 = (i32)(v1->x * 16.0f);
    const i32 y1 = (i32)(v1->y * 16.0f);
    const i32 x2 = (i32)(v2->x * 16.0f);
    const i32 y2 = (i32)(v2->y * 16.0f);

    const i32 dx01 = x0 - x1;
    const i32 dy01 = y0 - y1;
    const i32 dx12 = x1 - x2;
    const i32 dy12 = y1 - y2;
    const i32 dx20 = x2 - x0;
    const i32 dy20 = y2 - y0;

    i32 c01 = dy01 * x0 - dx01 * y0;
    i32 c12 = dy12 * x1 - dx12 * y1;
    i32 c20 = dy20 * x2 - dx20 * y2;
    if (dy01 < 0 || (dy01 == 0 && dx01 > 0)) {
        c01++;
    }
    if (dy12 < 0 || (dy12 == 0 && dx12 > 0)) {
        c12++;
    }
    if (dy20 < 0 || (dy20 == 0 && dx20 > 0)) {
        c20++;
    }
    return (edge_prep) {
        .active_edges = 3,
        //.c0 = c01, .c1 = c12, .c2 = c20,
        .cvals = {c01,c12,c20},
        .dxvals = {dx01,dx12,dx20}, 
        .dyvals = {dy01,dy12,dy20}
        //= dx01, .dy0 = dy01, 
        //.dx1 = dx12, .dy1 = dy12, 
        //.dx2 = dx20, .dy2 = dy20
    };
}

typedef enum {
    TRIVIAL_REJECT,
    TRIVIAL_ACCEPT,
    NOT_TRIVIAL
} trivial_res;

trivial_res check_trivial_reject_accept(int tile_x, int tile_y, edge_prep tri_edges) { //vert3f *v1, vert3f *v0, vert3f *v2) {   
    // if all four corners are outside of any of the one edges
    
    i32 c0 = tri_edges.cvals[0];
    i32 c1 = tri_edges.cvals[1];
    i32 c2 = tri_edges.cvals[2];
    i32 dx0 = tri_edges.dxvals[0];
    i32 dx1 = tri_edges.dxvals[1];
    i32 dx2 = tri_edges.dxvals[2];
    i32 dy0 = tri_edges.dyvals[0];
    i32 dy1 = tri_edges.dyvals[1];
    i32 dy2 = tri_edges.dyvals[2];

    i32 lx = (tile_x*TILE_SIZE) * 16;
    i32 rx = ((tile_x*TILE_SIZE)+TILE_SIZE-1)*16;
    i32 ty = (tile_y*TILE_SIZE) * 16;
    i32 by = ((tile_y*TILE_SIZE)+TILE_SIZE-1)*16;
    
    i32 top_left01 = c0 + dx0 * ty - dy0 * lx;
    i32 top_right01 = c0 + dx0 * ty - dy0 * rx;
    i32 bot_left01 = c0 + dx0 * by - dy0 * lx;
    i32 bot_right01 = c0 + dx0 * by - dy0 * rx;
    i32 most_positive01 = MAX(top_left01, MAX(top_right01, MAX(bot_left01, bot_right01)));
    i32 most_negative01 = MIN(top_left01, MIN(top_right01, MIN(bot_left01, bot_right01)));
    if(most_positive01 < 0) {
        return TRIVIAL_REJECT;
    } else if (most_negative01 < 0) {
        return NOT_TRIVIAL;
    }

    i32 top_left12 = c1 + dx1 * ty - dy1 * lx;
    i32 top_right12 = c1 + dx1 * ty - dy1 * rx;
    i32 bot_left12 = c1 + dx1 * by - dy1 * lx;
    i32 bot_right12 = c1 + dx1 * by - dy1 * rx;
    i32 most_positive12 = MAX(top_left12, MAX(top_right12, MAX(bot_left12, bot_right12)));
    i32 most_negative12 = MIN(top_left12, MIN(top_right12, MIN(bot_left12, bot_right12)));
    if(most_positive12 < 0) {
        return TRIVIAL_REJECT;
    } else if (most_negative12 < 0) {
        return NOT_TRIVIAL;
    }

    i32 top_left20 = c2 + dx2 * ty - dy2 * lx;
    i32 top_right20 = c2 + dx2 * ty - dy2 * rx;
    i32 bot_left20 = c2 + dx2 * by - dy2 * lx;
    i32 bot_right20 = c2 + dx2 * by - dy2 * rx;
    i32 most_positive20 = MAX(top_left20, MAX(top_right20, MAX(bot_left20, bot_right20)));
    i32 most_negative20 = MIN(top_left20, MIN(top_right20, MIN(bot_left20, bot_right20)));
    if(most_positive20 < 0) {
        return TRIVIAL_REJECT;
    } else if (most_negative20 < 0) {
        return NOT_TRIVIAL;
    }


    return TRIVIAL_ACCEPT;
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
    u8 texture_id) {

        if(total_triangles == MAX_GLOBAL_TRIS) {
            return;
        }

        f32 inv_z0 = 1.0f/v0->z;
        f32 inv_z1 = 1.0f/v1->z;
        f32 inv_z2 = 1.0f/v2->z;
        //f32 closest_inv_z = MAX(inv_z0, MAX(inv_z1, inv_z2));




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

        edge_prep tri_edges = calc_edges(v0, v1, v2);

        int rasterized_at_least_once = 0;
        for(int y = tile_start_y; y <= tile_end_y; y++) {
            for(int x = tile_start_x; x <= tile_end_x; x++) {
                u32 num_tris_in_tile = tiles[y*TILES_WIDE+x].num_triangles;
                u32 num_trivial_tris_in_tile = tiles[y*TILES_WIDE+x].num_trivial_triangles;

                trivial_res triv = check_trivial_reject_accept(x, y, tri_edges);
                if(triv == TRIVIAL_REJECT) {
                    continue;
                }

                if(0) { // triv == TRIVIAL_ACCEPT) {
                    //continue;
                    if(num_trivial_tris_in_tile == MAX_TILE_TRIS) {
                        continue;
                    }
                    rasterized_at_least_once = 1;
                    tiles[y*TILES_WIDE+x].trivial_tri_indexes[num_trivial_tris_in_tile++] = total_triangles;
                    tiles[y*TILES_WIDE+x].num_trivial_triangles = num_trivial_tris_in_tile;
                } else {
                    if(num_tris_in_tile == MAX_TILE_TRIS) {
                        continue;
                    }
                    rasterized_at_least_once = 1;
                    tiles[y*TILES_WIDE+x].tri_indexes[num_tris_in_tile++] = total_triangles;
                    tiles[y*TILES_WIDE+x].num_triangles = num_tris_in_tile;

                }
               
                
                //exotique_printf("N");

                // if our closest inv_z is further than this, it's HI-Z culled baby
                //f32 this_tiles_furthest_inv_z = tiles[y*TILES_WIDE+x].max_z;
                // but since it's inv_z, the value gets smaller as it gets further away
                //if(hi_z_enabled && closest_inv_z < this_tiles_furthest_inv_z) {
                //    triangles_hi_z_culled++;
                //    continue;
                //}
                //triangles_rasterized++;
                
                // filter out those edges that are INSIDE for ALL vertexes of the tile
                //edge_prep tile_tri_edges = process_edges_for_tile(x, y, tri_edges);



                
            }
        }
        if(rasterized_at_least_once) {
            triangles_rasterized += 1;
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
            global_tri_buffer[total_triangles++].tex = texture_id;
        }
}

int meshes_transformed = 0;
int triangles_transformed = 0;


void submit_mesh_draw_call(
    mesh_draw_call* mdc
) {
    obj_mesh *m = mdc->mesh;
    matrix *model_to_view = &mdc->model_to_view;
    matrix *model_to_world = &mdc->model_to_world;
    u8 texture_id = mdc->texture;

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
            texture_id
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


const f32 wall_tile_spacing = 1.61f;

f32 calc_wall_tile_x_position(int tot_tile_idx) {  
    f32 wall_length = (f32)(17 * wall_tile_spacing);
    f32 half = wall_length / 2.0f;

    int wall_side = tot_tile_idx/34;
    int row_on_wall = ((wall_side*34)-tot_tile_idx)/2;
    //int top = (tot_tile_idx&1);
    f32 start_position = half;
    f32 row_x_offset = (f32)row_on_wall * wall_tile_spacing;

    f32 x_position = start_position + row_x_offset;
    
    return x_position;
}

f32 calc_wall_tile_y_position(u32 cur_frame, wall *d, int tot_tile_idx) {
    f32 tile_fall_pos = CLAMP(
        (f32)(cur_frame - d->tile_fall_frame[tot_tile_idx]) / (f32)TILE_FALL_FRAMES, 
        0.0f, 1.0f);
    int top = (tot_tile_idx&1);

    f32 lerp_top_position = lerp(1.40f, TILE_SPAWN_POS_Y, 1.0f-tile_fall_pos);
    f32 lerp_bot_position = lerp(0.0f, TILE_SPAWN_POS_Y, 1.0f-tile_fall_pos);
    return top ? lerp_top_position : lerp_bot_position;//1.40f : 0.0f;

}

f32 wall_offsets_x[4] = {17.0f, 0.0f, -17.0f, 0.0f};
f32 wall_offsets_z[4] = {0.0f, 17.0f, 0.0, -17.0f};
f32 wall_rots_y[4] = {(f32)M_PI * 0.5f, 0.0f, -(f32)M_PI * 0.5f, (f32)M_PI};

vert3f calc_wall_tile_global_position(u32 cur_frame, wall* w, int tot_tile_idx) {
    transform wall_trans = identity_transform();
    int wall_side = tot_tile_idx/34;
    wall_trans.position.x = wall_offsets_x[wall_side];
    wall_trans.position.z = wall_offsets_z[wall_side];
    wall_trans.rotation.y = wall_rots_y[wall_side];
    matrix wall_matrix = transform_to_matrix(&wall_trans);

    vert3f local_pos;
    local_pos.x = calc_wall_tile_x_position(tot_tile_idx);
    local_pos.y = calc_wall_tile_y_position(cur_frame, w, tot_tile_idx);
    local_pos.z = 0.0f;

    return mat_mul_vert3(&wall_matrix, &local_pos);
}

const f32 hand_tile_spacing = 2.0f;

f32 calc_hand_x_position(int idx) {
    f32 whole_hand_width = (f32)(13 * 2);
    f32 half = whole_hand_width / hand_tile_spacing;
    f32 base_position_x = half;
    return base_position_x - hand_tile_spacing*(f32)idx;
}
f32 calc_hand_y_position(hand* h, int idx) {
    return (h->selected_tile_idx == idx ? 1.47f : 0.455f);
}

vert3f calc_global_hand_position(hand* h, int idx, matrix* hand_to_world_matrix) {
    f32 whole_hand_width = (f32)(13 * 2);
    f32 half = whole_hand_width / hand_tile_spacing;
    f32 base_position_x = half;
    vert3f local;
    local.x = base_position_x - hand_tile_spacing*(f32)idx;
    local.y = calc_hand_y_position(h, idx);
    local.z = 0.0f;
    return mat_mul_vert3(hand_to_world_matrix, &local);
}

void draw_hand(u32 cur_frame, wall* w, hand* h, matrix* hand_to_view_matrix, matrix* hand_to_world_matrix) {
    (void)cur_frame; (void)w;
    mesh_draw_call draw_calls[14];



    int draw_idx = 0;

    int standard_draw_tiles = MIN(13, h->num_closed_tiles + h->num_open_tiles);
    int has_extra_tile = (h->num_closed_tiles + h->num_open_tiles) != standard_draw_tiles;

    matrix world_to_hand = mat_inverse_affine(hand_to_world_matrix);

    for(int i = 0; i < standard_draw_tiles; i++) {
        //int is_open = 0;//(i >= h->num_closed_tiles);

        f32 anim_progress = CLAMP(((f32)(cur_frame - h->deal_frame_for_tiles[i]) / (f32)TILE_DEAL_FRAMES), 0.0f, 1.0f);
        vert3f local;
        if(anim_progress >= 1.0f) {
            local.x = calc_hand_x_position(i);
            local.y = calc_hand_y_position(h, i);
            local.z = 0.0f;
        } else {
            vert3f global_wall_position = calc_wall_tile_global_position(cur_frame, w, h->wall_index_for_tiles[i]);
            vert3f global_hand_position = calc_global_hand_position(h, i, hand_to_world_matrix);


            vert3f lerp_pos;
            lerp_pos.x = lerp(global_wall_position.x, global_hand_position.x, anim_progress);
            lerp_pos.y = lerp(global_wall_position.y, global_hand_position.y, anim_progress);
            lerp_pos.z = lerp(global_wall_position.z, global_hand_position.z, anim_progress);
            local = mat_mul_vert3(&world_to_hand, &lerp_pos);
        }


        transform tile_trans = identity_transform();
        tile_trans.position.x = local.x;  //calc_hand_x_position(i); //lerp_x; //position;
        tile_trans.position.y = local.y;  //calc_hand_y_position(i); //position_y; //lerp_y; //(h->selected_tile_idx == i ? 1.47f : 0.455f);
        tile_trans.position.z = local.z;  //0.0f; //lerp_z;
        tile_trans.rotation.x = 1.57f; // rotate back towards player


        matrix tile_mat = transform_to_matrix(&tile_trans);
        matrix tile_to_view_matrix = mat_mul_mat(hand_to_view_matrix, &tile_mat);
        matrix tile_to_world_matrix = mat_mul_mat(hand_to_world_matrix, &tile_mat);

        draw_calls[draw_idx].mesh = &tile_mesh;
        draw_calls[draw_idx].texture = h->tiles[i];
        draw_calls[draw_idx].model_to_view = tile_to_view_matrix;
        draw_calls[draw_idx].model_to_world = tile_to_world_matrix;
        //draw_calls[draw_idx].model_to_view = hand_to_view_matrix;
        draw_idx++;
    }

    if(has_extra_tile) {
        
        transform tile_trans = identity_transform();
        tile_trans.position.x = calc_hand_x_position(h->num_open_tiles);
        tile_trans.rotation.x = 1.57f; // rotate back towards player
        tile_trans.position.y = calc_hand_y_position(h, 13);
        matrix tile_mat = transform_to_matrix(&tile_trans);

        matrix tile_to_view_matrix = mat_mul_mat(hand_to_view_matrix, &tile_mat);
        matrix tile_to_world_matrix = mat_mul_mat(hand_to_world_matrix, &tile_mat);

        draw_calls[draw_idx].mesh = &tile_mesh;
        draw_calls[draw_idx].texture = h->tiles[13];
        draw_calls[draw_idx].model_to_view = tile_to_view_matrix;
        draw_calls[draw_idx].model_to_world = tile_to_world_matrix;
        draw_idx++;
    }

    /*
        draw open tiles

    */
    for(int i = h->num_closed_tiles; i < standard_draw_tiles; i++) {
       //int is_open = 1;//(i >= h->num_closed_tiles);


        transform tile_trans = identity_transform();
        tile_trans.position.x = calc_hand_x_position(h->num_open_tiles + (has_extra_tile) ? 1 : 0);
        tile_trans.rotation.x = 0.0f; // rotate back towards player
        tile_trans.position.y = 0.0f;
        matrix tile_mat = transform_to_matrix(&tile_trans);

        matrix tile_to_view_matrix = mat_mul_mat(hand_to_view_matrix, &tile_mat);
        matrix tile_to_world_matrix = mat_mul_mat(hand_to_world_matrix, &tile_mat);

        draw_calls[draw_idx].mesh = &tile_mesh;
        draw_calls[draw_idx].texture = h->tiles[i];
        draw_calls[draw_idx].model_to_view = tile_to_view_matrix;
        draw_calls[draw_idx].model_to_world = tile_to_world_matrix;
        draw_idx++;
    }

    
    submit_draw_calls(draw_calls, draw_idx, FRUSTUM_CULL);
}


void draw_wall(u32 cur_frame, wall *d, matrix *view_mat) {



    mesh_draw_call draw_calls[34*4];

    int draw_idx = 0;

    for(int i = 0; i < 4; i++) {

        transform wall_trans = identity_transform();
        wall_trans.position.x = wall_offsets_x[i];
        wall_trans.position.z = wall_offsets_z[i];
        wall_trans.rotation.y = wall_rots_y[i];

        matrix wall_matrix = transform_to_matrix(&wall_trans);
        matrix wall_view_matrix = mat_mul_mat(view_mat, &wall_matrix);



        for(int j = 0; j < 34; j++) {
            int tot_tile_idx = i*34 + j;

            if(tot_tile_idx >= d->rem) {
                break;
            }
            int top = (j&1);

            if(top) {
                if ((tot_tile_idx+d->split_distance) == 136) {
                    //exotique_printf("split distance %i\n", d->split_distance);
                    //position -= 1.0f;
                }
            }


            tile_type this_tile = d->tiles[tot_tile_idx];

            transform tile_trans = identity_transform();
            tile_trans.position.x = calc_wall_tile_x_position(tot_tile_idx); //this_row_position;
            tile_trans.rotation.x = (f32)M_PI;

            tile_trans.position.y = calc_wall_tile_y_position(cur_frame, d, tot_tile_idx);


            matrix tile_mat = transform_to_matrix(&tile_trans);
            matrix tile_to_view_matrix = mat_mul_mat(&wall_view_matrix, &tile_mat);
            matrix tile_to_world_matrix = mat_mul_mat(&wall_matrix, &tile_mat);

            draw_calls[draw_idx].mesh = &tile_mesh;
            draw_calls[draw_idx].model_to_view = tile_to_view_matrix;
            draw_calls[draw_idx].model_to_world = tile_to_world_matrix;
            draw_calls[draw_idx++].texture = this_tile;


        }
    }

    submit_draw_calls(draw_calls, draw_idx, FRUSTUM_CULL);
}


void draw_board(ExotiqueInterface *ei, u32 cur_frame, board* b, matrix* view_mat) {
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

    draw_hand(cur_frame, &b->board_wall, &b->east_hand, &east_view_matrix, &east_matrix);
    draw_hand(cur_frame, &b->board_wall, &b->south_hand, &south_view_matrix, &south_matrix);
    draw_hand(cur_frame, &b->board_wall, &b->west_hand, &west_view_matrix, &west_matrix);
    draw_hand(cur_frame, &b->board_wall, &b->north_hand, &north_view_matrix, &north_matrix);

    draw_wall(cur_frame, &b->board_wall, view_mat);


    mesh_draw_call draw_board_call;
    transform board_transform = identity_transform();
    board_transform.position.y = -0.73f;
    board_transform.scale.x = 32.0f;
    board_transform.scale.z = 32.0f;
    board_transform.scale.y = 2.0f;
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
    u64 cur_frame_ticks = ei->ticks;

    exotique_printf("%llu ms\n", cur_frame_ticks - last_frame_ticks);
    exotique_printf("trivial reject %i\n", trivial_reject_enabled);
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
    draw_board(ei, frame, &game_board, &view_mat);


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