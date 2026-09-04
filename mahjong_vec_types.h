#ifndef MAHJONG_VEC_TYPES
#define MAHJONG_VEC_TYPES

#define VEC_LANES 4

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

static inline u64 encode_float_inv_z_vec(f32_vec inv_z) {
    f32_vec scaled = (inv_z*65536.0f);
    u64 res = (
        (((u64)((u16)scaled[0]))<<0) |
        (((u64)((u16)scaled[1]))<<16) |
        (((u64)((u16)scaled[2]))<<32) |
        (((u64)((u16)scaled[3]))<<48)
    );
    return res;
}
static inline u64 encode_float_inv_z_vec_sse2(__m128 inv_z) {
    __m128 scaled = _mm_mul_ps(inv_z, _mm_set1_ps(65536.0f)); // we now have 4 32-bit floats scaled into a 0->65536 range
    __m128i scaled_ints = _mm_cvtps_epi32(scaled); // now 4 32-bit integers in the same range

    // now we want to grab the low 2 bytes of each value
    __m128i low_words_mask = _mm_setr_epi8(0, 1, 4, 5, 8, 9, 12, 13, -1, -1, -1, -1, -1, -1, -1, -1);
    return (u64)_mm_cvtsi128_si64(_mm_shuffle_epi8(scaled_ints, low_words_mask));
}

static inline f32_vec decode_u64_inv_z_vec(u64 inv_z) {
    f32_vec scaled = (f32_vec) {
        (f32)((inv_z>>0)&0xFFFF),
        (f32)((inv_z>>16)&0xFFFF),
        (f32)((inv_z>>32)&0xFFFF),
        (f32)((inv_z>>48)&0xFFFF)
    };
    return scaled / 65536.0f;
}

static inline __m128 decode_u64_inv_z_vec_sse2(u64 inv_z) {
    __m128 recip_divisor = _mm_set1_ps(1.0f/65536.0f);
    __m128 scaled = _mm_setr_ps(
        (f32)(inv_z&0xFFFF), 
        (f32)((inv_z>>16)&0xFFFF), 
        (f32)((inv_z>>32)&0xFFFF), 
        (f32)((inv_z>>48)&0xFFFF)
    );
    return _mm_mul_ps(scaled, recip_divisor);
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



#endif