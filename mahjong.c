#define WIN32_LEAN_AND_MEAN
#include "exotique.h"
#include "miniaudio.h"

#include "mahjong_common.h"

#ifdef _WIN32
    #include <winsock2.h>
#endif

u8 color_buffer0[RENDER_TILE_SIZE*RENDER_TILE_SIZE] __attribute__((aligned(64)));
u8 color_buffer1[RENDER_TILE_SIZE*RENDER_TILE_SIZE] __attribute__((aligned(64)));
u8 color_buffer2[RENDER_TILE_SIZE*RENDER_TILE_SIZE] __attribute__((aligned(64)));
u8 color_buffer3[RENDER_TILE_SIZE*RENDER_TILE_SIZE] __attribute__((aligned(64)));
u16 zbuf0[RENDER_TILE_SIZE*RENDER_TILE_SIZE] __attribute__((aligned(64)));
u16 zbuf1[RENDER_TILE_SIZE*RENDER_TILE_SIZE] __attribute__((aligned(64)));
u16 zbuf2[RENDER_TILE_SIZE*RENDER_TILE_SIZE] __attribute__((aligned(64)));
u16 zbuf3[RENDER_TILE_SIZE*RENDER_TILE_SIZE] __attribute__((aligned(64)));

u8 *color_buffers[4] = {color_buffer0, color_buffer1, color_buffer2, color_buffer3};
u16 *z_buffers[4] = {zbuf0, zbuf1, zbuf2, zbuf3};



//#define ENABLE_MUSIC

#include "mahjong_block_timing.h"

#include "mahjong_vec_types.h"

#include "mahjong_math.h"

#include "mahjong_matrix.h"


// RENDERING

#define MAX_GLOBAL_TRIS 1000000
typedef struct {
    vert2i proj_v0, proj_v1, proj_v2; // 24 bytes
    f32 inv_z0, inv_z1, inv_z2;       // 12 bytes
    vert2f uv0_over_z, uv1_over_z, uv2_over_z; // 24 bytes
    f32 b0, b1, b2;
    u8 tex, mip_level_or_color; u8 colorkey;
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

#include "asset_headers/mesh_board.h"
//#include "asset_headers/mesh_dragon.h"
#include "asset_headers/mesh_dragon_low_poly.h"
#include "asset_headers/mesh_mahjong_tile.h"
#include "asset_headers/mesh_quad.h"
#include "asset_headers/mesh_tenbou.h"
#include "asset_headers/mesh_wind_indicator.h"

#include "asset_headers/palette_mahjong.h"
#include "asset_headers/palette_background.h"



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



typedef enum {
    BLACK = 0,
    GREEN = 1,
    GOLD = 2,
    WHITE = 3,
    RED = 4,
    BLUE = 5
} base_colors;

typedef struct {
    u8 palette[4]; // up to 4 colors in per-image palette.  these are indexes of the global palette (2 are shared)
    u8 *compressed_packets;
} compressed_texture;

typedef enum {
    UNCOMPRESSED,
    COMPRESSED,
    BASE_COLOR_INDEXES // uses base color indexes that get mapped to palette indexes
} tex_compression_type;

typedef struct {
    compressed_texture* comp_tex_ptr;
    u8* texels[4];
    int width, height;
    tex_compression_type compressed; 
    u8 default_pal_idx;
} texture;

void rasterize_triangle_2x2_quad(
    u8 *restrict color_buffer,
    u16 *restrict zbuffer,
    transformed_tri *restrict tri_attributes,
    texture *restrict tex,
    i32 start_x, i32 end_x,
    i32 start_y, i32 end_y
) {
    int colorkey = tri_attributes->colorkey;
    // swap everything for first two vertexes (actual vertex positions and attributes)
    f32 iz0 = tri_attributes->inv_z1;
    f32 iz1 = tri_attributes->inv_z0;
    f32 iz2 = tri_attributes->inv_z2;

    vert2i v0 = tri_attributes->proj_v1;
    vert2i v1 = tri_attributes->proj_v0;
    vert2i v2 = tri_attributes->proj_v2;

    vert2f uv0_over_z = tri_attributes->uv1_over_z;
    vert2f uv1_over_z = tri_attributes->uv0_over_z;
    vert2f uv2_over_z = tri_attributes->uv2_over_z;


    f32 b0 = tri_attributes->b1;
    f32 b1 = tri_attributes->b0;
    f32 b2 = tri_attributes->b2;

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
    iz1 = (iz1-iz0)*recip_area;
    iz2 = (iz2-iz0)*recip_area;

    v1u_over_z = (v1u_over_z-v0u_over_z)*recip_area;
    v1v_over_z = (v1v_over_z-v0v_over_z)*recip_area;

    v2u_over_z = (v2u_over_z-v0u_over_z)*recip_area;
    v2v_over_z = (v2v_over_z-v0v_over_z)*recip_area;

    b1 = (b1 - b0) * recip_area;
    b2 = (b2 - b0) * recip_area;

    
    f32_vec iz0_vec = broadcast_f32_vec(iz0);
    f32_vec iz1_vec = broadcast_f32_vec(iz1);
    f32_vec iz2_vec = broadcast_f32_vec(iz2);
    f32_vec b0_vec = broadcast_f32_vec(b0);
    f32_vec b1_vec = broadcast_f32_vec(b1);
    f32_vec b2_vec = broadcast_f32_vec(b2);

    

    
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
        u32 *col_buf_ptr = __builtin_assume_aligned(&color_buffer[tile_idx], 4);
        u64 *zbuf_ptr = __builtin_assume_aligned(&zbuffer[tile_idx], 8);
        for (i32 x = 0; x < (maxx-minx); x += 2, cx01_vec -= dy01_shifted_vec, cx12_vec -= dy12_shifted_vec, cx20_vec -= dy20_shifted_vec, col_buf_ptr++, zbuf_ptr++) {

            i32_vec covered_vec = ~((cx01_vec|cx12_vec|cx20_vec)>>31);


            int coverage_mask = i32_vec_any(covered_vec);
            //if(coverage_mask != 0xF) {
            //} else 
            if(coverage_mask != 0x0) {
                // skip completely uncovered quads
                u64 zbuf_val_vec_u64 = *zbuf_ptr;
                f32_vec zbuf_val_vec = decode_u64_inv_z_vec(zbuf_val_vec_u64);
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
                    // if any of the bytes in lit_color_qw are zeros, they shouldn't be drawn either
                    if(colorkey) {
                        u32 zero_bits =
                            (lit_color_qw - 0x01010101u) &
                            ~lit_color_qw &
                            0x80808080u;

                        // Turn each 0x80 flag into 0xFF in that byte.
                        u32 zero_mask =
                            zero_bits |
                            (zero_bits >> 1) |
                            (zero_bits >> 2) |
                            (zero_bits >> 3) |
                            (zero_bits >> 4) |
                            (zero_bits >> 5) |
                            (zero_bits >> 6) |
                            (zero_bits >> 7);

                        zero_mask &= 0xFFFFFFFFu;

                        mask_bytes &= ~zero_mask;
                    }
                    u32 masked_color = (cbuf_val & (~mask_bytes)) | (lit_color_qw & mask_bytes);
                    *col_buf_ptr = masked_color;

                    f32_vec new_zbuf_vec = (f32_vec)((in_tri_and_unoccluded & (i32_vec)inv_z_vec) | ((~in_tri_and_unoccluded) & (i32_vec)zbuf_val_vec));

                    
                    *zbuf_ptr = encode_float_inv_z_vec(new_zbuf_vec);
                }
            }
        }
    }
}


void rasterize_triangle_2x2_quad_no_tmap_avx2(
    u8 *restrict color_buffer,
    u16 *restrict zbuffer,
    transformed_tri *restrict tri_attributes,
    i32 start_x, i32 end_x, i32 start_y, i32 end_y
) {
    u8 color = tri_attributes->mip_level_or_color;
    // swap everything for first two vertexes (actual vertex positions and attributes)
    f32 iz0 = tri_attributes->inv_z1;
    f32 iz1 = tri_attributes->inv_z0;
    f32 iz2 = tri_attributes->inv_z2;

    vert2i v0 = tri_attributes->proj_v1;
    vert2i v1 = tri_attributes->proj_v0;
    vert2i v2 = tri_attributes->proj_v2;

    f32 b0 = tri_attributes->b1;
    f32 b1 = tri_attributes->b0;
    f32 b2 = tri_attributes->b2;

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

    const __m128i dy01_shifted_vec = _mm_set1_epi32(dy01<<5);
    const __m128i dy12_shifted_vec = _mm_set1_epi32(dy12<<5);
    const __m128i dy20_shifted_vec = _mm_set1_epi32(dy20<<5);
    const __m128i dx01_shifted_vec = _mm_set1_epi32(dx01<<5);
    const __m128i dx12_shifted_vec = _mm_set1_epi32(dx12<<5);
    const __m128i dx20_shifted_vec = _mm_set1_epi32(dx20<<5); // shift by 4 for subpixel, but double again because we're using 2x2 quad blocks now 


    i32 area = (dx01 * dy20 - dy01 * dx20);
    // barycentric weights weights (scaled by area)
    f32 recip_area = 1.0f / (f32)area;

    iz1 = (iz1 - iz0) * recip_area;
    iz2 = (iz2 - iz0) * recip_area;
    b1 = (b1 - b0) * recip_area;
    b2 = (b2 - b0) * recip_area;

    
    __m128 iz0_vec = _mm_set1_ps(iz0);
    __m128 iz1_vec = _mm_set1_ps(iz1);
    __m128 iz2_vec = _mm_set1_ps(iz2);
    __m128 b0_vec = _mm_set1_ps(b0);
    __m128 b1_vec = _mm_set1_ps(b1);
    __m128 b2_vec = _mm_set1_ps(b2);

    //iz1_vec = _mm_mul_ps(_mm_sub_ps(iz1_vec, iz0_vec), recip_area);
    //iz2_vec = _mm_mul_ps(_mm_sub_ps(iz2_vec, iz0_vec), recip_area);

    //b1_vec = _mm_mul_ps(_mm_sub_ps(b1_vec, b0_vec), recip_area);
    //b2_vec = _mm_mul_ps(_mm_sub_ps(b2_vec, b0_vec), recip_area);

    
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

    __m128i cy01_vec = _mm_setr_epi32(
        c01 + dx01 * startY - dy01 * startX,
        c01 + dx01 * startY - dy01 * (startX+FIXED_ONE_PX),
        c01 + dx01 * (startY+FIXED_ONE_PX) - dy01 * startX,
        c01 + dx01 * (startY+FIXED_ONE_PX) - dy01 * (startX+FIXED_ONE_PX)
    );
    __m128i cy12_vec = _mm_setr_epi32(
        c12 + dx12 * startY - dy12 * startX,
        c12 + dx12 * startY - dy12 * (startX+FIXED_ONE_PX),
        c12 + dx12 * (startY+FIXED_ONE_PX) - dy12 * startX,
        c12 + dx12 * (startY+FIXED_ONE_PX) - dy12 * (startX+FIXED_ONE_PX)
    );
    __m128i cy20_vec = _mm_setr_epi32(
        c20 + dx20 * startY - dy20 * startX,
        c20 + dx20 * startY - dy20 * (startX+FIXED_ONE_PX),
        c20 + dx20 * (startY+FIXED_ONE_PX) - dy20 * startX,
        c20 + dx20 * (startY+FIXED_ONE_PX) - dy20 * (startX+FIXED_ONE_PX)
    );

    for (i32 y = miny; y < maxy; y += 2, cy01_vec = _mm_add_epi32(cy01_vec, dx01_shifted_vec), cy12_vec = _mm_add_epi32(cy12_vec, dx12_shifted_vec), cy20_vec = _mm_add_epi32(cy20_vec, dx20_shifted_vec)) {

        __m128i cx01_vec = cy01_vec;
        __m128i cx12_vec = cy12_vec;
        __m128i cx20_vec = cy20_vec;

        int in_tile_y = y-start_y;
        int in_tile_x = minx-start_x;
        int tile_idx = (in_tile_y&~1)*RENDER_TILE_SIZE + ((in_tile_x&~1)<<1);

        u32 *col_buf_ptr = __builtin_assume_aligned(&color_buffer[tile_idx], 4);
        u64 *zbuf_ptr = __builtin_assume_aligned(&zbuffer[tile_idx], 8);


        for (i32 x = minx; x < maxx; x += 2, cx01_vec = _mm_sub_epi32(cx01_vec, dy01_shifted_vec), cx12_vec = _mm_sub_epi32(cx12_vec, dy12_shifted_vec), cx20_vec = _mm_sub_epi32(cx20_vec, dy20_shifted_vec), col_buf_ptr++, zbuf_ptr++) {
            
            //__m128i covered_vec = _mm_cmplt_epi32(_mm_set1_epi32(0), _mm_or_si128(cx01_vec, _mm_or_si128(cx12_vec, cx20_vec)));
            __m128i or_vec = _mm_or_si128(cx01_vec, _mm_or_si128(cx12_vec, cx20_vec));
            __m128i covered_vec = _mm_andnot_si128(_mm_srai_epi32(or_vec, 31), _mm_set1_epi32(-1));

            int all_pixels_uncovered = _mm_testz_si128(covered_vec, covered_vec);

            // skip completely uncovered quads
            if(all_pixels_uncovered) {
                continue;
            }

            u32 cbuf_val = *col_buf_ptr;
                        
            __m128 zbuf_val_vec = decode_u64_inv_z_vec_avx2(*zbuf_ptr);

            __m128 w1_vec = _mm_cvtepi32_ps(cx20_vec); 
            __m128 w2_vec = _mm_cvtepi32_ps(cx01_vec);

            __m128 inv_z_vec = _mm_add_ps(
                iz0_vec, 
                _mm_add_ps(
                    _mm_mul_ps(w1_vec, iz1_vec),
                    _mm_mul_ps(w2_vec, iz2_vec)
                )
            );


            __m128 brightness_vec = _mm_add_ps(b0_vec, _mm_add_ps(_mm_mul_ps(w1_vec, b1_vec), _mm_mul_ps(w2_vec, b2_vec)));
            brightness_vec = _mm_max_ps(_mm_set1_ps(0), brightness_vec);
            brightness_vec = _mm_min_ps(_mm_set1_ps((f32)(NUM_SHADES-1)), brightness_vec);

            __m128i unoccluded = (__m128i)_mm_cmpge_ps(inv_z_vec, zbuf_val_vec);

            __m128i in_tri_and_unoccluded = _mm_and_si128(unoccluded, covered_vec);


            __m128i low_bytes_mask = _mm_setr_epi8(0, 4, 8, 12, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1);
            u32 mask_bytes = (u32)_mm_cvtsi128_si32(_mm_shuffle_epi8(in_tri_and_unoccluded, low_bytes_mask));
             
            // wow! its a tiny bit faster without this!
            //int no_pixels_unoccluded_and_covered = _mm_testz_si128(in_tri_and_unoccluded, in_tri_and_unoccluded);
            //if(no_pixels_unoccluded_and_covered) {
            //    continue;
            // }

            // 256 bytes per each of the 25 top level indexes.
            // so essentially we need to multiply those brightness by 256.

            // man if this palette is ordered, or we had 32-bit color this could be way better, oh well
            
            __m128i lit_pal_indexes = _mm_slli_epi32(_mm_cvtps_epi32(brightness_vec), 8);


            __m128i color_indexes = _mm_add_epi32(lit_pal_indexes, _mm_set1_epi32(color));
            __m128i colors = _mm_i32gather_epi32((int*)full_light_remap_table, color_indexes, 1); // we're loading a full 32-bit value per color, but that's okay

            __m128i shuffled = _mm_shuffle_epi8(colors, low_bytes_mask);
            u32 lit_color_qw = (u32)_mm_cvtsi128_si32(shuffled);


            u32 masked_color = (cbuf_val & (~mask_bytes)) | (lit_color_qw & mask_bytes);
            
            *col_buf_ptr = masked_color;

            __m128 new_zbuf_vec = (__m128)_mm_or_si128(_mm_and_si128(in_tri_and_unoccluded, (__m128i)inv_z_vec), _mm_andnot_si128(in_tri_and_unoccluded, (__m128i)zbuf_val_vec));

            u64 new_zbuf_val =  encode_float_inv_z_vec_avx2(new_zbuf_vec);
            *zbuf_ptr = new_zbuf_val;
        }
    }
}


