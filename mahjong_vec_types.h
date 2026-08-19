#ifndef MAHJONG_VEC_TYPES
#define MAHJONG_VEC_TYPES

#define VEC_LANES 4

typedef u16 u16_vec __attribute__((vector_size(2*VEC_LANES)));
typedef f32 f32_vec __attribute__((vector_size(4*VEC_LANES)));
typedef i32 i32_vec __attribute__((vector_size(4*VEC_LANES)));

static inline f32_vec broadcast_f32_vec(f32 a) {
    return (f32_vec){ a, a, a, a};
}


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

static inline u16_vec i32_mask_vec_to_u16_vec(i32_vec v) {
    return (u16_vec){v[0]&0xFFFF, v[1]&0xFFFF, v[2]&0xFFFF, v[3]&0xFFFF};
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
u32 u16_vec_extract_bytes(const u16_vec a) {
    u32 res = 0;
    for(int i = 0; i < 4; i++) {
        res |= ((u32)(a[i] ? 0xFF : 0x00)<<(i*8));
    }
    return res;
}


#endif