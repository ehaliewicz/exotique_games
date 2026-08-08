#include "exotique.h"
#include "miniaudio.h"

#define OUTPUT_TILE_SIZE 64
#define RENDER_TILE_SIZE (2*OUTPUT_TILE_SIZE)
#define TILE_ROUND(x) ((x+OUTPUT_TILE_SIZE-1)&(~(OUTPUT_TILE_SIZE-1)))
#define OUTPUT_WIDTH TILE_ROUND(1920)
#define OUTPUT_HEIGHT TILE_ROUND(1080)
#define RENDER_WIDTH (2*OUTPUT_WIDTH)
#define RENDER_HEIGHT (2*OUTPUT_HEIGHT)
const int kScreenWidth = OUTPUT_WIDTH;
const int kScreenHeight = OUTPUT_HEIGHT;

u8 render_target[RENDER_TILE_SIZE*RENDER_TILE_SIZE] __attribute__((aligned(64)));
u16 zbuf[RENDER_TILE_SIZE*RENDER_TILE_SIZE] __attribute__((aligned(64)));

#define TILES_WIDE (RENDER_WIDTH/RENDER_TILE_SIZE)
#define TILES_HIGH (RENDER_HEIGHT/RENDER_TILE_SIZE)

typedef struct block block;

struct block {
    const char* name;
    u64 count, cumulative_count;
    u64 cur_start_ticks, total_ticks, cumulative_total_ticks;
    int num_children;
    block *children[16];
};

void enter_block(block* blk) {
    blk->cur_start_ticks = exotique_get_ticks();
}

void exit_block(block* blk) {
    blk->count++;
    blk->total_ticks = exotique_get_ticks() - blk->cur_start_ticks;
    blk->cumulative_count++;
    blk->cumulative_total_ticks += blk->total_ticks;
}

block new_timed_block(const char* name) {
    return (block){name,0,0,0,0,0,0,{NULL}};
}

#define ROOT_TIMED_BLOCK(var, name) {name, 0, 0, 0, 0, 0, 0, {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}}; do {   \
    enter_block(&var);                           \

