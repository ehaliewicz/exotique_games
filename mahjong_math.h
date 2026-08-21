#ifndef MAHJONG_MATH_H
#define MAHJONG_MATH_H

//  DATA TYPES, SCALAR AND VERTEX/VECTOR MATH


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


f32 absf(f32 a) { 
    if(a < 0.0f) { return -a; }
    return a;
}


static inline  f32_vec lerp_f32_vec(f32_vec a, f32_vec b, f32_vec mix) {
    return a + ((b-a) * mix);
}

static inline f32_vec dot_batch_single(const f32_vec a_comps[3], const vert3f b) {
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


i32_vec f32_vec_floor(f32_vec a) {    
    i32_vec i = (i32_vec){(i32)a[0], (i32)a[1], (i32)a[2], (i32)a[3]};
    f32_vec ii = (f32_vec){(f32)i[0], (f32)i[1], (f32)i[2], (f32)i[3]};

    return i - (ii > a);
}


#endif 