void rasterize_triangle_2x2_quad_no_tmap(
    u8 *restrict color_buffer,
    u16 *restrict zbuffer,
    transformed_tri *restrict tri_attributes,
    i32 start_x, i32 end_x, i32 start_y, i32 end_y
) {
    u8 color = tri_attributes->mip_level_or_color;
    // swap everything for first two vertexes (actual vertex positions and attributes)
    f32 iz0 = tri_attributes->inv_z1;
    f32 iz1 = tri_attributes->inv_z0;
    f32 iz2 = tri_attributes->inv_z2;

    vert2i v0 = tri_attributes->proj_v1;
    vert2i v1 = tri_attributes->proj_v0;
    vert2i v2 = tri_attributes->proj_v2;

    f32 b0 = tri_attributes->b1;
    f32 b1 = tri_attributes->b0;
    f32 b2 = tri_attributes->b2;


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
    iz1 = (iz1 - iz0) * recip_area;
    iz2 = (iz2 - iz0) * recip_area;

    b1 = (b1 - b0) * recip_area;
    b2 = (b2 - b0) * recip_area;
    //b1_vec = (b1_vec - b0_vec) * recip_area;
    //b2_vec = (b2_vec - b0_vec) * recip_area;

    
    f32_vec iz0_vec = broadcast_f32_vec(iz0);
    f32_vec iz1_vec = broadcast_f32_vec(iz1);
    f32_vec iz2_vec = broadcast_f32_vec(iz2);
    f32_vec b0_vec = broadcast_f32_vec(b0);
    f32_vec b1_vec = broadcast_f32_vec(b1);
    f32_vec b2_vec = broadcast_f32_vec(b2);

    
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

        u32 *col_buf_ptr = __builtin_assume_aligned(&color_buffer[tile_idx], 4);
        u64 *zbuf_ptr = __builtin_assume_aligned(&zbuffer[tile_idx], 8);


        for (i32 x = minx; x < maxx; x += 2, cx01_vec -= dy01_shifted_vec, cx12_vec -= dy12_shifted_vec, cx20_vec -= dy20_shifted_vec, col_buf_ptr++, zbuf_ptr++) {
            i32_vec covered_vec = ~((cx01_vec|cx12_vec|cx20_vec)>>31);

            int coverage_mask = i32_vec_any(covered_vec);

            // skip completely uncovered quads
            if(coverage_mask == 0x00) {
                continue;
            }
            u32 cbuf_val = *col_buf_ptr;
            u64 zbuf_val_vec_u64 = *zbuf_ptr;
            f32_vec zbuf_val_vec = decode_u64_inv_z_vec(zbuf_val_vec_u64);

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
    LIT_TEXTURED_COLORKEY,
    //LIT_UNTEXTURED
} shader;

typedef struct {
    const obj_mesh* mesh; 
    bbox* bounds;
    u8 texture;
    matrix model_to_view;
    // currently we use view space light direction (simply pointing in the camera direction :) ), so we don't need this.  previously we used it to transform normals into world space
    // then I tried inversing it to transform world-space light into object space, but that also required this matrix, and a bunch of math and stores to pass it down to the transform code
    // but now, we can simply multiply view space light dir by an inverse(model_to_view_matrix) to get object space lighting.
    //matrix model_to_world;  
    shader shdr;
    int force_mip0;
} mesh_draw_call;

const f32 camx = (f32)(RENDER_WIDTH/2.0f);
const f32 camy = (f32)(RENDER_HEIGHT/2.0f);

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
    // HI-Z culling
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


#include "asset_headers/texture_one_man.h"
#include "asset_headers/texture_two_man.h"
#include "asset_headers/texture_three_man.h"
#include "asset_headers/texture_four_man.h"
#include "asset_headers/texture_five_man.h"
#include "asset_headers/texture_five_man_red.h"
#include "asset_headers/texture_six_man.h"
#include "asset_headers/texture_seven_man.h"
#include "asset_headers/texture_eight_man.h"
#include "asset_headers/texture_nine_man.h"
#include "asset_headers/texture_one_pin.h"
#include "asset_headers/texture_two_pin.h"
#include "asset_headers/texture_three_pin.h"
#include "asset_headers/texture_four_pin.h"
#include "asset_headers/texture_five_pin.h"
#include "asset_headers/texture_five_pin_red.h"
#include "asset_headers/texture_six_pin.h"
#include "asset_headers/texture_seven_pin.h"
#include "asset_headers/texture_eight_pin.h"
#include "asset_headers/texture_nine_pin.h"
#include "asset_headers/texture_one_sou.h"
#include "asset_headers/texture_two_sou.h"
#include "asset_headers/texture_three_sou.h"
#include "asset_headers/texture_four_sou.h"
#include "asset_headers/texture_five_sou.h"
#include "asset_headers/texture_five_sou_red.h"
#include "asset_headers/texture_six_sou.h"
#include "asset_headers/texture_seven_sou.h"
#include "asset_headers/texture_eight_sou.h"
#include "asset_headers/texture_nine_sou.h"
#include "asset_headers/texture_north.h"
#include "asset_headers/texture_east.h"
#include "asset_headers/texture_south.h"
#include "asset_headers/texture_west.h"
#include "asset_headers/texture_green_dragon.h"
#include "asset_headers/texture_red_dragon.h"
#include "asset_headers/texture_white_dragon.h"
#include "asset_headers/texture_red_tenbou.h"
#include "asset_headers/texture_blue_tenbou.h"
#include "asset_headers/texture_gold_tenbou.h"
#include "asset_headers/texture_white_tenbou.h"
#include "asset_headers/texture_board.h"
#include "asset_headers/texture_wind_indicator.h"

#define PIN_VAL 100
#define SOU_VAL 200
#define MAN_VAL 300
#define HONOR_BASE_VAL 400
#define HONOR 1
#define NOT_HONOR 0
#define TERMINAL 1
#define NOT_TERMINAL 0
#define TILE_LIST \
    X(ONE_PIN, one_pin, PIN_VAL+1, TERMINAL, NOT_HONOR)  \
    X(TWO_PIN, two_pin, PIN_VAL+2, NOT_TERMINAL, NOT_HONOR)  \
    X(THREE_PIN, three_pin, PIN_VAL+3, NOT_TERMINAL, NOT_HONOR)  \
    X(FOUR_PIN, four_pin, PIN_VAL+4, NOT_TERMINAL, NOT_HONOR)  \
    X(FIVE_PIN, five_pin, PIN_VAL+5, NOT_TERMINAL, NOT_HONOR)  \
    X(FIVE_PIN_RED, five_pin_red, PIN_VAL+5, NOT_TERMINAL, NOT_HONOR)  \
    X(SIX_PIN, six_pin, PIN_VAL+6, NOT_TERMINAL, NOT_HONOR)  \
    X(SEVEN_PIN, seven_pin, PIN_VAL+7, NOT_TERMINAL, NOT_HONOR)  \
    X(EIGHT_PIN, eight_pin, PIN_VAL+8, NOT_TERMINAL, NOT_HONOR)  \
    X(NINE_PIN, nine_pin, PIN_VAL+9, TERMINAL, NOT_HONOR)  \
    X(ONE_MAN, one_man, MAN_VAL+1, NOT_TERMINAL, NOT_HONOR)  \
    X(TWO_MAN, two_man, MAN_VAL+2, NOT_TERMINAL, NOT_HONOR)  \
    X(THREE_MAN, three_man, MAN_VAL+3, NOT_TERMINAL, NOT_HONOR)  \
    X(FOUR_MAN, four_man, MAN_VAL+4, NOT_TERMINAL, NOT_HONOR)  \
    X(FIVE_MAN, five_man, MAN_VAL+5, NOT_TERMINAL, NOT_HONOR)  \
    X(FIVE_MAN_RED, five_man_red, MAN_VAL+5, NOT_TERMINAL, NOT_HONOR)  \
    X(SIX_MAN, six_man, MAN_VAL+6, NOT_TERMINAL, NOT_HONOR)  \
    X(SEVEN_MAN, seven_man, MAN_VAL+7, NOT_TERMINAL, NOT_HONOR)  \
    X(EIGHT_MAN, eight_man, MAN_VAL+8, NOT_TERMINAL, NOT_HONOR)  \
    X(NINE_MAN, nine_man, MAN_VAL+9, TERMINAL, NOT_HONOR)  \
    X(ONE_SOU, one_sou, SOU_VAL+1, TERMINAL, NOT_HONOR)  \
    X(TWO_SOU, two_sou, SOU_VAL+2, NOT_TERMINAL, NOT_HONOR)  \
    X(THREE_SOU, three_sou, SOU_VAL+3, NOT_TERMINAL, NOT_HONOR)  \
    X(FOUR_SOU, four_sou, SOU_VAL+4, NOT_TERMINAL, NOT_HONOR)  \
    X(FIVE_SOU, five_sou, SOU_VAL+5, NOT_TERMINAL, NOT_HONOR)  \
    X(FIVE_SOU_RED, five_sou_red, SOU_VAL+5, NOT_TERMINAL, NOT_HONOR)  \
    X(SIX_SOU, six_sou, SOU_VAL+6, NOT_TERMINAL, NOT_HONOR)  \
    X(SEVEN_SOU, seven_sou, SOU_VAL+7, NOT_TERMINAL, NOT_HONOR)  \
    X(EIGHT_SOU, eight_sou, SOU_VAL+8, NOT_TERMINAL, NOT_HONOR)  \
    X(NINE_SOU, nine_sou, SOU_VAL+9, TERMINAL, NOT_HONOR)  \
    X(NORTH, north, HONOR_BASE_VAL, NOT_TERMINAL, HONOR)  \
    X(EAST, east, HONOR_BASE_VAL+100, NOT_TERMINAL, HONOR)  \
    X(SOUTH, south, HONOR_BASE_VAL+200, NOT_TERMINAL, HONOR)  \
    X(WEST, west, HONOR_BASE_VAL+300, NOT_TERMINAL, HONOR)  \
    X(WHITE_DRAGON, white_dragon, HONOR_BASE_VAL+400, NOT_TERMINAL, HONOR)  \
    X(RED_DRAGON, red_dragon, HONOR_BASE_VAL+500, NOT_TERMINAL, HONOR)  \
    X(GREEN_DRAGON, green_dragon, HONOR_BASE_VAL+600, NOT_TERMINAL, HONOR)

typedef enum __attribute__((packed)) {
#define X(tile_name, lowercase_name, sort_val, is_terminal, is_honor) tile_name,
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
    SCORE_TEXTURE,
    NUM_ALL_TEXTURE_TYPES
} tile_type;

const char* tile_names[NUM_TILES] = {
#define X(name, lowercase_name, val, term, honor) #name,
    TILE_LIST
#undef X
};


int is_honor_tile[NUM_TILES] = {
#define X(tile_name, lowercase_name, sort_val, is_terminal, is_honor) is_honor,
    TILE_LIST
#undef X
};

int is_terminal_tile[NUM_TILES] = {
#define X(tile_name, lowercase_name, sort_val, is_terminal, is_honor) is_terminal,
    TILE_LIST
#undef X
};

int tile_sort_val[NUM_TILES] = {
#define X(tile_name, lowercase_name, sort_val, is_terminal, is_honor) sort_val,
    TILE_LIST
#undef X
};

u8 texture_buffer[NUM_ALL_TEXTURE_TYPES][256*256] __attribute__((aligned(64)));
u8 texture_mip_buffer[NUM_ALL_TEXTURE_TYPES][128*128] __attribute__((aligned(64)));
u8 texture_mip_2_buffer[NUM_ALL_TEXTURE_TYPES][64*64] __attribute__((aligned(64)));
u8 texture_mip_3_buffer[NUM_ALL_TEXTURE_TYPES][32*32] __attribute__((aligned(64)));
#define NULL_PTR 0


texture textures[NUM_ALL_TEXTURE_TYPES+1] = {
#define X(tile_name, lowercase_name, sort_val, is_terminal, is_honor) { &comp_tex_ ## lowercase_name, {NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 256, 256, COMPRESSED, WHITE },
    TILE_LIST
#undef X
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
    { // score_texture
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

// maps 2 palette indexes to a palette index referencing the closest color to a 50:50 mix
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


//  DRAW CALLS, TILE FILLS, VERTEX SHADERS

int got_board_min_max_coords = 0;
f32 board_min_y, board_max_y;
f32 board_top_min_x, board_top_max_x;
f32 board_bot_min_x, board_bot_max_x;
//#define AVX2

void rasterize_tile(ExotiqueInterface *ei, u8 *color_buffer, u16 *zbuffer, tile* t) {
    u32 i;

    int base_x = t->start_x;
    int base_y = t->start_y;

    f32 left_dx_per_dy = (board_bot_min_x - board_top_min_x) / (board_max_y-board_min_y);
    f32 right_dx_per_dy = (board_bot_max_x - board_top_max_x) / (board_max_y-board_min_y);


    /* clear zbuffer for this tile, fill with background, OR game board */
    u32 *col_val_ptr = __builtin_assume_aligned(&color_buffer[0], 4);
    u64 *zbuf_ptr = __builtin_assume_aligned(&zbuffer[0], 8);

    f32_vec inv_far_vec_flt = broadcast_f32_vec(1.0f/FAR_Z);
    u64 inv_far_vec = encode_float_inv_z_vec(inv_far_vec_flt);
    u32 board_col_idx = light_remap_table[7][GREEN];
    board_col_idx |= (board_col_idx<<8);
    board_col_idx |= (board_col_idx<<16);

    for(int y = 0; y < RENDER_TILE_SIZE; y += 2) {
        int global_y = (base_y + y);
        //f32 y_portion = (f32)global_y / (f32)RENDER_HEIGHT;
        //int tex_y_coord = (int)(y_portion * (f32)BACKGROUND_TEX_HEIGHT);

        if(0) { //global_y >= board_min_y && global_y < board_max_y-2) {
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
    #ifdef AVX2
        rasterize_triangle_2x2_quad_no_tmap_avx2
    #else
        rasterize_triangle_2x2_quad_no_tmap
    #endif
        (
            color_buffer, zbuffer,
            &global_tri_buffer[global_tri_idx],
            t->start_x, t->start_x+RENDER_TILE_SIZE,
            t->start_y, t->start_y+RENDER_TILE_SIZE
        );

    }

    for(i = 0; i < num_tris; i++) {
        u32 global_tri_idx = t->tex_tri_indexes[i];
        rasterize_triangle_2x2_quad(
            color_buffer, zbuffer,
            &global_tri_buffer[global_tri_idx],
            &textures[global_tri_buffer[global_tri_idx].tex],
            t->start_x, t->start_x+RENDER_TILE_SIZE,
            t->start_y, t->start_y+RENDER_TILE_SIZE
        );
    }

    /* flush tile to output */
    col_val_ptr = __builtin_assume_aligned(&color_buffer[0], 4);
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

        if(0) { //global_y >= board_min_y && global_y < board_max_y-2) {
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

#include <windows.h>
#include <process.h>

typedef struct {
    u8* color_buffer;
    u16* z_buffer;
    ExotiqueInterface *ei;
    u8 draw_tile_bmp;
    HANDLE start_event; // Signal from Main to Worker: "Wake up and work!"
    HANDLE done_event;  // Signal from Worker to Main: "I am finished!"
    bool shutdown;      // Signal to cleanly close thread at app exit
} RasterThreadCtx;


void rasterize_tiles(ExotiqueInterface *ei, u8 *color_buffer, u16 *z_buffer, int draw_tile_bmp) {

    for(int y = 0; y < TILES_HIGH; y++) {
        for(int x = 0; x < TILES_WIDE; x++) {
            u8 bmp = (y&0b10)|((x&0b10)>>1);
            if(bmp != draw_tile_bmp) { continue; }

            //if(((x^y)&1) == draw_even_tiles) { continue; }
            
            tile* t = &tiles[y*TILES_WIDE+x];
            
            if(t->num_tex_triangles || t->num_solid_triangles) {
                rasterize_tile(ei, color_buffer, z_buffer, &tiles[y*TILES_WIDE+x]);
            } else {
                fill_background_for_tile(ei, &tiles[y*TILES_WIDE+x]);
            }
        }
    }
}
#define NUM_RASTER_THREADS 4
HANDLE raster_thread_handles[NUM_RASTER_THREADS];
RasterThreadCtx raster_contexts[NUM_RASTER_THREADS];

void raster_worker(void *arg) {
    RasterThreadCtx *ctx = (RasterThreadCtx*)arg;
    while(1) {
        WaitForSingleObject(ctx->start_event, INFINITE);
        if(ctx->shutdown) {
            break;
        }

        // otherwise, 
        rasterize_tiles(ctx->ei, ctx->color_buffer, ctx->z_buffer, ctx->draw_tile_bmp);

        SetEvent(ctx->done_event);
    }
    _endthread();
}

void rasterize_tiles_parallel() {

    for(int i = 0; i < NUM_RASTER_THREADS; i++) {
        SetEvent(raster_contexts[i].start_event);
    }
    

    HANDLE completion_events[NUM_RASTER_THREADS];
    for(int i = 0; i < NUM_RASTER_THREADS; i++) {
        completion_events[i] = raster_contexts[i].done_event;
    }
    WaitForMultipleObjects(NUM_RASTER_THREADS, completion_events, TRUE, INFINITE);

}



int triangles_rasterized;
void bin_triangle(
    vert3f *v0, vert3f *v1, vert3f *v2,
    vert2f *v0_uv, vert2f *v1_uv, vert2f *v2_uv,
    f32 b0, f32 b1, f32 b2,
    u8 texture_id, shader cur_shader, int force_mip0) {

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
            if(force_mip0) {
                mip_level = 0;
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
            } else {
                vert2f uv0_over_z = {uv0.x * inv_z0, uv0.y * inv_z0};
                vert2f uv1_over_z = {uv1.x * inv_z1, uv1.y * inv_z1};
                vert2f uv2_over_z = {uv2.x * inv_z2, uv2.y * inv_z2};
                global_tri_buffer[total_triangles].uv0_over_z = uv0_over_z;
                global_tri_buffer[total_triangles].uv1_over_z = uv1_over_z;
                global_tri_buffer[total_triangles].uv2_over_z = uv2_over_z;
                global_tri_buffer[total_triangles].tex = texture_id;
                global_tri_buffer[total_triangles].mip_level_or_color = mip_level;
            }
            
            global_tri_buffer[total_triangles].inv_z0 = inv_z0;
            global_tri_buffer[total_triangles].inv_z1 = inv_z1;
            global_tri_buffer[total_triangles].inv_z2 = inv_z2;
            global_tri_buffer[total_triangles++].colorkey = (cur_shader == LIT_TEXTURED_COLORKEY);
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

void vertex_shader(const int cache_tag_idx, const obj_vertex *vertex_stream, const matrix *model_to_view, const vert3f object_space_light_dir, const shader cur_shader) {

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
        f32 dot_light = dot(*n0, object_space_light_dir);
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
    const matrix* model_to_view,
    const vert3f obj_space_light_dir,
    const shader cur_shader) {

    f32_vec rot_vert_comps[3];

    mat_mul_vert3_batch(model_to_view, vert_poses_soa, rot_vert_comps); // transform all four vertexes at once
    
    vert3f s0[4];
    parallel_project_coord(rot_vert_comps, s0);
    
    vertexes_transformed += 4;


    f32_vec hemi = vert_norms_soa[1] * 0.5f + 0.5f;
    f32_vec ambient = lerp_f32_vec(broadcast_f32_vec(0.20f), broadcast_f32_vec(0.40f), hemi);

    f32_vec l0;

    if(cur_shader == UNLIT_TEXTURED) {
        l0 = ambient;
    } else {
        l0 = dot_batch_single(vert_norms_soa, obj_space_light_dir) + ambient;
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

void parallel_vertex_shader(const int num_verts, const obj_vertex* vertex_stream, const matrix* model_to_view, const vert3f obj_space_light_dir, const shader cur_shader) {
    
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
        process_vertex_batch(batch_tag_idx, poses_soa, vert_uvs, norms_soa, model_to_view, obj_space_light_dir, cur_shader);

    }
    
    for(int i = num_bunches*4; i < num_verts; i++) {
        vertex_shader(
            i, vertex_stream, model_to_view, obj_space_light_dir, cur_shader
        );
    }
}

static inline int triangle_backfacing(vert3f *v0, vert3f *v1, vert3f *v2) {

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
    //matrix *model_to_world = &mdc->model_to_world;
    //matrix world_to_model = mat_inverse_affine(model_to_world);
    //vert3f obj_space_light_dir = mat_mul_normal(&world_to_model, &light);

    matrix view_to_model = mat_inverse_affine(model_to_view);
    vert3f obj_space_light_dir = mat_mul_normal(&view_to_model, &light);
    

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
            model_to_view, obj_space_light_dir,
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
                texture_id, cur_shader, mdc->force_mip0
            );
        }
    }
}

typedef enum {
    NO_FRUSTUM_CULL,
    FRUSTUM_CULL
} culling_mode;

void submit_draw_calls(mesh_draw_call *list, int num_meshes, culling_mode frustum_cull_mode) {
    //int meshes_clipped = 0;
    (void)frustum_cull_mode;
    for(int i = 0; i < num_meshes; i++) {
        if(frustum_cull_mode == FRUSTUM_CULL) {
            clip_res clipped = clip_bounding_box(&list[i]);
            if(clipped == FAR_CLIPPED || clipped == NEAR_CLIPPED || clipped == OFF_SCREEN) {
                //meshes_clipped++;
                continue;
            }
        }
        submit_mesh_draw_call(&list[i]);
    }
}


//    SOUND EFFECTS

#include "asset_headers/pon_4b.h"
#include "asset_headers/chii_4b.h"
#include "asset_headers/tsumo.h"
#include "asset_headers/riichi_4b.h"
#include "asset_headers/tile_click_4b.h"
#include "asset_headers/tsumo_4b.h"
#include "asset_headers/ron_4b.h"

typedef enum {
    TILE_CLICK,
    PON,
    CHII,
    RIICHI_SND,
    TSUMO,
    RON,
#ifdef ENABLE_MUSIC
    MUSIC,
#endif
    NUM_SOUNDS
} sound;

i16 decompressed_sound_buffer[NUM_SOUNDS][32768];

#ifdef ENABLE_MUSIC
#include "asset_headers/music_mp3.h"
i16 decompressed_music_buffer[5260800*2];
#endif

typedef enum {
    IMA_ADPCM, // used for sfx
    MP3 // used for music
} sound_compression_type;

typedef struct {
    void *compressed_raw_data;
    i16* decompressed_data;
    u32 num_bytes, num_mono_samples;
    sound_compression_type comp_type;
} sound_data;


sound_data sounds[NUM_SOUNDS] = {
    {tile_click_4b_raw_data, decompressed_sound_buffer[0], TILE_CLICK_NUM_BYTES, 0, IMA_ADPCM},
    {pon_4b_raw_data, decompressed_sound_buffer[1], PON_NUM_BYTES, 0, IMA_ADPCM},
    {chii_4b_raw_data, decompressed_sound_buffer[2], CHII_NUM_BYTES, 0, IMA_ADPCM},
    {riichi_4b_raw_data, decompressed_sound_buffer[3], RIICHI_NUM_BYTES, 0, IMA_ADPCM},
    {tsumo_4b_raw_data, decompressed_sound_buffer[4], TSUMO_NUM_BYTES, 0, IMA_ADPCM},
    {ron_4b_raw_data, decompressed_sound_buffer[5], RON_NUM_BYTES, 0, IMA_ADPCM},
#ifdef ENABLE_MUSIC
    {music_mp3_raw_dat, decompressed_music_buffer, MUSIC_NUM_BYTES, 0, MP3}
#endif
};

u32 decompress_MP3(u8* raw_data, i16* output, u32 num_bytes) {
#ifdef ENABLE_MUSIC
    ma_decoder decoder;
    ma_decoder_config decoder_config = ma_decoder_config_init(ma_format_s16, 1, 22050);
    ma_result decode_result = ma_decoder_init_memory(raw_data, num_bytes, &decoder_config, &decoder);
    if(decode_result != MA_SUCCESS) {
        exotique_printf("error decoding MP3 audio\n");
        exit(1);
    }
    u64 num_frames;
    ma_result get_frames_result = ma_decoder_get_available_frames(&decoder, &num_frames);
    if(get_frames_result != MA_SUCCESS) {
        exotique_printf("error getting MP3 num frames\n");
        exit(1);
    }
    u64 frames_read;
    ma_result read_frames_result = ma_decoder_read_pcm_frames(&decoder, output, num_frames, &frames_read);
    if(read_frames_result != MA_SUCCESS || frames_read != num_frames) {
        exotique_printf("error reading MP3 data\n");
        exit(1);
    }

    return (u32)frames_read;
#else 
    (void)raw_data; (void)output; (void)num_bytes;
    return 0;
#endif
}

u32 decompress_adpcm(u8* raw, i16 *output, u32 num_bytes) {

    static const int index_table[16] = {
        -1,-1,-1,-1, 2, 4, 6, 8, -1,-1,-1,-1, 2, 4, 6, 8
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
        switch(sounds[i].comp_type) {
            case IMA_ADPCM:
                sounds[i].num_mono_samples = decompress_adpcm(sounds[i].compressed_raw_data, sounds[i].decompressed_data, sounds[i].num_bytes);
                break;
            case MP3:                
                sounds[i].num_mono_samples = decompress_MP3(sounds[i].compressed_raw_data, sounds[i].decompressed_data, sounds[i].num_bytes);
                break;
        }
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

#ifdef ENABLE_MUSIC
int enable_music = 1;
#else
int enable_music = 0;
#endif
static void drain_pending_sounds(void) {
    while(read_pos != write_pos) {
        if(num_active_sounds < MAX_SOUNDS) {
            active_sounds[num_active_sounds++] = pending_sounds[read_pos];
        }
        read_pos = (read_pos + 1) & (MAX_SOUNDS - 1);
    }
#ifdef ENABLE_MUSIC
    if(enable_music && num_active_sounds == 0) {
        active_sounds[num_active_sounds++] = (active_sound){MUSIC, 0.05f, 0, (vert3f){0.0f,0.0f,0.0f}};
    }
#endif
}

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    (void)pInput;
    (void)pDevice;

    drain_pending_sounds(); 
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
                    
            buffer[bufferIdx]   += (f32)data[playback_offset]   * sound_vol * 0.5f;
            buffer[bufferIdx+1] += (f32)data[playback_offset++] * sound_vol * 0.5f;
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

#define MAX_DISCARDS 18
#define MAX_CLOSED_TILES 14
#define MAX_OPEN_TILES 16
#define TILES_IN_DECK 136
#define TILE_SPAWN_POS_Y 20.0f

#define TILE_FALL_DURATION 0.2f
#define TILE_DEAL_DURATION 0.25f
#define DISCARD_DURATION  0.34f
#define SWITCH_PLAYER_DURATION 1.0f
#define SHOW_SCORE_DURATION 10.0f
#define AI_PLAYER_SPEED 0.450f
// the gap between tile shuffle/wall build steps
#define SHUFFLE_WAIT_DURATION 0.033f
// gap between player deals on hand start
#define DEAL_WAIT_DURATION 0.50f

typedef struct __attribute__((packed)) {
    i8 num_closed_tiles;
    tile_type tiles[MAX_CLOSED_TILES];
    //u32 deal_frame_for_tiles[14];
    int draw_timer_handles[MAX_CLOSED_TILES];
    u8 wall_index_for_tiles[MAX_CLOSED_TILES];
    tile_type open_tiles[MAX_OPEN_TILES];
    u8 open_tile_rotated[MAX_OPEN_TILES];
    tile_type discards[MAX_DISCARDS];
    u8 discard_rotated[MAX_DISCARDS];
    u32 discard_timer_handles[MAX_DISCARDS];
    i8 discard_from_hand_idx[MAX_DISCARDS];
    int score;
    i8 num_open_tiles;
    i8 selected_tile_idx;
    i8 num_discards;
    i8 in_riichi;
    int sorted;
} hand;

typedef struct __attribute__((packed)) {
    int tile_fall_timer_handles[TILES_IN_DECK];
    tile_type tiles[TILES_IN_DECK];
} wall;


typedef enum __attribute__((packed)) {
    STARTUP,
    INITIAL_SHUFFLE_AND_SETUP,
    DEALING,
    IN_GAME,
    SHOW_WINNING_HAND,
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


#define SCORE_MODIFIERS         \
    X(DORA, "dora")             \
    X(URA_DORA, "ura dora")     \
    X(AKA_DORA, "aka dora")     \
    X(PINFU, "pinfu")           \
    X(KAN, "kan")               \
    X(TANYAO, "tanyao")         \
    X(MENZEN_TSUMO, "tsumo")    \
    X(RIICHI, "riichi")         \
    X(IPPATSU, "ippatsu")       \
    X(YAKUHAI, "yakuhai")       \
    X(CHIITOITSU, "chiitoitsu") \
    X(KOKUSHI_MUSOU, "kokushi musou")


typedef enum {
#define X(nm, str) nm,
    SCORE_MODIFIERS
#undef X
    NUM_SCORE_MODIFIERS
} score_modifier_type;

const char* score_modifier_names[NUM_SCORE_MODIFIERS] = {
#define X(nm, str) str,
    SCORE_MODIFIERS
#undef X
};

typedef struct {
    int num_mods;
    score_modifier_type mods[16];
} hand_score_modifiers;

typedef enum {
    UNDRAWN,
    DRAWING,
    DRAWN
} tile_draw_state;

typedef enum {
    WAITING,
    COOLDOWN,
} ai_state;

typedef struct {
    u32 seeds[4];
    game_wind cur_wind;
    tile_type dora_tile; // TODO: support for multiple doras
    int next_deal_pos; // 
    u32 frame;
    u32 sim_frame;
    i8 deal_steps;
    i8 cur_player;
    int switch_player_timer_handle, shuffle_timer_handle, deal_timer_handle;
    int hand_winner;
    hand_score_modifiers hand_winner_mods;
    tile_draw_state draw_state;
    i8 cur_dealer;
    tile_type last_discard;
    i8 last_discard_player;
    game_state cur_game_state;
    
    u8 wall_rem;
    int wall_split_distance;

    hand hands[4];
    ai_state ai_player_states[4];
    int ai_player_timer_handles[4];
    player_type player_types[4];
    int waiting_for_calls; // 0 -> no calls necessary.  1+ waiting for calls
    int waiting_for_calls_players[4]; // player numbers that we are waiting for are in here
} game_data_t;


// NOTE: I extracted this from game_data
// because I was considering sending periodic game state 
// and figured I could omit the wall because it's generated from a RNG seed
// but honestly it should probably go back
wall board_wall = { 0 };


game_data_t game_data = { 
    // random seeds
    {0x27cb588d, 0x096379a9, 0xe81f5914, 0x2ee1c98c}, 
    // game wind
    EAST_WIND,
    // dora
    NUM_TILES, // invalid tile
    // next deal pos
    0,
    // frame
    0,
    // seq number
    0,
    // deal steps
    0,
    // cur player
    0,
    // switch_player_timer handle
    -1,
    // shuffle timer handle
    -1,
    // deal timer handle
    -1,
    // hand winner
    -1,
    // hand winner score mods
    {.num_mods = 0},
    // draw state
    UNDRAWN,
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
    {WAITING, WAITING, WAITING, WAITING},
    // ai player timer handles
    {-1, -1, -1, -1},
    // player types
    {HUMAN, AGGRESSIVE_AI, CHAOTIC_AI, CONSERVATIVE_AI},
    // waiting for calls
    0,
    // players from which we are waiting for calls from
    {-1, -1, -1, -1}
};


i8 human_player = 0; // default value for host, for clients this gets assigned to an initial player num in a packet from the host
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

const f32 wall_offsets_x[4] = {17.0f, 0.0f, -17.0f, 0.0f};
const f32 wall_offsets_z[4] = {0.0f, 17.0f, 0.0, -17.0f};
const f32 wall_y_rots[4] = {(f32)M_PI * 0.5f, 0.0f, -(f32)M_PI * 0.5f, (f32)M_PI};

typedef void (*timer_callback)(u64 callback_data);

typedef struct {
    const char *name;
    u64 callback_data;
    u64 start_ticks;
    u64 end_ticks;
    timer_callback expire_cb;
    u8 used:1;
    u8 finished:1;
    u8 release_on_expire:1;
} timer;

#define MAX_NUM_TIMERS 128
timer timers[MAX_NUM_TIMERS];
int num_active_timers = 0;

void reset_timers() {
    num_active_timers = 0;
    for(int i = 0; i < MAX_NUM_TIMERS; i++) {
        timers[i].used = 0;
        timers[i].finished = 0;
    }
}

int timer_get_handle(const char* timer_name) {
    for(int i = 0; i < MAX_NUM_TIMERS; i++) {
        if(timers[i].used) {
            continue;
        }
        num_active_timers++;
        timers[i].used = 1;
        timers[i].finished = 0;
        timers[i].name = timer_name;
        return i;
    }
    return -1;
}


void timer_null_callback(u64 callback_data) {
    (void)callback_data;
}


void timer_release(int handle) {
    if(handle == -1) { return; }
    timers[handle].finished = 0;
    num_active_timers--;
    timers[handle].used = 0;
    timers[handle].name = NULL;
}

void timer_start(int handle, f32 duration_seconds, int release_on_expire, timer_callback expire_cb, u64 callback_data) {
    u64 start_ticks = exotique_get_ticks();

    timers[handle].start_ticks = start_ticks;
    timers[handle].end_ticks = start_ticks + (u64)(duration_seconds*1000.0f);
    timers[handle].expire_cb = expire_cb;
    timers[handle].callback_data = callback_data;
    timers[handle].finished = 0;
    timers[handle].release_on_expire = release_on_expire;
}

void timer_start_no_callback(int handle, f32 duration_seconds, int release_on_expire) {
    timer_start(handle, duration_seconds, release_on_expire, timer_null_callback, 0);
}

f32 timer_get_progress(int handle) {
    if(handle < 0 || handle >= MAX_NUM_TIMERS) {
        return 1.0f;
    }
    f32 end_ticks = timers[handle].end_ticks;
    f32 start_ticks = timers[handle].start_ticks;
    f32 cur_ticks = exotique_get_ticks();
    f32 dur = end_ticks-start_ticks;
    f32 dtime = cur_ticks - start_ticks;

    return CLAMP(dtime / dur, 0.0f, 1.0f);
}

void timer_step(u64 cur_ticks) {
    for(int i = 0; i < MAX_NUM_TIMERS; i++) {
        if(timers[i].used == 0 || timers[i].finished) {
            continue;
        }

        if(cur_ticks >= timers[i].end_ticks) {
            timers[i].finished = 1;
            //exotique_printf("CALLING FINISH CALLBACK FOR %s\n", timers[i].name);
            timers[i].expire_cb(timers[i].callback_data);
            if(timers[i].release_on_expire) {
                timers[i].used = 0;
                num_active_timers--;
            }
        }
    }
}

transform identity_transform(void) {
    return (transform){
        .scale =    {1.0f, 1.0f, 1.0f},
        .position = {0.0f, 0.0f, 0.0f},
        .rotation = {0.0f, 0.0f, 0.0f}
    };
}

vert3f calc_global_discard_position(hand *h, int discard_i, matrix* hand_to_world_matrix);

f32 calc_wall_tile_x_position(int row_on_wall) {

    f32 wall_length = (f32)(16 * WALL_TILE_SPACING);
    f32 half = wall_length / 2.0f;
    f32 row_x_offset = (f32)row_on_wall * -WALL_TILE_SPACING;

    f32 x_position = half + row_x_offset;
    
    return x_position;
}

f32 calc_wall_tile_y_position(wall *d, int tot_tile_idx) {
    f32 tile_fall_pos = timer_get_progress(d->tile_fall_timer_handles[tot_tile_idx]);
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

vert3f calc_wall_tile_global_position(wall* w, int tot_tile_idx) {
    // NOTE: this takes into account the split distance offset
    
    int offset_pos = mod_positive(tot_tile_idx-(34-game_data.wall_split_distance*2),TILES_IN_DECK);
    int wall_side = (offset_pos/34);
    int position_in_wall = (offset_pos-wall_side*34)/2;

    transform wall_trans = get_wall_transform(wall_side);
    matrix wall_matrix = transform_to_matrix(&wall_trans);

    vert3f local_pos;
    local_pos.x = calc_wall_tile_x_position(position_in_wall);
    local_pos.y = calc_wall_tile_y_position(w, tot_tile_idx);
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

f32 calc_hand_y_position(hand* h, int idx, int hand_won) { //}, int is_cur_player) {
    f32 cur_player_height = ((h->selected_tile_idx == idx && !hand_won) ? SELECTED_TILE_Y_POS : UNSELECTED_TILE_Y_POS);
    return cur_player_height;
}

vert3f calc_local_hand_position(hand* h, int tile_in_hand_idx, int hand_won) {
    return (vert3f) {
        calc_hand_x_position(h, tile_in_hand_idx),
        calc_hand_y_position(h, tile_in_hand_idx, hand_won),
        -2.0f
    };
}

vert3f calc_global_hand_position(hand* h, int tile_in_hand_idx, int hand_won, matrix* hand_to_world_matrix) {
    vert3f local = calc_local_hand_position(h, tile_in_hand_idx, hand_won);
    return mat_mul_vert3(hand_to_world_matrix, &local);
}

vert3f calc_animated_hand_tile_position(wall *w, hand *h, int tile_in_hand_idx, int hand_won, matrix *hand_to_world, matrix *world_to_hand) {
    /* 
        this function calculates global positions for in the wall and in the hand
        lerps between them
        then multiplies by the intverse matrix of world->hand to get a hand-local position

        kinda roundabout and less efficient than it could be, but its okay :)
    */  
    f32 anim_progress = timer_get_progress(h->draw_timer_handles[tile_in_hand_idx]);

    vert3f local;
    if(anim_progress >= 1.0f) {
        local = calc_local_hand_position(h, tile_in_hand_idx, hand_won);
    } else {
        vert3f global_wall_position = calc_wall_tile_global_position(w, h->wall_index_for_tiles[tile_in_hand_idx]);
        vert3f global_hand_position = calc_global_hand_position(h, tile_in_hand_idx, hand_won, hand_to_world);


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

vert3f calc_local_discard_position(hand *h, int discard_i) {
    int discard_row = discard_i/6;
    int pos_in_row = discard_i - (discard_row*6);

    int got_riichi_discard = 0;
    int is_riichi_discard = h->discard_rotated[discard_i];
    for(int i = 1; i <= pos_in_row; i++) {
        if(h->discard_rotated[discard_i-i]) {
            got_riichi_discard = 1;
            break;
        }

    }

    const f32 discard_row_size = -1.8f + 6*1.8f;
    const f32 half_row_size = discard_row_size/2.0f;
    f32 pos_x = half_row_size + (f32)pos_in_row * -1.8f - (is_riichi_discard ? 0.27 : 0.0f) - (got_riichi_discard ? 0.65f : 0.0f);
    f32 pos_y = 0.0f;
    f32 pos_z = -19.5f + (f32)discard_row * 2.5f;
    return (vert3f){pos_x + discard_x_offsets[discard_i], pos_y, pos_z + discard_z_offsets[discard_i]};
}

vert3f calc_global_discard_position(hand *h, int discard_i, matrix* hand_to_world_matrix) {
    vert3f local = calc_local_discard_position(h, discard_i);
    return mat_mul_vert3(hand_to_world_matrix, &local);
}

int use_dragon_model = 1;

void draw_hand(
    wall* w, hand* h, 
    int draw_wind_indicator, int hand_won,
    matrix* hand_to_view_matrix, matrix* hand_to_world_matrix) {
    mesh_draw_call draw_calls[14 + MAX_DISCARDS + MAX_OPEN_TILES + 1 + 40]; // one extra for wind indicator if necessary, plus 20 for tenbou sticks :)

    int draw_idx = 0;

    matrix world_to_hand = mat_inverse_affine(hand_to_world_matrix);

    for(int i = 0; i <  h->num_closed_tiles; i++) {

        vert3f local = calc_animated_hand_tile_position(w, h, i, hand_won, hand_to_world_matrix, &world_to_hand);

        transform tile_trans = identity_transform();
        tile_trans.position.x = local.x;
        tile_trans.position.y = local.y;
        tile_trans.position.z = local.z;
        tile_trans.rotation.x = hand_won ? 0.0f : 1.57f;
        tile_trans.scale = (vert3f){TILE_SCALE, TILE_SCALE, TILE_SCALE};


        matrix tile_mat = transform_to_matrix(&tile_trans);
        matrix tile_to_view_matrix = mat_mul_mat(hand_to_view_matrix, &tile_mat);
        //matrix tile_to_world_matrix = mat_mul_mat(hand_to_world_matrix, &tile_mat);

        draw_calls[draw_idx].shdr = LIT_TEXTURED; 
        draw_calls[draw_idx].mesh = &mahjong_tile_mesh;
        draw_calls[draw_idx].bounds = &tile_bbox;
        draw_calls[draw_idx].texture = h->tiles[i];
        //draw_calls[draw_idx].model_to_world = tile_to_world_matrix;
        draw_calls[draw_idx++].model_to_view = tile_to_view_matrix;
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

        tile_trans.rotation.x = 0.0f;
        tile_trans.rotation.y = h->open_tile_rotated[i] ? (f32)M_PI/2.0f : 0.0f;
        tile_trans.scale = (vert3f){TILE_SCALE, TILE_SCALE, TILE_SCALE};

        matrix tile_mat = transform_to_matrix(&tile_trans);

        matrix tile_to_view_matrix = mat_mul_mat(hand_to_view_matrix, &tile_mat);
        //matrix tile_to_world_matrix = mat_mul_mat(hand_to_world_matrix, &tile_mat);

        draw_calls[draw_idx].shdr = LIT_TEXTURED; 
        draw_calls[draw_idx].mesh = &mahjong_tile_mesh;
        draw_calls[draw_idx].bounds = &tile_bbox;
        draw_calls[draw_idx].texture = h->open_tiles[i];
        //draw_calls[draw_idx].model_to_world = tile_to_world_matrix;
        draw_calls[draw_idx++].model_to_view = tile_to_view_matrix;
    }

    
    /*
        draw discards

    */
    for(int i = 0; i < h->num_discards; i++) {

        vert3f discard_pos = calc_local_discard_position(h, i);
        f32 discard_progress = timer_get_progress(h->discard_timer_handles[i]);

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
        discard_transform.rotation.y = discard_y_rots[i] + (h->discard_rotated[i] ? 1.57f : 0.0f);
        discard_transform.scale = (vert3f){TILE_SCALE, TILE_SCALE, TILE_SCALE};
        matrix discard_tile_mat = transform_to_matrix(&discard_transform);
      
        matrix tile_to_view_matrix = mat_mul_mat(hand_to_view_matrix, &discard_tile_mat);
        //matrix tile_to_world_matrix = mat_mul_mat(hand_to_world_matrix, &discard_tile_mat);

        draw_calls[draw_idx].shdr = LIT_TEXTURED; 
        draw_calls[draw_idx].mesh = &mahjong_tile_mesh;
        draw_calls[draw_idx].bounds = &tile_bbox;
        draw_calls[draw_idx].texture = h->discards[i];
        //draw_calls[draw_idx].model_to_world = tile_to_world_matrix;
        draw_calls[draw_idx++].model_to_view = tile_to_view_matrix;
    }

    if(draw_wind_indicator) {
        transform wind_indicator_trans = identity_transform();
        wind_indicator_trans.position.x = -25.0f;

        wind_indicator_trans.rotation.z = game_data.cur_wind == EAST_WIND ? 0.0f : (f32)M_PI; 

        if(use_dragon_model) {
            wind_indicator_trans.rotation.y = (f32)game_data.frame/1024.0f;
            wind_indicator_trans.scale = (vert3f){25.0f, 25.0f, 25.0f};
        } else {
            wind_indicator_trans.rotation.y = (f32)M_PI;
            wind_indicator_trans.scale = (vert3f){2.0f, 2.0f, 2.0f};
        }

        matrix wind_indicator_to_hand_mat = transform_to_matrix(&wind_indicator_trans);

        draw_calls[draw_idx].shdr = LIT_TEXTURED;
        draw_calls[draw_idx].mesh = use_dragon_model ? &dragon_low_poly_mesh : &wind_indicator_mesh;
        draw_calls[draw_idx].bounds = 0;
        draw_calls[draw_idx].texture = WIND_INDICATOR;
        //draw_calls[draw_idx].model_to_world = mat_mul_mat(hand_to_world_matrix, &wind_indicator_to_hand_mat);
        draw_calls[draw_idx++].model_to_view = mat_mul_mat(hand_to_view_matrix, &wind_indicator_to_hand_mat);
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
                    //matrix tenbou_to_world_matrix = mat_mul_mat(hand_to_world_matrix, &tenbou_mat);
                    

                    draw_calls[draw_idx].shdr = LIT_TEXTURED;
                    draw_calls[draw_idx].mesh = &tenbou_mesh;
                    draw_calls[draw_idx].bounds = &tile_bbox; // TODO: invalid but unused
                    draw_calls[draw_idx].texture = color;
                    //draw_calls[draw_idx].model_to_world = tenbou_to_world_matrix;
                    draw_calls[draw_idx++].model_to_view = tenbou_to_view_matrix;
                }
            }


        }
    }

    submit_draw_calls(draw_calls, draw_idx, NO_FRUSTUM_CULL);
}

void draw_wall(game_state cur_state, wall *w, matrix *view_mat) {

    mesh_draw_call draw_calls[34*4];

    int draw_idx = 0;

    //matrix wall_matrixes[4];
    matrix wall_view_matrixes[4];

    for(int i = 0; i < 4; i++) {

        transform wall_trans = get_wall_transform(i);

        matrix wall_matrix = transform_to_matrix(&wall_trans);
        matrix wall_view_matrix = mat_mul_mat(view_mat, &wall_matrix);
        //wall_matrixes[i] = wall_matrix;
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

        tile_trans.position.y = calc_wall_tile_y_position(w, j);
        tile_trans.position.z = ((wall_side & 1) ? 0.0f : wall_z_tile_offsets[position_in_wall]);
        tile_trans.scale = (vert3f){TILE_SCALE, TILE_SCALE, TILE_SCALE};



        matrix tile_mat = transform_to_matrix(&tile_trans);
        matrix tile_to_view_matrix = mat_mul_mat(&wall_view_matrixes[wall_side], &tile_mat);
        //matrix tile_to_world_matrix = mat_mul_mat(&wall_matrixes[wall_side], &tile_mat);

        draw_calls[draw_idx].shdr = LIT_TEXTURED;
        draw_calls[draw_idx].mesh = &mahjong_tile_mesh;
        draw_calls[draw_idx].bounds = &tile_bbox;
        draw_calls[draw_idx].model_to_view = tile_to_view_matrix;
        //draw_calls[draw_idx].model_to_world = tile_to_world_matrix;

        draw_calls[draw_idx++].texture = this_tile;
    }

    submit_draw_calls(draw_calls, draw_idx, FRUSTUM_CULL);
}

void draw_riichi_game(game_state cur_state, int hand_winner, matrix* view_mat) {
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
        draw_hand(&board_wall, this_hand, 
            (i == game_data.cur_dealer), // draw wind indicator
            hand_winner == i,
            hand_matrixes[i][0], hand_matrixes[i][1]
        );

    }

    draw_wall(cur_state, &board_wall, view_mat);
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
    draw_board_call.bounds = &board_bbox;
    draw_board_call.model_to_view = board_to_view_matrix;
    //draw_board_call.model_to_world = board_matrix;
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

#include "asset_headers/font.h"

static void bit_draw(const u8 *sprite, i32 x, i32 y, i32 font_width, i32 font_height, u8 color, u8 bkgd_color, u8* tex, int tex_width, int tex_height) {
    for (i32 row = 0; row < font_height; ++row) {
        for (i32 col = 0; col < font_width; ++col) {
            i32 bit_idx = row * font_width + col;
            if ((x + col) >= 0 && (x + col < tex_width) && (y + row) >= 0 && (y + row) < tex_height) {
                tex[(y + row) * tex_width + (x + col)] = (sprite[bit_idx >> 3] << (bit_idx & 7) & 0x80) ? color : bkgd_color;
            }
        }
    }
}

static void text_draw(const u8* font, i32 x, i32 y, i32 font_width, i32 font_height, u8 color, u8 bkgd_color, u8* tex, int tex_width, int tex_height, const char* str) {
  if (str) {
    for (i32 i = 0; str[i]; ++i) {
      i32 char_index = (i32)str[i] - 32;
      if (char_index >= 0 && char_index < 96) {
        bit_draw(font+(char_index*font_height), x * font_width + (font_width * i), y * font_height, font_width, font_height, color, bkgd_color, tex, tex_width, tex_height);
      }
    }
  }
}


u8 score_tex[256*256];
void draw_string_to_texture(char* str, int x, int y, u8 col, u8 bkgd_col, u8* texture) {
      text_draw(twtr_8x8_font, x, y, 8, 8, col, bkgd_col, texture, 256, 256, str);
}


transform cam_view_trans;
static u64 ms_per_frame;
static u64 prev_frame_ticks = 0;  

void look_at_yx(transform *cam, vert3f position, vert3f target);

void game_draw(ExotiqueInterface* ei) {

    u64 cur_frame_ticks = ei->ticks;

    u64 prev_ms_per_frame = ms_per_frame;
    (void)prev_ms_per_frame;
    ms_per_frame = cur_frame_ticks - prev_frame_ticks;    
    prev_frame_ticks = cur_frame_ticks;

    vcache_misses = 0;
    vcache_hits = 0;
    total_triangles = 0;

    static block whole_frame_block = ROOT_TIMED_BLOCK(whole_frame_block, "draw frame")
        matrix view_matrix = transform_to_view_matrix(&cam_view_trans);
        static block clear_tiles_block = START_TIMED_BLOCK(clear_tiles_block, "clear buf", whole_frame_block)
            clear_tile_bins();
        END_TIMED_BLOCK(clear_tiles_block)
        
        static block riichi_block = START_TIMED_BLOCK(riichi_block, "draw game", whole_frame_block)
            draw_riichi_game(game_data.cur_game_state, game_data.hand_winner, &view_matrix);
        END_TIMED_BLOCK(riichi_block)

        static block board_block = START_TIMED_BLOCK(board_block, "draw board", whole_frame_block)
            draw_board(&view_matrix);
        END_TIMED_BLOCK(board_block)

        static block raster_block = START_TIMED_BLOCK(raster_block, "rast. tiles", whole_frame_block)
            rasterize_tiles_parallel();
        END_TIMED_BLOCK(raster_block)

    END_TIMED_BLOCK(whole_frame_block)

    if((game_data.frame & 31) == 0) {
        //print_and_reset_root_block(&whole_frame_block);
        //exotique_printf("used anim timers %i\n", num_active_timers);
        //exotique_printf("total %.2f ms\n", (double)(prev_ms_per_frame + ms_per_frame) / 2.0);
        //exotique_printf("vcache misses %i, hits %i %.2f\n", vcache_misses, vcache_hits, (double)vcache_hits*100.0/(double)(vcache_misses+vcache_hits));
        //for(int i = 0; i < MAX_NUM_TIMERS; i++) {
        //    if(timers[i].used) {
        //        if(timers[i].finished) {
        //            exotique_printf("stale timer: %s\n", timers[i].name);
        //        } else {
        //            exotique_printf("active timer: %s\n", timers[i].name);
        //        }
        //    }
        //}
    }
    
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


hand init_empty_hand() {
    hand h;
    h.num_open_tiles = 0; 
    h.num_closed_tiles = 0;
    h.num_discards = 0;
    h.selected_tile_idx = -1;
    h.in_riichi = 0;
    h.score = 25000;
    for(int i = 0; i < MAX_CLOSED_TILES; i++) {
        h.draw_timer_handles[i] = -1;
    }
    for(int i = 0; i < MAX_DISCARDS; i++) {
        h.discard_timer_handles[i] = 1;
    }

    h.sorted = 0;
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

void init_seeds() {
    game_data.seeds[3] = game_data.seeds[2];
    game_data.seeds[1] = game_data.seeds[0];
    game_data.seeds[0] = (u32)exotique_get_perf_counter();
}

tile_type player_winds[4] = {
    EAST, NORTH, WEST, SOUTH 
};


void reset_ai_player_state_callback(u64 callback_data) {
    u8 idx = (u8)callback_data;
    game_data.ai_player_states[idx] = WAITING;
}

void reset_ai_player_state(int idx) {
    if(game_data.ai_player_timer_handles[idx] == -1) {
        int ai_reset_timer_handle = timer_get_handle("reset ai state");
        game_data.ai_player_timer_handles[idx] = ai_reset_timer_handle;
    }
    timer_start(game_data.ai_player_timer_handles[idx], AI_PLAYER_SPEED, 0, reset_ai_player_state_callback, idx);
}

void reset_queue();

void reset_game() {
    static int is_not_first_game = 0;
    reset_timers();
    
    reset_queue();

    if(is_not_first_game) {
        int tmp = player_winds[0];
        player_winds[0] = player_winds[1];
        player_winds[1] = player_winds[2];
        player_winds[2] = player_winds[3];
        player_winds[3] = tmp;
    }
    game_data.hand_winner = -1;
    game_data.hand_winner_mods.num_mods = 0;
    game_data.cur_wind = EAST_WIND;
    game_data.cur_game_state = INITIAL_SHUFFLE_AND_SETUP;
    game_data.frame = 0;
    game_data.sim_frame = 0;
    game_data.draw_state = UNDRAWN;
    game_data.waiting_for_calls = 0;
    for(int i = 0; i < 4; i++) {
        game_data.ai_player_timer_handles[i] = -1;
        game_data.waiting_for_calls_players[i] = -1;
        reset_ai_player_state(i);
    }
    game_data.deal_steps = 0;
    game_data.cur_dealer++;
    if(game_data.cur_dealer == 4) {
        game_data.cur_dealer = 0;
        game_data.cur_wind = (game_data.cur_wind+1)%2;
    }
    game_data.cur_player = game_data.cur_dealer;
    game_data.switch_player_timer_handle = -1;
    game_data.shuffle_timer_handle = -1;
    game_data.deal_timer_handle = -1;
    game_data.wall_rem = 0;

    init_wall();
    for(int i = 0; i < 4; i++) {
        int score = game_data.hands[i].score;
        game_data.hands[i] = init_empty_hand();
        if(is_not_first_game) {
            game_data.hands[i].score = score;
        }
    }
    shuffle_deck(&board_wall);

    game_data.next_deal_pos = 0;
    game_data.last_discard_player = -1;
    is_not_first_game = 1;
}


#include "SDL_net.h"

static TCPsocket serv_sock = NULL;
IPaddress server_ip;
SDLNet_SocketSet socket_set;
int num_socks_in_set = 0;

#define MAKE_NUM(A, B, C, D)    (((A+B)<<8)|(C+D))

#define JONG_PORT MAKE_NUM('J','O','N','G')
#define JONG_PORT1 MAKE_NUM('J','O','N','G')+1
#define JONG_PORT2 MAKE_NUM('J','O','N','G')+2
#define JONG_PORT3 MAKE_NUM('J','O','N','G')+3
#define MAX_CLIENTS 3
static struct {
    int active;
    int player_num;
    TCPsocket sock;
    IPaddress peer; // host and port used to connect to server
    u16 listen_port; // port this client is listening on
    Uint8 name[256+1];
} client_info[MAX_CLIENTS];


typedef struct {
    int ready;
    SOCKET channel; // actual socket fd
    IPaddress remoteAddress;
    IPaddress localAddress;
    int sflag;
} internal_TCPsocket;

void disable_nagle(TCPsocket sock) {
    internal_TCPsocket *int_sock = (internal_TCPsocket*)sock;
    int opt = 1;
    int res = setsockopt(int_sock->channel, IPPROTO_TCP, TCP_NODELAY, (char*)&opt, sizeof(opt));
    if(res != 0) {
        int err = WSAGetLastError();
        exotique_printf("Error disabling nagle's algorithm on socket: %i\n", err);
        exit(1);
    }
}

TCPsocket SDLNet_TCP_Open_NODELAY(IPaddress *ip) {
    TCPsocket sock = SDLNet_TCP_Open(ip);
    //disable_nagle(sock);
    return sock;
}


TCPsocket SDLNet_TCP_OpenClient_NODELAY(IPaddress *ip) {
    TCPsocket sock = SDLNet_TCP_OpenClient(ip);
    disable_nagle(sock);
    return sock;
}


TCPsocket SDLNet_TCP_Accept_NODELAY(TCPsocket sock) {
    TCPsocket new_sock = SDLNet_TCP_Accept(sock);
    disable_nagle(new_sock);
    return new_sock;
}


void server_get_connection() {
    TCPsocket new_sock;
    int which;

    new_sock = SDLNet_TCP_Accept_NODELAY(serv_sock);
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
    int added_sock = SDLNet_TCP_AddSocket(socket_set, client_info[which].sock);
    if(added_sock == -1) {
        exotique_printf("error adding socket to set\n");
        exit(1);
    }
    num_socks_in_set = added_sock;
    exotique_printf("Client connected at %i %i\n", client_info[which].peer.host, client_info[which].peer.port);
}



typedef enum __attribute__((packed)) {
    LISTEN_PORT = 1,
    RANDOM_SEED_AND_PLAYER_INFO = 3,
    INPUT_FROM_CLIENT = 5,
} msg_type;

void setup_host(int num_clients) {
    if(num_clients < 0 || num_clients > 3) {
        exotique_printf("Please specify number of clients between 0 and 3, --num-clients [num]\n");
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
    int added_sock = SDLNet_TCP_AddSocket(socket_set, serv_sock);
    if(added_sock == -1) {
        exotique_printf("error adding socket to set\n");
        exit(1);
    }
    num_socks_in_set = added_sock;

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
    
    // remove server listen socket from socket set
    added_sock = SDLNet_TCP_DelSocket(socket_set, serv_sock);
    if(added_sock == -1) {
        exotique_printf("error removing socket to set\n");
        exit(1);
    }
    num_socks_in_set = added_sock;
}

IPaddress client_ip;
TCPsocket client_listen_sock;
u16 client_listen_port;
void setup_client(char* server_address) {
    socket_set = SDLNet_AllocSocketSet(4);
    if(socket_set == NULL) {
        exotique_printf("Couldn't create socket set: %s\n", SDLNet_GetError());
        exit(1);
    }

    for(int listen_port = JONG_PORT+1; listen_port < JONG_PORT+100; listen_port++) {
        if(SDLNet_ResolveHost(&client_ip, NULL, listen_port) == 0) {
            client_listen_sock = SDLNet_TCP_Open(&client_ip);

            if(client_listen_sock != NULL) {
                exotique_printf("Listening on port %i:%i / %i\n", SDLNet_Read32(&client_ip.host), SDLNet_Read16(&client_ip.port), listen_port);
                client_listen_port = client_ip.port;
                break;
            }
        }
    }
    if(client_listen_sock == NULL) {
        exotique_printf("Couldn't open listen socket\n");
        exit(1);
    }

    exotique_printf("Attempting to connect to server @ %s:%i\n", server_address, JONG_PORT);
    SDLNet_ResolveHost(&server_ip, server_address, JONG_PORT);
    if(server_ip.host == INADDR_NONE) {
        exotique_printf("Couldn't resolve hostname\n");
        exit(1);
    }
    exotique_printf("Connecting to %s %i\n", server_address, JONG_PORT);
    while(1) {
        serv_sock = SDLNet_TCP_Open(&server_ip);
        if(serv_sock != NULL) {
            break;
            //exotique_printf("Failed to connect :(\n");
            //exit(1);
        }
        exotique_printf("Failed, retrying\n");
        int counter = 1000000;
        while(counter) { counter--; }
    }
    disable_nagle(serv_sock);

    int added_sock = SDLNet_TCP_AddSocket(socket_set, serv_sock);
    if(added_sock == -1) {
        exotique_printf("error adding socket to set\n");
        exit(1);
    }
    num_socks_in_set = added_sock;
}

typedef struct {
    int player_num;
    IPaddress ip; // address and port on which they are going to listen
} player_conn_info;

typedef struct {
    u32 seeds[4];
    u32 num_other_clients;
    player_conn_info clients_info[3];

    IPaddress your_ip; 
    int player_num;
} seed_and_player_info;

#define MAX_PACKET_SIZE (sizeof(seed_and_player_info)+1)

char data_buf[MAX_PACKET_SIZE];

void send_packet_to_socket(TCPsocket sock, msg_type type, u32 num_bytes_after_type, void* src_buf, const char* obj_type) {
    data_buf[0] = type;
    memcpy(data_buf+1, src_buf, num_bytes_after_type);
    int sent = SDLNet_TCP_Send(sock, &data_buf, num_bytes_after_type+1);
    if(sent < 0) {
        exotique_printf("Error: disconnected while sending %s\n", obj_type);
        exit(1);
    }
    if(sent != (int)num_bytes_after_type+1) {
        exotique_printf("Error sending %s (of %i bytes), only sent %i bytes\n", obj_type, num_bytes_after_type+1, sent);
        exit(1);
    }
}

void receive_packet_from_socket(TCPsocket sock, msg_type type, u32 num_bytes_after_type, void* dst_buf, const char* obj_type) {
    int recvd = SDLNet_TCP_Recv(sock, data_buf, num_bytes_after_type+1);
    if(recvd < 0) {
        exotique_printf("Error: disconnected while receiving %s :(\n", obj_type);
        exit(1);
    }
    if(recvd != (int)num_bytes_after_type+1) {
        exotique_printf("Error receiving %s (of %i bytes), only received %i bytes\n", obj_type, num_bytes_after_type+1, recvd);
        exit(1);
    }
    if(data_buf[0] != type) {
        exotique_printf("Got unexpected byte from client when waiting for %s: %i\n", obj_type, data_buf[0]);
        exit(1);
    }
    memcpy(dst_buf, data_buf+1, num_bytes_after_type);
}

typedef struct {
    u16 listen_port;
} listen_port_t;

void server_wait_for_listen_port_from_players() {
    
    for(int i = 0; i < MAX_CLIENTS; i++) {
        if(client_info[i].sock) {
            exotique_printf("Waiting for listen port from %i:%i\n", SDLNet_Read32(&client_info[i].peer.host), SDLNet_Read16(&client_info[i].peer.port));
            
            listen_port_t port;
            receive_packet_from_socket(client_info[i].sock, LISTEN_PORT, sizeof(listen_port_t), &port, "listen port");
            
            client_info[i].listen_port = port.listen_port;
            exotique_printf("Client is listening on %i\n", port.listen_port);
        }
    }
}




void client_send_listen_port_to_server() {
    listen_port_t dat; dat.listen_port = client_listen_port;
    send_packet_to_socket(serv_sock, LISTEN_PORT, sizeof(listen_port_t), &dat, "listen port");
}


void send_initial_state() {
    seed_and_player_info seed_info;

    int num_all_clients = 0;
    memcpy(seed_info.seeds, game_data.seeds, sizeof(u32)*4);

    for(int i = 0; i < MAX_CLIENTS; i++) {
        if(client_info[i].sock) {
            seed_info.clients_info[num_all_clients].ip = client_info[i].peer;
            seed_info.clients_info[num_all_clients].ip.port = client_info[i].listen_port;
            exotique_printf("set listen port to %i\n", client_info[i].listen_port);
            seed_info.clients_info[num_all_clients++].player_num = client_info[i].player_num;
        }
    }
    seed_info.num_other_clients = num_all_clients;

    for(int i = 0; i < MAX_CLIENTS; i++) {
        if(client_info[i].sock) {
            seed_info.your_ip = client_info[i].peer;
            seed_info.your_ip.port = client_info[i].listen_port;
            seed_info.player_num = client_info[i].player_num;

            send_packet_to_socket(client_info[i].sock, RANDOM_SEED_AND_PLAYER_INFO, sizeof(seed_and_player_info), &seed_info, "initial state");
        }
    }
}

seed_and_player_info receive_initial_state() {
    exotique_printf("Waiting for initial state\n");

    seed_and_player_info start_info;    
    receive_packet_from_socket(serv_sock, RANDOM_SEED_AND_PLAYER_INFO, sizeof(seed_and_player_info), &start_info, "initial state");

    human_player = start_info.player_num;
    memcpy(&game_data.seeds, start_info.seeds, sizeof(u32)*4);

    return start_info;
}
int is_host = 0, is_client = 0;


SDLNet_SocketSet client_socket_set;
IPaddress client_to_client_ips[3];
TCPsocket client_to_client_sockets[3];


void setup_connections_to_other_clients(seed_and_player_info initial_state) {

    int our_index = -1;
    for(u32 i = 0; i < initial_state.num_other_clients; i++) {
        // sub-connections to wait for 
        
        if(initial_state.clients_info[i].player_num == initial_state.player_num) {
            // this is us!!
            our_index = i;
        }
    }

    int connections_to_open = our_index;

    int connections_to_listen_for = initial_state.num_other_clients-1 - our_index;

    // now setup player types
    game_data.player_types[0] = NETWORK_HUMAN;
    for(int i = 0; i < connections_to_open; i++) {
        game_data.player_types[i+1] = NETWORK_HUMAN;
    }
    game_data.player_types[1+connections_to_open] = HUMAN;
    
    for(int i = connections_to_open; i < connections_to_open+connections_to_listen_for; i++) {
        game_data.player_types[i+2] = NETWORK_HUMAN;
    }

    for(int i = connections_to_listen_for+connections_to_open+2; i < 4; i++) {
        game_data.player_types[i] = CONSERVATIVE_AI;
    }

    exotique_printf("Connections to listen for %i, connections to open %i\n", connections_to_listen_for, connections_to_open);
    int connected_clients = 0;

    for(int i = initial_state.num_other_clients-1; i > our_index; i--) {
        // wait for connections from last client to next after our index
        while(1) {
            TCPsocket new_sock = SDLNet_TCP_Accept(client_listen_sock);
            if(new_sock == NULL) {
                // just try again
                continue;
            }
            disable_nagle(new_sock);

            IPaddress *ipptr = SDLNet_TCP_GetPeerAddress(new_sock);
            exotique_printf("got p2p connection from %i:%i\n", SDLNet_Read32(&ipptr->host), SDLNet_Read16(&ipptr->port));
        

            client_info[connected_clients].sock = new_sock;
            int added_sock = SDLNet_TCP_AddSocket(socket_set, client_info[connected_clients].sock);
            if(added_sock == -1) {
                exotique_printf("error adding socket to set\n");
                exit(1);
            }
            num_socks_in_set = added_sock;
            client_info[connected_clients++].player_num = initial_state.clients_info[i].player_num;
            break;
        }
    }
    for(int i = our_index-1; i >= 0; i--) {
        // open connections to clients from the one before us to 0
        IPaddress p2p_ip = initial_state.clients_info[i].ip;
        exotique_printf("Opening connection to %i:%i/%i\n", SDLNet_Read32(&p2p_ip.host), p2p_ip.port, SDLNet_Read16(&p2p_ip.port));

        TCPsocket p2p_sock = SDLNet_TCP_OpenClient(&p2p_ip);
        if(p2p_sock == NULL) {
            exotique_printf("Couldn't open p2p socket to player %i: %s\n", initial_state.clients_info[i].player_num, SDLNet_GetError());
            exit(1);
        }
        disable_nagle(p2p_sock);

        exotique_printf("opened p2p connection to player %i %i:%i\n", initial_state.clients_info[i].player_num, SDLNet_Read32(&p2p_ip.host), SDLNet_Read16(&p2p_ip.port));
        client_info[connected_clients].sock = p2p_sock;
        int added_sock = SDLNet_TCP_AddSocket(socket_set, client_info[connected_clients].sock);
        if(added_sock == -1) {
            exotique_printf("error adding socket to set\n");
            exit(1);
        }
        num_socks_in_set = added_sock;
        client_info[connected_clients++].player_num = initial_state.clients_info[i].player_num;
    }

    // add server socket to client info
    client_info[connected_clients].player_num = 0;
    client_info[connected_clients].sock = serv_sock;
}

#define ACTIONS             \
    X(MOVE_SELECTED_LEFT)   \
    X(MOVE_SELECTED_RIGHT)  \
    X(SORT_HAND)            \
    X(ATTEMPT_CALL)         \
    X(NO_ATTEMPT_CALL)      \
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
    u32 sim_frame;
    player_action_type cmd;
} player_action;

player_action make_player_action(i8 player_num, player_action_type cmd) {
    //game_data.cur_frame;
    return (player_action){player_num, game_data.sim_frame, cmd};
}

#define MAX_PLAYER_EVENTS 128
player_action action_queue[MAX_PLAYER_EVENTS];

// writes to head, and reads from tail
int queue_head = 0;
int queue_tail = 0;

typedef struct {
    player_action action;
} action_packet;

int queue_full() {
    return (queue_head - queue_tail) == MAX_PLAYER_EVENTS;
}
int queue_empty() {
    return (queue_head - queue_tail) == 0;
}
int queue_size() {
    return (queue_head - queue_tail);
}

void reset_queue() {
    queue_head = queue_tail = 0;
}

player_action queue_peek() {
    return action_queue[queue_tail&(MAX_PLAYER_EVENTS-1)];
}

player_action queue_pop() {
    //exotique_printf("QUEUE POP\n");
    if(queue_empty()) {
        exotique_printf("ACTION QUEUE UNDERFLOW!\n");
        exit(1);
    }
    player_action act = queue_peek();
    queue_tail++;
    return act;
}

int first_event_is_lesser(player_action act1, player_action act2) {
    if(act1.sim_frame < act2.sim_frame) {
        return 1;
    }
    if(act1.sim_frame > act2.sim_frame) {
        return 0;
    }
    // lower player number means it will be earlier in queue
    if(act1.player_num < act2.player_num) {
        return 1;
    }
    // >= player number means it will stay in the correct order

    // this ensures that the same player's events will remain ordered, relative to other events by THAT player, in the same order as they arrived

    return 0;
}

void queue_push(player_action act) {
    if(queue_full()) {
        exotique_printf("ACTION QUEUE OVERFLOW!\n");
        exit(1);
    }
    /*
    exotique_printf(
        "PUSH action: frame=%i player=%i cmd=%s | current frame=%i | head=%i tail=%i size=%i\n",
        act.sim_frame,
        act.player_num,
        action_names[act.cmd],
        game_data.sim_frame,
        queue_head,
        queue_tail,
        queue_size()
    );
    */

    action_queue[queue_head&(MAX_PLAYER_EVENTS-1)] = act;

    int cur_new_idx = queue_head++;

    // insertion sort the new event into place
    // sort by sequence number ASCENDING, if both sequence numbers are the same, sort by player number ascending
    while(cur_new_idx-1 >= queue_tail && first_event_is_lesser(action_queue[cur_new_idx&(MAX_PLAYER_EVENTS-1)], action_queue[(cur_new_idx-1)&(MAX_PLAYER_EVENTS-1)])) {

        player_action old = action_queue[(cur_new_idx-1)&(MAX_PLAYER_EVENTS-1)];
        action_queue[(cur_new_idx-1)&(MAX_PLAYER_EVENTS-1)] = act;
        action_queue[cur_new_idx&(MAX_PLAYER_EVENTS-1)] = old;
        cur_new_idx--;
    }

    /*
    for (int i = queue_tail; i < queue_head; i++) {
        player_action a = action_queue[i & (MAX_PLAYER_EVENTS - 1)];

        exotique_printf(
            "  [%i] frame=%i player=%i cmd=%s\n",
            i,
            a.sim_frame,
            a.player_num,
            action_names[a.cmd]
        );
    }
    */
}

void client_send_input_to_all_clients() {
    player_action this_action;
    if(queue_size() == 0) {
        exotique_printf("Expected one entry (player input) in queue, but got 0\n");
        exit(1);
    } else {
        this_action = queue_peek();
    }

    for(int i = 0; i < MAX_CLIENTS; i++) {
        if(client_info[i].sock && client_info[i].player_num != human_player) {
            send_packet_to_socket(client_info[i].sock, INPUT_FROM_CLIENT, sizeof(player_action), &this_action, "action");
        }
    }
}

void client_wait_for_all_inputs() {
    player_action action;

    while(socket_set != NULL && SDLNet_CheckSockets(socket_set, 0)) {
        for(int i = 0; i < MAX_CLIENTS; i++) {
            if(client_info[i].sock && client_info[i].player_num != human_player) {
                if(!SDLNet_SocketReady(client_info[i].sock)) {
                    continue;
                }

                receive_packet_from_socket(client_info[i].sock, INPUT_FROM_CLIENT, sizeof(player_action), &action, "input");

                if(action.player_num == human_player){
                    exotique_printf("Error: corrupted packet claims to be from this client\n");
                    exit(1);
                }
                exotique_printf("got an action from player %i\n", action.player_num);
                queue_push(action);
            }
        }
    }
}


void game_load(ExotiqueInterface* ei, int argc, const char* argv[]) {
    char* server_address = NULL;
    int num_clients = -1;
    for(int i = 1; i < argc; i++) {
        if((strcmp(argv[i], "--host") == 0) || (strcmp(argv[i], "-h") == 0)) {
            is_host = 1;
        }
        else if((strcmp(argv[i], "--client") == 0) || (strcmp(argv[i], "-c") == 0)) {
            is_client = 1;
            if(argc > i+1) {
                // next arg is address and port
                server_address = (char*)argv[i+1];
            }
        }
        else if(strcmp(argv[i], "--no-music") == 0) {
            enable_music = 0;
        }
        else if(strcmp(argv[i], "--no-dragon") == 0) {
            use_dragon_model = 0;
        }
        else if(strcmp(argv[i], "--num-clients") == 0 && argc > i+1) {
            char *endptr;
            errno = 0;
            num_clients = strtol(argv[i+1],&endptr, 10);
            if(num_clients < 0 || num_clients > 3 || endptr == argv[i+1]) {
                exotique_printf("Invalid number of clients %i\n", num_clients);
                exit(1);
            }
        }
    }

        
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
                u32 rgba = (palette[i]<<8)|0xFF; 
                u8 br = (u8)(rgba >> 24);
                u8 bg = (u8)(rgba >> 16);
                u8 bb = (u8)(rgba >> 8);
                u32 byte_r = (u32)CLAMP(((f32)br)*scale, 0.0f, 255.0f);
                u32 byte_g = (u32)CLAMP(((f32)bg)*scale, 0.0f, 255.0f);
                u32 byte_b = (u32)CLAMP(((f32)bb)*scale, 0.0f, 255.0f);
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
    decompress_sounds();
    decompress_textures(ei);

    texture_board[0] = light_remap_table[NUM_SHADES/2-1][GREEN];
    
    clear_tile_bins();

    tile_bbox = get_mesh_bbox(&mahjong_tile_mesh);
    board_bbox = get_mesh_bbox(&board_mesh);

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
        server_wait_for_listen_port_from_players();
        init_seeds();
        send_initial_state();
    } else if (is_client) {
        setup_client(server_address);
        client_send_listen_port_to_server();
        seed_and_player_info initial_state = receive_initial_state();
        setup_connections_to_other_clients(initial_state);
    }
    exotique_printf("Connections setup to %i total players\n", num_socks_in_set);
    reset_game();

    // setup rasterization contexts
    for(int i = 0; i < NUM_RASTER_THREADS; i++) {
        raster_contexts[i].draw_tile_bmp = i;
        raster_contexts[i].ei = ei;
        raster_contexts[i].shutdown = 0;        
        raster_contexts[i].start_event = CreateEvent(NULL, FALSE, FALSE, NULL);
        raster_contexts[i].done_event  = CreateEvent(NULL, FALSE, FALSE, NULL);
        raster_contexts[i].color_buffer = color_buffers[i];
        raster_contexts[i].z_buffer = z_buffers[i];
        raster_thread_handles[i] = (HANDLE)_beginthread(raster_worker, 0, &raster_contexts[i]);
    }
}


// grab a timer handle for each falling tile
// release on expire
// add sound callback

void tile_fall_callback(u64 tile_idx) {
    add_sound(TILE_CLICK, 0.085f);
    board_wall.tile_fall_timer_handles[tile_idx] = -1;
}

void last_tile_fall_callback(u64 tile_idx) {
    add_sound(TILE_CLICK, 0.085f);
    board_wall.tile_fall_timer_handles[tile_idx] = -1;
    game_data.cur_game_state = DEALING;
}

void shuffle_timer_callback(u64 data) {
    (void)data;
    game_data.shuffle_timer_handle = -1;
}

void step_shuffle_and_setup(wall* w) {
    if(game_data.shuffle_timer_handle != -1) {
        return;
    }

    if(game_data.wall_rem == TILES_IN_DECK) {
        return;
    }
    int handle = timer_get_handle("shuffle step finish");
    w->tile_fall_timer_handles[game_data.wall_rem] = handle;
    if(game_data.wall_rem+1 == TILES_IN_DECK) {
        // callback for last tile
        timer_start(handle, TILE_FALL_DURATION, 1, last_tile_fall_callback, game_data.wall_rem);
    } else {
        timer_start(handle, TILE_FALL_DURATION, 1, tile_fall_callback, game_data.wall_rem);
    }
    game_data.wall_rem++;
    game_data.shuffle_timer_handle = timer_get_handle("shuffle wait");
    timer_start(game_data.shuffle_timer_handle, SHUFFLE_WAIT_DURATION, 1, shuffle_timer_callback, 0);
}

void sort_tile_list(tile_type *hand_tiles, int num_tiles, int reverse) {
    for(int frontier = 0; frontier < num_tiles; frontier++) {
        // below frontier we are perfectly sorted
        
        int smallest = frontier;
        for(int j = frontier+1; j < num_tiles; j++) {
            if(reverse) {
                if(tile_sort_val[hand_tiles[j]] > tile_sort_val[hand_tiles[smallest]]) {
                    smallest = j;
                }
            } else {
                if(tile_sort_val[hand_tiles[j]] < tile_sort_val[hand_tiles[smallest]]) {
                    smallest = j;
                }
            }
        }
        tile_type prev = hand_tiles[frontier];
        hand_tiles[frontier] = hand_tiles[smallest];
        hand_tiles[smallest] = prev;
    }
}

void sort_hand(int player) { 
    hand *h = &game_data.hands[player];
    if(h->sorted) {
        // sort in reverse order
        sort_tile_list(h->tiles, h->num_closed_tiles, 1);
        h->sorted = 0;
    } else {
        sort_tile_list(h->tiles, h->num_closed_tiles, 0);
        h->sorted = 1;
    }
}

typedef union {
    struct {
        u16 player;
        u16 tile_idx;
    };
    u64 raw;
} deal_or_draw_finish_info;

void deal_finish_callback(u64 callback_data) {
    deal_or_draw_finish_info inf; inf.raw = callback_data;

    u32 player = inf.player;
    u32 tile_idx = inf.tile_idx;
    game_data.hands[player].draw_timer_handles[tile_idx] = -1;
}

void step_deal_callback(u64 data) {
    (void)data;
    game_data.deal_timer_handle = -1;
}

void step_deal() {
    if(game_data.deal_timer_handle != -1) {
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
        int anim_handle = timer_get_handle("deal step finish");
        this_hand->draw_timer_handles[cur_num_tiles] = anim_handle;
        timer_start(anim_handle, TILE_DEAL_DURATION, 1, deal_finish_callback, ((deal_or_draw_finish_info){.player=hand_index, .tile_idx=cur_num_tiles}).raw);

        this_hand->wall_index_for_tiles[cur_num_tiles] = --game_data.wall_rem;
        this_hand->tiles[cur_num_tiles++] = board_wall.tiles[game_data.wall_rem];

    }
    this_hand->num_closed_tiles = cur_num_tiles;
    this_hand->selected_tile_idx = (i8)(cur_num_tiles-1);
    game_data.deal_steps++;
    if(game_data.deal_steps == 17) {
        game_data.cur_game_state = IN_GAME;
        game_data.draw_state = DRAWN;
        
        /*
        game_data.hands[0].tiles[0] = WHITE_DRAGON;
        game_data.hands[0].tiles[1] = WHITE_DRAGON;
        game_data.hands[1].tiles[0] = WHITE_DRAGON;

        game_data.hands[0].tiles[0] = WHITE_DRAGON;
        game_data.hands[0].tiles[1] = WHITE_DRAGON;
        game_data.hands[0].tiles[2] = RED_DRAGON;
        game_data.hands[0].tiles[3] = RED_DRAGON;
        game_data.hands[0].tiles[4] = GREEN_DRAGON;
        game_data.hands[0].tiles[5] = GREEN_DRAGON;
        game_data.hands[0].tiles[6] = NORTH;
        game_data.hands[0].tiles[7] = NORTH;
        game_data.hands[0].tiles[8] = EAST;
        game_data.hands[0].tiles[9] = EAST;
        game_data.hands[0].tiles[10] = SOUTH;
        game_data.hands[0].tiles[11] = SOUTH;
        game_data.hands[0].tiles[12] = WEST;
        game_data.hands[0].tiles[13] = WEST;
        */

        game_data.switch_player_timer_handle = -1;
        
    }

    int timer = timer_get_handle("deal wait");
    game_data.deal_timer_handle = timer;
    timer_start(timer, DEAL_WAIT_DURATION, 1, step_deal_callback, 0);
}

int last_start_pushed = 0, last_select_pushed = 0;
int last_a_pushed = 0, last_b_pushed = 0;
int last_left_pushed = 0, last_right_pushed = 0;
int last_x_pushed = 0, last_y_pushed = 0;

void discard_finish_callback(u64 callback_data) {
    deal_or_draw_finish_info info; info.raw = callback_data;

    u32 player = info.player;
    u32 tile_idx = info.tile_idx;
    game_data.hands[player].discard_timer_handles[tile_idx] = -1;
    add_sound(TILE_CLICK, 0.125f);
}

void discard_current_tile(i8 player, int riichi_discard) {
    hand* cur_player_hand = &game_data.hands[player];

    int discard_idx = cur_player_hand->num_discards++;
    i8 selected_tile_idx = cur_player_hand->selected_tile_idx;
    cur_player_hand->discards[discard_idx] = cur_player_hand->tiles[selected_tile_idx];
    cur_player_hand->discard_rotated[discard_idx] = riichi_discard;
    game_data.last_discard = cur_player_hand->tiles[selected_tile_idx];
    game_data.last_discard_player = player;

    int handle = timer_get_handle("discard");
    cur_player_hand->discard_timer_handles[discard_idx] = handle;
    timer_start(handle, DISCARD_DURATION, 1, discard_finish_callback, ((deal_or_draw_finish_info){.player=player, .tile_idx=discard_idx}).raw);

    cur_player_hand->discard_from_hand_idx[discard_idx] = selected_tile_idx;

    for(int i = cur_player_hand->selected_tile_idx; i < cur_player_hand->num_closed_tiles-1; i++) {
        cur_player_hand->tiles[i] = cur_player_hand->tiles[i+1];
    }

    cur_player_hand->num_closed_tiles--;
}

void deal_timer_expired(u64 callback_data) {
    (void)callback_data;
    u8 player = (u8)callback_data;
    game_data.draw_state = DRAWN;
    hand* cur_player_hand = &game_data.hands[player];
    i8 cur_num_tiles = cur_player_hand->num_closed_tiles;
    cur_player_hand->draw_timer_handles[cur_num_tiles-1] = -1;
}

void draw_next_tile(int player) {
    hand* cur_player_hand = &game_data.hands[player];
    i8 cur_num_tiles = cur_player_hand->num_closed_tiles++;
    game_data.draw_state = DRAWING;

    cur_player_hand->draw_timer_handles[cur_num_tiles] = timer_get_handle("draw");
    timer_start(cur_player_hand->draw_timer_handles[cur_num_tiles], TILE_DEAL_DURATION, 1, deal_timer_expired, player);

    cur_player_hand->wall_index_for_tiles[cur_num_tiles] = --game_data.wall_rem;
    cur_player_hand->tiles[cur_num_tiles] = board_wall.tiles[game_data.wall_rem];
    //cur_player_hand->selected_tile_idx = cur_num_tiles;
}

int count_tile_in_hand(tile_type hand_tiles[], int num_tiles, tile_type search_tile) {
    int count = 0;
    for(int i = 0; i < num_tiles; i++) {
        count += (hand_tiles[i] == search_tile ? 1 : 0);
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
    tile_type round_wind = game_wind_to_tile_wind[game_data.cur_wind];
    tile_type seat_wind = player_winds[player];
    
    int winds_in_hand[4];
    for(int i = 0; i < 4; i++) {
        winds_in_hand[i] = count_tile_in_hand(h->tiles, h->num_closed_tiles, player_winds[i]);
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
        if(count_tile_in_hand(h->tiles, h->num_closed_tiles, dragons[i]) == 1) {
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
            if(count_tile_in_hand(h->tiles, h->num_closed_tiles, ttile+1) == 0 && count_tile_in_hand(h->tiles, h->num_closed_tiles, ttile+2) == 0) {
                return i;
            }
        }
        if(ttile == NINE_PIN || ttile == NINE_MAN || ttile == NINE_SOU) {
            if(count_tile_in_hand(h->tiles, h->num_closed_tiles, ttile-1) == 0 && count_tile_in_hand(h->tiles, h->num_closed_tiles, ttile-2) == 0) {
                return i;
            }
        }
    }

    if(h->num_closed_tiles <= 2) {
        return 0;
    }
    return h->num_closed_tiles-2;
}

int waiting_on_call_from(int player);

void run_ai_player(i8 player, hand* h, int this_player_turn) {

    if(game_data.ai_player_states[player] != WAITING) {
        return;    
    }

    player_type ai_type = game_data.player_types[player];

    game_data.ai_player_states[player] = COOLDOWN;
    reset_ai_player_state(player);

    if(!this_player_turn && ai_type == AGGRESSIVE_AI) {
        if(game_data.last_discard_player != -1 && game_data.last_discard_player != player) {
            queue_push(make_player_action(player, ATTEMPT_CALL));
            return;
        }
    }
    if(waiting_on_call_from(player)) {
       queue_push(make_player_action(player, (ai_type == AGGRESSIVE_AI) ? ATTEMPT_CALL : NO_ATTEMPT_CALL));
        return;
    }

    int best_discard = get_best_discard(player, h);
    if(this_player_turn && h->selected_tile_idx == best_discard) {
        if(h->num_closed_tiles + h->num_open_tiles == 14) {
            if(game_data.draw_state == DRAWN) {
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
        } else {
            if(h->selected_tile_idx > best_discard) {
                queue_push(make_player_action(player, MOVE_SELECTED_LEFT));
            } else if (h->selected_tile_idx < best_discard)  {
                queue_push(make_player_action(player, MOVE_SELECTED_RIGHT));
            }
        }
    }
}

const sound call_sounds[5] = {PON, CHII, RIICHI_SND, TSUMO, RON};

typedef enum {
    PON_CALL = 0,
    CHII_CALL = 1,
    RIICHI_CALL = 2,
    TSUMO_CALL = 3,
    RON_CALL = 4,
    NO_CALL
} call_type;


int copy_hand_tiles_to_tmp(hand* h, tile_type tmp[14]) {
    for(int t = 0; t < h->num_closed_tiles; t++) {
        tmp[t] = h->tiles[t];
    }
    for(int t = 0; t < h->num_open_tiles; t++) {
        tmp[h->num_closed_tiles+t] = h->open_tiles[t];
    }
    return h->num_closed_tiles + h->num_open_tiles;
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

int is_winning_hand_shape(tile_type hand_tiles[14]) {
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

int is_tanyao(tile_type hand_tiles[14]) {
    for(int i = 0; i < 14; i++) {
        if(is_honor_tile[hand_tiles[i]] || is_terminal_tile[hand_tiles[i]]) {
            return 0;
        }
    }
    return 1;
}

int is_chiitoitsu(tile_type hand_tiles[14]) {
    for(int i = 0; i < 14; i += 2) {
        if(hand_tiles[i] != hand_tiles[i+1]) {
            return 0;
        }
    }
    return 1;
}


#define MAX_HAN 13
// if a dealer wins

// tsumo vs ron (for tsumo all players pay in, otherwise just the called player)
const int dealer_score_tables[9][2] = {
    {  500,  1500},
    { 1000,  2900},
    { 2000,  5800},
    { 3900, 11600},
    { 4000, 12000},
    { 6000, 18000},
    { 8000, 24000},
    {12000, 36000},
    {16000, 48000}
};

const int nondealer_score_tables[9][3] = {
    {   300,   500,  1000},
    {   500,  1000,  2000},
    {  1000,  2000,  3900},
    {  2000,  3900,  7700},
    {  2000,  4000,  8000},
    {  3000,  6000, 12000},
    {  4000,  8000, 16000},
    {  6000, 12000, 24000},
    {  8000, 16000, 32000}
};

int get_score_table_index(int han) {
    if(han < 6) {
        return han-1;
    }
    if(han == 7) {
        return 5;
    }
    if(han < 11) {
        return 6;
    }
    if(han < 13) {
        return 7;
    }
    return 8;
}

int get_dealer_score(int han, int is_ron) {
    int idx = get_score_table_index(han);
    return dealer_score_tables[idx][is_ron];
}

typedef struct {
    int for_dealer;
    int for_non_dealer;
} non_dealer_score;


// get score for a non dealer winner.  is dealer parameter represents whether the loser is the dealer or not
int get_non_dealer_score(int han, int is_ron, int is_dealer) {
    int idx = get_score_table_index(han);
    int sub_idx = is_ron ? 2 : is_dealer ? 1 : 0;
    return nondealer_score_tables[idx][sub_idx];
}


void adjust_scores(int is_ron, int rond_player) {
    int han = 0;
    hand_score_modifiers mods = game_data.hand_winner_mods;

    for(int i = 0; i < mods.num_mods; i++) {
        switch(mods.mods[i]) {
            case KOKUSHI_MUSOU:
                han += 13;
                break;
            case CHIITOITSU:
                han += 2;
                break;
            case PINFU:
            case RIICHI:
            case IPPATSU:
            case TSUMO:
            case TANYAO:
            case YAKUHAI:
            case DORA:
            case AKA_DORA:
            case URA_DORA:
                han += 1;
                break;
            default:
                break;
        }
    }

    int score_added = 0;
    int winner_is_dealer = game_data.hand_winner == game_data.cur_dealer;
    for(int i = 0; i < 4; i++) {
        if(game_data.hand_winner == i) {
            continue;
        }
        int is_dealer = game_data.cur_dealer == i;
        if (is_ron) {
            if(i != rond_player) {
                continue;
            }
            int this_score = winner_is_dealer ? get_dealer_score(han, is_ron) : get_non_dealer_score(han, is_ron, is_dealer);
            game_data.hands[i].score -= this_score;
            score_added += this_score;
        } else {
            int this_score = winner_is_dealer ? get_dealer_score(han, is_ron) : get_non_dealer_score(han, is_ron, is_dealer);
            game_data.hands[i].score -= this_score;
            score_added += this_score;
        }
        
    }
    game_data.hands[game_data.hand_winner].score += score_added;

}


// returns 1 if it's a winning hand
// writes score modifiers
int calc_winning_score_modifiers(hand_score_modifiers *mods, tile_type hand_tiles[14], int is_tsumo, int is_closed, int in_riichi, tile_type dora, tile_type round_wind, tile_type seat_wind) {
    mods->num_mods = 0;
    if(is_chiitoitsu(hand_tiles)) {
        return 1;
    }
    if(0) { // is_kokushi_musou(hand_tiles)) {
        return 1;
    }

    if(!is_winning_hand_shape(hand_tiles)) {
        return 0;
    }

    int winning = 0;
    if(in_riichi) {
        mods->mods[mods->num_mods++] = RIICHI;
        winning = 1;
    }

    int num_dora = count_tile_in_hand(hand_tiles, 14, dora);
    for(int i = 0; i < num_dora; i++) {
        mods->mods[mods->num_mods++] = DORA;
    }

    if(is_tsumo && is_closed) {
        mods->mods[mods->num_mods++] = MENZEN_TSUMO;
    }

    if(is_tanyao(hand_tiles)) {
        mods->mods[mods->num_mods++] = TANYAO;
        winning = 1;
    }

    tile_type dragons[3] = { RED_DRAGON, GREEN_DRAGON, WHITE_DRAGON };
    for(int i = 0; i < 3; i++) {
        if(count_tile_in_hand(hand_tiles, 14, dragons[i]) >= 3) {
            mods->mods[mods->num_mods++] = YAKUHAI;
            winning = 1;
        }
    }
    if(count_tile_in_hand(hand_tiles, 14, round_wind) >= 3) {
        mods->mods[mods->num_mods++] = YAKUHAI;
        winning = 1;
    }
    if(count_tile_in_hand(hand_tiles, 14, seat_wind) >= 3) {
        mods->mods[mods->num_mods++] = YAKUHAI;
        winning = 1;
    }


    return winning;
}


int is_win(hand_score_modifiers *mods, hand* h, int player, int is_tsumo) {
    
    tile_type tmp[14];
    copy_hand_tiles_to_tmp(h, tmp);
    // sort open and closed tiles together
    sort_tile_list(tmp, h->num_closed_tiles+h->num_open_tiles, 0);
    return calc_winning_score_modifiers(mods, tmp, is_tsumo, (h->num_open_tiles == 0), h->in_riichi, game_data.dora_tile, game_wind_to_tile_wind[game_data.cur_wind], player_winds[player]);
}

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

typedef struct {
    call_type call;
    int index1, index2;
} call_info;

call_info can_call(int player, hand_score_modifiers *mods) {
    
    call_info call_res; call_res.call = NO_CALL;

    hand* cur_hand = &game_data.hands[player];
    tile_type prev_discard = game_data.last_discard;

    tile_type tmp_full_hand[14];
    copy_hand_tiles_to_tmp(cur_hand, tmp_full_hand);
    if(cur_hand->num_closed_tiles + cur_hand->num_open_tiles != 13) {
        exotique_printf("!!!EXPECTED 13 TILES, KAN OR BUG?\n");
        exit(1);
    }

    tmp_full_hand[13] = prev_discard;
    if(is_win(mods, cur_hand, player, 0)) {
        call_res.call = RON_CALL;
        return call_res;
    }

    int prev_player = player-1;
    if(prev_player == -1) {
        prev_player = 3;
    }
    int can_chii = game_data.last_discard_player == prev_player;

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

    // TODO: this is currently not allowing KAN because those would crash the game
    // however, it should be a different call type and handled separately
    // eg open kan, closed kan
    if(pon_cnt == 2 && count_tile_in_hand(tmp_full_hand, cur_hand->num_closed_tiles, prev_discard) == 2) {
        call_res.index1 = pon_index1;
        call_res.index2 = pon_index2;
        call_res.call = PON_CALL;
        return call_res;
    }

    if(can_chii && got_ll && got_l) {
        call_res.index1 = ll_index;
        call_res.index2 = l_index;
        call_res.call = CHII_CALL;
        return call_res;
    }
    if(can_chii && got_l && got_r) {
        call_res.index1 = l_index;
        call_res.index2 = r_index;
        call_res.call = CHII_CALL;
        return call_res;
    }
    if(can_chii && got_r && got_rr) {
        call_res.index1 = r_index;
        call_res.index2 = rr_index;
        call_res.call = CHII_CALL;
        return call_res;
    }

    return call_res;
}

call_type attempt_call(int player, hand_score_modifiers *mods) {
    
    call_info call = can_call(player, mods);
    if(call.call == RON_CALL) {
        return RON_CALL;
    } else if(call.call == NO_CALL) {
        return NO_CALL;
    }

    // chii or pon

    //return call.call_type;

    hand* prev_hand = &game_data.hands[game_data.last_discard_player];
    hand* cur_hand = &game_data.hands[player];
    tile_type prev_discard = prev_hand->discards[prev_hand->num_discards-1];


    if(call.index1 < call.index2) { call.index2--; }
    if(call.call == PON_CALL) {
        copy_tile_index_to_open(cur_hand, call.index1);
        copy_tile_index_to_open(cur_hand, call.index2);
        cur_hand->open_tiles[cur_hand->num_open_tiles] = prev_discard;
        cur_hand->open_tile_rotated[cur_hand->num_open_tiles++] = 1;
        prev_hand->num_discards--;
    }

    if(call.call == CHII_CALL) {
        copy_tile_index_to_open(cur_hand, call.index1);
        copy_tile_index_to_open(cur_hand, call.index2);
        cur_hand->open_tiles[cur_hand->num_open_tiles] = prev_discard;
        cur_hand->open_tile_rotated[cur_hand->num_open_tiles++] = 1;
        prev_hand->num_discards--;
    }
    return call.call;
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
    return;
    int dst_idx = get_index_to_move_called_tile_to(caller, callee) + h->num_open_tiles - 3;
    //int src_idx = 
    while(!h->open_tile_rotated[dst_idx]) {
        // rotate everything
        rotate_last_three_open_tiles(h);
    }
}

int in_tenpai_for_discard_internal(hand* h, int player, int discard_idx) {

    tile_type tmp[14];
    int tiles_in_hand = copy_hand_tiles_to_tmp(h, tmp);
    if(tiles_in_hand < 14) {
        return 0;
    }
    // copy into a temporary hand
    tile_type og_tile = tmp[discard_idx];

    for(tile_type new_tile = 0; new_tile < NUM_TILES; new_tile++) {
        tmp[h->selected_tile_idx] = new_tile;
        if(is_win(&game_data.hand_winner_mods, h, player, 1)) {
            return 1;
        }
    }
    tmp[h->selected_tile_idx] = og_tile;
    return 0;
}

int in_tenpai_for_discard(hand *h, int player) {
    return in_tenpai_for_discard_internal(h, player, h->selected_tile_idx);
}

int in_tenpai(hand* h, int player) {
    
    tile_type tmp[14];
    copy_hand_tiles_to_tmp(h, tmp);


    for(int discard = 0; discard < 14; discard++) {
        return in_tenpai_for_discard_internal(h, player, discard);
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

    if(in_tenpai_for_discard(cur_hand, player)) {
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

void switch_player_callback(u64 callback_data) {
    (void)callback_data;
    game_data.cur_player = (game_data.cur_player+1)&3;
    game_data.draw_state = UNDRAWN;
    game_data.switch_player_timer_handle = -1;
}

void switch_player() {
    int handle = timer_get_handle("switch player");
    game_data.switch_player_timer_handle = handle;
    timer_start(handle, SWITCH_PLAYER_DURATION, 1, switch_player_callback, 0);
}


void show_winning_hand_finish_callback(u64 callback_data) {
    (void)callback_data;
    reset_game();
}

void show_winning_hand() {
    game_data.cur_game_state = SHOW_WINNING_HAND;
    int handle = timer_get_handle("show winning hand");
    timer_start(handle, SHOW_SCORE_DURATION, 1, show_winning_hand_finish_callback, 0);
}


int waiting_on_call_from(int player) {
    for(int i = 0; i < game_data.waiting_for_calls; i++) {
        if(game_data.waiting_for_calls_players[i] == player) {
            return 1;
        }
    }
    return 0;
}

void remove_player_from_waiting_on_call_list(int player) {
    if(game_data.waiting_for_calls == 0) {
        exotique_printf("Bug when trying to remove player %i from call list, list is empty\n", player);
        exit(1);
    }
    int idx = -1;
    for(int i = 0; i < game_data.waiting_for_calls; i++) {
        if(game_data.waiting_for_calls_players[i] == player) {
            idx = i;
            break;
        }
    }
    if(idx == -1) {
        exotique_printf("Bug when trying to remove player from call wait list\n", player);
        //return;
        exit(1);
    }

    game_data.waiting_for_calls_players[idx] = game_data.waiting_for_calls_players[game_data.waiting_for_calls-1];
    game_data.waiting_for_calls--;
}

void add_player_to_waiting_on_call_list(int player) {
    for(int i = 0; i < game_data.waiting_for_calls; i++) {
        if(game_data.waiting_for_calls_players[i] == player) {
            exotique_printf("Bug when trying to add player %i to waiting for call list, already in list\n", player);
            exit(1);
        }
    }
    game_data.waiting_for_calls_players[game_data.waiting_for_calls++] = player;
    if(game_data.waiting_for_calls > 4) {
        exotique_printf("Bug when adding player to waiting for call list, have %i players!\n", game_data.waiting_for_calls);
        exit(1);
    }
}


int step_frame = 0;
char* step_reason = NULL;
void advance_frame(const char* reason) {
    exotique_printf("advancing frame to %i after %s\n", ++game_data.sim_frame, reason);
    step_frame = 0;
}

void stage_advance_frame(const char* reason) {
    if(step_frame) {
        exotique_printf("TRYING TO ADVANCE FRAME TWICE!\n");
        exit(1);
    }
    step_frame = 1;
    step_reason = (char *)reason;
}

int player_can_perform_action_and_is_in_draw_state(int player_num, tile_draw_state draw_state) {
    int switching = (game_data.switch_player_timer_handle != -1);
    int this_players_turn = game_data.cur_player == player_num;

    if(!this_players_turn) {
        return 0;
    }
    if(switching) {
        return 0;
    }
    return (game_data.draw_state == draw_state);
}

int player_can_discard(int player_num) {
    return player_can_perform_action_and_is_in_draw_state(player_num, DRAWN);
}

int player_can_draw(int player_num) {
    return player_can_perform_action_and_is_in_draw_state(player_num, UNDRAWN);
}

block run_game(ExotiqueInterface *ei, block whole_frame_block) {
    int pushed_y = ei->input->y && !last_y_pushed;
    int pushed_x = ei->input->x && !last_x_pushed;
    int pushed_a = ei->input->a && !last_a_pushed;
    int pushed_b = ei->input->b && !last_b_pushed;


    int human_can_draw = player_can_draw(human_player);
    int human_can_discard = player_can_discard(human_player);
    int human_can_riichi_or_tsumo = human_can_discard;

    int send_input = 0;
    if(waiting_on_call_from(human_player)) {
        // waiting on a call
        // only allow X or B
        if(pushed_x) {
            queue_push(make_player_action(human_player, ATTEMPT_CALL));
            send_input = 1;  
        } else if(pushed_b) {
            queue_push(make_player_action(human_player, NO_ATTEMPT_CALL));
            send_input = 1;  
        } else if (pushed_y) {
            queue_push(make_player_action(human_player, SORT_HAND));
            send_input = 1;
            //sort_hand(human_player);
        }
    } else {
        // not waiting, don't allow CALLs
        if(ei->input->left && !last_left_pushed) {
            queue_push(make_player_action(human_player, MOVE_SELECTED_LEFT));
            send_input = 1;  
        } else if (ei->input->right && !last_right_pushed) {
            queue_push(make_player_action(human_player, MOVE_SELECTED_RIGHT));
            send_input = 1;  
        } else if(pushed_a && human_can_draw) {
            queue_push(make_player_action(human_player, ATTEMPT_DRAW));
            send_input = 1;  
        } else if(pushed_b && human_can_discard) {
            queue_push(make_player_action(human_player, ATTEMPT_DISCARD));
            send_input = 1;  
        } else if (pushed_x && human_can_riichi_or_tsumo) {
            queue_push(make_player_action(human_player, ATTEMPT_RIICHI_OR_WIN));
            send_input = 1;  
        } else if (pushed_y) {
            queue_push(make_player_action(human_player, SORT_HAND));
            send_input = 1;
            //sort_hand(human_player);
        }
    }

    static block network_send_block = START_TIMED_BLOCK(network_send_block, "send input", whole_frame_block)
    if(send_input) {
        client_send_input_to_all_clients();
    }
    END_TIMED_BLOCK(network_send_block);

    static block network_receive_block = START_TIMED_BLOCK(network_receive_block, "recv inputs", whole_frame_block)
        client_wait_for_all_inputs();
    END_TIMED_BLOCK(network_receive_block);

    // all clients run AI
    for(i8 i = 0; i < 4; i++) {
        if(game_data.player_types[i] != HUMAN && game_data.player_types[i] != NETWORK_HUMAN) {
            run_ai_player(i, &game_data.hands[i], (game_data.cur_player == i));
        }
    }

    
    if(game_data.waiting_for_calls) {
        while(!queue_empty() && game_data.waiting_for_calls) {
            player_action action = queue_peek();
            i8 this_player = action.player_num;
            hand *player_hand = &game_data.hands[action.player_num];
            // we can execute MOVE_SELECTED_LEFT and MOVE_SELECTED_RIGHT packets, but everything else is ignored for now.
            // however, we still have to verify sim frames!
            if(action.sim_frame > game_data.sim_frame) {
                exotique_printf("NEXT EVENT IS AHEAD OF OUR CURRENT SIM FRAME (THEIRS %i, OURS %i), BAILING OUT OF EVENT LOOP FOR NOW\n", action.sim_frame, game_data.sim_frame);
                break;
            }
            switch(action.cmd) {
                case MOVE_SELECTED_LEFT:
                    queue_pop();
                    player_hand->selected_tile_idx--;
                    fixup_selected_idx(player_hand); 
                    add_sound(TILE_CLICK, 0.085f); 
                    break;
                case MOVE_SELECTED_RIGHT:
                    queue_pop();
                    player_hand->selected_tile_idx++;
                    fixup_selected_idx(player_hand);
            
                    add_sound(TILE_CLICK, 0.085f);
                    break;

                case SORT_HAND:
                    queue_pop();
                    sort_hand(this_player);
                    break;

                case NO_ATTEMPT_CALL:
                    queue_pop();
                    if(!waiting_on_call_from(action.player_num)) {
                        // ignore these from players other than who we're waiting for
                        continue;
                    }
                    // remove this player from the list of waiting players
                    remove_player_from_waiting_on_call_list(action.player_num);
                    if(game_data.waiting_for_calls == 0) {
                        switch_player_callback(0);
                        stage_advance_frame("no calls made");
                    }
                    break;
                case ATTEMPT_CALL:
                    queue_pop();
                    if(!waiting_on_call_from(action.player_num)) {
                        // discard calls in this sim_frame from players we're not waiting for
                        // since they are totally invalid
                        continue;
                    }
                    // now we can process the call (if applicable)

                    // well uh, we KNOW that a call from this player should be valid, since we've already CHECKED
                    call_info call = can_call(this_player, &game_data.hand_winner_mods);

                    
                    if(call.call == NO_CALL) {
                        exotique_printf("GOT CALL FROM PLAYER WHO WE'RE WAITING ON, BUT CANNOT CALL DUE TO BUG?!\n");
                        exit(1);
                    }
                    game_data.waiting_for_calls = 0;   
                    add_sound(call_sounds[call.call], 0.125f); //location 
                    
                    timer_release(game_data.switch_player_timer_handle);
                    game_data.switch_player_timer_handle = -1;
                    
                    if(call.call == RON_CALL) {
                        game_data.hand_winner = this_player;
                        adjust_scores(1, game_data.last_discard_player);
                        show_winning_hand();
                    } else {
                        attempt_call(this_player, &game_data.hand_winner_mods);

                        sort_last_three_open_tiles_based_on_player_order(player_hand, this_player, game_data.last_discard_player);
                        fixup_selected_idx(&game_data.hands[this_player]);
                        game_data.cur_player = this_player;

                        stage_advance_frame("call made");
                        if(step_frame) {
                            advance_frame(step_reason);
                        }
                    }
                    // preempt all other players
                    // sorry, but high ping should not inconvenience other players :)                        
                    return whole_frame_block;
                
                default:
                    //goto exit_loop;    
                    //break;
                    queue_pop();
            }
        }

    } else {


        while(!queue_empty()) {
             player_action action = queue_peek();
            if(action.sim_frame > game_data.sim_frame) {
                exotique_printf("NEXT EVENT IS AHEAD OF OUR CURRENT SIM FRAME (THEIRS %i, OURS %i), BAILING OUT OF EVENT LOOP FOR NOW\n", action.sim_frame, game_data.sim_frame);
                break;
            }

            i8 this_player = action.player_num;

            if(action.sim_frame < game_data.sim_frame) {
                exotique_printf("Ignoring old input!\n");
                queue_pop();
                continue;
                //exotique_printf("RECEIVED OLD INPUT, MUST ADD SYNCHRONIZATION BARRIERS! (theirs %i, ours %i)\n", action.sim_frame, game_data.sim_frame);
                //exit(1);
            }

            queue_pop();

            
            // if switching, we can do a call, but nothing else
            int can_draw = player_can_draw(this_player);
            int can_discard = player_can_discard(this_player);
            int can_riichi_or_tsumo = can_discard;

            hand *player_hand = &game_data.hands[action.player_num];

            switch(action.cmd) {
                case MOVE_SELECTED_LEFT:
                    player_hand->selected_tile_idx--;
                    fixup_selected_idx(player_hand); 
                    add_sound(TILE_CLICK, 0.085f); 
                    break;
                case MOVE_SELECTED_RIGHT:
                    player_hand->selected_tile_idx++;
                    fixup_selected_idx(player_hand);
            
                    add_sound(TILE_CLICK, 0.085f);
                    break;

                case SORT_HAND:
                    sort_hand(this_player);
                    break;

                case ATTEMPT_DRAW:

                    if(!can_draw) {
                        break;
                    }

                    if(game_data.wall_rem == 14) {
                        show_winning_hand();

                        return whole_frame_block;
                    }
                    draw_next_tile(action.player_num);
                    stage_advance_frame("draw made");
                    break;
                case ATTEMPT_DISCARD:
                    if(!can_discard) {
                        break;
                    }

                    discard_current_tile(game_data.cur_player, 0);
                    stage_advance_frame("discard made");
                    if(player_hand->selected_tile_idx >= player_hand->num_closed_tiles) {
                        player_hand->selected_tile_idx = player_hand->num_closed_tiles-1;
                    }

                    for(int i = 0; i < 4; i++) {

                        if(game_data.player_types[i] != HUMAN) {
                            reset_ai_player_state(i);
                        }
                        // don't wait for player that just discarded
                        if(i == game_data.last_discard_player) {
                            continue;
                        }
                        call_info call = can_call(i, &game_data.hand_winner_mods);
                        if(call.call != NO_CALL) {
                            add_player_to_waiting_on_call_list(i);
                        }
                    }
                    
                    if(game_data.waiting_for_calls == 0) {
                        switch_player();
                    } else {
                        
                        exotique_printf("waiting for calls from: \n");
                        for(int i = 0; i < game_data.waiting_for_calls; i++) {
                            exotique_printf("%i\n", game_data.waiting_for_calls_players[i]);
                        }
                        return whole_frame_block;
                    }
                    break;
                case ATTEMPT_RIICHI_OR_WIN: 
                    if(!can_riichi_or_tsumo) {
                        break;
                    }

                    if(is_win(&game_data.hand_winner_mods, player_hand, this_player, 1)) {
                        exotique_printf("TSUMO WIN !\n");
                        add_sound(TSUMO, 0.25f);
                        game_data.hand_winner = this_player;
                        adjust_scores(0, -1);
                        show_winning_hand();
                        return whole_frame_block;
                    } else if (attempt_riichi(this_player)) {             
                        player_hand->in_riichi = 1;
                        add_sound(RIICHI_SND, 0.125f);
                        discard_current_tile(this_player, 1);
                        stage_advance_frame("riichi discard made");
                        switch_player();
                        for(int i = 0; i < 4; i++) {
                            if(game_data.player_types[i] != HUMAN) {
                                reset_ai_player_state(i);
                            }
                        }
                    }
                    break;
                default:
                    exotique_printf("Unhandled event type %i\n", action.cmd);
                    break;
            }
        }
    }
    
    if(step_frame) {
        advance_frame(step_reason);
    }
    return whole_frame_block;
}

vert3f orbit_camera_position(float yaw, float pitch, float radius) {
    float cp = cosf(pitch);

    return (vert3f){sinf(yaw) * cp * radius, sinf(pitch) * radius, cosf(yaw) * cp * radius};
}

void look_at_yx(transform *cam, vert3f position, vert3f target) {
    float dx = target.x - position.x;
    float dy = target.y - position.y;
    float dz = target.z - position.z;

    float horizontal = my_sqrt(dx * dx + dz * dz);
    cam->rotation = (vert3f){-fast_atan2(dy, horizontal), fast_atan2(dx, dz), 0.0f};
    cam->position = position;
}

void game_update(ExotiqueInterface* ei) {

    timer_step(exotique_get_ticks());
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

        //vert3f world_light = {0, 1, 0};
        //matrix view_matrix = transform_to_view_matrix(&cam_view_trans);
        //light = mat_mul_normal(&view_matrix, &world_light);
        

        vert3f forward = {0, 0, -1};

        //matrix rx = rotation_x_matrix(.95f);
        //matrix ry = rotation_y_matrix(cam_view_trans.rotation.y);
        //matrix rot = mat_mul_mat(&ry, &rx); // inverse of your view rotation

        //light = normalize(mat_mul_vert3(&rot, &forward));
        light = forward;


        
        switch(game_data.cur_game_state) {
            case INITIAL_SHUFFLE_AND_SETUP:
                step_shuffle_and_setup(&board_wall);
                break;
            case DEALING:
                step_deal();
                break;
            case IN_GAME:
                whole_frame_block = run_game(ei, whole_frame_block);
                break;
            case SHOW_WINNING_HAND:
                //show_winning_hand();
                break;
            default:
            case STARTUP:
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