#define START_TIMED_BLOCK(var, name, parent) {name, 0, 0, 0, 0, 0, 0, {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}}; static int init ## var = 0; \
    if(! init ## var ) { block_add_child(&parent, &var); init ## var = 1; }     \
    enter_block(&var);    do {          \

#define END_TIMED_BLOCK(blk)    \
    exit_block(&blk);           \
} while(0);                     \

void print_prefix(int depth) {
    for(int i = 0; i < depth; i++) {
        exotique_printf("  ");
    }
    exotique_printf("-");
}

void print_and_reset_block(block *blk, int depth) {
    double total_per_tick = (double)blk->cumulative_total_ticks/(double)blk->cumulative_count;
    u64 children_total = 0;
    u64 children_cumulative_total = 0;
    for(int i = 0; i < blk->num_children; i++) {
        children_total += blk->children[i]->total_ticks;
        children_cumulative_total += blk->children[i]->cumulative_total_ticks;
    }
    u64 exclusive_total = blk->total_ticks - children_total;
    u64 exclusive_cumulative_total = blk->cumulative_total_ticks - children_cumulative_total;
    double exclusive_per_tick = (double)exclusive_cumulative_total / (double)blk->cumulative_count;

    print_prefix(depth); exotique_printf("%11s %5llums   %5.3fms  %5llums   %5.3fms\n", blk->name, blk->total_ticks, total_per_tick, exclusive_total, exclusive_per_tick);
        
    for(int i = 0; i < blk->num_children; i++) {
        print_and_reset_block(blk->children[i], depth+1);
    }
    //print_prefix(depth);
    //exotique_printf("END %s\n", blk->name);

    blk->count = 0;
    blk->total_ticks = 0;
}

void print_and_reset_root_block(block *blk) {
    exotique_printf("                 inc   inc per      exc   exc per\n", blk->name);
    print_and_reset_block(blk, 0);
}

void block_add_child(block *parent, block *child) {
    parent->children[parent->num_children++] = child;
}


//    DATA TYPES, SCALAR AND VERTEX/VECTOR MATH

typedef struct {
    f32 x,y;
} vert2f;

typedef struct {
    f32 x,y,z;
} vert3f;

typedef struct {
    i32 x,y,z;
} vert3i;

typedef struct {
    i32 x,y;
} vert2i;

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

vert3f lerp_vert3f(vert3f a, vert3f b, f32 mix) {
    return (vert3f){lerp(a.x,b.x, mix), lerp(a.y,b.y, mix), lerp(a.z,b.z, mix)};
}

static inline f32 fast_atan2(f32 y, f32 x) {
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

static inline f32 fast_inv_sqrt( f32 number ) {
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

static inline vert3f normalize(vert3f v) {
    f32 recip_len = fast_inv_sqrt(dot(v,v));
    //f32 recip_len = 1.0f / len;

    vert3f out = {
        v.x * recip_len,
        v.y * recip_len,
        v.z * recip_len
    };

    return out;
}

static inline vert3f scale_vert3(vert3f a, f32 b) {
    return (vert3f){.x = a.x*b, .y = a.y*b, .z = a.z*b};
}

static inline vert2f scale_vert2(vert2f a, f32 b) {
    return (vert2f){.x = a.x*b, .y = a.y*b};
}

static inline vert3f add_vert3(vert3f a, vert3f b) {
    return (vert3f){.x = a.x+b.x, .y = a.y+b.y, .z = a.z+b.z};
}

static inline int fast_floor(f32 x) {
    int i = (int)x;
    return i - (i > x);
}

static inline int fast_ceil(f32 x) {
    int i = (int)x;
    return i + (i < x);
}


//    MATRIXES AND TRANSFORMS


typedef struct {
    float m[4][4];
} matrix;

typedef struct {
    vert3f position;
    vert3f rotation;
    vert3f scale;
} transform;

matrix mat_mul_mat(const matrix *a, const matrix *b) { 
    matrix r; 
    for (int i = 0; i < 4; ++i) { 
         r.m[i][0] = (a->m[i][0] * b->m[0][0] + a->m[i][1] * b->m[1][0] + a->m[i][2] * b->m[2][0] + a->m[i][3] * b->m[3][0]);
         r.m[i][1] = (a->m[i][0] * b->m[0][1] + a->m[i][1] * b->m[1][1] + a->m[i][2] * b->m[2][1] + a->m[i][3] * b->m[3][1]);
         r.m[i][2] = (a->m[i][0] * b->m[0][2] + a->m[i][1] * b->m[1][2] + a->m[i][2] * b->m[2][2] + a->m[i][3] * b->m[3][2]);
         r.m[i][3] = (a->m[i][0] * b->m[0][3] + a->m[i][1] * b->m[1][3] + a->m[i][2] * b->m[2][3] + a->m[i][3] * b->m[3][3]);
    } 
    return r; 
}

matrix mat_inverse_affine(const matrix *a) {
    float m00 = a->m[0][0], m01 = a->m[0][1], m02 = a->m[0][2];
    float m10 = a->m[1][0], m11 = a->m[1][1], m12 = a->m[1][2];
    float m20 = a->m[2][0], m21 = a->m[2][1], m22 = a->m[2][2];

    float det = m00 * (m11 * m22 - m12 * m21) - m01 * (m10 * m22 - m12 * m20) + m02 * (m10 * m21 - m11 * m20);

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

matrix scale_matrix(vert3f scale) { 
    matrix r = {0}; 
    r.m[0][0] = scale.x; 
    r.m[1][1] = scale.y; 
    r.m[2][2] = scale.z;
    r.m[3][3] = 1.0f; 
    return r; 
} 

matrix translation_matrix(vert3f translate) { 
    matrix r = {0}; 
    r.m[0][0] = 1.0f; 
    r.m[1][1] = 1.0f; 
    r.m[2][2] = 1.0f;
    r.m[3][3] = 1.0f; 
    r.m[0][3] = translate.x; 
    r.m[1][3] = translate.y;
    r.m[2][3] = translate.z; 
    return r; 
} 

matrix rotation_x_matrix(f32 angle) { 
    f32 s = sinf(angle); 
    f32 c = cosf(angle); 
    matrix r = { {
        {1,0,0,0},
        {0,c,-s,0},
        {0,s,c,0},
        {0,0,0,1}
        }
    };
    return r; 
}

matrix rotation_y_matrix(f32 angle) { 
    f32 s = sinf(angle); 
    f32 c = cosf(angle); 
    matrix r = { {
        {c,0,s,0},
        {0,1,0,0},
        {-s,0,c,0},
        {0,0,0,1}
        }
    };
    return r; 
}

matrix rotation_z_matrix(f32 angle) { 
    f32 s = sinf(angle); 
    f32 c = cosf(angle); 
    matrix r = { {
        {c,-s,0,0},
        {s,c,0,0},
        {0,0,1,0},
        {0,0,0,1}
        }
    };
    return r; 
}

matrix transform_to_matrix(const transform *t) { 
    matrix sc = scale_matrix(
        t->scale
    );
    matrix tr = translation_matrix(t->position);
    matrix rx = rotation_x_matrix(t->rotation.x);
    matrix ry = rotation_y_matrix(t->rotation.y);
    matrix rz = rotation_z_matrix(t->rotation.z);

    matrix r = mat_mul_mat(&rz, &ry);
    r = mat_mul_mat(&r, &rx);
    matrix rs = mat_mul_mat(&r, &sc);
    return mat_mul_mat(&tr, &rs);

}

matrix transform_to_view_matrix(const transform *cam)
{
    matrix t = translation_matrix(scale_vert3(cam->position, -1.0f));
    matrix rx = rotation_x_matrix(-cam->rotation.x);
    matrix ry = rotation_y_matrix(-cam->rotation.y);
    matrix r = mat_mul_mat(&rx, &ry);
    return mat_mul_mat(&r, &t);
}

vert3f mat_mul_vert3(const matrix *m, const vert3f *v) {
    vert3f r;
    r.x = m->m[0][0] * v->x + m->m[0][1] * v->y + m->m[0][2] * v->z + m->m[0][3];
    r.y = m->m[1][0] * v->x + m->m[1][1] * v->y + m->m[1][2] * v->z + m->m[1][3];
    r.z = m->m[2][0] * v->x + m->m[2][1] * v->y + m->m[2][2] * v->z + m->m[2][3];
    return r;
}

vert3f mat_mul_normal(const matrix *m, const vert3f *n) {
    vert3f r;
    r.x = m->m[0][0] * n->x + m->m[0][1] * n->y + m->m[0][2] * n->z;
    r.y = m->m[1][0] * n->x + m->m[1][1] * n->y + m->m[1][2] * n->z;
    r.z = m->m[2][0] * n->x + m->m[2][1] * n->y + m->m[2][2] * n->z;

    return normalize(r);
}

//
//
//    VECTORIZED/SIMD math
//
//

#define VEC_LANES 4

typedef u16 u16_vec __attribute__((vector_size(2*VEC_LANES)));
typedef f32 f32_vec __attribute__((vector_size(4*VEC_LANES)));
typedef i32 i32_vec __attribute__((vector_size(4*VEC_LANES)));

static inline f32_vec broadcast_f32_vec(f32 a) {
    return (f32_vec){ a, a, a, a};
}

static inline  f32_vec lerp_f32_vec(f32_vec a, f32_vec b, f32_vec mix) {
    return a + ((b-a) * mix);
}

static inline f32_vec dot_batch_single(f32_vec a_comps[3], vert3f b) {
    f32_vec axs = a_comps[0];
    f32_vec ays = a_comps[1];
    f32_vec azs = a_comps[2];
    f32_vec bxs = broadcast_f32_vec(b.x);
    f32_vec bys = broadcast_f32_vec(b.y);
    f32_vec bzs = broadcast_f32_vec(b.z);

    f32_vec xs = axs * bxs;
    f32_vec ys = ays * bys;
    f32_vec zs = azs * bzs;

    return xs + ys + zs;
}

static inline void normalize_batch(f32_vec comps[3], f32_vec normalized_out[3]) {

    f32_vec xs = comps[0];
    f32_vec ys = comps[1];
    f32_vec zs = comps[2];

    f32_vec xxs = xs*xs;
    f32_vec yys = ys*ys;
    f32_vec zzs = zs*zs;
    f32_vec dots = xxs + yys + zzs; // square lens (dot product)

    f32_vec recip_lens = (f32_vec){fast_inv_sqrt(dots[0]), fast_inv_sqrt(dots[1]), fast_inv_sqrt(dots[2]), fast_inv_sqrt(dots[3])};


    f32_vec norm_xs = xs * recip_lens;
    f32_vec norm_ys = ys * recip_lens;
    f32_vec norm_zs = zs * recip_lens;

    normalized_out[0] = norm_xs;
    normalized_out[1] = norm_ys;
    normalized_out[2] = norm_zs;
}

void mat_mul_vert3_batch(const matrix *m, const f32_vec poses_soa[3], f32_vec out_comps[3]) {
    f32_vec vx = poses_soa[0];
    f32_vec vy = poses_soa[1];
    f32_vec vz = poses_soa[2];

    f32_vec rx = broadcast_f32_vec(m->m[0][0]) * vx + broadcast_f32_vec(m->m[0][1]) * vy + broadcast_f32_vec(m->m[0][2]) * vz + broadcast_f32_vec(m->m[0][3]);
    f32_vec ry = broadcast_f32_vec(m->m[1][0]) * vx + broadcast_f32_vec(m->m[1][1]) * vy + broadcast_f32_vec(m->m[1][2]) * vz + broadcast_f32_vec(m->m[1][3]);
    f32_vec rz = broadcast_f32_vec(m->m[2][0]) * vx + broadcast_f32_vec(m->m[2][1]) * vy + broadcast_f32_vec(m->m[2][2]) * vz + broadcast_f32_vec(m->m[2][3]);

    out_comps[0] = rx;
    out_comps[1] = ry;
    out_comps[2] = rz;
}

void mat_mul_normal_batch(const matrix *m, const f32_vec norm_comp_soa[3], f32_vec out_comps[3]) {
    /* Transpose 4 vertices into SoA lanes */
    f32_vec vx = norm_comp_soa[0];
    f32_vec vy = norm_comp_soa[1];
    f32_vec vz = norm_comp_soa[2];

    f32_vec rx = broadcast_f32_vec(m->m[0][0]) * vx + broadcast_f32_vec(m->m[0][1]) * vy + broadcast_f32_vec(m->m[0][2]) * vz;
    f32_vec ry = broadcast_f32_vec(m->m[1][0]) * vx + broadcast_f32_vec(m->m[1][1]) * vy + broadcast_f32_vec(m->m[1][2]) * vz;
    f32_vec rz = broadcast_f32_vec(m->m[2][0]) * vx + broadcast_f32_vec(m->m[2][1]) * vy + broadcast_f32_vec(m->m[2][2]) * vz;

    out_comps[0] = rx;
    out_comps[1] = ry;
    out_comps[2] = rz;
}


// RENDERING

#define MAX_GLOBAL_TRIS 1000000
typedef struct {
    vert2i proj_v0, proj_v1, proj_v2; // 24 bytes
    f32 inv_z0, inv_z1, inv_z2;       // 12 bytes
    vert2f uv0_over_z, uv1_over_z, uv2_over_z; // 24 bytes
    f32 b0, b1, b2;
    u8 tex_or_dithered, mip_level_or_color;
} transformed_tri;

#define MAX_TILE_TRIS 16384
typedef struct {
    i16 start_x; i16 start_y;
    u32 num_tex_triangles, num_solid_triangles;
    u32 tex_tri_indexes[MAX_TILE_TRIS]; // up to 2048 triangles per tile
    u32 solid_tri_indexes[MAX_TILE_TRIS];
} tile;

tile tiles[TILES_WIDE*TILES_HIGH];

transformed_tri global_tri_buffer[MAX_GLOBAL_TRIS]; /* up to one million? */

typedef struct {
    vert3f pos, norm;
    vert2f uv;
} obj_vertex;

typedef struct {
    vert3f *pos, *norm;
    vert2f *uv;
} obj_vertex_stream;

typedef struct {
    const obj_vertex *vertexStream;
    const u16 *indexStream; 

    int vertexCount, indexCount;
} obj_mesh;

#include "mesh_board.h"
#include "mesh_dragon.h"
#include "mesh_mahjong_tile.h"
#include "mesh_tenbou.h"
#include "mesh_wind_indicator.h"

#include "palette_mahjong.h"
#include "palette_background.h"

#define BLACK 0
#define GREEN 1
#define GOLD 2
#define WHITE 3
#define RED 4
#define BLUE 5

#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define CLAMP(a, mi, ma) MIN(MAX(a, mi), ma)

const f32 FAR_Z = 256.0f;
const f32 NEAR_Z = 1.0f;

#define DEFAULT_CAM_ROT_X 0.63f
#define DEFAULT_CAM_ROT_Y 0.0f
f32 camera_rot_y = DEFAULT_CAM_ROT_Y;
f32 camera_rot_x = DEFAULT_CAM_ROT_X;
f32 camera_radius = 142.0f;
f32 camera_radius_top = 100.0f;//300.0f;

#define NUM_BASE_COLORS 6
#define NUM_SHADES 25

// REMAPS FROM BASE COLORS TO SHADES WITHIN THE PALETTE
u8 light_remap_table[NUM_SHADES][NUM_BASE_COLORS];
// REMAPS FROM ALL COLORS IN THE PALETTE, TO SHADES OF THOSE COLORS WITHIN THE PALETTE
u8 full_light_remap_table[NUM_SHADES][256];

vert3f light = { 1.0f, 1.0f, -.5f};

typedef struct {
    u8 palette[4]; // up to 4 colors in per-image palette.  these are indexes of the global palette (2 are shared)
    u8 *compressed_packets;
} compressed_texture;

typedef enum {
    UNCOMPRESSED,
    COMPRESSED,
    BASE_COLOR_INDEXES
} compress_type;

typedef struct {
    compressed_texture* comp_tex_ptr;
    u8* texels[4];
    int width, height;
    compress_type compressed; 
    u8 default_pal_idx;
} texture;

static inline u16 encode_float_inv_z(f32 inv_z) {
    return (u16)(inv_z*65536.0f);
}

static inline f32 decode_u16_inv_z(u16 inv_z) {
    return ((f32)inv_z)/65536.0f;
}

static inline u16_vec encode_float_inv_z_vec(f32_vec inv_z) {
    //return (u16_vec)(inv_z*65536.0f);
    f32_vec scaled = (inv_z*65536.0f);
    u16_vec res;
    for(int i = 0; i < 4; i++) {
        res[i] = (u16)scaled[i];
    }
    return res;
}

static inline f32_vec decode_u16_inv_z_vec(u16_vec inv_z) {
    f32_vec scaled;
    for(int i = 0; i < 4; i++) {
        scaled[i] = (f32)inv_z[i];
    }
    return scaled / 65536.0f;
}

static inline i32_vec broadcast_i32_vec(i32 a) {
    return (i32_vec){ a, a, a, a};
}

static inline i32_vec i32_vec_select(i32_vec mask, i32_vec a, i32_vec b) {
    // if mask[i] ? b[i] : a[i];
    i32_vec res;
    for(int i = 0; i < 4; i++) {
        res[i] = mask[i] ? a[i] : b[i];
    }
    return res;
}

static inline f32_vec i32_vec_convert_f32(i32_vec a) {
    f32_vec res;
    for(int i = 0; i < 4; i++) { res[i] = (f32)a[i]; }
    return res;
}

static inline f32_vec f32_vec_clamp(f32_vec a, f32 mi, f32 ma) {
    return (f32_vec) {
        CLAMP(a[0], mi, ma),  CLAMP(a[1], mi, ma),  CLAMP(a[2], mi, ma), CLAMP(a[3], mi, ma),
    };
}

static inline i32_vec f32_vec_convert_i32(f32_vec a) {
    i32_vec res;
    for(int i = 0; i < 4; i++) { res[i] = (i32)a[i]; }
    return res;
}

static inline u8 i32_vec_extract_low_bits(const i32_vec a) {
    u8 res = 0;
    for(int i = 0; i < 4; i++) {
        res |= (u8)((a[i]&1)<<i);
    }
    return res;
}

static inline int i32_vec_any(const i32_vec a) {
  for(int i=0; i<4; i++) { if(a[i]) return 1; }
  return 0;
}

u32 i32_vec_extract_bytes(const i32_vec a) {
    u32 res = 0;
    for(int i = 0; i < 4; i++) {
        res |= ((u32)(a[i] ? 0xFF : 0x00)<<(i*8));
    }
    return res;
}

i32_vec f32_vec_floor(f32_vec a) {    
    i32_vec i = (i32_vec){(i32)a[0], (i32)a[1], (i32)a[2], (i32)a[3]};
    f32_vec ii = (f32_vec){(f32)i[0], (f32)i[1], (f32)i[2], (f32)i[3]};

    return i - (ii > a);
}

int fast_log2(float x)
{
    return (int)((*(u32*)&x) >> 23) - 127;
}

f32 absf(f32 a) { 
    if(a < 0.0f) { return -a; }
    return a;
}

static inline u32 parallel_pixel_shader(
    f32_vec z, f32_vec w1, f32_vec w2, 
    f32 v0u_over_z, f32 v0v_over_z, 
    f32 v1u_over_z, f32 v1v_over_z, 
    f32 v2u_over_z, f32 v2v_over_z, 
    u8* texels, int tex_width, int tex_height, 
    f32_vec brightness_vec
) {
    f32_vec u_over_z = (v0u_over_z + w1 * v1u_over_z + w2 * v2u_over_z);
    f32_vec v_over_z = (v0v_over_z + w1 * v1v_over_z + w2 * v2v_over_z);
    f32_vec u = (u_over_z * z);
    f32_vec v = (v_over_z * z);

    u8 *lit_pal_ptr0 = full_light_remap_table[(u8)(brightness_vec[0])];
    u8 *lit_pal_ptr1 = full_light_remap_table[(u8)(brightness_vec[1])];
    u8 *lit_pal_ptr2 = full_light_remap_table[(u8)(brightness_vec[2])];
    u8 *lit_pal_ptr3 = full_light_remap_table[(u8)(brightness_vec[3])];


    // it's impossible to wrap around or go out of bounds in a way that is visible or harmful
    // so flooring is not necessary
    i32_vec int_u = f32_vec_convert_i32(u * (f32)tex_width);//f32_vec_floor(u * (f32)tex_height); 
    // (i32)fast_floor(u * (f32)tex_width);// & 1023;
    i32_vec int_v = f32_vec_convert_i32(v * (f32)tex_height);//f32_vec_floor(v * (f32)tex_height);
    // & 1023;

    int_u &= (tex_width-1);
    int_v &= (tex_height-1);

    i32_vec scaled_v_offset = tex_width * int_v;
    i32_vec uv = scaled_v_offset + int_u;

    u8 pal_idx0 = texels[uv[0]];
    u8 pal_idx1 = texels[uv[1]];
    u8 pal_idx2 = texels[uv[2]];
    u8 pal_idx3 = texels[uv[3]];


    u32 res = 0;
    res |= (u32)lit_pal_ptr0[pal_idx0]<<24;
    res |= (u32)lit_pal_ptr1[pal_idx1]<<16;
    res |= (u32)lit_pal_ptr2[pal_idx2]<<8;
    res |= (u32)lit_pal_ptr3[pal_idx3]<<0;
    return res;
}

int rasterize_triangle_2x2_quad(
    u16 *zbuffer,
    transformed_tri* tri_attributes,
    texture *tex,
    i32 start_x, i32 end_x,
    i32 start_y, i32 end_y
) {
    // swap everything for first two vertexes (actual vertex positions and attributes)
    f32 iz0 = tri_attributes->inv_z1;
    f32 iz1 = tri_attributes->inv_z0;
    f32 iz2 = tri_attributes->inv_z2;
    f32_vec iz0_vec = broadcast_f32_vec(iz0);
    f32_vec iz1_vec = broadcast_f32_vec(iz1);
    f32_vec iz2_vec = broadcast_f32_vec(iz2);

    vert2i v0 = tri_attributes->proj_v1;
    vert2i v1 = tri_attributes->proj_v0;
    vert2i v2 = tri_attributes->proj_v2;

    vert2f uv0_over_z = tri_attributes->uv1_over_z;
    vert2f uv1_over_z = tri_attributes->uv0_over_z;
    vert2f uv2_over_z = tri_attributes->uv2_over_z;


    //u16 quantized_brightness = tri_attributes->b0;
    //u8* lit_pal_ptr = full_light_remap_table[quantized_brightness];
    f32 b0 = tri_attributes->b1;
    f32 b1 = tri_attributes->b0;
    f32 b2 = tri_attributes->b2;
    f32_vec b0_vec = broadcast_f32_vec(b0);
    f32_vec b1_vec = broadcast_f32_vec(b1);
    f32_vec b2_vec = broadcast_f32_vec(b2);


    int drew_pixel = 0;

    f32 v0u_over_z = uv0_over_z.x;
    f32 v0v_over_z = uv0_over_z.y;

    f32 v1u_over_z = uv1_over_z.x;
    f32 v1v_over_z = uv1_over_z.y;

    f32 v2u_over_z = uv2_over_z.x;
    f32 v2v_over_z = uv2_over_z.y;

    int mip_level = tri_attributes->mip_level_or_color;

    int tex_width = tex->width>>mip_level;
    int tex_height = tex->height>>mip_level;
    u8* texels = tex->texels[mip_level];

    const i32 x0 = v0.x;
    const i32 y0 = v0.y;
    const i32 x1 = v1.x;
    const i32 y1 = v1.y;
    const i32 x2 = v2.x;
    const i32 y2 = v2.y;
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
    //f32_vec recip_area_vec = broadcast_f32_vec(recip_area);
    iz1_vec = (iz1_vec-iz0_vec)*recip_area;
    iz2_vec = (iz2_vec-iz0_vec)*recip_area;

    v1u_over_z = (v1u_over_z-v0u_over_z)*recip_area;
    v1v_over_z = (v1v_over_z-v0v_over_z)*recip_area;

    v2u_over_z = (v2u_over_z-v0u_over_z)*recip_area;
    v2v_over_z = (v2v_over_z-v0v_over_z)*recip_area;

    b1_vec = (b1_vec - b0_vec) * recip_area;
    b2_vec = (b2_vec - b0_vec) * recip_area;

    

    
    // bounding box of triangle (not so good for larger triangles)
    i32 minx = MIN(x0, MIN(x1, x2));
    i32 maxx = MAX(x0, MAX(x1, x2));
    i32 miny = MIN(y0, MIN(y1, y2)); 
    i32 maxy = MAX(y0, MAX(y1, y2));

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

    i32_vec cy01_vec = (i32_vec){
        c01 + dx01 * startY - dy01 * startX,
        c01 + dx01 * startY - dy01 * (startX+FIXED_ONE_PX),
        c01 + dx01 * (startY+FIXED_ONE_PX) - dy01 * startX,
        c01 + dx01 * (startY+FIXED_ONE_PX) - dy01 * (startX+FIXED_ONE_PX)
    };
    i32_vec cy12_vec = (i32_vec){
        c12 + dx12 * startY - dy12 * startX,
        c12 + dx12 * startY - dy12 * (startX+FIXED_ONE_PX),
        c12 + dx12 * (startY+FIXED_ONE_PX) - dy12 * startX,
        c12 + dx12 * (startY+FIXED_ONE_PX) - dy12 * (startX+FIXED_ONE_PX)
    };
    i32_vec cy20_vec = (i32_vec){
        c20 + dx20 * startY - dy20 * startX,
        c20 + dx20 * startY - dy20 * (startX+FIXED_ONE_PX),
        c20 + dx20 * (startY+FIXED_ONE_PX) - dy20 * startX,
        c20 + dx20 * (startY+FIXED_ONE_PX) - dy20 * (startX+FIXED_ONE_PX)
    };


    for (i32 y = miny; y < maxy; y += 2, cy01_vec += dx01_shifted_vec, cy12_vec += dx12_shifted_vec, cy20_vec += dx20_shifted_vec) {
        i32_vec cx01_vec = cy01_vec;
        i32_vec cx12_vec = cy12_vec;
        i32_vec cx20_vec = cy20_vec;

        int in_tile_y = y-start_y;
        int in_tile_x = minx-start_x;
        int tile_idx = (in_tile_y&~1)*RENDER_TILE_SIZE + ((in_tile_x&~1)<<1);
        u32 *col_buf_ptr = __builtin_assume_aligned(&render_target[tile_idx], 4);
        u16_vec *zbuf_ptr = __builtin_assume_aligned(&zbuffer[tile_idx], 8);
        for (i32 x = 0; x < (maxx-minx); x += 2, cx01_vec -= dy01_shifted_vec, cx12_vec -= dy12_shifted_vec, cx20_vec -= dy20_shifted_vec, col_buf_ptr++, zbuf_ptr++) {

            i32_vec covered_vec = ~((cx01_vec|cx12_vec|cx20_vec)>>31);


            int coverage_mask = i32_vec_any(covered_vec);
            //if(coverage_mask != 0xF) {
            //} else 
            if(coverage_mask != 0x0) {
                // skip completely uncovered quads
                u16_vec zbuf_val_vec_u16 = *zbuf_ptr;
                f32_vec zbuf_val_vec = decode_u16_inv_z_vec(zbuf_val_vec_u16);
                u32 cbuf_val = *col_buf_ptr;

                f32_vec w1_vec = i32_vec_convert_f32(cx20_vec);
                f32_vec w2_vec = i32_vec_convert_f32(cx01_vec);
                f32_vec inv_z_vec = (
                                    iz0_vec +
                                    (w1_vec * iz1_vec) +
                                    (w2_vec * iz2_vec)
                    );
                inv_z_vec = inv_z_vec;

                f32_vec brightness_vec = f32_vec_clamp(b0_vec + (w1_vec * b1_vec) + (w2_vec * b2_vec), 0, (f32)(NUM_SHADES-1));

                i32_vec unoccluded = inv_z_vec >= zbuf_val_vec;
                i32_vec in_tri_and_unoccluded = unoccluded & covered_vec;

                u32 mask_bytes = i32_vec_extract_bytes(in_tri_and_unoccluded);
                if(mask_bytes != 0) {
                    drew_pixel = 1;


                    f32_vec z_vec = 1.0f / inv_z_vec;
                    //f32_vec brightness_vec = brightness_over_z_vec * z_vec;
                                                                
                    u32 lit_color_qw = parallel_pixel_shader(
                        z_vec,
                        w1_vec, w2_vec, 
                        v0u_over_z, v0v_over_z, 
                        v1u_over_z, v1v_over_z, 
                        v2u_over_z, v2v_over_z,
                        texels, tex_width, tex_height, 
                        brightness_vec
                        //lit_pal_ptr
                    );
                    u32 masked_color = (cbuf_val & (~mask_bytes)) | (lit_color_qw & mask_bytes);
                    *col_buf_ptr = masked_color;

                    f32_vec new_zbuf_vec = (f32_vec)((in_tri_and_unoccluded & (i32_vec)inv_z_vec) | ((~in_tri_and_unoccluded) & (i32_vec)zbuf_val_vec));

                    
                    *zbuf_ptr = encode_float_inv_z_vec(new_zbuf_vec);
                }
            }
        }
    }
    return drew_pixel;
}

int rasterize_triangle_2x2_quad_no_tmap(
    u16 *zbuffer,
    transformed_tri* tri_attributes,
    i32 start_x, i32 end_x, i32 start_y, i32 end_y
) {
    u8 color = tri_attributes->mip_level_or_color;
    u8 dithered =  tri_attributes->tex_or_dithered;
    // swap everything for first two vertexes (actual vertex positions and attributes)
    f32 iz0 = tri_attributes->inv_z1;
    f32 iz1 = tri_attributes->inv_z0;
    f32 iz2 = tri_attributes->inv_z2;
    f32_vec iz0_vec = broadcast_f32_vec(iz0);
    f32_vec iz1_vec = broadcast_f32_vec(iz1);
    f32_vec iz2_vec = broadcast_f32_vec(iz2);

    vert2i v0 = tri_attributes->proj_v1;
    vert2i v1 = tri_attributes->proj_v0;
    vert2i v2 = tri_attributes->proj_v2;


    //u8 quantized_brightness = tri_attributes->b0;
    //u8* lit_pal_ptr = full_light_remap_table[quantized_brightness];
    //u8 lit_color = lit_pal_ptr[color];
    //u32 lit_color_qw = ((u32)lit_color<<24)|((u32)lit_color<<16)|((u32)lit_color<<8)|(u32)lit_color;

    f32 b0 = tri_attributes->b1;
    f32 b1 = tri_attributes->b0;
    f32 b2 = tri_attributes->b2;
    f32_vec b0_vec = broadcast_f32_vec(b0);
    f32_vec b1_vec = broadcast_f32_vec(b1);
    f32_vec b2_vec = broadcast_f32_vec(b2);


    int drew_pixel = 0;


    // 28.4 fixed point
    const i32 x0 = v0.x;
    const i32 y0 = v0.y;
    const i32 x1 = v1.x;
    const i32 y1 = v1.y;
    const i32 x2 = v2.x;
    const i32 y2 = v2.y;
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
    iz1_vec = (iz1_vec-iz0_vec)*recip_area;
    iz2_vec = (iz2_vec-iz0_vec)*recip_area;

    b1_vec = (b1_vec - b0_vec) * recip_area;
    b2_vec = (b2_vec - b0_vec) * recip_area;

    
    // bounding box of triangle (not so good for larger triangles)
    i32 minx = MIN(x0, MIN(x1, x2));
    i32 maxx = MAX(x0, MAX(x1, x2));
    i32 miny = MIN(y0, MIN(y1, y2)); 
    i32 maxy = MAX(y0, MAX(y1, y2));

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

    i32_vec cy01_vec = (i32_vec){
        c01 + dx01 * startY - dy01 * startX,
        c01 + dx01 * startY - dy01 * (startX+FIXED_ONE_PX),
        c01 + dx01 * (startY+FIXED_ONE_PX) - dy01 * startX,
        c01 + dx01 * (startY+FIXED_ONE_PX) - dy01 * (startX+FIXED_ONE_PX)
    };
    i32_vec cy12_vec = (i32_vec){
        c12 + dx12 * startY - dy12 * startX,
        c12 + dx12 * startY - dy12 * (startX+FIXED_ONE_PX),
        c12 + dx12 * (startY+FIXED_ONE_PX) - dy12 * startX,
        c12 + dx12 * (startY+FIXED_ONE_PX) - dy12 * (startX+FIXED_ONE_PX)
    };
    i32_vec cy20_vec = (i32_vec){
        c20 + dx20 * startY - dy20 * startX,
        c20 + dx20 * startY - dy20 * (startX+FIXED_ONE_PX),
        c20 + dx20 * (startY+FIXED_ONE_PX) - dy20 * startX,
        c20 + dx20 * (startY+FIXED_ONE_PX) - dy20 * (startX+FIXED_ONE_PX)
    };

    i32_vec no_pixel_dither_mask = dithered ? (i32_vec){0x00000000, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000} : broadcast_i32_vec(0xFFFFFFFF); // if not dithered, mask all pixels on (if covered and unoccluded)
    for (i32 y = miny; y < maxy; y += 2, cy01_vec += dx01_shifted_vec, cy12_vec += dx12_shifted_vec, cy20_vec += dx20_shifted_vec) {

        i32_vec cx01_vec = cy01_vec;
        i32_vec cx12_vec = cy12_vec;
        i32_vec cx20_vec = cy20_vec;

        int in_tile_y = y-start_y;
        int in_tile_x = minx-start_x;
        int tile_idx = (in_tile_y&~1)*RENDER_TILE_SIZE + ((in_tile_x&~1)<<1);

        u32 *col_buf_ptr = __builtin_assume_aligned(&render_target[tile_idx], 4);
        u16_vec *zbuf_ptr = __builtin_assume_aligned(&zbuffer[tile_idx], 8);


        for (i32 x = minx; x < maxx; x += 2, cx01_vec -= dy01_shifted_vec, cx12_vec -= dy12_shifted_vec, cx20_vec -= dy20_shifted_vec, col_buf_ptr++, zbuf_ptr++) {
            i32_vec dither_mask = ((x&0b10)^(y&0b10)) ? no_pixel_dither_mask : broadcast_i32_vec(0xFFFFFFFF);
            i32_vec covered_vec = ~((cx01_vec|cx12_vec|cx20_vec)>>31) & dither_mask;

            int coverage_mask = i32_vec_any(covered_vec);

            // skip completely uncovered quads
            if(coverage_mask == 0x00) {
                continue;
            }
            u32 cbuf_val = *col_buf_ptr;
            u16_vec zbuf_val_vec_u16 = *zbuf_ptr;
            f32_vec zbuf_val_vec = decode_u16_inv_z_vec(zbuf_val_vec_u16);

            f32_vec w1_vec = i32_vec_convert_f32(cx20_vec);
            f32_vec w2_vec = i32_vec_convert_f32(cx01_vec);
            f32_vec inv_z_vec = (iz0_vec + 
                                (w1_vec * iz1_vec) +
                                (w2_vec * iz2_vec));

            f32_vec brightness_vec = f32_vec_clamp(b0_vec + (w1_vec * b1_vec) + (w2_vec * b2_vec), 0, (f32)(NUM_SHADES-1));


            i32_vec unoccluded = inv_z_vec >= zbuf_val_vec;
            i32_vec in_tri_and_unoccluded = unoccluded & covered_vec;

            u32 mask_bytes = i32_vec_extract_bytes(in_tri_and_unoccluded);


            if(mask_bytes != 0) {
                drew_pixel = 1;
                //f32_vec z_vec = 1.0f / inv_z_vec;
                //f32_vec brightness_vec = brightness_over_z_vec * z_vec;
                u8 *lit_pal_ptr0 = full_light_remap_table[(u8)(brightness_vec[0])];
                u8 *lit_pal_ptr1 = full_light_remap_table[(u8)(brightness_vec[1])];
                u8 *lit_pal_ptr2 = full_light_remap_table[(u8)(brightness_vec[2])];
                u8 *lit_pal_ptr3 = full_light_remap_table[(u8)(brightness_vec[3])];
                u8 lit_color0 = lit_pal_ptr0[color];
                u8 lit_color1 = lit_pal_ptr1[color];
                u8 lit_color2 = lit_pal_ptr2[color];
                u8 lit_color3 = lit_pal_ptr3[color];
                u32 lit_color_qw = ((u32)lit_color0<<24)|((u32)lit_color1<<16)|((u32)lit_color2<<8)|(u32)lit_color3;


                u32 masked_color = (cbuf_val & (~mask_bytes)) | (lit_color_qw & mask_bytes);
                *col_buf_ptr = masked_color;

                f32_vec new_zbuf_vec = (f32_vec)((in_tri_and_unoccluded & (i32_vec)inv_z_vec) | ((~in_tri_and_unoccluded) & (i32_vec)zbuf_val_vec));
                
                *zbuf_ptr = encode_float_inv_z_vec(new_zbuf_vec);
            }
        }
    }
    return drew_pixel;
}


void clear_tile_bins() {
    for(i16 y = 0; y < TILES_HIGH; y++) {
        for(i16 x = 0; x < TILES_WIDE; x++) {
            tiles[y*TILES_WIDE+x].num_tex_triangles = 0;
            tiles[y*TILES_WIDE+x].num_solid_triangles = 0;
            tiles[y*TILES_WIDE+x].start_y = (i16)(y * RENDER_TILE_SIZE);
            tiles[y*TILES_WIDE+x].start_x = (i16)(x * RENDER_TILE_SIZE);
        }
    }
}

static u32 total_triangles;

typedef struct {
    f32 min_x, max_x;
    f32 min_y, max_y;
    f32 min_z, max_z;
} bbox;

typedef enum {
    //UNLIT_UNTEXTURED,
    UNLIT_TEXTURED,
    LIT_TEXTURED,
    LIT_TEXTURED_DITHERED,
    //LIT_UNTEXTURED
} shader;

typedef struct {
    const obj_mesh* mesh; 
    bbox* bounds;
    u8 texture;
    matrix model_to_view;
    matrix model_to_world;
    shader shdr;
} mesh_draw_call;

//const f32 focal = 500.0f;
const f32 camx = (f32)(RENDER_WIDTH/2.0f);
const f32 camy = (f32)(RENDER_HEIGHT/2.0f);

vert3f isometric_project_coord(vert3f r) {
    //f32 fov_y = 1.047f;//deg_to_rad(76.0f); // desired vertical FOV in degrees -> radians
    const f32 focal = (RENDER_HEIGHT / 2.0f) / 0.6f; //tanf(fov_y / 2.0f);

    return (vert3f){
            camx + focal * r.x / 40.0f,
            camy - focal * r.y / 40.0f,
            r.z
    };
}

vert3f project_coord(vert3f r) {
    //f32 fov_y = 1.047f;//deg_to_rad(76.0f); // desired vertical FOV in degrees -> radians
    const f32 focal = (RENDER_HEIGHT / 2.0f) / 0.15f; //tanf(fov_y / 2.0f);

    return (vert3f){
            camx + focal * r.x / r.z,
            camy - focal * r.y / r.z,
            r.z
    };
}

void parallel_project_coord(f32_vec comps[3], vert3f* out) {
    //f32 fov_y = 1.047f;//deg_to_rad(76.0f); // desired vertical FOV in degrees -> radians
    const f32 focal = (RENDER_HEIGHT / 2.0f) / 0.15f; //tanf(fov_y / 2.0f);
    f32_vec rxs = comps[0];
    f32_vec rys = comps[1];
    f32_vec rzs = comps[2];
    f32_vec proj_xs = camx + focal * rxs / rzs;
    f32_vec proj_ys = camy - focal * rys / rzs;
    f32_vec proj_zs = rzs;

    for(int i = 0; i < 4; i++) {
        out[i] = (vert3f){proj_xs[i], proj_ys[i], proj_zs[i]};
    }

}

typedef enum {
    ON_SCREEN,
    NEAR_CLIPPED,
    FAR_CLIPPED,
    OFF_SCREEN
} clip_res;

void project_bounding_box(bbox* box, matrix* model_to_view, vert3f verts[8]) {

    verts[0] = (vert3f){.x = box->min_x, .y = box->min_y, .z = box->min_z};
    verts[1] = (vert3f){.x = box->max_x, .y = box->min_y, .z = box->min_z};
    verts[2] = (vert3f){.x = box->min_x, .y = box->max_y, .z = box->min_z};
    verts[3] = (vert3f){.x = box->max_x, .y = box->max_y, .z = box->min_z};
    verts[4] = (vert3f){.x = box->min_x, .y = box->min_y, .z = box->max_z};
    verts[5] = (vert3f){.x = box->max_x, .y = box->min_y, .z = box->max_z};
    verts[6] = (vert3f){.x = box->min_x, .y = box->max_y, .z = box->max_z};
    verts[7] = (vert3f){.x = box->max_x, .y = box->max_y, .z = box->max_z};

    
    for(int i = 0; i < 8; i++) {
        vert3f r = mat_mul_vert3(model_to_view, &verts[i]);
        verts[i] = project_coord(r);
    }
}

clip_res clip_bounding_box(mesh_draw_call* m) {

    vert3f verts[8];
    project_bounding_box(m->bounds, &m->model_to_view, verts);
    

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

    if(min_y >= RENDER_HEIGHT || min_x >= RENDER_WIDTH || max_x <= 0 || max_y <= 0) {
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
bbox board_bbox;

bbox get_mesh_bbox(const obj_mesh *m) {
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


#include "texture_one_man.h"
#include "texture_two_man.h"
#include "texture_three_man.h"
#include "texture_four_man.h"
#include "texture_five_man.h"
#include "texture_five_man_red.h"
#include "texture_six_man.h"
#include "texture_seven_man.h"
#include "texture_eight_man.h"
#include "texture_nine_man.h"
#include "texture_one_pin.h"
#include "texture_two_pin.h"
#include "texture_three_pin.h"
#include "texture_four_pin.h"
#include "texture_five_pin.h"
#include "texture_five_pin_red.h"
#include "texture_six_pin.h"
#include "texture_seven_pin.h"
#include "texture_eight_pin.h"
#include "texture_nine_pin.h"
#include "texture_one_sou.h"
#include "texture_two_sou.h"
#include "texture_three_sou.h"
#include "texture_four_sou.h"
#include "texture_five_sou.h"
#include "texture_five_sou_red.h"
#include "texture_six_sou.h"
#include "texture_seven_sou.h"
#include "texture_eight_sou.h"
#include "texture_nine_sou.h"
#include "texture_north.h"
#include "texture_east.h"
#include "texture_south.h"
#include "texture_west.h"
#include "texture_green_dragon.h"
#include "texture_red_dragon.h"
#include "texture_white_dragon.h"
#include "texture_red_tenbou.h"
#include "texture_blue_tenbou.h"
#include "texture_gold_tenbou.h"
#include "texture_white_tenbou.h"
#include "texture_board.h"
#include "texture_wind_indicator.h"

#define PIN_VAL 100
#define SOU_VAL 200
#define MAN_VAL 300
#define HONOR_BASE_VAL 400
#define HONOR 1
#define NOT_HONOR 0
#define TERMINAL 1
#define NOT_TERMINAL 0
#define TILE_LIST \
    X(ONE_PIN, PIN_VAL+1, TERMINAL, NOT_HONOR)  \
    X(TWO_PIN, PIN_VAL+2, NOT_TERMINAL, NOT_HONOR)  \
    X(THREE_PIN, PIN_VAL+3, NOT_TERMINAL, NOT_HONOR)  \
    X(FOUR_PIN, PIN_VAL+4, NOT_TERMINAL, NOT_HONOR)  \
    X(FIVE_PIN, PIN_VAL+5, NOT_TERMINAL, NOT_HONOR)  \
    X(FIVE_PIN_RED, PIN_VAL+5, NOT_TERMINAL, NOT_HONOR)  \
    X(SIX_PIN, PIN_VAL+6, NOT_TERMINAL, NOT_HONOR)  \
    X(SEVEN_PIN, PIN_VAL+7, NOT_TERMINAL, NOT_HONOR)  \
    X(EIGHT_PIN, PIN_VAL+8, NOT_TERMINAL, NOT_HONOR)  \
    X(NINE_PIN, PIN_VAL+9, TERMINAL, NOT_HONOR)  \
    X(ONE_MAN, MAN_VAL+1, NOT_TERMINAL, NOT_HONOR)  \
    X(TWO_MAN, MAN_VAL+2, NOT_TERMINAL, NOT_HONOR)  \
    X(THREE_MAN, MAN_VAL+3, NOT_TERMINAL, NOT_HONOR)  \
    X(FOUR_MAN, MAN_VAL+4, NOT_TERMINAL, NOT_HONOR)  \
    X(FIVE_MAN, MAN_VAL+5, NOT_TERMINAL, NOT_HONOR)  \
    X(FIVE_MAN_RED, MAN_VAL+5, NOT_TERMINAL, NOT_HONOR)  \
    X(SIX_MAN, MAN_VAL+6, NOT_TERMINAL, NOT_HONOR)  \
    X(SEVEN_MAN, MAN_VAL+7, NOT_TERMINAL, NOT_HONOR)  \
    X(EIGHT_MAN, MAN_VAL+8, NOT_TERMINAL, NOT_HONOR)  \
    X(NINE_MAN, MAN_VAL+9, TERMINAL, NOT_HONOR)  \
    X(ONE_SOU, SOU_VAL+1, TERMINAL, NOT_HONOR)  \
    X(TWO_SOU, SOU_VAL+2, NOT_TERMINAL, NOT_HONOR)  \
    X(THREE_SOU, SOU_VAL+3, NOT_TERMINAL, NOT_HONOR)  \
    X(FOUR_SOU, SOU_VAL+4, NOT_TERMINAL, NOT_HONOR)  \
    X(FIVE_SOU, SOU_VAL+5, NOT_TERMINAL, NOT_HONOR)  \
    X(FIVE_SOU_RED, SOU_VAL+5, NOT_TERMINAL, NOT_HONOR)  \
    X(SIX_SOU, SOU_VAL+6, NOT_TERMINAL, NOT_HONOR)  \
    X(SEVEN_SOU, SOU_VAL+7, NOT_TERMINAL, NOT_HONOR)  \
    X(EIGHT_SOU, SOU_VAL+8, NOT_TERMINAL, NOT_HONOR)  \
    X(NINE_SOU, SOU_VAL+9, TERMINAL, NOT_HONOR)  \
    X(NORTH, HONOR_BASE_VAL, NOT_TERMINAL, HONOR)  \
    X(EAST, HONOR_BASE_VAL+100, NOT_TERMINAL, HONOR)  \
    X(SOUTH, HONOR_BASE_VAL+200, NOT_TERMINAL, HONOR)  \
    X(WEST, HONOR_BASE_VAL+300, NOT_TERMINAL, HONOR)  \
    X(WHITE_DRAGON, HONOR_BASE_VAL+400, NOT_TERMINAL, HONOR)  \
    X(RED_DRAGON, HONOR_BASE_VAL+500, NOT_TERMINAL, HONOR)  \
    X(GREEN_DRAGON, HONOR_BASE_VAL+600, NOT_TERMINAL, HONOR)

typedef enum __attribute__((packed)) {
#define X(tile_name, sort_val, is_terminal, is_honor) tile_name,
    TILE_LIST
#undef X
    NUM_TILES,
    WHITE_TENBOU,
    BLUE_TENBOU,
    RED_TENBOU,
    GREEN_TENBOU,
    GOLD_TENBOU,
    BOARD,
    WIND_INDICATOR,
    NUM_ALL_TEXTURE_TYPES
} tile_type;

const char* tile_names[NUM_TILES] = {
#define X(name, val, term, honor) #name,
    TILE_LIST
#undef X
};


int is_honor_tile[NUM_TILES] = {
#define X(tile_name, sort_val, is_terminal, is_honor) is_honor,
    TILE_LIST
#undef X
};

int is_terminal_tile[NUM_TILES] = {
#define X(tile_name, sort_val, is_terminal, is_honor) is_terminal,
    TILE_LIST
#undef X
};

int tile_sort_val[NUM_TILES] = {
#define X(tile_name, sort_val, is_terminal, is_honor) sort_val,
    TILE_LIST
#undef X
};

u8 texture_buffer[NUM_ALL_TEXTURE_TYPES][256*256] __attribute__((aligned(64)));
u8 texture_mip_buffer[NUM_ALL_TEXTURE_TYPES][128*128] __attribute__((aligned(64)));
u8 texture_mip_2_buffer[NUM_ALL_TEXTURE_TYPES][64*64] __attribute__((aligned(64)));
u8 texture_mip_3_buffer[NUM_ALL_TEXTURE_TYPES][32*32] __attribute__((aligned(64)));
#define NULL_PTR 0

texture textures[NUM_ALL_TEXTURE_TYPES+1] = {
    {
        &comp_tex_one_pin, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_two_pin, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_three_pin, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_four_pin, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_five_pin, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_five_pin_red, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_six_pin, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_seven_pin, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_eight_pin, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_nine_pin, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_one_man, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_two_man, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_three_man, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_four_man, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_five_man, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_five_man_red, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_six_man, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_seven_man, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_eight_man, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_nine_man, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_one_sou, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_two_sou, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_three_sou, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_four_sou, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_five_sou, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_five_sou_red, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_six_sou, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_seven_sou, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_eight_sou, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_nine_sou, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_north, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_east, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_south, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_west, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_white_dragon, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_red_dragon, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    {
        &comp_tex_green_dragon, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    { // NUM_TILES
        &comp_tex_east, {texture_board, NULL_PTR, NULL_PTR, NULL_PTR}, 1, 1, UNCOMPRESSED, WHITE
    },
    { // WHITE TENBOU
        &comp_tex_white_tenbou, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE
    },
    { // BLUE TENBOU
        &comp_tex_blue_tenbou, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, BLUE
    },
    { // RED TENBOU
        &comp_tex_red_tenbou, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, RED
    },
    { // GREEN TENBOU
        &comp_tex_white_tenbou, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, GREEN
    },
    { // GOLD TENBOU
        &comp_tex_gold_tenbou, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, GOLD
    },
    { // BOARD
        &comp_tex_east, {texture_board, NULL_PTR, NULL_PTR, NULL_PTR}, 1, 1, UNCOMPRESSED, 0
    },
    { // wind indicator
        &comp_tex_wind_indicator, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, GOLD
    },
    {
        &comp_tex_east, {texture_board, NULL_PTR, NULL_PTR, NULL_PTR}, 1, 1, UNCOMPRESSED, 0
    }
};

void decompress_texture(compressed_texture* comp_tex, u8* dst, int num_total_bytes, u8 default_pal_color) {
    u8 *packets = comp_tex->compressed_packets;
    u8 *local_palette = comp_tex->palette;
    int packet_idx = 0;
    int dst_idx = 0;
    while(dst_idx < num_total_bytes) {
        u8 packet = packets[packet_idx++];
        u8 global_pal_idx;
        int length;

        if(packet&1) {
            // non-white packet
            u8 bit = (u8)((packet>>1)&1);
            u8 pal_idx = (local_palette[bit]);
            global_pal_idx = light_remap_table[NUM_SHADES-1][pal_idx];
            length = (packet>>2)+1;
        } else {
            global_pal_idx = light_remap_table[NUM_SHADES-1][default_pal_color];
            length = (packet>>1)+1;
            u8 next_packet = packets[packet_idx++];
            length += (next_packet<<7);
        }

        for(int i = 0; i < length; i++) {
            dst[dst_idx++] = global_pal_idx;
        }
    }
    return;
}

void translate_texture(u8* src, u8* dst,  int total_pixels, u8 base_color) {
    for(int i = 0; i < total_pixels; i++) {
        u8 col = *src++;
        if(col != WHITE) {
            col = base_color;
        } else if (base_color == WHITE) { // if the base color is WHITE, the WHITE texels will be replaced with black
            col = BLACK;
        }
        *dst++ = light_remap_table[NUM_SHADES-1][col];
    }
}

vert3f rgba_to_f32_rgb(u32 rgba) {
    u32 r = (rgba>>24)&0xFF;
    u32 g = (rgba>>16)&0xFF;
    u32 b = (rgba>>8)&0xFF;
    return (vert3f){(f32)r/255.0f, (f32)g/255.0f, (f32)b/255.0f};
}

f32 color_dist(vert3f c1, vert3f c2) {
    
    f32 r1 = c1.x;
    f32 g1 = c1.y;
    f32 b1 = c1.z;
    f32 r2 = c2.x;
    f32 g2 = c2.y;
    f32 b2 = c2.z;
    f32 rmean = (r1 + r2) / 2.0f;
    f32 dr = r1 - r2;
    f32 dg = g1 - g2;
    f32 db = b1 - b2;
    f32 dr_sq = dr*dr;
    f32 dg_sq = dg*dg;
    f32 db_sq = db*db;


    f32 dist = my_sqrt(
        (2.0f + rmean) * dr_sq
        + 4.0f * dg_sq
        + (2.0f + (1.0f - rmean)) * db_sq
    );
    return dist;
}

u8 closest_overall_color_idx(ExotiqueInterface *ei, vert3f target_rgb) {

    f32 best_dist = 1024024.0f;
    int best_dist_idx = 0;
    for(int i = 0; i < 256; i++) {

        vert3f pal_rgb = rgba_to_f32_rgb(ei->palette[i]);
        f32 dist = color_dist(target_rgb, pal_rgb);

        if(dist < best_dist) {
            best_dist_idx = i;
            best_dist = dist;
        }
    }
    return (u8)best_dist_idx;
}

u8 mix_table[256*256] __attribute__((aligned(64)));

void mip_texture(ExotiqueInterface *ei, u8 *src, u8 *dst, int src_size, i16 override_color) {
    (void)ei;
    int dst_size = src_size>>1;
    for(int y = 0; y < src_size; y+=2) {
        for(int x = 0 ; x < src_size; x+=2) {

            u8 idx0 = src[y*src_size + x];
            u8 idx1 = src[y*src_size + x + 1];
            u8 idx2 = src[(y+1)*src_size + x];
            u8 idx3 = src[(y+1)*src_size + x + 1];
            
            u8 idx01 = mix_table[(idx0<<8)|idx1];
            u8 idx23 = mix_table[(idx2<<8)|idx3];
            u8 best_idx = mix_table[(idx01<<8)|idx23];
            
            u8 pick_color = (override_color != -1) ? (u8)override_color : best_idx;
            dst[(y>>1)*dst_size + (x>>1)] = pick_color;
        }
    }
}

void decompress_textures(ExotiqueInterface *ei) {

    for(int i = 0; i < NUM_ALL_TEXTURE_TYPES; i++) {

        u8* mip_tex_buf = texture_mip_buffer[i];
        u8* mip_2_tex_buf = texture_mip_2_buffer[i];
        u8* mip_3_tex_buf = texture_mip_3_buffer[i];
        int width = textures[i].width;
        int height = textures[i].height;
        u8* tex_buf;
        if(textures[i].compressed == COMPRESSED) {
            tex_buf = texture_buffer[i];
            if(i == BLUE_TENBOU || i == RED_TENBOU || i == GREEN_TENBOU) {
                // replace black index with white
                if(textures[i].comp_tex_ptr->palette[0] == BLACK) {
                    textures[i].comp_tex_ptr->palette[0] = WHITE;
                } else {
                    textures[i].comp_tex_ptr->palette[1] = WHITE;
                }
            } else if (i == GOLD_TENBOU) {
                // replace black index with red
                if(textures[i].comp_tex_ptr->palette[0] == BLACK) {
                    textures[i].comp_tex_ptr->palette[0] = RED;
                } else {
                    textures[i].comp_tex_ptr->palette[1] = RED;
                }
            } else if (i == WHITE_TENBOU) {
                textures[i].comp_tex_ptr->palette[0] = textures[i].comp_tex_ptr->palette[0];
            }
            decompress_texture(textures[i].comp_tex_ptr, tex_buf, width*height, textures[i].default_pal_idx);    
            
        } else if (textures[i].compressed == BASE_COLOR_INDEXES) {
            tex_buf = texture_buffer[i];
            translate_texture(textures[i].texels[0], tex_buf, width*height, textures[i].default_pal_idx);
        } else {
            tex_buf = textures[i].texels[0];

        }
        textures[i].texels[0] = tex_buf;
        textures[i].texels[1]  = mip_tex_buf;
        textures[i].texels[2]  = mip_2_tex_buf;
        textures[i].texels[3]  = mip_3_tex_buf;
    
        if(width > 1) {
            mip_texture(ei, tex_buf, mip_tex_buf, width, -1);
            int mip_width = width>>1;
            int mip_height = height>>1;

            mip_texture(ei, mip_tex_buf, mip_2_tex_buf, mip_width, -1);
            int mip2_width = mip_width>>1;
            int mip2_height = mip_height>>1;

            
            mip_texture(ei, mip_2_tex_buf, mip_3_tex_buf, mip2_width, -1);
            int mip3_width = mip2_width>>1;
            int mip3_height = mip2_height>>1;
            if(i < NUM_TILES) {
                mip_tex_buf[(mip_height-2)*mip_width+mip_width-2] = light_remap_table[NUM_SHADES-1][BLUE];
                mip_tex_buf[(mip_height-2)*mip_width+mip_width-1] = light_remap_table[NUM_SHADES-1][BLUE];
                mip_tex_buf[(mip_height-1)*mip_width+mip_width-2] = light_remap_table[NUM_SHADES-1][BLUE];
                mip_tex_buf[(mip_height-1)*mip_width+mip_width-1] = light_remap_table[NUM_SHADES-1][BLUE];

                mip_2_tex_buf[(mip2_height-2)*mip2_width+mip2_width-2] = light_remap_table[NUM_SHADES-1][BLUE];
                mip_2_tex_buf[(mip2_height-2)*mip2_width+mip2_width-1] = light_remap_table[NUM_SHADES-1][BLUE];
                mip_2_tex_buf[(mip2_height-1)*mip2_width+mip2_width-2] = light_remap_table[NUM_SHADES-1][BLUE];
                mip_2_tex_buf[(mip2_height-1)*mip2_width+mip2_width-1] = light_remap_table[NUM_SHADES-1][BLUE];

                mip_3_tex_buf[(mip3_height-2)*mip3_width+mip3_width-2] = light_remap_table[NUM_SHADES-1][BLUE];
                mip_3_tex_buf[(mip3_height-2)*mip3_width+mip3_width-1] = light_remap_table[NUM_SHADES-1][BLUE];
                mip_3_tex_buf[(mip3_height-1)*mip3_width+mip3_width-2] = light_remap_table[NUM_SHADES-1][BLUE];
                mip_3_tex_buf[(mip3_height-1)*mip3_width+mip3_width-1] = light_remap_table[NUM_SHADES-1][BLUE];


                tex_buf[(height-1)*height+width-1] = light_remap_table[NUM_SHADES-1][BLUE]; //(y >= 504) ? GOLD : (y >= 496) ? WHITE : BLACK;
            }
        }
    }


}


void output_palette(ExotiqueInterface *ei) {
    // P6 binary PPM header
    exotique_printf("P3\n16 16\n255\n");

    for (int i = 0; i < 256; i++) {
        //uint8_t rgb[3];
        u32 rgba = ei->palette[i];
        exotique_printf("%i %i %i\n", (rgba>>24)&0xFF, (rgba>>16)&0xFF, (rgba>>8)&0xFF);
    }
}

void output_full_light_remap_table(ExotiqueInterface *ei) {
    // for each color, output NUM_SHADES
    // y index will be color
    exotique_printf("P3\n256 %i\n255\n", NUM_SHADES);
    for(int j = 0; j < NUM_SHADES; j++) {
        for(int i = 0; i < 256; i++) {
            u8 idx = full_light_remap_table[j][i];
            u32 rgba = ei->palette[idx];
            exotique_printf("%i %i %i\n", (rgba>>24)&0xFF, (rgba>>16)&0xFF, (rgba>>8)&0xFF);

        }
    }
}

void output_mixing_table(ExotiqueInterface *ei) {
    exotique_printf("P3\n256 256\n255\n");
    for(int j = 0; j < 256; j++) {
        for(int i = 0; i < 256; i++) {
            u8 idx = mix_table[(j<<8)|i];
            u32 rgba = ei->palette[idx];
            exotique_printf("%i %i %i\n", (rgba>>24)&0xFF, (rgba>>16)&0xFF, (rgba>>8)&0xFF);
        }
    }
}


//    DRAW CALLS, TILE FILLS, VERTEX SHADERS

int got_board_min_max_coords = 0;
f32 board_min_y, board_max_y;
f32 board_top_min_x, board_top_max_x;
f32 board_bot_min_x, board_bot_max_x;

void rasterize_tile(ExotiqueInterface *ei, u16 *zbuffer, tile* t) {
    u32 i;

    int base_x = t->start_x;
    int base_y = t->start_y;

    f32 left_dx_per_dy = (board_bot_min_x - board_top_min_x) / (board_max_y-board_min_y);
    f32 right_dx_per_dy = (board_bot_max_x - board_top_max_x) / (board_max_y-board_min_y);


    /* clear zbuffer for this tile, fill with background, OR game board */
    u32 *col_val_ptr = __builtin_assume_aligned(&render_target[0], 4);
    u16_vec *zbuf_ptr = __builtin_assume_aligned(&zbuf[0], 8);

    f32_vec inv_far_vec_flt = broadcast_f32_vec(1.0f/FAR_Z);
    u16_vec inv_far_vec = encode_float_inv_z_vec(inv_far_vec_flt);
    u32 board_col_idx = light_remap_table[7][GREEN];
    board_col_idx |= (board_col_idx<<8);
    board_col_idx |= (board_col_idx<<16);

    for(int y = 0; y < RENDER_TILE_SIZE; y += 2) {
        int global_y = (base_y + y);
        //f32 y_portion = (f32)global_y / (f32)RENDER_HEIGHT;
        //int tex_y_coord = (int)(y_portion * (f32)BACKGROUND_TEX_HEIGHT);

        if(global_y >= board_min_y && global_y < board_max_y-2) {
            f32 left = board_top_min_x + left_dx_per_dy * ((f32)global_y - board_min_y);
            f32 right = board_top_max_x + right_dx_per_dy * ((f32) global_y - board_min_y);
            for(int x = 0; x < RENDER_TILE_SIZE; x += 2) {
                *zbuf_ptr++ = inv_far_vec;
                u32 bkgd_idx;
                (void)bkgd_idx;
                int global_x = (base_x + x);

                //f32 x_portion = (f32)global_x/(f32)RENDER_WIDTH;
                //int tex_x_coord = (int)(x_portion * (f32)BACKGROUND_TEX_WIDTH);

                if(global_x >= (i32)left && global_x < (i32)right) {
                    bkgd_idx = board_col_idx;
                } else {
                    bkgd_idx = 126 + 4; 
                    //bkgd_idx = texture_background[tex_y_coord*BACKGROUND_TEX_WIDTH+tex_x_coord];
                    bkgd_idx = (bkgd_idx<<24)|(bkgd_idx<<16)|(bkgd_idx<<8)|bkgd_idx;
                }
                *col_val_ptr++ = bkgd_idx;
            
            }
        } else {
            for(int x = 0; x < RENDER_TILE_SIZE; x += 2) {

                //int global_x = (base_x + x);
                //f32 x_portion = (f32)global_x/(f32)RENDER_WIDTH;
                //int tex_x_coord = (int)(x_portion * (f32)BACKGROUND_TEX_WIDTH);
                *zbuf_ptr++ = inv_far_vec;

                
                u32 bkgd_idx = 126 + 4;
                //bkgd_idx = texture_background[tex_y_coord*BACKGROUND_TEX_WIDTH+tex_x_coord];
                (void)bkgd_idx;

                *col_val_ptr++ = (bkgd_idx<<24)|(bkgd_idx<<16)|(bkgd_idx<<8)|bkgd_idx;
            }
        }
    }

    //int drew_pix = 0;

    u32 num_tris = t->num_tex_triangles;
    u32 num_solid_tris = t->num_solid_triangles;

    for(i = 0; i < num_solid_tris; i++) {
        u32 global_tri_idx = t->solid_tri_indexes[i];
        // drew_pix
        rasterize_triangle_2x2_quad_no_tmap(
            zbuffer,
            &global_tri_buffer[global_tri_idx],
            t->start_x, t->start_x+RENDER_TILE_SIZE,
            t->start_y, t->start_y+RENDER_TILE_SIZE
        );
    }

    for(i = 0; i < num_tris; i++) {
        u32 global_tri_idx = t->tex_tri_indexes[i];
        rasterize_triangle_2x2_quad(
            zbuffer,
            &global_tri_buffer[global_tri_idx],
            &textures[global_tri_buffer[global_tri_idx].tex_or_dithered],
            t->start_x, t->start_x+RENDER_TILE_SIZE,
            t->start_y, t->start_y+RENDER_TILE_SIZE
        );
    }

    /* flush tile to output */
    col_val_ptr = __builtin_assume_aligned(&render_target[0], 4);
    for(int y = 0; y < RENDER_TILE_SIZE; y+=2) {
        int output_y = (base_y+y)>>1;

        u8* output_row = &ei->screen[output_y*kScreenWidth+ (base_x>>1)];

        for(int x = 0; x < RENDER_TILE_SIZE; x+=2) {
            u32 colors = *col_val_ptr++;
            u16 top_two = (u16)(colors>>16);
            u16 bot_two = (u16)colors;
            u8 mixt = mix_table[top_two];
            u8 mixb = mix_table[bot_two];
            u8 mix = mix_table[(mixt<<8)|mixb];

            *output_row++ = mix;
        }
    }
}

void fill_background_for_tile(ExotiqueInterface *ei, tile* t) {

    int base_x = t->start_x;
    int base_y = t->start_y;
    
    f32 left_dx_per_dy = (board_bot_min_x - board_top_min_x) / (board_max_y-board_min_y);
    f32 right_dx_per_dy = (board_bot_max_x - board_top_max_x) / (board_max_y-board_min_y);


    u32 board_col_idx = light_remap_table[7][GREEN];


    for(int y = 0; y < RENDER_TILE_SIZE; y+=2) {
        int global_y = (base_y + y);
        int output_y = (base_y+y)>>1;
        
        u8* output_row = &ei->screen[output_y*kScreenWidth+ (base_x>>1)];

        if(global_y >= board_min_y && global_y < board_max_y-2) {
            f32 left = board_top_min_x + left_dx_per_dy * ((f32)global_y - board_min_y);
            f32 right = board_top_max_x + right_dx_per_dy * ((f32) global_y - board_min_y);
            for(int x = 0; x < RENDER_TILE_SIZE; x += 2) {
                //*zbuf_ptr++ = inv_far_vec;
                u8 bkgd_idx;
                int global_x = (base_x + x);

                //f32 x_portion = (f32)global_x/(f32)RENDER_WIDTH;
                //int tex_x_coord = (int)(x_portion * (f32)BACKGROUND_TEX_WIDTH);

                if(global_x >= (i32)left && global_x < (i32)right) {
                    bkgd_idx = board_col_idx;
                } else {
                    bkgd_idx = 126 + 4; 
                    //bkgd_idx = texture_background[tex_y_coord*BACKGROUND_TEX_WIDTH+tex_x_coord];
                    //bkgd_idx = (bkgd_idx<<24)|(bkgd_idx<<16)|(bkgd_idx<<8)|bkgd_idx;
                }
                *output_row++ = bkgd_idx;
            
            }
        } else {
            for(int x = 0; x < RENDER_TILE_SIZE; x+=2) {
                *output_row++ = 130;
            }
        }
    }
}

void rasterize_tiles(ExotiqueInterface *ei, u16 *zbuffer) {

    for(int y = 0; y < TILES_HIGH; y++) {
        for(int x = 0; x < TILES_WIDE; x++) {

            tile* t = &tiles[y*TILES_WIDE+x];
            
            if(t->num_tex_triangles || t->num_solid_triangles) {
                rasterize_tile(ei, zbuffer, &tiles[y*TILES_WIDE+x]);
            } else {
                fill_background_for_tile(ei, &tiles[y*TILES_WIDE+x]);
            }
        }
    }
}

int triangles_rasterized;
void bin_triangle(
    vert3f *v0, vert3f *v1, vert3f *v2,
    vert2f *v0_uv, vert2f *v1_uv, vert2f *v2_uv,
    f32 b0, f32 b1, f32 b2,
    u8 texture_id, shader cur_shader) {

        if(total_triangles == MAX_GLOBAL_TRIS) {
            return;
        }

        f32 minx = MIN(v0->x, MIN(v1->x, v2->x));
        f32 maxx = MAX(v0->x, MAX(v1->x, v2->x));
        f32 miny = MIN(v0->y, MIN(v1->y, v2->y));
        f32 maxy = MAX(v0->y, MAX(v1->y, v2->y));

        i32 startX = CLAMP((int)fast_floor(minx), 0, RENDER_WIDTH-1);
        i32 endX   = CLAMP((int)fast_ceil(maxx), 0, RENDER_WIDTH-1);

        i32 startY = CLAMP((int)fast_floor(miny), 0, RENDER_HEIGHT-1);
        i32 endY   = CLAMP((int)fast_ceil(maxy), 0, RENDER_HEIGHT-1);

        int tile_start_x = startX / RENDER_TILE_SIZE;
        int tile_start_y = startY / RENDER_TILE_SIZE;
        int tile_end_x = endX / RENDER_TILE_SIZE;
        int tile_end_y = endY / RENDER_TILE_SIZE;

        // HACK
        // only texturemap if there is a difference in U and V over the triangle
        // we only have two models, and the only textured face is mapped over a 2d area (so it changes in both U and V)
        // TODO: implement this with materials instead of this hack
        u8 no_tmap = ((f32s_equal(v0_uv->x, v1_uv->x) &&f32s_equal(v0_uv->x, v2_uv->x)) ||
                      (f32s_equal(v0_uv->y, v1_uv->y) && f32s_equal(v0_uv->y, v2_uv->y)));

        int rasterized_at_least_once = 0;
        for(int y = tile_start_y; y <= tile_end_y; y++) {
            for(int x = tile_start_x; x <= tile_end_x; x++) {

                u32 num_tex_tris_in_tile = tiles[y*TILES_WIDE+x].num_tex_triangles;
                u32 num_solid_tris_in_tile = tiles[y*TILES_WIDE+x].num_solid_triangles;

                if(no_tmap) {
                    if(num_solid_tris_in_tile == MAX_TILE_TRIS) {
                        continue;
                    }
                    rasterized_at_least_once = 1;
                    tiles[y*TILES_WIDE+x].solid_tri_indexes[num_solid_tris_in_tile++] = total_triangles;
                    tiles[y*TILES_WIDE+x].num_solid_triangles = num_solid_tris_in_tile;
                } else {
                    if(num_tex_tris_in_tile == MAX_TILE_TRIS) {
                        continue;
                    }
                    rasterized_at_least_once = 1;
                    tiles[y*TILES_WIDE+x].tex_tri_indexes[num_tex_tris_in_tile++] = total_triangles;
                    tiles[y*TILES_WIDE+x].num_tex_triangles = num_tex_tris_in_tile;
                }
                triangles_rasterized++;
            }
        }
        if(rasterized_at_least_once) {
            triangles_rasterized += 1;
            vert2i proj_v0 = {(i32)(v0->x*16.0f),(i32)(v0->y*16.0f)};
            vert2i proj_v1 = {(i32)(v1->x*16.0f),(i32)(v1->y*16.0f)};
            vert2i proj_v2 = {(i32)(v2->x*16.0f),(i32)(v2->y*16.0f)};
            

            vert2f uv0 = *v0_uv;
            vert2f uv1 = *v1_uv;
            vert2f uv2 = *v2_uv;
            uv0.y = 1.0f-uv0.y;
            uv1.y = 1.0f-uv1.y;
            uv2.y = 1.0f-uv2.y;

            f32 inv_z0 = 1.0f/v0->z;
            f32 inv_z1 = 1.0f/v1->z;
            f32 inv_z2 = 1.0f/v2->z;



            u8 mip_level = 1;
            int tex_width = textures[texture_id].width;
            int tex_height = textures[texture_id].height;
            if(!no_tmap && tex_width > 1) {
                mip_level = 1;
                f32 v1v0dx = fabsf(v1->x - v0->x)*(f32)tex_width;
                f32 v1v0dy = fabsf(v1->y - v0->y)*(f32)tex_height;
                f32 v1v0len = (v1v0dx * v1v0dx + v1v0dy * v1v0dy);
                f32 v2v0dx = fabsf(v2->x - v0->x)*(f32)tex_width;
                f32 v2v0dy = fabsf(v2->y - v0->y)*(f32)tex_height;
                f32 v2v0len = (v2v0dx * v2v0dx + v2v0dy * v2v0dy);
                f32 v1v0du = fabsf(uv1.x - uv0.x)*(f32)tex_width;
                f32 v1v0dv = fabsf(uv1.y - uv0.y)*(f32)tex_height;
                f32 v1v0uvlen = (v1v0du * v1v0du + v1v0dv * v1v0dv);
                f32 v2v0du = fabsf(uv2.x - uv0.x)*(f32)tex_width;
                f32 v2v0dv = fabsf(uv2.y - uv0.y)*(f32)tex_height;
                f32 v2v0uvlen = (v2v0du * v2v0du + v2v0dv * v2v0dv);

                f32 edge0_dv_per_len = v1v0uvlen / v1v0len;
                f32 edge1_dv_per_len = v2v0uvlen / v2v0len;
                f32 duv_per_pix = MAX(edge0_dv_per_len, edge1_dv_per_len);
                                
                //if(duv_per_pix > 1.0f) {
                //    mip_level = 1;
                    if(duv_per_pix > 2.0f) { // 6
                        mip_level = 2;
                        if(duv_per_pix > 4.0f) { // 12
                            mip_level = 3;
                        }
                    }
                //}
                duv_per_pix /= (f32)(2 << mip_level);
                duv_per_pix = duv_per_pix;
            }
            
            global_tri_buffer[total_triangles].proj_v0 = proj_v0;
            global_tri_buffer[total_triangles].proj_v1 = proj_v1;
            global_tri_buffer[total_triangles].proj_v2 = proj_v2;
            
            global_tri_buffer[total_triangles].b0 = b0;
            global_tri_buffer[total_triangles].b1 = b1;
            global_tri_buffer[total_triangles].b2 = b2;

            if(no_tmap) {
                texture tex = textures[texture_id];
                u8 *texels = tex.texels[0];

                i32 int_u = (i32)fast_floor(uv0.x * (f32)tex.width);
                i32 int_v = (i32)fast_floor(uv0.y * (f32)tex.height);
                int_u &= (tex.width-1);
                int_v &= (tex.height-1);
                u8 tex_pal_idx = texels[int_v*tex.width+int_u];


                global_tri_buffer[total_triangles].mip_level_or_color = tex_pal_idx;
                global_tri_buffer[total_triangles].tex_or_dithered = cur_shader == LIT_TEXTURED_DITHERED;
            } else {
                vert2f uv0_over_z = {uv0.x * inv_z0, uv0.y * inv_z0};
                vert2f uv1_over_z = {uv1.x * inv_z1, uv1.y * inv_z1};
                vert2f uv2_over_z = {uv2.x * inv_z2, uv2.y * inv_z2};
                global_tri_buffer[total_triangles].uv0_over_z = uv0_over_z;
                global_tri_buffer[total_triangles].uv1_over_z = uv1_over_z;
                global_tri_buffer[total_triangles].uv2_over_z = uv2_over_z;
                global_tri_buffer[total_triangles].tex_or_dithered = texture_id;
                global_tri_buffer[total_triangles].mip_level_or_color = mip_level;
            }
            
            global_tri_buffer[total_triangles].inv_z0 = inv_z0;
            global_tri_buffer[total_triangles].inv_z1 = inv_z1;
            global_tri_buffer[total_triangles++].inv_z2 = inv_z2;
        }
}

int meshes_transformed = 0;
int vertexes_transformed = 0;

typedef struct {
    vert3f rotv;
    vert2f uv;
    f32 brightness;
} shaded_vert;

#define VCACHE_SIZE 16

typedef struct {
    vert3f rotv[VCACHE_SIZE];
    vert2f uv[VCACHE_SIZE];
    f32 brightness[VCACHE_SIZE];
} vert_cache_soa;

typedef struct {
    u8 v0_cache_idx, v1_cache_idx, v2_cache_idx;
} tri_cache_idxs; 
tri_cache_idxs tris_to_shade[VCACHE_SIZE];

vert_cache_soa vert_cache;
i16 vert_cache_tags[VCACHE_SIZE+3]; // an extra 3 slots so we can be optimistic :)
int vcache_idx;
void vcache_reset() {
    vcache_idx = 0;
}

i32 vcache_lookup(i16 vidx) {
    for(int i = 0; i < vcache_idx; i++) {
        if(vert_cache_tags[i] == vidx) {
            return i;
        }
    }
    return -1;
}

int vcache_insert_tag(i16 tag) {
    vert_cache_tags[vcache_idx] = tag;
    return vcache_idx++;
}

int vcache_rem() {
    return VCACHE_SIZE - vcache_idx;
}

void vertex_shader(const int cache_tag_idx, const obj_vertex *vertex_stream, const matrix *model_to_view, const matrix *model_to_world, const shader cur_shader) {

    i16 v_idx = vert_cache_tags[cache_tag_idx];
    const obj_vertex* in_vert = &vertex_stream[v_idx];


    // Move in front of the camera.
    vert3f rot_vert = mat_mul_vert3(model_to_view, &in_vert->pos);
    vert3f proj_vert = project_coord(rot_vert);
    

    vertexes_transformed += 1;

    const vert3f *n0 = &in_vert->norm;
    float hemi = n0->y * 0.5f + 0.5f;

    float ambient = lerp(0.20f, 0.40f, hemi);
    //float ambient = lerp(0.40f, 0.80f, hemi);
    f32 l0;
    if(cur_shader == UNLIT_TEXTURED) {
        l0 = ambient;
    } else {
        // Rotate normals into world space (not view)
        vert3f rn0 = mat_mul_normal(model_to_world, n0);
        vert3f nn0 = normalize(rn0);
        f32 dot_light = dot(nn0, light);
        l0 = dot_light + ambient;
    }

    f32 c0 = CLAMP(l0, 0.0f, 1.0f);
    f32 diffuse = CLAMP(c0, 0.0f, 1.0f);
    f32 scaled_brightness = (diffuse * (NUM_SHADES-1));

    vert_cache.brightness[cache_tag_idx] = scaled_brightness;
    vert_cache.rotv[cache_tag_idx] = proj_vert;
    vert_cache.uv[cache_tag_idx] = in_vert->uv;
}

void process_vertex_batch(const int batch_tag_idx, 
    const f32_vec vert_poses_soa[3], 
    const vert2f vert_uvs[4], 
    const f32_vec vert_norms_soa[3],
    const matrix* model_to_view, const matrix *model_to_world, 
    const shader cur_shader) {

    f32_vec rot_vert_comps[3];

    mat_mul_vert3_batch(model_to_view, vert_poses_soa, rot_vert_comps); // transform all four vertexes at once
    
    vert3f s0[4];
    parallel_project_coord(rot_vert_comps, s0);
    
    vertexes_transformed += 4;


    f32_vec hemi = vert_norms_soa[1] * 0.5f + 0.5f;
    f32_vec ambient = lerp_f32_vec(broadcast_f32_vec(0.20f), broadcast_f32_vec(0.40f), hemi);

    f32_vec l0;
    f32_vec rot_norm_comps[3];
    f32_vec normalized_norm_comps[3];
    if(cur_shader == UNLIT_TEXTURED) {
        l0 = ambient;
    } else {
        // Rotate normals into world space (not view)
        mat_mul_normal_batch(model_to_world, vert_norms_soa, rot_norm_comps);
        normalize_batch(rot_norm_comps, normalized_norm_comps);

        l0 = dot_batch_single(normalized_norm_comps, light) + ambient;
    }

    f32_vec c0 = f32_vec_clamp(l0, 0.0f, 1.0f);
    f32_vec diffuse = f32_vec_clamp(c0, 0.0f, 1.0f);
    f32_vec scaled_brightness = (diffuse * (NUM_SHADES-1));

    // we can load 
    for(int i = 0; i < 4; i++) {
        vert_cache.brightness[batch_tag_idx+i] = scaled_brightness[i]; //(quantized_brightness[i]);
        vert_cache.rotv[batch_tag_idx+i] = s0[i];
        vert_cache.uv[batch_tag_idx+i] = vert_uvs[i];
    }
}

void parallel_vertex_shader(const int num_verts, const obj_vertex* vertex_stream, const matrix* model_to_view, const matrix *model_to_world, const shader cur_shader) {
    
    int num_bunches = num_verts/4;
    for(int bunch = 0; bunch < num_bunches; bunch++) {
        int batch_tag_idx = bunch*4;

        vert2f vert_uvs[4];
        f32_vec norms_soa[3]; // xxxx, yyyy, zzzz
        f32_vec poses_soa[3]; // xxxx, yyyy, zzzz

        for(int i = 0; i < 4; i++) {
            i16 idx = vert_cache_tags[batch_tag_idx+i];
            poses_soa[0][i] = vertex_stream[idx].pos.x;
            poses_soa[1][i] = vertex_stream[idx].pos.y;
            poses_soa[2][i] = vertex_stream[idx].pos.z;
            norms_soa[0][i] = vertex_stream[idx].norm.x;
            norms_soa[1][i] = vertex_stream[idx].norm.y;
            norms_soa[2][i] = vertex_stream[idx].norm.z;
            vert_uvs[i] = vertex_stream[idx].uv;
        }
        process_vertex_batch(batch_tag_idx, poses_soa, vert_uvs, norms_soa, model_to_view, model_to_world, cur_shader);

    }
    
    for(int i = num_bunches*4; i < num_verts; i++) {
        vertex_shader(
            i, vertex_stream, model_to_view, model_to_world, cur_shader
        );
    }
}

int triangle_backfacing(vert3f *v0, vert3f *v1, vert3f *v2) {

    // 28.4 fixed point
    const i32 X0 = (i32)v1->x;
    const i32 Y0 = (i32)v1->y;
    const i32 X1 = (i32)v0->x;
    const i32 Y1 = (i32)v0->y;
    const i32 X2 = (i32)v2->x;
    const i32 Y2 = (i32)v2->y;
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
    i32 area =(dx01 * dy20 - dy01 * dx20);
    return (area <= 0);
}

int vcache_misses;
int vcache_hits;
void submit_mesh_draw_call(mesh_draw_call* mdc) {
    const obj_mesh *m = mdc->mesh;
    matrix *model_to_view = &mdc->model_to_view;
    matrix *model_to_world = &mdc->model_to_world;
    u8 texture_id = mdc->texture;
    shader cur_shader = mdc->shdr;
    (void)texture_id;

    meshes_transformed += 1;

    int tri = 0;
    int tri_count = m->indexCount/3;
    while(tri < tri_count) {
        vcache_reset();
        int start_tri = tri;
        //
        while(tri < tri_count && (tri - start_tri) < VCACHE_SIZE) {
            int idx = tri*3;
            i16 v0_idx = (i16)m->indexStream[idx+0];
            i16 v1_idx = (i16)m->indexStream[idx+1];
            i16 v2_idx = (i16)m->indexStream[idx+2];
            i32 v0_cache_idx = vcache_lookup(v0_idx);
            int needed_vcache_slots = 0;
            if(v0_cache_idx == -1) {
                vcache_misses++;
                needed_vcache_slots++;
            } else {
                vcache_hits++;
            }
            i32 v1_cache_idx = vcache_lookup(v1_idx);
            if(v1_cache_idx == -1) {
                vcache_misses++;
                needed_vcache_slots++;
            } else {
                vcache_hits++;
            }
            i32 v2_cache_idx = vcache_lookup(v2_idx);
            if(v2_cache_idx == -1) {
                vcache_misses++;
                needed_vcache_slots++;
            } else {
                vcache_hits++;
            }

            if(needed_vcache_slots > vcache_rem()) {
                break;
            }

            if(v0_cache_idx == -1) {
                v0_cache_idx = vcache_insert_tag(v0_idx);
            }
            if(v1_cache_idx == -1) {
                v1_cache_idx = vcache_insert_tag(v1_idx);
            }
            if(v2_cache_idx == -1) {
                v2_cache_idx = vcache_insert_tag(v2_idx);
            }

            tris_to_shade[tri-start_tri].v0_cache_idx = (u8)v0_cache_idx;
            tris_to_shade[tri-start_tri].v1_cache_idx = (u8)v1_cache_idx;
            tris_to_shade[tri-start_tri].v2_cache_idx = (u8)v2_cache_idx;
            tri++;
        }    
        parallel_vertex_shader(
            vcache_idx,
            m->vertexStream,
            model_to_view, model_to_world,
            cur_shader
        );
            
        int tris_to_draw = tri - start_tri;  
        for(int t = 0; t < tris_to_draw; t++) {
            int v0_cache_idx = tris_to_shade[t].v0_cache_idx;
            int v1_cache_idx = tris_to_shade[t].v1_cache_idx;
            int v2_cache_idx = tris_to_shade[t].v2_cache_idx;

            vert3f *rotv0 = &vert_cache.rotv[v0_cache_idx];
            vert3f *rotv1 = &vert_cache.rotv[v1_cache_idx];
            vert3f *rotv2 = &vert_cache.rotv[v2_cache_idx];

            if(rotv0->z < NEAR_Z && rotv1->z < NEAR_Z && rotv2->z < NEAR_Z) {
                continue;
            }
            
            if(triangle_backfacing(rotv0, rotv1, rotv2)) {
                // do not submit backfacing triangles
                continue;
            }

            vert2f *uv0 = &vert_cache.uv[v0_cache_idx];
            vert2f *uv1 = &vert_cache.uv[v1_cache_idx];
            vert2f *uv2 = &vert_cache.uv[v2_cache_idx];

            f32 b0 = vert_cache.brightness[v0_cache_idx];
            f32 b1 = vert_cache.brightness[v1_cache_idx];
            f32 b2 = vert_cache.brightness[v2_cache_idx];


            bin_triangle(
                rotv0, rotv1, rotv2,
                uv0, uv1, uv2,
                b0, b1, b2, 
                texture_id, cur_shader
            );
        }
    }
}

typedef enum {
    NO_FRUSTUM_CULL,
    FRUSTUM_CULL
} culling_mode;

void submit_draw_calls(mesh_draw_call *list, int num_meshes, culling_mode frustum_cull_mode) {
    int meshes_clipped = 0;
    (void)frustum_cull_mode;
    for(int i = 0; i < num_meshes; i++) {
        if(frustum_cull_mode == FRUSTUM_CULL) {
            clip_res clipped = clip_bounding_box(&list[i]);
            if(clipped == FAR_CLIPPED || clipped == NEAR_CLIPPED || clipped == OFF_SCREEN) {
                meshes_clipped++;
                continue;
            }
        }
        submit_mesh_draw_call(&list[i]);
    }
}


//    SOUND EFFECTS

#include "pon_4b.h"
#include "chii_4b.h"
#include "tsumo.h"
#include "riichi_4b.h"
#include "tile_click_4b.h"
#include "tsumo_4b.h"
#include "ron_4b.h"

typedef enum {
    TILE_CLICK,
    PON,
    CHII,
    RIICHI,
    TSUMO,
    RON,
    NUM_SOUNDS
} sound;

i16 decompressed_sound_buffer[NUM_SOUNDS][32768];
typedef struct {
    void *compressed_raw_data;
    i16* decompressed_data;
    u32 num_bytes, num_mono_samples;
} sound_data;

sound_data sounds[NUM_SOUNDS] = {
    {tile_click_4b_raw_data, decompressed_sound_buffer[0], TILE_CLICK_NUM_BYTES, 0},
    {pon_4b_raw_data, decompressed_sound_buffer[1], PON_NUM_BYTES, 0},
    {chii_4b_raw_data, decompressed_sound_buffer[2], CHII_NUM_BYTES, 0},
    {riichi_4b_raw_data, decompressed_sound_buffer[3], RIICHI_NUM_BYTES, 0},
    {tsumo_4b_raw_data, decompressed_sound_buffer[4], TSUMO_NUM_BYTES, 0},
    {ron_4b_raw_data, decompressed_sound_buffer[5], RON_NUM_BYTES, 0}
};

u32 decompress_adpcm(u8* raw, i16 *output, u32 num_bytes) {

    static const int index_table[16] = {
        -1,-1,-1,-1,
        2, 4, 6, 8,
        -1,-1,-1,-1,
        2, 4, 6, 8
    };
    static const int step_table[89] = {
           7,      8,     9,    10,    11,    12,    13,    14,
          16,     17,    19,    21,    23,    25,    28,    31,
          34,     37,    41,    45,    50,    55,    60,    66,
          73,     80,    88,    97,   107,   118,   130,   143,
         157,    173,   190,   209,   230,   253,   279,   307,
         337,    371,   408,   449,   494,   544,   598,   658,
         724,    796,   876,   963,  1060,  1166,  1282,  1411,
        1552,   1707,  1878,  2066,  2272,  2499,  2749,  3024,
        3327,   3660,  4026,  4428,  4871,  5358,  5894,  6484,
        7132,   7845,  8630,  9493, 10442, 11487, 12635, 13899,
        15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
        32767
    };

    u32 num_blocks = num_bytes / 512;
    i16 *out = output;
    for(u32 blk = 0; blk < num_blocks; blk++) {
        int predictor = (i16)(raw[0]|(raw[1]<<8));
        raw += 2;
        int step_index = *raw++;
        raw++; // skip over reserved;
        *out++ = (i16)predictor;
        for(u32 byte_in_block = 0; byte_in_block < 508; byte_in_block++) {
            u8 byte = *raw++;
            // loop twice to output two samples for one byte
            for(int i = 0; i < 2; i++) {
                int code = (byte>>(4*i)) & 0xF;
                int step = step_table[step_index];
                int diff = step >> 3;

                if (code & 1) diff += step >> 2;
                if (code & 2) diff += step >> 1;
                if (code & 4) diff += step;

                if (code & 8) {
                    predictor -= diff;
                } else {
                    predictor += diff;
                }
                predictor = CLAMP(predictor, -32768, 32767);
                step_index += index_table[code];
                step_index = CLAMP(step_index, 0, 88);
                *out++ = (i16)predictor;
            } 
        }
    }
    return (u32)(out-output);
}

void decompress_sounds() {
    for(int i = 0; i < NUM_SOUNDS; i++) {
        sounds[i].num_mono_samples = decompress_adpcm(sounds[i].compressed_raw_data, decompressed_sound_buffer[i], sounds[i].num_bytes);
        sounds[i].decompressed_data = decompressed_sound_buffer[i];
    }
}

int num_active_sounds = 0;
#define MAX_SOUNDS 64
typedef struct {
    sound voice;
    f32 volume;
    u32 playback_offset;
    vert3f location;
} active_sound;

active_sound active_sounds[MAX_SOUNDS]; // contains the playback index of the sound?

f32 buffer[4096];
static volatile active_sound pending_sounds[MAX_SOUNDS];
vert3f pending_sound_locations[MAX_SOUNDS];
static volatile u32 write_pos = 0;  // written only by game thread
static volatile u32 read_pos  = 0;  // written only by audio thread

void add_sound(sound voice, f32 volume) { //}, vert3f location) {
    u32 next = (write_pos + 1) & (MAX_SOUNDS - 1);
    if(next == read_pos) {
        return; // queue full, drop
    }
    pending_sounds[write_pos].voice = voice;
    //pending_sounds[write_pos].location = location;
    pending_sounds[write_pos].volume = volume;
    pending_sounds[write_pos].playback_offset = 0;
    write_pos = next;
}

static void drain_pending_sounds(void) {
    while(read_pos != write_pos) {
        if(num_active_sounds < MAX_SOUNDS) {
            active_sounds[num_active_sounds++] = pending_sounds[read_pos];
        }
        read_pos = (read_pos + 1) & (MAX_SOUNDS - 1);
    }
}

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    (void)pInput;
    (void)pDevice;

    drain_pending_sounds();   // <-- only new line: pull in any clicks queued since last callback
    i16* out = (i16*)pOutput;

    for(u32 i = 0; i < frameCount; i++) {
        buffer[i*2] = 0.0f;
        buffer[i*2+1] = 0.0f;
    }

    // mix into buffer — unchanged
    for(u32 i = 0; i < frameCount; i++) {
        u32 bufferIdx = i*2;

        for(int sound_idx = 0; sound_idx < num_active_sounds; sound_idx++) {
        continue_without_increment:;
            active_sound snd = active_sounds[sound_idx];
            i16 *data = sounds[snd.voice].decompressed_data;
            f32 sound_vol = snd.volume;
            //vert3f view = mat_mul_vert3(&view_matrix, &snd.location);
            f32 pan = 0.0f; //CLAMP(view.x/fabsf(view.z), -1.0f, 1.0f);
            
            if(pan < 0.0f) { 
                pan = -1.0f; 
            } else if (pan > 0.0f) { 
                pan = 1.0f; 
            } else {
                pan = 0.0f;
            }
            f32 left  = 0.5f - pan * 0.5f;
            f32 right = 0.5f + pan * 0.5f;
            u32 playback_offset = snd.playback_offset;
            u32 num_smples = sounds[snd.voice].num_mono_samples;
            

            if(playback_offset >= num_smples) {
                num_active_sounds--;
                if(sound_idx == num_active_sounds) {
                    // this WAS the last element — nothing to swap in, don't reprocess
                    continue;
                }
                active_sounds[sound_idx] = active_sounds[num_active_sounds];
                goto continue_without_increment;
            }
                    
            buffer[bufferIdx]   += (f32)data[playback_offset]   * sound_vol * left;
            buffer[bufferIdx+1] += (f32)data[playback_offset++] * sound_vol * right;
            active_sounds[sound_idx].playback_offset = playback_offset;
        }
    }

    for(u32 i = 0; i < frameCount; i++) {
        *out++ = (i16)CLAMP(buffer[i*2],   -32768.0f, 32767.0f);
        *out++ = (i16)CLAMP(buffer[i*2+1], -32768.0f, 32767.0f);
    }
}

void stop_device_callback(ma_device* pDevice) {
    (void)pDevice;
}
ma_device_config ma_config;
ma_device sound_device;


// WORLD AND HAND DRAWING/TRANSFORMS

#define MAX_DISCARDS 17
#define MAX_OPEN_TILES 16
#define TILES_IN_DECK 136
#define TILE_SPAWN_POS_Y 20.0f

#define TILE_FALL_DURATION .2f
#define TILE_DEAL_DURATION .25f
#define DISCARD_DURATION .34f
#define SWITCH_PLAYER_DURATION 1.0f

typedef struct __attribute__((packed)) {
    i8 num_closed_tiles;
    tile_type tiles[14];
    u32 deal_frame_for_tiles[14];
    u8 wall_index_for_tiles[14];
    tile_type open_tiles[MAX_OPEN_TILES];
    u8 open_tile_rotated[MAX_OPEN_TILES];
    tile_type discards[MAX_DISCARDS];
    u32 discard_frames[MAX_DISCARDS];
    i8 discard_from_hand_idx[MAX_DISCARDS];
    int score;
    i8 num_open_tiles;
    i8 selected_tile_idx;
    i8 num_discards;
    i8 in_riichi;
} hand;

typedef struct __attribute__((packed)) {
    u32 tile_fall_frames[TILES_IN_DECK];
    tile_type tiles[TILES_IN_DECK];
} wall;


typedef enum __attribute__((packed)) {
    STARTUP,
    INITIAL_SHUFFLE_AND_SETUP,
    DEALING,
    IN_GAME,
    NUM_GAME_STATES
} game_state;

typedef enum __attribute__((packed)) {
    EAST_WIND = 0,
    SOUTH_WIND = 1
} game_wind;

const tile_type game_wind_to_tile_wind[] = {
    EAST, SOUTH
};



typedef enum __attribute__((packed)) {
    NETWORK_HUMAN,
    HUMAN,
    CONSERVATIVE_AI,
    AGGRESSIVE_AI,
    CHAOTIC_AI,
} player_type;


typedef struct __attribute__((packed)) {
    u32 seeds[4];
    game_wind cur_wind;
    int next_deal_pos; // 
    u32 frame;
    i8 deal_steps;
    i8 cur_player;
    int switch_player_timer;
    i32 draw_end_frame;
    i8 cur_dealer;
    tile_type last_discard;
    i8 last_discard_player;
    game_state cur_game_state;
    
    u8 wall_rem;
    int wall_split_distance;

    hand hands[4];
    u32 ai_player_next_move_frames[4];
    player_type player_types[4];
} game_data_t;

wall board_wall = { 0 };


game_data_t game_data = { 
    // random seeds
    {0x27cb588d, 0x096379a9, 0xe81f5914, 0x2ee1c98c}, 
    // game wind
    EAST_WIND,
    // next deal pos
    0,
    // frame
    0,
    // deal steps
    0,
    // cur player
    0,
    // switch_player_timer
    -1,
    // draw end frame
    0,
    // cur dealer
    -1,
    // last discard
    0,
    // last discard player
    -1,
    // game state
    STARTUP,

    // wall rem
    0,
    // wall split distance
    -1,
    
    // hands
    {
        { 0 },
        { 0 },
        { 0 },
        { 0 }
    },
    // ai player move frames
    {0, 24, 36, 52},
    // ai move states
    // player types
    {HUMAN, AGGRESSIVE_AI, CHAOTIC_AI, CONSERVATIVE_AI}
};


i8 human_player = 0; // default value for host, for clients this gets assigned to an initial player num in a packet
static u32 rotl(const u32 x, i32 k) {
  return (x << k) | (x >> (32 - k));
}


u32 nextrand(void) {

  const u32 result = rotl(game_data.seeds[0] + game_data.seeds[3], 7) + game_data.seeds[0];

  const u32 t = game_data.seeds[1] << 9;

  game_data.seeds[2] ^= game_data.seeds[0];
  game_data.seeds[3] ^= game_data.seeds[1];
  game_data.seeds[1] ^= game_data.seeds[2];
  game_data.seeds[0] ^= game_data.seeds[3];

  game_data.seeds[2] ^= t;

  game_data.seeds[3] = rotl(game_data.seeds[3], 11);

  return result;
}

u32 roll_die() {
    return 1+(nextrand()%6);
}


#define TILE_SCALE 1.0f
#define WALL_TILE_SPACING 1.63f
#define HAND_TILE_SPACING 2.0f
#define SELECTED_TILE_Y_POS 1.47f
#define DISCARDING_TILE_Y_POS 2.0f
#define UNSELECTED_TILE_Y_POS 0.455f

#define MS_PER_FRAME 33

const f32 wall_offsets_x[4] = {17.0f, 0.0f, -17.0f, 0.0f};
const f32 wall_offsets_z[4] = {0.0f, 17.0f, 0.0, -17.0f};
const f32 wall_y_rots[4] = {(f32)M_PI * 0.5f, 0.0f, -(f32)M_PI * 0.5f, (f32)M_PI};

static u64 bench_frame_ms;

typedef struct {
    int used;
    f32 key;
    u32 frames;
} cache_entry;
cache_entry duration_cache[32] = { 0 };

u32 get_frames_for_duration(f32 duration) {
    static int initialized = 0;
    static int cache_used_entries = 0;
    if(!initialized) {
        for(int i = 0; i < 32; i++) {
            duration_cache[i].used = 0;
        }
        initialized = 1;
    }
    for(int i = 0; i < cache_used_entries; i++) {
        if(!duration_cache[i].used) {
            break;
        }
        if(f32s_equal(duration_cache[i].key, duration)) {
            return duration_cache[i].frames;
        }
    }
    f32 total_ms = (duration * 1000.0f);
    u32 frames = (u32)(total_ms / (f32)MS_PER_FRAME);
    duration_cache[cache_used_entries].key = duration;
    duration_cache[cache_used_entries].frames = frames;
    duration_cache[cache_used_entries++].used = 1;
    exotique_printf("FOR DURATION OF %.2f seconds, %u frames\n", (double)duration, frames);
    return frames;
}

transform identity_transform(void) {
    return (transform){
        .scale =    {1.0f, 1.0f, 1.0f},
        .position = {0.0f, 0.0f, 0.0f},
        .rotation = {0.0f, 0.0f, 0.0f}
    };
}

vert3f calc_global_discard_position(int discard_i, matrix* hand_to_world_matrix);

f32 calc_wall_tile_x_position(int row_on_wall) {

    f32 wall_length = (f32)(16 * WALL_TILE_SPACING);
    f32 half = wall_length / 2.0f;
    f32 row_x_offset = (f32)row_on_wall * -WALL_TILE_SPACING;

    f32 x_position = half + row_x_offset;
    
    return x_position;
}

f32 calc_wall_tile_y_position(u32 cur_frame, wall *d, int tot_tile_idx) {
    f32 progress = (f32)(cur_frame - d->tile_fall_frames[tot_tile_idx]) / (f32)get_frames_for_duration(TILE_FALL_DURATION);
    f32 tile_fall_pos = CLAMP(
        progress,
        0.0f, 1.0f);
    int top = (tot_tile_idx&1);

    f32 lerp_top_position = lerp(1.10f, TILE_SPAWN_POS_Y, 1.0f-tile_fall_pos);
    f32 lerp_bot_position = lerp(0.0f, TILE_SPAWN_POS_Y, 1.0f-tile_fall_pos);
    return top ? lerp_top_position : lerp_bot_position;//1.40f : 0.0f;
}

int mod_positive(int x, int m) {
    int r = x % m;
    return r < 0 ? r + m : r;
}

transform get_wall_transform(int i) {
    transform wall_trans = identity_transform();
    wall_trans.position.x = wall_offsets_x[i];
    wall_trans.position.z = wall_offsets_z[i];
    wall_trans.rotation.y = wall_y_rots[i]; // - 0.05f;
    return wall_trans;
}

vert3f calc_wall_tile_global_position(u32 cur_frame, wall* w, int tot_tile_idx) {
    // NOTE: this takes into account the split distance offset
    
    int offset_pos = mod_positive(tot_tile_idx-(34-game_data.wall_split_distance*2),TILES_IN_DECK);
    int wall_side = (offset_pos/34);
    int position_in_wall = (offset_pos-wall_side*34)/2;

    transform wall_trans = get_wall_transform(wall_side);
    matrix wall_matrix = transform_to_matrix(&wall_trans);

    vert3f local_pos;
    local_pos.x = calc_wall_tile_x_position(position_in_wall);
    local_pos.y = calc_wall_tile_y_position(cur_frame, w, tot_tile_idx);
    local_pos.z = 0.0f;

    return mat_mul_vert3(&wall_matrix, &local_pos);
}

f32 calc_hand_x_position(hand* h, int idx) {
    f32 whole_hand_width = (f32)(13 * HAND_TILE_SPACING);
    f32 half_width = whole_hand_width / 2.0f;
    f32 normal = half_width - HAND_TILE_SPACING*(f32)idx;
    if(idx == h->num_closed_tiles) {
        return normal - 1.0f;
    } else {
        return normal;
    }
}

f32 calc_hand_y_position(hand* h, int idx) { //}, int is_cur_player) {
    f32 cur_player_height = (h->selected_tile_idx == idx ? SELECTED_TILE_Y_POS : UNSELECTED_TILE_Y_POS);
    //f32 non_cur_player_height = UNSELECTED_TILE_Y_POS;
    return cur_player_height; //is_cur_player ? cur_player_height : non_cur_player_height;
}

vert3f calc_local_hand_position(hand* h, int tile_in_hand_idx) {
    return (vert3f) {
        calc_hand_x_position(h, tile_in_hand_idx),
        calc_hand_y_position(h, tile_in_hand_idx),
        -2.0f
    };
}

vert3f calc_global_hand_position(hand* h, int tile_in_hand_idx, matrix* hand_to_world_matrix) {
    (void)h;
    vert3f local = calc_local_hand_position(h, tile_in_hand_idx);
    return mat_mul_vert3(hand_to_world_matrix, &local);
}

vert3f calc_animated_hand_tile_position(u32 cur_frame, wall *w, hand *h, matrix *hand_to_world, matrix *world_to_hand, int tile_in_hand_idx) {
    /* 
        this function calculates global positions for in the wall and in the hand
        lerps between them
        then multiplies by the intverse matrix of world->hand to get a hand-local position

        kinda roundabout and less efficient than it could be, but its okay :)
    */
    f32 anim_progress = CLAMP(((f32)(cur_frame - h->deal_frame_for_tiles[tile_in_hand_idx]) / (f32)get_frames_for_duration(TILE_DEAL_DURATION)), 0.0f, 1.0f);
    vert3f local;
    if(anim_progress >= 1.0f) {
        local = calc_local_hand_position(h, tile_in_hand_idx);
    } else {
        vert3f global_wall_position = calc_wall_tile_global_position(cur_frame, w, h->wall_index_for_tiles[tile_in_hand_idx]);
        vert3f global_hand_position = calc_global_hand_position(h, tile_in_hand_idx, hand_to_world);


        vert3f lerp_pos;
        lerp_pos.x = lerp(global_wall_position.x, global_hand_position.x, anim_progress);
        lerp_pos.y = lerp(global_wall_position.y, global_hand_position.y, anim_progress);
        lerp_pos.z = lerp(global_wall_position.z, global_hand_position.z, anim_progress);
        local = mat_mul_vert3(world_to_hand, &lerp_pos);
    }

    return local;
}

const f32 discard_x_offsets[18] = {
    -0.02f, -0.04f, 0.0f, 0.03f, 0.0f, 0.0f,
    0.0f, -0.05f, -0.04f, 0.0f, 0.0f, 0.03f,
    -0.03f, 0.0f, 0.03f, 0.0f, 0.05f, 0.0f,
};

const f32 discard_z_offsets[18] = {
    -0.02f, -0.04f, 0.0f, 0.03f, 0.0f, 0.0f,
    0.0f, -0.05f, -0.04f, 0.0f, 0.0f, 0.03f,
    -0.03f, 0.0f, 0.03f, 0.0f, 0.05f, 0.0f,
};

const f32 discard_y_rots[18] = {
    -0.02f, -0.02f, 0.0f, 0.02f, 0.0f, 0.0f,
    0.0f, -0.01f, -0.02f, 0.0f, 0.0f, 0.03f,
    -0.01f, 0.0f, 0.03f, 0.0f, 0.02f, 0.0f,
};

const f32 wall_z_tile_offsets[17] = {
    -0.02f, -0.04f, 0.0f, 0.03f, 0.0f, 0.0f,
    0.0f, -0.05f, -0.04f, 0.0f, 0.0f, 0.03f,
    -0.03f, 0.0f, 0.03f, 0.0f, 0.05f,
};

vert3f calc_local_discard_position(int discard_i) {
    int discard_row = discard_i/6;
    int pos_in_row = discard_i - (discard_row*6);
    const f32 discard_row_size = -1.8f + 6*1.8f;
    const f32 half_row_size = discard_row_size/2.0f;
    f32 pos_x = half_row_size + (f32)pos_in_row * -1.8f;
    f32 pos_y = 0.0f;
    f32 pos_z = -19.5f + (f32)discard_row * 2.5f;
    return (vert3f){pos_x + discard_x_offsets[discard_i], pos_y, pos_z + discard_z_offsets[discard_i]};
}

vert3f calc_global_discard_position(int discard_i, matrix* hand_to_world_matrix) {
    vert3f local = calc_local_discard_position(discard_i);
    return mat_mul_vert3(hand_to_world_matrix, &local);
}

void draw_hand(
    u32 cur_frame, wall* w, hand* h, 
    int draw_wind_indicator,
    matrix* hand_to_view_matrix, matrix* hand_to_world_matrix) {
    mesh_draw_call draw_calls[14 + MAX_DISCARDS + MAX_OPEN_TILES + 1 + 40]; // one extra for wind indicator if necessary, plus 20 for tenbou sticks :)

    int draw_idx = 0;

    matrix world_to_hand = mat_inverse_affine(hand_to_world_matrix);

    for(int i = 0; i <  h->num_closed_tiles; i++) {

        vert3f local = calc_animated_hand_tile_position(cur_frame, w, h, hand_to_world_matrix, &world_to_hand, i);

        transform tile_trans = identity_transform();
        tile_trans.position.x = local.x;
        tile_trans.position.y = local.y;
        tile_trans.position.z = local.z;
        tile_trans.rotation.x = 1.57f;
        tile_trans.scale = (vert3f){TILE_SCALE, TILE_SCALE, TILE_SCALE};


        matrix tile_mat = transform_to_matrix(&tile_trans);
        matrix tile_to_view_matrix = mat_mul_mat(hand_to_view_matrix, &tile_mat);
        matrix tile_to_world_matrix = mat_mul_mat(hand_to_world_matrix, &tile_mat);

        draw_calls[draw_idx].shdr = LIT_TEXTURED; 
        draw_calls[draw_idx].mesh = &mahjong_tile_mesh;
        draw_calls[draw_idx].bounds = &tile_bbox;
        draw_calls[draw_idx].texture = h->tiles[i];
        draw_calls[draw_idx].model_to_view = tile_to_view_matrix;
        draw_calls[draw_idx].model_to_world = tile_to_world_matrix;
        draw_idx++;
    }

    /*
        draw open tiles

    */
    f32 row_poses[3][3] = {   
        {-20.0f, -22.6f, -24.6f}, // first is rotated
        {-20.0f, -22.3f, -24.6f}, // second is rotated
        {-20.0f, -22.0f, -24.3f}, // third is rotated
    };

    for(int i = 0; i < h->num_open_tiles; i++) {

        int pos_in_row = i%3;
        int open_tile_row = i/3;
        f32 *row;// = row_poses[0];
        if(pos_in_row == 0) {
            row = (h->open_tile_rotated[i] ? row_poses[0] : h->open_tile_rotated[i+1] ? row_poses[1] : row_poses[2]);
        } else if (pos_in_row == 1) {
            row = (h->open_tile_rotated[i-1] ? row_poses[0] : h->open_tile_rotated[i] ? row_poses[1] : row_poses[2]);
        } else {
            row = (h->open_tile_rotated[i-2] ? row_poses[0] : h->open_tile_rotated[i-1] ? row_poses[1] : row_poses[2]);
        }
        transform tile_trans = identity_transform();
        tile_trans.position.x = row[pos_in_row]; //(-20.0f - (f32)pos_in_row * 2.2f) - rot_space_offset;
        tile_trans.position.z = 2.0f + (f32)open_tile_row * -2.6f;
        tile_trans.rotation.x = 0.0f; // rotate back towards player
        tile_trans.rotation.y = h->open_tile_rotated[i] ? (f32)M_PI/2.0f : 0.0f;
        tile_trans.scale = (vert3f){TILE_SCALE, TILE_SCALE, TILE_SCALE};

        matrix tile_mat = transform_to_matrix(&tile_trans);

        matrix tile_to_view_matrix = mat_mul_mat(hand_to_view_matrix, &tile_mat);
        matrix tile_to_world_matrix = mat_mul_mat(hand_to_world_matrix, &tile_mat);

        draw_calls[draw_idx].shdr = LIT_TEXTURED; 
        draw_calls[draw_idx].mesh = &mahjong_tile_mesh;
        draw_calls[draw_idx].bounds = &tile_bbox;
        draw_calls[draw_idx].texture = h->open_tiles[i];
        draw_calls[draw_idx].model_to_view = tile_to_view_matrix;
        draw_calls[draw_idx].model_to_world = tile_to_world_matrix;
        draw_idx++;
    }
    
    
    /*
        draw discards

    */
    u32 discard_frames = get_frames_for_duration(DISCARD_DURATION);
    for(int i = 0; i < h->num_discards; i++) {


        vert3f discard_pos = calc_local_discard_position(i);


        f32 discard_progress = (f32)(cur_frame - h->discard_frames[i])/ (f32)discard_frames;
        discard_progress = CLAMP(discard_progress, 0.0f, 1.0f);
        int hand_slot_idx = h->discard_from_hand_idx[i];
        

        vert3f old_pos = (vert3f){
            calc_hand_x_position(h, hand_slot_idx),
            DISCARDING_TILE_Y_POS,
            0.0f
        };
        f32 old_rot = 1.57f;

        vert3f cur_pos = lerp_vert3f(old_pos, discard_pos, discard_progress);

        f32 cur_rot_x = lerp(old_rot, 0.0f, discard_progress);

        // rotate up
        // position downwards
        transform discard_transform = identity_transform();
        discard_transform.position = cur_pos;
        discard_transform.rotation.x = cur_rot_x;
        discard_transform.rotation.y = discard_y_rots[i];
        discard_transform.scale = (vert3f){TILE_SCALE, TILE_SCALE, TILE_SCALE};
        matrix discard_tile_mat = transform_to_matrix(&discard_transform);
      
        matrix tile_to_view_matrix = mat_mul_mat(hand_to_view_matrix, &discard_tile_mat);
        matrix tile_to_world_matrix = mat_mul_mat(hand_to_world_matrix, &discard_tile_mat);

        draw_calls[draw_idx].shdr = LIT_TEXTURED; 
        draw_calls[draw_idx].mesh = &mahjong_tile_mesh;
        draw_calls[draw_idx].bounds = &tile_bbox;
        draw_calls[draw_idx].texture = h->discards[i];
        draw_calls[draw_idx].model_to_view = tile_to_view_matrix;
        draw_calls[draw_idx].model_to_world = tile_to_world_matrix;
        draw_idx++;
    }

    if(draw_wind_indicator) {
        transform wind_indicator_trans = identity_transform();
        wind_indicator_trans.position.x = -25.0f;
        //wind_indicator_trans.rotation.y = (f32)M_PI;
        wind_indicator_trans.rotation.y = (f32)game_data.frame/1024.0f;
        wind_indicator_trans.rotation.z = game_data.cur_wind == EAST_WIND ? 0.0f : (f32)M_PI; 

        //wind_indicator_trans.scale = (vert3f){2.0f, 2.0f, 2.0f};
        wind_indicator_trans.scale = (vert3f){25.0f, 25.0f, 25.0f};

        matrix wind_indicator_to_hand_mat = transform_to_matrix(&wind_indicator_trans);

        draw_calls[draw_idx].shdr = LIT_TEXTURED;
        draw_calls[draw_idx].mesh = &dragon_decimated_mesh;
        draw_calls[draw_idx].bounds = 0;
        draw_calls[draw_idx].texture = WIND_INDICATOR;
        draw_calls[draw_idx].model_to_view = mat_mul_mat(hand_to_view_matrix, &wind_indicator_to_hand_mat);
        draw_calls[draw_idx++].model_to_world = mat_mul_mat(hand_to_world_matrix, &wind_indicator_to_hand_mat);
    }

    {
        /* draw tenbou */
        
        f32 stick_poses[10][4] = {
            {0.01f, 0.0f, 0.0f},{-0.01f, 0.0f, 0.5f},{-0.02f, 0.0f, 1.0f},{0.0f, 0.0f, 1.5f},
                 {0.0f, 0.4f, 0.25f},{0.0f, 0.4f, 0.75f},{0.0f, 0.4f, 1.25f},
                        {0.0f, 0.8f, 0.5f},{0.0f, 0.8f, 1.0f},
                             {0.0f, 1.2f, 0.75f}
        };
        

        tile_type tenbou_colors[4] = {RED_TENBOU, GOLD_TENBOU, BLUE_TENBOU, WHITE_TENBOU};
        int num_tenbou[4] = {0,0,0,0}; // 1x10,000 2x5000 (20000) 4x1000 (4000) 10x100 (1000)
        int tenbou_vals[4] = {10000, 5000, 1000, 100};
        int score = h->score;

        for(int i = 0; i < 4; i++) {
            while(score >= tenbou_vals[i]) {
                num_tenbou[i] += 1;
                score -= tenbou_vals[i];
            }
        }

        int cur_stack = 0;
        // if we have say, 20 tenbou of one color, it needs two stacks
        for(int i = 0; i < 4; i++) {

            transform tenbou_transform = identity_transform();
            tenbou_transform.scale = (vert3f){4.0f, 6.0f, 4.0f};

            tile_type color = tenbou_colors[i];

            int num_ten = num_tenbou[i];

            int num_stacks = (num_ten+9)/10;
            for(int stack = 0; stack < num_stacks; stack++) {
                f32 stack_x_position = 8.0f - (f32)(6*cur_stack);
                cur_stack++;
                int start_idx = stack*10;
                int amount_in_this_stack = MIN(10, num_ten - start_idx);
                for(int j = 0; j < amount_in_this_stack; j++) {
                    f32 x_position = stack_x_position + stick_poses[j][0];
                    f32 y_position = stick_poses[j][1];
                    f32 z_position = 2.5f + stick_poses[j][2];
                    f32 y_rot = stick_poses[j][0];
                    tenbou_transform.position = (vert3f){x_position, y_position, z_position};
                    tenbou_transform.rotation.y = y_rot;

                    matrix tenbou_mat = transform_to_matrix(&tenbou_transform);
                    matrix tenbou_to_view_matrix = mat_mul_mat(hand_to_view_matrix, &tenbou_mat);
                    matrix tenbou_to_world_matrix = mat_mul_mat(hand_to_world_matrix, &tenbou_mat);
                    

                    draw_calls[draw_idx].shdr = LIT_TEXTURED;
                    draw_calls[draw_idx].mesh = &tenbou_mesh;
                    draw_calls[draw_idx].bounds = &tile_bbox; // TODO: invalid but unused
                    draw_calls[draw_idx].texture = color;
                    draw_calls[draw_idx].model_to_view = tenbou_to_view_matrix;
                    draw_calls[draw_idx++].model_to_world = tenbou_to_world_matrix;
                }
            }


        }
    }

    submit_draw_calls(draw_calls, draw_idx, NO_FRUSTUM_CULL);
}

void draw_wall(game_state cur_state, u32 cur_frame, wall *w, matrix *view_mat) {

    mesh_draw_call draw_calls[34*4];

    int draw_idx = 0;

    matrix wall_matrixes[4];
    matrix wall_view_matrixes[4];

    for(int i = 0; i < 4; i++) {

        transform wall_trans = get_wall_transform(i);

        matrix wall_matrix = transform_to_matrix(&wall_trans);
        matrix wall_view_matrix = mat_mul_mat(view_mat, &wall_matrix);
        wall_matrixes[i] = wall_matrix;
        wall_view_matrixes[i] = wall_view_matrix;
    }

    for(int j = 0; j < game_data.wall_rem; j++) {
        // OFFSET BY SPLIT DISTANCE
        int offset_pos = mod_positive(j-(34-game_data.wall_split_distance*2),TILES_IN_DECK);

        int wall_side = (offset_pos/34);
        int position_in_wall = (offset_pos-wall_side*34)/2;

        tile_type this_tile = w->tiles[j];

        transform tile_trans = identity_transform();
        tile_trans.position.x = calc_wall_tile_x_position(position_in_wall);
        tile_trans.rotation.x = (cur_state >= IN_GAME && j == 3) ? (f32)0 : (f32)M_PI;

        tile_trans.position.y = calc_wall_tile_y_position(cur_frame, w, j);
        tile_trans.position.z = ((wall_side & 1) ? 0.0f : wall_z_tile_offsets[position_in_wall]);
        tile_trans.scale = (vert3f){TILE_SCALE, TILE_SCALE, TILE_SCALE};



        matrix tile_mat = transform_to_matrix(&tile_trans);
        matrix tile_to_view_matrix = mat_mul_mat(&wall_view_matrixes[wall_side], &tile_mat);
        matrix tile_to_world_matrix = mat_mul_mat(&wall_matrixes[wall_side], &tile_mat);

        draw_calls[draw_idx].shdr = LIT_TEXTURED;
        draw_calls[draw_idx].mesh = &mahjong_tile_mesh;
        draw_calls[draw_idx].bounds = &tile_bbox;
        draw_calls[draw_idx].model_to_view = tile_to_view_matrix;
        draw_calls[draw_idx].model_to_world = tile_to_world_matrix;
        draw_calls[draw_idx++].texture = this_tile;
    }

    submit_draw_calls(draw_calls, draw_idx, FRUSTUM_CULL);
}

void draw_riichi_game(game_state cur_state, u32 cur_frame, matrix* view_mat) {
    transform t_east = identity_transform(); t_east.rotation.y = (f32)M_PI * 0.5f; t_east.position.x = 26.5f; // push 10 units to the right
    transform t_south = identity_transform(); t_south.rotation.y = 0; t_south.position.z = 26.5f; // push 10 units down
    transform t_west = identity_transform(); t_west.rotation.y = -(f32)M_PI * 0.5f; t_west.position.x = -26.5f; // push 10 units left
    transform t_north = identity_transform(); t_north.rotation.y = (f32)M_PI; t_north.position.z = -26.5f;


    matrix east_matrix = transform_to_matrix(&t_east);
    matrix south_matrix = transform_to_matrix(&t_south);
    matrix west_matrix = transform_to_matrix(&t_west);
    matrix north_matrix = transform_to_matrix(&t_north);

    matrix east_view_matrix = mat_mul_mat(view_mat, &east_matrix);
    matrix south_view_matrix = mat_mul_mat(view_mat, &south_matrix);
    matrix west_view_matrix = mat_mul_mat(view_mat, &west_matrix);
    matrix north_view_matrix = mat_mul_mat(view_mat, &north_matrix);


    matrix *hand_matrixes[4][2] = {
        {&east_view_matrix, &east_matrix},
        {&south_view_matrix, &south_matrix},
        {&west_view_matrix, &west_matrix},
        {&north_view_matrix, &north_matrix}
    };

    for(int i = 0; i < 4; i++) {
        hand* this_hand = &game_data.hands[i];
        draw_hand(cur_frame, 
            &board_wall, this_hand, 
            (i == game_data.cur_dealer), // draw wind indicator
            hand_matrixes[i][0], hand_matrixes[i][1]
        );
        for(int j = 0; j < this_hand->num_discards; j++) {
            if(cur_frame == (this_hand->discard_frames[j] + get_frames_for_duration(DISCARD_DURATION)) && this_hand->discard_frames[j] != 0 ) {
                add_sound( TILE_CLICK, 0.125f);
                //calc_global_discard_position(j, hand_matrixes[i][1])
                
                this_hand->discard_frames[j] = 0;
            }
        }
    }

    draw_wall(cur_state, cur_frame, &board_wall, view_mat);
}

static obj_mesh board_sides;
void draw_board(matrix* view_mat) {
    mesh_draw_call draw_board_call;
    transform board_transform = identity_transform();
    board_transform.position.y = -0.73f;
    board_transform.scale = (vert3f){32.0, 10.0f, 32.0f};
    matrix board_matrix = transform_to_matrix(&board_transform);
    matrix board_to_view_matrix = mat_mul_mat(view_mat, &board_matrix);
    
    draw_board_call.shdr = UNLIT_TEXTURED;
    //draw_board_call.mesh = &board_mesh;
    draw_board_call.bounds = &board_bbox;
    draw_board_call.model_to_view = board_to_view_matrix;
    draw_board_call.model_to_world = board_matrix;
    draw_board_call.texture = BOARD;

    board_sides.indexCount = board_mesh.indexCount - 12;
    board_sides.indexStream = board_mesh.indexStream + 12;
    board_sides.vertexCount = board_mesh.vertexCount;
    board_sides.vertexStream = board_mesh.vertexStream;
    draw_board_call.mesh = &board_sides;
    

    if(!got_board_min_max_coords) {
        vert3f board_verts[8];
        got_board_min_max_coords = 1;

        project_bounding_box(&board_bbox, &board_to_view_matrix, board_verts);
        f32 min_y = board_verts[0].y, max_y = board_verts[0].y;
        for(int i = 0; i < 8; i++) {
            min_y = MIN(min_y, board_verts[i].y);
            max_y = MAX(max_y, board_verts[i].y);
        }
        f32 top_min_x = 100000.0f, top_max_x = -100000.0f;
        f32 bot_min_x = 100000.0f, bot_max_x = -100000.0f;
        for(int i = 0; i < 8; i++) {
            if(fabsf(board_verts[i].y-min_y) < 4.0f) {
                top_min_x = MIN(top_min_x, board_verts[i].x);
                top_max_x = MAX(top_max_x, board_verts[i].x);
            }

            if(fabsf(board_verts[i].y-max_y) < 4.0f) {
                bot_min_x = MIN(bot_min_x, board_verts[i].x);
                bot_max_x = MAX(bot_max_x, board_verts[i].x);
            }
        }
        board_min_y = min_y;
        board_max_y = max_y;
        board_top_min_x = top_min_x;
        board_top_max_x = top_max_x;
        board_bot_min_x = bot_min_x-2.0f;
        board_bot_max_x = bot_max_x+2.0f;
    }

    submit_draw_calls(&draw_board_call, 1, FRUSTUM_CULL);

}

transform cam_view_trans;
static u64 ms_per_frame;
static u64 prev_frame_ticks = 0;  


void game_draw(ExotiqueInterface* ei) {

    u64 cur_frame_ticks = ei->ticks;

    u64 prev_ms_per_frame = ms_per_frame;
    (void)prev_ms_per_frame;
    ms_per_frame = cur_frame_ticks - prev_frame_ticks;    
    prev_frame_ticks = cur_frame_ticks;
    static int init;
    if(!init) {
        //exotique_printf("setting bench ms to %llu\n", 33);
        bench_frame_ms = 33;
        init = 1;
    }

    vcache_misses = 0;
    vcache_hits = 0;
    total_triangles = 0;

    static block whole_frame_block = ROOT_TIMED_BLOCK(whole_frame_block, "draw frame")
        matrix view_matrix = transform_to_view_matrix(&cam_view_trans);
        static block clear_tiles_block = START_TIMED_BLOCK(clear_tiles_block, "clear buf", whole_frame_block)
            clear_tile_bins();
        END_TIMED_BLOCK(clear_tiles_block)
        
        static block riichi_block = START_TIMED_BLOCK(riichi_block, "draw game", whole_frame_block)
            draw_riichi_game(game_data.cur_game_state, game_data.frame, &view_matrix);
        END_TIMED_BLOCK(riichi_block)

        static block board_block = START_TIMED_BLOCK(board_block, "draw board", whole_frame_block)
            draw_board(&view_matrix);
        END_TIMED_BLOCK(board_block)


        static block raster_block = START_TIMED_BLOCK(raster_block, "rast. tiles", whole_frame_block)
            rasterize_tiles(ei, zbuf);
        END_TIMED_BLOCK(raster_block)

    END_TIMED_BLOCK(whole_frame_block)

    if((game_data.frame & 31) == 0) {
        print_and_reset_root_block(&whole_frame_block);
        exotique_printf("total %.2f ms\n", (double)(prev_ms_per_frame + ms_per_frame) / 2.0);
        exotique_printf("vcache misses %i, hits %i %.2f\n", vcache_misses, vcache_hits, (double)vcache_hits*100.0/(double)(vcache_misses+vcache_hits));
    }
    

    //exotique_printf("meshes %i\n", meshes_transformed);
    //exotique_printf("triangles transformed %i\n", vertexes_transformed);
    //exotique_printf("triangles rasterized %i\n", triangles_rasterized);
    meshes_transformed = 0;
    vertexes_transformed = 0;
    triangles_rasterized = 0;
}

//    GAME LOGIC

void init_wall() {
    game_data.wall_rem = 0;
    game_data.wall_split_distance = 17 - (int)(roll_die() + roll_die());

}

hand init_hand() {
    hand h;
    h.num_closed_tiles = 0;
    h.num_open_tiles = 0;
    return h;
}

// 1 10,000 stick (RED)
// 2 5,000 sticks (GOLD)
// 4 1,000 sticks (BLUE)
// ten 100 sticks (WHITE)
hand init_empty_hand() {
    hand h;
    h.num_open_tiles = 0; 
    h.num_closed_tiles = 0;
    h.num_discards = 0;
    h.selected_tile_idx = -1;
    h.in_riichi = 0;
    h.score = 25000;

    return h;
}

void shuffle_deck(wall *game_wall) {
    tile_type *deck = game_wall->tiles;
    int out = 0;
    for(int i = 0; i < NUM_TILES; i++) {
        if(i == FIVE_SOU_RED || i == FIVE_MAN_RED || i == FIVE_PIN_RED) {
            deck[out-1] = i;
            continue;
        }
        deck[out++] = i; deck[out++] = i; deck[out++] = i; deck[out++] = i; // 4 of each card type unless it's akadora
    }

    u32 j = TILES_IN_DECK-1;
    for(u32 i = TILES_IN_DECK-1; i >= 1; i--) {
        j = 1 + (nextrand() % i);
        tile_type tmp = deck[i];
        deck[i] = deck[j];
        deck[j] = tmp;
    }
}

void init_seeds(ExotiqueInterface *ei) {
    game_data.seeds[3] = game_data.seeds[2];
    game_data.seeds[1] = game_data.seeds[0];
    game_data.seeds[0] = (u32)ei->ticks;
}

void reset_game() {
    game_data.cur_wind = EAST_WIND;
    game_data.cur_game_state = INITIAL_SHUFFLE_AND_SETUP;
    game_data.frame = 0;
    game_data.deal_steps = 0;
    game_data.cur_dealer++;
    if(game_data.cur_dealer == 4) {
        game_data.cur_dealer = 0;
        game_data.cur_wind = (game_data.cur_wind+1)%2;
    }
    game_data.cur_player = game_data.cur_dealer;
    game_data.switch_player_timer = -1;
    game_data.draw_end_frame = 0;
    
    game_data.wall_rem = 0;

    init_wall();
    for(int i = 0; i < 4; i++) {
        game_data.hands[i] = init_empty_hand();
    }
    // shuffle a second deck in memory
    shuffle_deck(&board_wall);



    game_data.next_deal_pos = 0;
    game_data.last_discard_player = -1;
}

#include "SDL_net.h"

static TCPsocket serv_sock = NULL;
IPaddress server_ip;
SDLNet_SocketSet socket_set;

#define MAKE_NUM(A, B, C, D)    (((A+B)<<8)|(C+D))

#define JONG_PORT MAKE_NUM('J','O','N','G')
#define MAX_CLIENTS 3
static struct {
    int active;
    int player_num;
    TCPsocket sock;
    IPaddress peer;
    Uint8 name[256+1];
} client_info[MAX_CLIENTS];

void server_get_connection() {
    TCPsocket new_sock;
    int which;

    new_sock = SDLNet_TCP_Accept(serv_sock);
    if(new_sock == NULL) {
        return;
    }

    // look for unconnected person slot
    for(which = 0; which < MAX_CLIENTS; which++) {
        if(!client_info[which].sock) {
            break;
        }
    }
    if(which == MAX_CLIENTS) {
        // another client is attempting to connect but we're already full
        exotique_printf("we're already full\n");
        return;
    }

    client_info[which].sock = new_sock;
    client_info[which].peer = *SDLNet_TCP_GetPeerAddress(new_sock);
    client_info[which].player_num = -1;
    for(int i = 0; i < 4; i++) {
        if(game_data.player_types[i] == NETWORK_HUMAN) {
            int already_used = 0;
            for(int j = 0; j < MAX_CLIENTS; j++) {
                if(client_info[j].sock && client_info[j].player_num == i) {
                    already_used = 1;
                    break;
                }
            }
            if(already_used == 0) {
                client_info[which].player_num = i;
                break;
            }
        }
    }
    if(client_info[which].player_num == -1) {
        exotique_printf("All players are used even though we have a free network slot?\n");
        exit(1);
    }
    SDLNet_TCP_AddSocket(socket_set, client_info[which].sock);
    exotique_printf("Client connected at %i %i\n", client_info[which].peer.host, client_info[which].peer.port);
}

typedef enum __attribute__((packed)) {
    RANDOM_SEED_AND_PLAYER_NUM = 1,
    INPUT_FROM_CLIENT = 3,
    INPUTS_FROM_ALL_PLAYERS = 5,
} msg_type;

#define ACTIONS             \
    X(NO_ACTION)            \
    X(MOVE_SELECTED_LEFT)   \
    X(MOVE_SELECTED_RIGHT)  \
    X(ATTEMPT_CALL)         \
    X(ATTEMPT_DRAW)         \
    X(ATTEMPT_DISCARD)      \
    X(ATTEMPT_RIICHI_OR_WIN)

typedef enum __attribute__((packed)) {
#define X(a) a,
    ACTIONS
#undef X
} player_action_type;
const char* action_names[] = {
#define X(a) #a,
    ACTIONS
#undef X
};

typedef struct {
    i8 player_num;
    player_action_type cmd;
} player_action;

player_action make_player_action(i8 player_num, player_action_type cmd) {
    return (player_action){player_num, cmd};
}

typedef struct {
    player_action actions[3];
} action_from_other_clients;

typedef struct {
    u32 seeds[4];
    int player_num;
} seed_and_player_num;

#define MAX_PACKET_SIZE (sizeof(seed_and_player_num)+1)

void setup_host(int num_clients) {
    if(num_clients < 0 || num_clients > 3) {
        exotique_printf("Please specify number of clients between 0 and 3, --num_clients [num]\n");
        exit(1);
    }
    
    game_data.player_types[0] = HUMAN;
    for(int i = 1; i <= num_clients; i++) {
        game_data.player_types[i] = NETWORK_HUMAN;
    }
    for(int i = num_clients+1; i < 4; i++) {
        game_data.player_types[i] = CONSERVATIVE_AI;
    }
    if(num_clients == 0) {
        return; 
    }
    socket_set = SDLNet_AllocSocketSet(num_clients+1);
    if(socket_set == NULL) {
        exotique_printf("Couldn't create socket set: %s\n", SDLNet_GetError());
        exit(1);
    }
    SDLNet_ResolveHost(&server_ip, NULL, JONG_PORT);
    exotique_printf("Server IP: %x, %d\n", server_ip.host, SDLNet_Read16(&server_ip.port));
    serv_sock = SDLNet_TCP_Open(&server_ip);
    if ( serv_sock == NULL ) {
        exotique_printf("Couldn't create server socket: %s\n",SDLNet_GetError());
        exit(1);
    }
    SDLNet_TCP_AddSocket(socket_set, serv_sock);

    // wait until clients connect
    exotique_printf("Waiting for clients to connect to port %i\n", JONG_PORT);

    while(1) {
        SDLNet_CheckSockets(socket_set, ~0);
        if(SDLNet_SocketReady(serv_sock)) {
            server_get_connection();
        }
        int connected_clients = 0;
        for(int i = 0; i < MAX_CLIENTS; i++) {
            connected_clients += client_info[i].sock ? 1 : 0;
        }
        if(connected_clients == num_clients) {
            exotique_printf("All clients connected.\n");
            break;
        }
    }
}

void setup_client(char* server_address) {
    //SDLNet_AllocPacketV(4, sizeof(game_data));

    exotique_printf("Attempting to connect to server @ %s:%i\n", server_address, JONG_PORT);
    SDLNet_ResolveHost(&server_ip, server_address, JONG_PORT);
    if(server_ip.host == INADDR_NONE) {
        exotique_printf("Couldn't resolve hostname\n");
        exit(1);
    }
    exotique_printf("Connecting to %s %i\n", server_address, JONG_PORT);
    serv_sock = SDLNet_TCP_Open(&server_ip);
    if(serv_sock == NULL) {
        exotique_printf("Failed to connect :(\n");
        exit(1);
    }
}

char data_buf[MAX_PACKET_SIZE];
void send_initial_state() {
    char data_buf[1+sizeof(seed_and_player_num)];
    data_buf[0] = RANDOM_SEED_AND_PLAYER_NUM;

    seed_and_player_num seed_info;


    for(int i = 0; i < MAX_CLIENTS; i++) {
        if(client_info[i].sock) {
            memcpy(seed_info.seeds, game_data.seeds, sizeof(u32)*4);
            seed_info.player_num = client_info[i].player_num;
            memcpy(data_buf+1, &seed_info, sizeof(seed_and_player_num));

            SDLNet_TCP_Send(client_info[i].sock, &data_buf, 1+sizeof(seed_and_player_num));
        }
    }
}

void receive_initial_state() {
    exotique_printf("Waiting for initial state\n");
    long long unsigned int recvd = SDLNet_TCP_Recv(serv_sock, data_buf, 1+sizeof(seed_and_player_num));
    if(recvd <= 0) {
        exotique_printf("Error: disconnected from server :(\n");
        //exit(1);
        return;
    }

    if(recvd < 1+sizeof(seed_and_player_num)) {
        exotique_printf("Expected game state (%i bytes) but only got %i bytes", 1+sizeof(seed_and_player_num), recvd);
        //exit(1);
        return;
    }

    seed_and_player_num start_info;
    memcpy(&start_info, data_buf+1, sizeof(seed_and_player_num));

    human_player = start_info.player_num;
    exotique_printf("Game initialized, you are player %i\n", human_player);
    memcpy(&game_data.seeds, start_info.seeds, sizeof(u32)*4);


    //memcpy(&game_data, data_buf+1, sizeof(game_data));

}
int is_host = 0, is_client = 0;


#define MAX_PLAYER_EVENTS 4
player_action action_queue[MAX_PLAYER_EVENTS];

int queue_head = 0;
int queue_tail = 0;

typedef struct {
    int num_events;
    player_action action_queue[MAX_PLAYER_EVENTS];
} action_queue_pkt;

// 0-0 -> no events
// 0-1 -> 1 event
// 0-2 -> 2 events
// 0-3 -> 3 events
// 0-4 -> 4 events

int queue_full() {
    return (queue_head - queue_tail) == MAX_PLAYER_EVENTS;
}
int queue_empty() {
    return (queue_head - queue_tail) == 0;
}
int queue_size() {
    return (queue_head - queue_tail);
}

void exit(int);

player_action queue_peek() {
    return action_queue[queue_tail&(MAX_PLAYER_EVENTS-1)];
}

player_action queue_pop() {
    if(queue_empty()) {
        exotique_printf("ACTION QUEUE UNDERFLOW!\n");
        exit(1);
    }
    player_action act = queue_peek();
    queue_tail++;
    return act;
}

void queue_push(player_action act) {
    if(queue_full()) {
        exotique_printf("ACTION QUEUE OVERFLOW!\n");
        exit(1);
    }
    action_queue[queue_head&(MAX_PLAYER_EVENTS-1)] = act;
    queue_head++;
}

void copy_queue_entries(int *out_num_entries, player_action *out_queue) {
    int cur_tail = queue_tail;
    int num_entries = 0;
    if(queue_size() == 0) {
        return;
    }
    while(cur_tail != queue_head) {
        out_queue[num_entries++] = action_queue[cur_tail&(MAX_PLAYER_EVENTS-1)];
        cur_tail++;
    }
    *out_num_entries = num_entries;
}


void server_wait_for_actions_from_players() {
    char data_buf[1+sizeof(player_action)];
    
    for(int i = 0; i < MAX_CLIENTS; i++) {
        if(client_info[i].sock) {
            long long unsigned int recvd = SDLNet_TCP_Recv(client_info[i].sock, data_buf, 1+sizeof(player_action));
            if(recvd != 1+sizeof(player_action)) {
                exotique_printf("Didn't receive right packet size waiting for input (got %i bytes) from client %i %i\n",
                    recvd,
                    client_info[i].peer.host, client_info[i].peer.port
                );
                exit(1);
            }
            if(data_buf[0] != INPUT_FROM_CLIENT) {
                exotique_printf("Got unexpected byte from client when waiting for input %i\n", data_buf[0]);
                exit(1);
            }
            player_action act;
            memcpy(&act, data_buf+1, sizeof(player_action));
            if(act.player_num != client_info[i].player_num) {
                exotique_printf("PLAYER NUMBER MISMATCH ON CLIENT %i %i,  should be player %i, received player %i\n",
                    client_info[i].peer.host, client_info[i].peer.port,
                    client_info[i].player_num, act.player_num
                );
                exit(1);
            }
            queue_push(act);
        }
    }
}

void server_send_actions_to_players() {
    char data_buf[1+sizeof(action_queue_pkt)];
    data_buf[0] = INPUTS_FROM_ALL_PLAYERS;

    action_queue_pkt packet;

    copy_queue_entries(&packet.num_events, packet.action_queue);

    memcpy(data_buf+1, &packet, sizeof(action_queue_pkt));

    for(int i = 0; i < MAX_CLIENTS; i++) {
        // just send entire queue back
        if(client_info[i].sock) {
            long long unsigned int send = SDLNet_TCP_Send(client_info[i].sock, data_buf, 1+sizeof(action_queue_pkt));
            if(send < 1+sizeof(action_queue_pkt)) {
                exotique_printf("Send intputs to client failed, only sent %i bytes\n");
                exit(1);
            }
        }
    }
}

void client_send_input_to_server() {
    char data_buf[1+sizeof(player_action)];
    data_buf[0] = INPUT_FROM_CLIENT;
    player_action this_action;
    if(queue_size() == 0) {
        this_action = make_player_action(human_player, NO_ACTION);
    } else if (queue_size() == 1) {
        this_action = queue_peek();
    } else {
        exotique_printf("Expected only one entry (player input) in queue, but got %i\n", queue_size());
        exit(1);
    }
    memcpy(data_buf+1, &this_action, sizeof(player_action));

    long long unsigned int sent = SDLNet_TCP_Send(serv_sock, &data_buf, 1+sizeof(player_action));
    if(sent < 1+sizeof(player_action)) {
        exotique_printf("Send input to server failed, only sent %i bytes\n", sent);
        exit(1);
    }
    //exotique_printf("sent %i bytes of data to server.\n", sent);
}

void client_wait_for_actions_from_server() {
    char data_buf[1+sizeof(action_queue_pkt)];
    action_queue_pkt actions;

    long long unsigned int recvd = SDLNet_TCP_Recv(serv_sock, &data_buf, 1+sizeof(action_queue_pkt));

    if(recvd < 1+sizeof(action_queue_pkt)) {
        exotique_printf("Receive inputs from server failed, only received %i bytes\n", recvd);
        exit(1);
    }
    if(data_buf[0] != INPUTS_FROM_ALL_PLAYERS) {
        exotique_printf("Got unexpected header byte from server when waiting for all inputs %i\n", data_buf[0]);
    }
    memcpy(&actions, data_buf+1, sizeof(action_queue_pkt));
    for(int i = 0; i < actions.num_events; i++) {
        if(actions.action_queue[i].player_num == human_player) {
            continue;
        }
        queue_push(actions.action_queue[i]);
    }
}

void game_load(ExotiqueInterface* ei, int argc, const char* argv[]) {    
    char* server_address = NULL;
    for(int i = 1; i < argc; i++) {
        if((strcmp(argv[i], "--host") == 0) || (strcmp(argv[i], "-h") == 0)) {
            is_host = 1;
        }
        if((strcmp(argv[i], "--client") == 0) || (strcmp(argv[i], "-c") == 0)) {
            is_client = 1;
            if(argc > i+1) {
                // next arg is address and port
                server_address = (char*)argv[i+1];
            }
        }
    }
    int num_clients = -1;
    for(int i = 0; i < argc; i++) {
        if(strcmp(argv[i], "--num_clients") == 0 && argc > i+1) {
            char *endptr;
            errno = 0;
            num_clients = strtol(argv[i+1],&endptr, 10);
            if(errno == ERANGE || endptr == argv[i+1]) {
                exotique_printf("Invalid number of clients %i\n", num_clients);
                exit(1);
            }
        }
    }



    if(is_host && is_client) {
        exotique_printf("Args suggest both client and host, invalid\n");
        exit(1);
    } else if (is_host || is_client) {
        int net_init_error = SDLNet_Init();
        if(net_init_error != 0) {
            exotique_printf("error initializing network stack %s\n", SDLNet_GetError());
            SDLNet_Quit();
            exit(1);
        }
    } else {
        exotique_printf("Running in host mode with zero clients\n");
        is_host = 1;
        num_clients = 0;
    }

    

    if(is_host) {
        setup_host(num_clients);
        init_seeds(ei);
        send_initial_state();
    } else if (is_client) {
        setup_client(server_address);
        receive_initial_state();
    }
    reset_game();

    num_active_sounds = 0;
    
    ma_config = ma_device_config_init(ma_device_type_playback);

    ma_config.playback.format   = ma_format_s16;   // Set to ma_format_unknown to use the device's native format.
    ma_config.playback.channels = 2;               // Set to 0 to use the device's native channel count.
    ma_config.sampleRate        = 22050;           // Set to 0 to use the device's native sample rate.
    ma_config.dataCallback      = &data_callback;   // This function will be called when miniaudio needs more data.
    ma_config.stopCallback      = &stop_device_callback;
    ma_config.performanceProfile = ma_performance_profile_low_latency;
    ma_config.noClip = MA_TRUE;
    ma_config.noPreSilencedOutputBuffer = MA_TRUE;
    ma_config.periodSizeInFrames = 16;
    
    ma_result dev_init_res = ma_device_init(NULL_PTR, &ma_config, &sound_device);
    if (dev_init_res != MA_SUCCESS) {
        exotique_printf("miniaudio failed to init %i\n", dev_init_res);
        return;  // Failed to initialize the device.
    }
    
    ma_result dev_start_res = ma_device_start(&sound_device);     // The device is sleeping by default so you'll need to start it manually.
    if(dev_start_res != MA_SUCCESS) {
        exotique_printf("miniaudio failed to init %i\n", dev_start_res);
        return;
    }
    

    int i;
    int last_used_pal_idx = 0;

    // load lighting palette into the first 1+(NUM_SHADES)*(NUM_BASE_COLORS-1) palette slots.
    // one slot is used for black, the others get NUM_SHADES variants.
    for(int shade = 0; shade < NUM_SHADES; shade++) {
        int base = 1 + shade*(NUM_BASE_COLORS-1);
        f32 scale = lerp(1.0f/(f32)NUM_SHADES, (f32)NUM_SHADES/(f32)NUM_SHADES, (f32)shade/(f32)NUM_SHADES);
        for(i = 0; i < NUM_BASE_COLORS; i++) {
            if(i == 0) {
                // if the color is black, always point to the same palette index
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


    // load background texture entries into palette
    int num_bkgd_pal_entries = sizeof(palette_background)/sizeof(u32);

    for(i = 0; i < num_bkgd_pal_entries; i++) {
        u32 pal_entry = (palette_background[i]<<8)|0xFF;
        ei->palette[last_used_pal_idx+i+1] = pal_entry;
    }



    int free_slot = last_used_pal_idx+1+num_bkgd_pal_entries;
    ei->palette[free_slot] = 0xC45766FF;
    
    for(int shade = 0; shade < NUM_SHADES; shade++) {
        for(int p = 0; p < 256; p++) {
            vert3f rgba = rgba_to_f32_rgb(ei->palette[p]);
            f32 scale = lerp(1.0f/(f32)NUM_SHADES, (f32)NUM_SHADES/(f32)NUM_SHADES, (f32)shade/(f32)NUM_SHADES);
            rgba = scale_vert3(rgba, scale);
            full_light_remap_table[shade][p] = closest_overall_color_idx(ei, rgba);
        }
    }
    for(i = 0; i < 256; i++) {
        vert3f c1 = rgba_to_f32_rgb(ei->palette[i]);
        for(int j = 0; j < 256; j++) {
            if(i == j) {
                mix_table[(i<<8)|j] = (u8)i;
            } else {
                vert3f c2 = rgba_to_f32_rgb(ei->palette[j]);
                vert3f avg = scale_vert3(add_vert3(c1, c2), 0.5f);
                mix_table[(i<<8)|j] = closest_overall_color_idx(
                    ei, avg
                );
            }
        }
    }
    //output_mixing_table(ei);
    //output_palette(ei);
    decompress_sounds();
    decompress_textures(ei);

    texture_board[0] = light_remap_table[NUM_SHADES/2-1][GREEN];
    
    clear_tile_bins();

    tile_bbox = get_mesh_bbox(&mahjong_tile_mesh);
    board_bbox = get_mesh_bbox(&board_mesh);

}

void step_shuffle_and_setup(u32 cur_frame, wall* w) {
    //if((cur_frame&3) != 1) {
    //    return;
    //}

    if(game_data.wall_rem == TILES_IN_DECK) {
        if(w->tile_fall_frames[game_data.wall_rem-1]+get_frames_for_duration(TILE_FALL_DURATION) <= cur_frame) {
            game_data.cur_game_state = DEALING;
        }
    } else {
        w->tile_fall_frames[game_data.wall_rem] = cur_frame;
        game_data.wall_rem++;
    }
    for(int i = 0; i < game_data.wall_rem; i++) {
        if(cur_frame == w->tile_fall_frames[i] && w->tile_fall_frames[i] != 0) {
            
            add_sound(
                TILE_CLICK,
                0.085f
            );

        }
    }
}

void sort_hand(hand* h) {
    for(int i = 0; i < h->num_closed_tiles; i++) {
        int biggest_tile = h->tiles[i];

        for(int j = i+1; j < h->num_closed_tiles; j++) {
            int cur_tile = h->tiles[j];
            if(tile_sort_val[cur_tile] < tile_sort_val[biggest_tile]) {
                h->tiles[i] = cur_tile;
                h->tiles[j] = biggest_tile;
                biggest_tile = cur_tile;
            }
        }
    }
}

void step_deal(u32 cur_frame) {
    // start with east
    if((cur_frame & 15) != 0) {
        return;
    }

    int hand_index = (game_data.cur_dealer + game_data.deal_steps) & 0x3;
    hand* this_hand = &game_data.hands[hand_index];

    i8 cur_num_tiles = this_hand->num_closed_tiles;

    int cur_deal_count = 1;
    if(game_data.deal_steps < 12) {
        cur_deal_count = 4;
    }
    for(int i = 0; i < cur_deal_count; i++) {
        this_hand->deal_frame_for_tiles[cur_num_tiles] = cur_frame;
        this_hand->wall_index_for_tiles[cur_num_tiles] = --game_data.wall_rem;
        this_hand->tiles[cur_num_tiles++] = board_wall.tiles[game_data.wall_rem];
    }
    this_hand->num_closed_tiles = cur_num_tiles;
    this_hand->selected_tile_idx = (i8)(cur_num_tiles-1);
    game_data.deal_steps++;
    if(game_data.deal_steps == 17) {
        game_data.cur_game_state = IN_GAME;

        game_data.hands[0].tiles[0] = EAST;
        game_data.hands[0].tiles[1] = EAST;
        game_data.hands[0].tiles[2] = EAST;
        game_data.hands[0].tiles[3] = GREEN_DRAGON;
        game_data.hands[0].tiles[4] = GREEN_DRAGON;
        game_data.hands[0].tiles[5] = ONE_MAN;
        game_data.hands[0].tiles[6] = TWO_MAN;
        game_data.hands[0].tiles[7] = THREE_MAN;
        game_data.hands[0].tiles[8] = FIVE_MAN;
        game_data.hands[0].tiles[9] = FIVE_MAN;
        game_data.hands[0].tiles[10] = THREE_PIN;
        game_data.hands[0].tiles[11] = FOUR_PIN;
        game_data.hands[0].tiles[12] = FIVE_PIN;
        game_data.hands[0].tiles[13] = FIVE_MAN;

        game_data.switch_player_timer = -1;
        
        for(int i = 0; i < 4; i++) {
            sort_hand(&game_data.hands[i]);
        }
    }
}

int last_start_pushed = 0, last_select_pushed = 0;
int last_a_pushed = 0, last_b_pushed = 0;
int last_left_pushed = 0, last_right_pushed = 0;
int last_x_pushed = 0, last_y_pushed = 0;

void discard_current_tile(i8 player, u32 cur_frame) {
    hand* cur_player_hand = &game_data.hands[player];

    i8 selected_tile_idx = cur_player_hand->selected_tile_idx;
    cur_player_hand->discards[cur_player_hand->num_discards] = cur_player_hand->tiles[selected_tile_idx];
    game_data.last_discard = cur_player_hand->tiles[selected_tile_idx];
    game_data.last_discard_player = player;

    cur_player_hand->discard_frames[cur_player_hand->num_discards] = cur_frame;
    cur_player_hand->discard_from_hand_idx[cur_player_hand->num_discards++] = selected_tile_idx;

    for(int i = cur_player_hand->selected_tile_idx; i < cur_player_hand->num_closed_tiles-1; i++) {
        cur_player_hand->tiles[i] = cur_player_hand->tiles[i+1];
    }
    cur_player_hand->num_closed_tiles--;
    sort_hand(cur_player_hand);
}

u32 draw_next_tile(u32 cur_frame, int player) {
    hand* cur_player_hand = &game_data.hands[player];
    i8 cur_num_tiles = cur_player_hand->num_closed_tiles++;
    cur_player_hand->deal_frame_for_tiles[cur_num_tiles] = cur_frame;
    cur_player_hand->wall_index_for_tiles[cur_num_tiles] = --game_data.wall_rem;
    cur_player_hand->tiles[cur_num_tiles] = board_wall.tiles[game_data.wall_rem];
    cur_player_hand->selected_tile_idx = cur_num_tiles;
    return cur_frame + get_frames_for_duration(TILE_DEAL_DURATION);
}

#define AI_PLAYER_SPEED 0.333f

tile_type player_winds[4] = {
    EAST, NORTH, WEST, SOUTH 
};

int count_tile_in_hand(hand* h, tile_type search_tile) {
    int count = 0;
    for(int i = 0; i < h->num_closed_tiles; i++) {
        count += (h->tiles[i] == search_tile ? 1 : 0);
    }
    return count;
}

int find_index_of_first_tile_in_hand(hand* h, tile_type search_tile) {
    for(int i = 0; i < h->num_closed_tiles; i++) {
        if(h->tiles[i] == search_tile) {
            return i;
        }
    }
    return -1;
}

int get_best_discard(int player, hand* h) {
    // first discard winds
    // TODO: fix for rotating winds :)
    tile_type round_wind = game_wind_to_tile_wind[game_data.cur_wind];
    tile_type seat_wind = player_winds[player];
    
    int winds_in_hand[4];
    for(int i = 0; i < 4; i++) {
        winds_in_hand[i] = count_tile_in_hand(h, player_winds[i]);
    }

    // check for single non-seat non-round wind
    for(int i = 0; i < 4; i++) {
        if(player_winds[i] == round_wind || player_winds[i] == seat_wind) {
            continue;
        }
        if(winds_in_hand[i] == 1) {
            // discard non-helpful wind first
            int idx = find_index_of_first_tile_in_hand(h, player_winds[i]);
            if(idx == -1) {
                exotique_printf("BUG IN TILE DISCARD PRIORITIZATION, FOUND WIND TILE BUT DOESNT EXIST?\n");
            }
            return idx;
        }
    }
    tile_type dragons[3] = { RED_DRAGON, GREEN_DRAGON, WHITE_DRAGON };
    for(int i = 0; i < 3; i++) {
        if(count_tile_in_hand(h, dragons[i]) == 1) {
            int idx = find_index_of_first_tile_in_hand(h, dragons[i]);
            if(idx == -1) {
                exotique_printf("BUG IN TILE DISCARD PRIORITIZATION, FOUND WIND TILE BUT DOESNT EXIST?\n");
            }
            return idx;
        }
    }

    // search for useless terminals
    for(int i = 0; i < h->num_closed_tiles; i++) {
        tile_type ttile = h->tiles[i];
        if(ttile == ONE_PIN || ttile == ONE_MAN  || ttile == ONE_SOU) {
            if(count_tile_in_hand(h, ttile+1) == 0 && count_tile_in_hand(h, ttile+2) == 0) {
                return i;
            }
        }
        if(ttile == NINE_PIN || ttile == NINE_MAN || ttile == NINE_SOU) {
            if(count_tile_in_hand(h, ttile-1) == 0 && count_tile_in_hand(h, ttile-2) == 0) {
                return i;
            }
        }
    }

    if(h->num_closed_tiles <= 2) {
        return 0;
    }
    return h->num_closed_tiles-2;
}

void reset_ai_player_state(int idx, u32 cur_frame) {
    game_data.ai_player_next_move_frames[idx] = cur_frame + get_frames_for_duration(AI_PLAYER_SPEED);
}

void run_ai_player(i8 player, hand* h, u32 cur_frame, int this_player_turn) {
    // we know the hand is sorted
    player_type ai_type = game_data.player_types[player];
    // just drop the last tile before the one we just drew lol
    if(cur_frame >= game_data.ai_player_next_move_frames[player]) {

        game_data.ai_player_next_move_frames[player] = cur_frame + get_frames_for_duration(AI_PLAYER_SPEED);
        // aggressive AI is funny because it doesn't even prepare for it's discard
        // it always tries to call first :)
        // only it it's the AI's turn does it move it's selected tile around
        if(!this_player_turn && ai_type == AGGRESSIVE_AI) {
            if(game_data.last_discard_player != -1 && game_data.last_discard_player != player) {
                queue_push(make_player_action(player, ATTEMPT_CALL));
                return;
            }
        }


        int best_discard = get_best_discard(player, h);
        if(this_player_turn && h->selected_tile_idx == best_discard) {
            if(h->num_closed_tiles + h->num_open_tiles == 14) {
                if(cur_frame > (u32)game_data.draw_end_frame) {
                    queue_push(make_player_action(player, ATTEMPT_DISCARD));
                    return;
                }
            } else {
                queue_push(make_player_action(player, ATTEMPT_DRAW));   
                    return;
            }
        } else {
            int right = mod_positive(best_discard + h->num_closed_tiles - h->selected_tile_idx, h->num_closed_tiles);
            int left = mod_positive(h->selected_tile_idx + h->num_closed_tiles - best_discard, h->num_closed_tiles);
            if(left < right) {
                queue_push(make_player_action(player, MOVE_SELECTED_LEFT));
                return;
            } else if(right < left) {
                queue_push(make_player_action(player, MOVE_SELECTED_RIGHT));
                return;
            }
        }
    }
}

sound call_sounds[5] = {
    PON, 
    CHII,
    RIICHI,
    TSUMO,
    RON
};
#define NUM_CALL_SOUNDS (sizeof(call_sounds)/sizeof(call_sounds[0]))

typedef enum {
    PON_CALL = 0,
    CHII_CALL = 1,
    RIICHI_CALL = 2,
    TSUMO_CALL = 3,
    RON_CALL = 4,
    NO_CALL
} call_type;

void copy_tile_index_to_open(hand* h, int idx) {
    tile_type copy_tile = h->tiles[idx];
    h->open_tiles[h->num_open_tiles] = copy_tile;
    h->open_tile_rotated[h->num_open_tiles++] = 0;
    for(int i = idx; i < h->num_closed_tiles-1; i++) {
        h->tiles[i] = h->tiles[i+1];
    }
    h->num_closed_tiles--;
    //exotique_printf("copied tile idx %i to open tiles, now %i closed_tiles and %i open tiles\n", idx, h->num_closed_tiles, h->num_open_tiles);
}

call_type attempt_call(int player) {

    hand* prev_hand = &game_data.hands[game_data.last_discard_player];
    hand* cur_hand = &game_data.hands[player];
    tile_type prev_discard = prev_hand->discards[prev_hand->num_discards-1];

    // check for sequence
    int got_ll = 0, ll_index = -1;
    int got_l = 0, l_index = -1;
    int got_r = 0, r_index = -1;
    int got_rr = 0, rr_index = -1;
    int pon_index1 = -1, pon_index2 = -1;
    for(int i = 0; i < cur_hand->num_closed_tiles; i++) {
        tile_type til = cur_hand->tiles[i];
        if(tile_sort_val[til] == tile_sort_val[prev_discard]-1) {
            got_l = 1;
            l_index = i;
        } else if (tile_sort_val[til] == tile_sort_val[prev_discard]-2) {
            got_ll = 1;
            ll_index = i;
        } else if (tile_sort_val[til] == tile_sort_val[prev_discard]+1) {
            got_r = 1;
            r_index = i;
        } else if (tile_sort_val[til] == tile_sort_val[prev_discard]+2) {
            got_rr = 1;
            rr_index = i;
        } else if (tile_sort_val[til] == tile_sort_val[prev_discard]) {
            if(pon_index1 == -1) {
                pon_index1 = i;
            } else {
                pon_index2 = i;
            }
        }
    }
    int pon_cnt = (pon_index1 != 1 ? 1 : 0) + (pon_index2 != 1 ? 1 : 0);

    if(pon_cnt >= 3) {
        //exotique_printf("Can PON %s to complete\n", tile_names[prev_discard]);
        copy_tile_index_to_open(cur_hand, pon_index1);
        if(pon_index1 < pon_index2) { pon_index2--; }
        copy_tile_index_to_open(cur_hand, pon_index2);
        cur_hand->open_tiles[cur_hand->num_open_tiles] = prev_discard;
        cur_hand->open_tile_rotated[cur_hand->num_open_tiles++] = 1;
        prev_hand->num_discards--;
        return PON_CALL;
    }

    if(got_ll && got_l) {
        //exotique_printf("Can CHII %s to complete %s %s\n", 
        //    tile_names[prev_discard], 
        //    tile_names[cur_hand->tiles[ll_index]], tile_names[cur_hand->tiles[l_index]]);
        if(ll_index < l_index) { l_index--; }
        copy_tile_index_to_open(cur_hand, ll_index);
        copy_tile_index_to_open(cur_hand, l_index);
        cur_hand->open_tiles[cur_hand->num_open_tiles] = prev_discard;
        cur_hand->open_tile_rotated[cur_hand->num_open_tiles++] = 1;
        prev_hand->num_discards--;
        return CHII_CALL;
    }
    if(got_l && got_r) {
        //exotique_printf("Can CHII %s to complete %s %s\n", 
        //    tile_names[prev_discard], 
        //    tile_names[cur_hand->tiles[l_index]], tile_names[cur_hand->tiles[r_index]]);
        copy_tile_index_to_open(cur_hand, l_index);
        cur_hand->open_tiles[cur_hand->num_open_tiles] = prev_discard;
        cur_hand->open_tile_rotated[cur_hand->num_open_tiles++] = 1;
        prev_hand->num_discards--;
        if(ll_index < r_index) { r_index--; }
        copy_tile_index_to_open(cur_hand, r_index);
        return CHII_CALL;
    }
    if(got_r && got_rr) {
        //exotique_printf("Can CALL %s to complete %s %s\n", 
        //    tile_names[prev_discard], 
        //    tile_names[cur_hand->tiles[r_index]], tile_names[cur_hand->tiles[rr_index]]);
        cur_hand->open_tiles[cur_hand->num_open_tiles] = prev_discard;
        cur_hand->open_tile_rotated[cur_hand->num_open_tiles++] = 1;
        prev_hand->num_discards--;
        if(r_index < rr_index) { rr_index--; }
        copy_tile_index_to_open(cur_hand, r_index);
        copy_tile_index_to_open(cur_hand, rr_index);
        return CHII_CALL;
    }


    return NO_CALL;
}

// player 3 calling player 0 is like player 0 calling player 1, (0-3)%4 => 1
// player 2 calling player 1 is like player 0 calling player 3, (1-2)%4 => 3

// use mod_positive to wrap around
int get_index_to_move_called_tile_to(int caller, int callee) {
    return 3 - mod_positive(callee-caller,4);
}

void rotate_last_three_open_tiles(hand *h) {
    tile_type tmp_tile = h->open_tiles[h->num_open_tiles-3];
    u8 tmp_rot = h->open_tile_rotated[h->num_open_tiles-3];

    h->open_tiles[h->num_open_tiles-3] = h->open_tiles[h->num_open_tiles-2];
    h->open_tile_rotated[h->num_open_tiles-3] = h->open_tile_rotated[h->num_open_tiles-2];
  
    h->open_tiles[h->num_open_tiles-2] = h->open_tiles[h->num_open_tiles-1];
    h->open_tile_rotated[h->num_open_tiles-2] = h->open_tile_rotated[h->num_open_tiles-1];
  
    h->open_tiles[h->num_open_tiles-1] = tmp_tile;
    h->open_tile_rotated[h->num_open_tiles-1] = tmp_rot;
}

void sort_last_three_open_tiles_based_on_player_order(hand* h, int caller, int callee) {
    int dst_idx = get_index_to_move_called_tile_to(caller, callee) + h->num_open_tiles - 3;
    //int src_idx = 
    while(!h->open_tile_rotated[dst_idx]) {
        // rotate everything
        rotate_last_three_open_tiles(h);
    }
}

u32 is_unused(int idx, u32 unused_bmp) {
    return (unused_bmp & (u32)(1 << idx));
}

int find_unused_after(tile_type hand_tiles[14], tile_type find, int start_after_idx, u32 unused_bmp) {
    for(int idx = start_after_idx+1; idx < 14; idx++) {
        if (is_unused(idx, unused_bmp) && hand_tiles[idx] == find) {
            return idx;
        }
    }
    return -1;
}

int can_partition_into_melds(tile_type hand_tiles[14], u32 unused_bmp) {
    if(unused_bmp == 0) {
        return 1; // we've removed all the tiles
    }

    // find first unused tile, every tile needs to be in a meld, so we use this one
    // and search for either a triplet or a sequence
    int smallest_tile_idx = __builtin_ffs((int)unused_bmp)-1;
    
    tile_type til = hand_tiles[smallest_tile_idx];
    unused_bmp &= ~(1u << smallest_tile_idx); // clear unused bitmap

    int eq_idx = find_unused_after(hand_tiles, til, smallest_tile_idx, unused_bmp);
    if(eq_idx != -1) {
        u32 take_triplet_bmp = unused_bmp & ~(1u << eq_idx);
        int eq_2_idx = find_unused_after(hand_tiles, til, eq_idx, take_triplet_bmp);
        if(eq_2_idx != -1) {
            take_triplet_bmp &= ~(1u << eq_2_idx);
            if(can_partition_into_melds(hand_tiles, take_triplet_bmp)) {
                return 1;
            }
        }
    }

    int next_seq_idx = find_unused_after(hand_tiles, til+1, smallest_tile_idx, unused_bmp);
    if(next_seq_idx != -1) {
        u32 take_seq_bmp = unused_bmp & ~(1u << next_seq_idx);
        int next_next_seq_idx = find_unused_after(hand_tiles, til+2, next_seq_idx, take_seq_bmp);
        if(next_next_seq_idx != -1) {
            take_seq_bmp &= ~(1u << next_next_seq_idx);
            if(can_partition_into_melds(hand_tiles, take_seq_bmp)) {
                return 1;
            }
        }
    }
    return 0;
}

int is_winning_hand(tile_type hand_tiles[14]) {
    for(int i = 0; i < 14; i++) {
        u32 unused_bmp = 0x3FFF;
        unused_bmp &= (u32)~(1u << i); // use this tile
        for(int j = i+1; j < 14; j++) {
            if(hand_tiles[i] == hand_tiles[j]) {
                unused_bmp &= (u32)~(1u << j); // and this tile as a pair
                if(can_partition_into_melds(hand_tiles, unused_bmp)) { // are we one away from tempai?
                    return 1;
                }
            }
        }
    }
    return 0;
}

void copy_hand_tiles_to_tmp(hand* h, tile_type tmp[14]) {
    for(int t = 0; t < h->num_closed_tiles; t++) {
        tmp[t] = h->tiles[t];
    }
    for(int t = 0; t < h->num_open_tiles; t++) {
        tmp[h->num_closed_tiles+t] = h->open_tiles[t];
    }
}

int is_win(hand* h) {
    
    tile_type tmp[14];
    copy_hand_tiles_to_tmp(h, tmp);
    return is_winning_hand(tmp);

}


int in_tenpai_for_discard(hand* h) {

    tile_type tmp[14];
    copy_hand_tiles_to_tmp(h, tmp);

        // copy into a temporary hand
    tile_type og_tile = tmp[h->selected_tile_idx];

    for(tile_type new_tile = 0; new_tile < NUM_TILES; new_tile++) {
        tmp[h->selected_tile_idx] = new_tile;
        if(is_winning_hand(tmp)) {
            return 1;
        }
    }
    tmp[h->selected_tile_idx] = og_tile;
    return 0;
}

int in_tenpai(hand* h) {

    tile_type tmp[14];
    copy_hand_tiles_to_tmp(h, tmp);


    for(int discard = 0; discard < 14; discard++) {

        // copy into a temporary hand
        tile_type og_tile = tmp[discard];

        for(tile_type new_tile = 0; new_tile < NUM_TILES; new_tile++) {
            tmp[discard] = new_tile;
            if(is_winning_hand(tmp)) {
                return 1;
            }
        }
        tmp[discard] = og_tile;
    }
    return 0;
}

int attempt_riichi(int player) {

    hand* cur_hand = &game_data.hands[player];

    // if the player has opened their hand, cannot riichi
    if(cur_hand->num_open_tiles > 0) {
        return 0;
    }
    
    if(cur_hand->in_riichi) {
        return 0;
    }


    if(in_tenpai_for_discard(cur_hand)) {
        return 1;
    }
    return 0;
}

void fixup_selected_idx(hand *h) {
    if(h->selected_tile_idx < 0) {
        h->selected_tile_idx = (i8)(h->num_closed_tiles-1);
    }           
    if (h->selected_tile_idx >= h->num_closed_tiles) {
        h->selected_tile_idx = 0;
    }    
}

void run_game(ExotiqueInterface *ei, const u32 cur_frame) {

    int held_y = ei->input->y;
    int held_x = ei->input->x;
    int held_a = ei->input->a;
    int held_b = ei->input->b;
    int pushed_y = held_y && !last_y_pushed;
    int pushed_x = held_x && !last_x_pushed;
    int pushed_a = held_a && !last_a_pushed;
    int pushed_b = held_b && !last_b_pushed;

    int switching = (game_data.switch_player_timer != -1);
    if(switching) {
        game_data.switch_player_timer--;
        if(game_data.switch_player_timer == 0) {
            game_data.cur_player = (game_data.cur_player+1)&3;
            game_data.draw_end_frame = -1;
            game_data.switch_player_timer = -1;
        }
    }

    if(pushed_x) {
        // allow a call if either the 
        queue_push(make_player_action(human_player, ATTEMPT_CALL));        
    } else if(ei->input->left && !last_left_pushed) {
        queue_push(make_player_action(human_player, MOVE_SELECTED_LEFT));
    } else if (ei->input->right && !last_right_pushed) {
        queue_push(make_player_action(human_player, MOVE_SELECTED_RIGHT));
    } else if(pushed_a) {
        queue_push(make_player_action(human_player, ATTEMPT_DRAW));
    } else if(pushed_b) {
        queue_push(make_player_action(human_player, ATTEMPT_DISCARD));
    } else if (pushed_y) {
        queue_push(make_player_action(human_player, ATTEMPT_RIICHI_OR_WIN));
    }


    if(is_host) {
        // do not run AI if host
        for(i8 i = 0; i < 4; i++) {
            if(game_data.player_types[i] != HUMAN && game_data.player_types[i] != NETWORK_HUMAN) {
                run_ai_player(i, &game_data.hands[i], cur_frame, (game_data.cur_player == i));
            }
        }
        server_wait_for_actions_from_players();
        server_send_actions_to_players();
    } else {
        // wait for other player inputs from server
        client_send_input_to_server();
        client_wait_for_actions_from_server();
    }

    while(!queue_empty()) {
        player_action action = queue_pop();
        if(action.cmd != NO_ACTION) {
            //exotique_printf("got action %s from player %i\n", action_names[action.cmd], action.player_num);
        }
        i8 this_player = action.player_num;

        
        int draw_not_started = game_data.draw_end_frame == -1;
        int draw_started = !draw_not_started;
        int this_players_turn = game_data.cur_player == this_player;

        // if switching, we can do a call, but nothing else
        int can_draw = !switching && this_players_turn && draw_not_started;
        int can_discard = !switching && this_players_turn && cur_frame >= (u32)game_data.draw_end_frame && draw_started;


        hand *player_hand = &game_data.hands[action.player_num];

        switch(action.cmd) {
            case NO_ACTION:
                break;
            case MOVE_SELECTED_LEFT:
                player_hand->selected_tile_idx--;
                fixup_selected_idx(player_hand); 
                add_sound(
                    TILE_CLICK,
                    0.085f
                    //cam_pos
                );
                break;
            case MOVE_SELECTED_RIGHT:
                player_hand->selected_tile_idx++;
                fixup_selected_idx(player_hand);
        
                add_sound(
                    TILE_CLICK,
                    0.085f
                    //cam_pos
                );
                break;
            case ATTEMPT_DRAW:
                //exotique_printf("can draw %i, switching %i, this players turn %i, draw_not_started %i, draw_end_frame %i\n", can_draw, switching, this_players_turn, draw_not_started, game_data.draw_end_frame);
                if(can_draw) {
                    sort_hand(player_hand);
                    game_data.draw_end_frame = (i32)draw_next_tile(cur_frame, game_data.cur_player);
                }
                break;
            case ATTEMPT_DISCARD:
                if(can_discard) {
                    discard_current_tile(game_data.cur_player, cur_frame);
                    game_data.switch_player_timer = (int)get_frames_for_duration(SWITCH_PLAYER_DURATION);
                    for(int i = 0; i < 4; i++) {
                        if(game_data.player_types[i] != HUMAN) {
                            reset_ai_player_state(i, cur_frame);
                        }
                    }
                }
                break;
            case ATTEMPT_CALL: do {
                    // you can if
                    // the last discard WASNT YOU
                    // and either
                    // the current player ISNT YOU
                    // OR
                    // it IS YOU, but you haven't drawn yet

                    // but you can ONLY CALL A TILE IF ITS STILL THAT PERSON'S TURN.
                    // ONCE IT SWITCHES OVER TO THE NEXT PLAYER, ITS LOST
                    if(game_data.last_discard_player != -1 && game_data.last_discard_player != this_player && game_data.last_discard_player == game_data.cur_player && (game_data.cur_player != this_player || (game_data.draw_end_frame == -1))) {
                        call_type can_call = attempt_call(this_player);
                        if(can_call != NO_CALL) {
                            add_sound(
                                call_sounds[can_call],
                                0.125f
                                //location
                            );
                            sort_last_three_open_tiles_based_on_player_order(player_hand, this_player, game_data.last_discard_player);
                            fixup_selected_idx(&game_data.hands[this_player]);
                            //exotique_printf("switching to player %i\n", this_player);

                            game_data.cur_player = this_player;
                            game_data.switch_player_timer = -1;
                            // bail out so that the player switch code can run before we continue to process events here 
                            return;
                        }
                    }
                } while(0);
                break;
            case ATTEMPT_RIICHI_OR_WIN: 
                if(can_discard) {
                    if(is_win(player_hand)) {
                        add_sound(
                            TSUMO,
                            0.25f
                        );
                        reset_game();
                    } else if (attempt_riichi(this_player)) {
                        
                        player_hand->in_riichi = 1;
                        add_sound(RIICHI, 0.125f);
                        discard_current_tile(this_player, cur_frame);
                        game_data.switch_player_timer = (int)get_frames_for_duration(SWITCH_PLAYER_DURATION);
                        for(int i = 0; i < 4; i++) {
                            if(game_data.player_types[i] != HUMAN) {
                                reset_ai_player_state(i, cur_frame);
                            }
                        }
                    }
                } else {
                    // attempt ron
                }
                break;
            default:
                exotique_printf("Unhandled event type %i\n", action.cmd);
                break;
        }
    }
}

vert3f orbit_camera_position(float yaw, float pitch, float radius) {
    float cp = cosf(pitch);
    float sp = sinf(pitch);

    float cy = cosf(yaw);
    float sy = sinf(yaw);

    return (vert3f){sy * cp * radius, sp * radius, cy * cp * radius};
}

void look_at_yx(transform *cam, vert3f position, vert3f target) {
    float dx = target.x - position.x;
    float dy = target.y - position.y;
    float dz = target.z - position.z;

    float horizontal = my_sqrt(dx * dx + dz * dz);
    cam->rotation.y = fast_atan2(dx, dz);
    cam->rotation.x = -fast_atan2(dy, horizontal);
    cam->position = position;
}

void game_update(ExotiqueInterface* ei) {
    int cur_start_pushed = ei->input->start;
    int cur_select_pushed = ei->input->select;

    last_start_pushed = cur_start_pushed;

    f32 abs_cam_rot_y = wall_y_rots[human_player];

    camera_rot_x = CLAMP(camera_rot_x, 0.0f, 1.568f);
    f32 use_camera_rot_x = camera_rot_x;
    f32 lerped_cam_dist = camera_radius;

    static block whole_frame_block = ROOT_TIMED_BLOCK(whole_frame_block, "sim frame")

        vert3f cam_pos = orbit_camera_position(abs_cam_rot_y, use_camera_rot_x, lerped_cam_dist);
        look_at_yx(&cam_view_trans, cam_pos, (vert3f){0.0f,0.0f,0.0f});

        vert3f forward = {0, 0, -1};

        matrix rx = rotation_x_matrix(.95f);
        matrix ry = rotation_y_matrix(cam_view_trans.rotation.y);
        matrix rot = mat_mul_mat(&ry, &rx); // inverse of your view rotation

        light = normalize(mat_mul_vert3(&rot, &forward));
        
        //if(cur_select_pushed && !last_select_pushed) {
        //   reset_game(ei);
        //}
        switch(game_data.cur_game_state) {
            case STARTUP:
                break;
            case INITIAL_SHUFFLE_AND_SETUP:
                step_shuffle_and_setup(game_data.frame, &board_wall);
                break;
            case DEALING:
                step_deal(game_data.frame);
                break;
            case IN_GAME:
                run_game(ei, game_data.frame);
                break;
            default:
            case NUM_GAME_STATES:
                break;
        }
    END_TIMED_BLOCK(whole_frame_block)
    
    game_data.frame++;

    if((game_data.frame & 31) == 0) {
        //print_and_reset_root_block(&whole_frame_block);
    }
    
    last_select_pushed = cur_select_pushed;
    last_left_pushed = ei->input->left;
    last_right_pushed = ei->input->right;
    last_x_pushed = ei->input->x;
    last_y_pushed = ei->input->y;
    last_a_pushed = ei->input->a;
    last_b_pushed = ei->input->b;
}