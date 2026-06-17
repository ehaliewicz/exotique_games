#include "exotique.h"

#include <stdio.h>

#define WIDTH 900
#define HEIGHT 900
#define WIDTH_DIV_2 (WIDTH/2)
#define HEIGHT_DIV_2 (HEIGHT/2)
const int kScreenWidth  = WIDTH;
const int kScreenHeight = HEIGHT;

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

#define NUM_COLORS 8
#define NUM_SHADES 16
u8 init_colors[NUM_COLORS][3] = {
    {0xFF, 0xFF, 0xFF}, /* white */
    {0x00, 0xFF, 0xFF}, /* cyan */
    {0xFF, 0xFF, 0x00}, /* yellow */
    {0x80, 0x00, 0x80}, /* purple */
    {0x00, 0xFF, 0x00}, /* green */
    {0xFF, 0x00, 0x00}, /* red */
    {0x00, 0x00, 0xFF}, /* blue */
    {0xFF, 0x7F, 0x00}  /* orange */
};



typedef enum {
    NO_ROTATION, 
    X_ROTATION, 
    Y_ROTATION, 
    Z_ROTATION
} rotation;

typedef enum {
    NO_MOVE,
    MOVE_Y_DOWN,
    MOVE_Y_DOWN_2,
    MOVE_Y_UP,
    MOVE_Y_UP_2
} interp_move;

const char * rotation_names[] = {
    "no", "x", "y", "z"
};

int cur_tetro_matrix[16];
f32 temp_rot_matrix[16];
f32 interp_tetro_matrix[16];
int cur_rotate_timer = -1; 
rotation cur_rotation = NO_ROTATION;
interp_move cur_interp_move = NO_MOVE;


#define ROTATE_FRAMES 31

void swap_matrix_rows(int *row_a, int* row_b) {
    int tmp;
    int i;
    for(i = 0; i < 4; i++) {
        tmp = row_a[i];
        row_a[i] = row_b[i];
        row_b[i] = tmp;
    }
}
void negate_row(int *row) {
    int i;
    for(i = 0; i < 4; i++) {
        row[i] = -row[i];
    }
}

void set_identity_matrix() {
    int idx;
    for(idx = 0; idx < 16; idx++) {
        cur_tetro_matrix[idx] = 0;
    }
    cur_tetro_matrix[0] = 1;
    cur_tetro_matrix[5] = 1;
    cur_tetro_matrix[10] = 1;
    cur_tetro_matrix[15] = 1;
}

void copy_matrix_to_interp_matrix() {
    int idx;
    for(idx = 0; idx < 16; idx++) {
        interp_tetro_matrix[idx] = (f32)cur_tetro_matrix[idx];
    }
}

void rotate_matrix_x() {
    swap_matrix_rows(cur_tetro_matrix+4, cur_tetro_matrix+8);
    negate_row(cur_tetro_matrix+4);
}

void rotate_matrix_y() {
    /* positive x becomes negative -z
       z becomes x? */
    swap_matrix_rows(cur_tetro_matrix+0, cur_tetro_matrix+8);
    negate_row(cur_tetro_matrix+8);
}

void rotate_matrix_z() {
    /* x becomes -y 
       y becomes x   
    */
    swap_matrix_rows(cur_tetro_matrix+0, cur_tetro_matrix+4);
    negate_row(cur_tetro_matrix);
}

void multiply_vector(int *x, int *y, int *z) {
    int ox = *x;
    int oy = *y;
    int oz = *z;
    int ow = 1;

    *x = cur_tetro_matrix[0]*ox + cur_tetro_matrix[1]*oy + cur_tetro_matrix[2]*oz + cur_tetro_matrix[3]*ow;
    *y = cur_tetro_matrix[4]*ox + cur_tetro_matrix[5]*oy + cur_tetro_matrix[6]*oz + cur_tetro_matrix[7]*ow;
    *z = cur_tetro_matrix[8]*ox + cur_tetro_matrix[9]*oy + cur_tetro_matrix[10]*oz + cur_tetro_matrix[11]*ow;
}

void multiply_vector_by_interp_mat(f32 *x, f32 *y, f32 *z) {
    f32 ox = *x;
    f32 oy = *y;
    f32 oz = *z;
    f32 ow = 1.0f;

    *x = interp_tetro_matrix[0]*ox + interp_tetro_matrix[1]*oy + interp_tetro_matrix[2]*oz + interp_tetro_matrix[3]*ow;
    *y = interp_tetro_matrix[4]*ox + interp_tetro_matrix[5]*oy + interp_tetro_matrix[6]*oz + interp_tetro_matrix[7]*ow;
    *z = interp_tetro_matrix[8]*ox + interp_tetro_matrix[9]*oy + interp_tetro_matrix[10]*oz + interp_tetro_matrix[11]*ow;
}



void multiply_matrixes(f32 *A, int *B, f32 *C) {
    int row, col;
    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            C[row * 4 + col] =
                A[row * 4 + 0] * (f32)B[0 * 4 + col] +
                A[row * 4 + 1] * (f32)B[1 * 4 + col] +
                A[row * 4 + 2] * (f32)B[2 * 4 + col] +
                A[row * 4 + 3] * (f32)B[3 * 4 + col];
        }
    }
}

void x_rotate_interp_matrix(f32 radians) {
    f32 c = cosf(radians);
    f32 s = sinf(radians);

    temp_rot_matrix[0] = 1.0f;
    temp_rot_matrix[1] = 0.0f;
    temp_rot_matrix[2] = 0.0f;
    temp_rot_matrix[3] = 0.0f;

    temp_rot_matrix[4] = 0.0f;
    temp_rot_matrix[5] = c;
    temp_rot_matrix[6] = -s;
    temp_rot_matrix[7] = 0.0f;

    
    temp_rot_matrix[8] = 0.0f;
    temp_rot_matrix[9] = s;
    temp_rot_matrix[10] = c;
    temp_rot_matrix[11] = 0.0f;
    
    temp_rot_matrix[12] = 0.0f;
    temp_rot_matrix[13] = 0.0f;
    temp_rot_matrix[14] = 0.0f;
    temp_rot_matrix[15] = 1.0f;
    

    multiply_matrixes(temp_rot_matrix, cur_tetro_matrix, interp_tetro_matrix);
    /*
    f32 m0 = (f32)cur_tetro_matrix[0],  m1 = (f32)cur_tetro_matrix[1],  m2 = (f32)cur_tetro_matrix[2];
    f32 m4 = (f32)cur_tetro_matrix[4],  m5 = (f32)cur_tetro_matrix[5],  m6 = (f32)cur_tetro_matrix[6];
    f32 m8 = (f32)cur_tetro_matrix[8],  m9 = (f32)cur_tetro_matrix[9],  m10 = (f32)cur_tetro_matrix[10];

    interp_tetro_matrix[0] = m0;
    interp_tetro_matrix[1] = m1 * c + m2 * s;
    interp_tetro_matrix[2] = -m1 * s + m2 * c;

    interp_tetro_matrix[4] = m4;
    interp_tetro_matrix[5] = m5 * c + m6 * s;
    interp_tetro_matrix[6] = -m5 * s + m6 * c;

    interp_tetro_matrix[8]  = m8;
    interp_tetro_matrix[9]  = m9 * c + m10 * s;
    interp_tetro_matrix[10] = -m9 * s + m10 * c;
    */
}

void y_rotate_interp_matrix(f32 radians)
{
    f32 c = cosf(radians);
    f32 s = sinf(radians);

    temp_rot_matrix[0] = c;
    temp_rot_matrix[1] = 0.0f;
    temp_rot_matrix[2] = s;
    temp_rot_matrix[3] = 0.0f;

    temp_rot_matrix[4] = 0.0f;
    temp_rot_matrix[5] = 1.0f;
    temp_rot_matrix[6] = 0.0f;
    temp_rot_matrix[7] = 0.0f;

    temp_rot_matrix[8] = -s;
    temp_rot_matrix[9] = 0.0f;
    temp_rot_matrix[10] = c;
    temp_rot_matrix[11] = 0.0f;
    
    temp_rot_matrix[12] = 0.0f;
    temp_rot_matrix[13] = 0.0f;
    temp_rot_matrix[14] = 0.0f;
    temp_rot_matrix[15] = 1.0f;


    multiply_matrixes(temp_rot_matrix, cur_tetro_matrix, interp_tetro_matrix);
    /*
    f32 m0 = (f32)cur_tetro_matrix[0],  m1 = (f32)cur_tetro_matrix[1],  m2 = (f32)cur_tetro_matrix[2];
    f32 m4 = (f32)cur_tetro_matrix[4],  m5 = (f32)cur_tetro_matrix[5],  m6 = (f32)cur_tetro_matrix[6];
    f32 m8 = (f32)cur_tetro_matrix[8],  m9 = (f32)cur_tetro_matrix[9],  m10 = (f32)cur_tetro_matrix[10];

    interp_tetro_matrix[0] = m0 * c - m2 * s;
    interp_tetro_matrix[1] = m1;
    interp_tetro_matrix[2] = m0 * s + m2 * c;

    interp_tetro_matrix[4] = m4 * c - m6 * s;
    interp_tetro_matrix[5] = m5;
    interp_tetro_matrix[6] = m4 * s + m6 * c;

    interp_tetro_matrix[8]  = m8 * c - m10 * s;
    interp_tetro_matrix[9]  = m9;
    interp_tetro_matrix[10] = m8 * s + m10 * c;
    */
}

void z_rotate_interp_matrix(f32 radians)
{
    f32 c = cosf(radians);
    f32 s = sinf(radians);

    temp_rot_matrix[0] = c;
    temp_rot_matrix[1] = -s;
    temp_rot_matrix[2] = 0.0f;
    temp_rot_matrix[3] = 0.0f;

    temp_rot_matrix[4] = s;
    temp_rot_matrix[5] = c;
    temp_rot_matrix[6] = 0.0f;
    temp_rot_matrix[7] = 0.0f;

    temp_rot_matrix[8] = 0.0;
    temp_rot_matrix[9] = 0.0f;
    temp_rot_matrix[10] = 1.0f;
    temp_rot_matrix[11] = 0.0f;
    
    temp_rot_matrix[12] = 0.0f;
    temp_rot_matrix[13] = 0.0f;
    temp_rot_matrix[14] = 0.0f;
    temp_rot_matrix[15] = 1.0f;

    multiply_matrixes(temp_rot_matrix, cur_tetro_matrix, interp_tetro_matrix);
    /*
    f32 m0 = (f32)cur_tetro_matrix[0], m1 = (f32)cur_tetro_matrix[1];
    f32 m4 = (f32)cur_tetro_matrix[4], m5 = (f32)cur_tetro_matrix[5];
    f32 m8 = (f32)cur_tetro_matrix[8], m9 = (f32)cur_tetro_matrix[9];

    interp_tetro_matrix[0] = m0 * c + m1 * s;
    interp_tetro_matrix[1] = -m0 * s + m1 * c;

    interp_tetro_matrix[4] = m4 * c + m5 * s;
    interp_tetro_matrix[5] = -m4 * s + m5 * c;
    interp_tetro_matrix[8] = m8 * c + m9 * s;
    interp_tetro_matrix[9] = -m8 * s + m9 * c;
    */


}


int tetro_pos_x = 0;
int tetro_pos_y = 0;
int tetro_pos_z = 0;
u8 falling_tetro = 0;

f32 interp_tetro_pos_x = 0.0f;
f32 interp_tetro_pos_y = 0.0f;
f32 interp_tetro_pos_z = 0.0f;

void interpolate_position() {    
    f32 pct = ((f32)(ROTATE_FRAMES-cur_rotate_timer)) / ROTATE_FRAMES;
    interp_tetro_pos_x = (f32)tetro_pos_x;
    interp_tetro_pos_y = (f32)tetro_pos_y;
    interp_tetro_pos_z = (f32)tetro_pos_z;

    if(cur_rotate_timer >= 0) {
        printf("timer %i, pct %f\n", cur_rotate_timer, (double)pct);
    }
    switch(cur_interp_move) {
        case MOVE_Y_DOWN:
            interp_tetro_pos_y = (f32)tetro_pos_y + 1 - pct;
            break;
        case MOVE_Y_DOWN_2:
            interp_tetro_pos_y = (f32)tetro_pos_y + 2 - pct * 2.0f;
            break;
        case MOVE_Y_UP:
            interp_tetro_pos_y = (f32)tetro_pos_y - 1 + pct;
            break;
        case MOVE_Y_UP_2:
            interp_tetro_pos_y = (f32)tetro_pos_y - 1 + pct * 2.0f;
            break;
        case NO_MOVE:
            default:
            break;
    }
}

void interpolate_matrix() {
    f32 pct = ((f32)(cur_rotate_timer)) / ROTATE_FRAMES;
    f32 degs = -1.5707f * pct;



    /*frame 0, rotate back 90 degrees
    frame 1, rotate back 89 degrees
    etc*/

    /* 0                        1*/
    /* prev matrix -> cur_matrix */
    copy_matrix_to_interp_matrix();
    switch(cur_rotation) {
        case X_ROTATION:
            x_rotate_interp_matrix(degs);
            break;
        case Y_ROTATION:
            y_rotate_interp_matrix(degs);
            break;
        case Z_ROTATION:
            z_rotate_interp_matrix(degs);
            break;
        case NO_ROTATION:
        default:
            break;
    }
}

#define ORIGIN(x,y) ((y<<4)|(x))
#define NUM_TETROS 7

typedef enum {
    RIGHT,
    UP,
    LEFT,
    DOWN,
    UP_LEFT,
    UP_RIGHT, /* for the T piece*/
    DOUBLE_RIGHT,
    NUM_BASIS_VECTORS
} basis_vector;

/* no z needed */
int basis_vector_defs[NUM_BASIS_VECTORS][3] = {
    { 1,  0,  0},
    { 0,  1,  0},
    {-1,  0,  0},
    { 0, -1,  0},
    {-1,  1,  0},
    { 1,  1,  0},
    { 2,  0,  0}
};

u8 tetros[NUM_TETROS+1][4] = {
    /* no tetro */
    {0,0,0,0},
    /* I */
    { ORIGIN(1, 1), LEFT, DOUBLE_RIGHT, RIGHT },
    /* O */
    { ORIGIN(1, 1), RIGHT, DOWN, LEFT },
    /* T */
    { ORIGIN(1, 1), LEFT, DOUBLE_RIGHT, UP_LEFT },
    /* Z */
    { ORIGIN(1, 1), RIGHT, UP_LEFT, LEFT },
    /* S */
    { ORIGIN(1, 1), LEFT, UP_RIGHT, RIGHT },
    /* J */
    { ORIGIN(1, 1), UP_LEFT, DOWN, DOUBLE_RIGHT },
    /* L */
    { ORIGIN(0, 1), LEFT, DOUBLE_RIGHT, UP }
};


typedef int (*tetro_fp)(ExotiqueInterface *ei, int x, int y, int z, u8 color);
typedef int (*tetro_f32_fp)(ExotiqueInterface *ei, f32 x, f32 y, f32 z, f32* r, f32* u, f32* f, u8 color);

/* 
    takes a tetro, base x,y,z positions, and a function pointer
    it steps through the tetro,
    and at each step calls the function pointer with the current block position within the tetro
    if the function pointer returns 1, it short circuits and return 1 early (used for collision detection)
*/
int evaluate_tetro(ExotiqueInterface *ei, u8 tetro_num, int base_x, int base_y, int base_z, tetro_fp func_ptr) {
    int step, ret_val;
    u8* tetro_def = tetros[tetro_num];

    int packed_origin = tetro_def[0];

    int origin_x = (packed_origin&0xF);
    int origin_y = (packed_origin>>4);
    int origin_z = 0;

    int cur_x, cur_y, cur_z;

    multiply_vector(&origin_x, &origin_y, &origin_z);

    cur_x = origin_x;
    cur_y = origin_y;
    cur_z = origin_z;

    ret_val = func_ptr(ei, cur_x+base_x, cur_y+base_y, cur_z+base_z, tetro_num);
    if(ret_val) {
        return ret_val;
    }
    for(step = 1; step < 4; step++) {
        basis_vector bsv = tetro_def[step];
        int step_x = basis_vector_defs[bsv][0];
        int step_y = basis_vector_defs[bsv][1];
        int step_z = basis_vector_defs[bsv][2];
        multiply_vector(&step_x, &step_y, &step_z);
        cur_x += step_x;
        cur_y += step_y;
        cur_z += step_z;
        ret_val = func_ptr(ei, cur_x+base_x, cur_y+base_y, cur_z+base_z, tetro_num);
        if(ret_val) {
            return ret_val;
        }
    }
    return 0;
}

int evaluate_interp_tetro(ExotiqueInterface *ei, u8 tetro_num, f32 base_x, f32 base_y, f32 base_z, tetro_f32_fp func_ptr) {
    int step, ret_val;
    u8* tetro_def = tetros[tetro_num];

    int packed_origin = tetro_def[0];

    f32 f_base_x = (f32)base_x;
    f32 f_base_y = (f32)base_y;
    f32 f_base_z = (f32)base_z;

    f32 origin_x = (f32)(packed_origin&0xF);
    f32 origin_y = (f32)(packed_origin>>4);
    f32 origin_z = 0.0f;

    f32 cur_x, cur_y, cur_z;
    /* basis vectors to determine where to draw block wireframe */
    f32 right[3];
    f32 up[3];
    f32 forward[3];
    right[0] = 0.5f; right[1] = 0.0f; right[2] = 0.0f;
    up[0] = 0.0f; up[1] = 0.5f; up[2] = 0.0f;
    forward[0] = 0.0f; forward[1] = 0.0f; forward[2] = 0.5f;
    

    multiply_vector_by_interp_mat(&origin_x, &origin_y, &origin_z);

    /* the block wireframe basis vectors should also be rotated */
    multiply_vector_by_interp_mat(  &right[0],   &right[1],   &right[2]);
    multiply_vector_by_interp_mat(     &up[0],      &up[1],      &up[2]);
    multiply_vector_by_interp_mat(&forward[0], &forward[1], &forward[2]);


    cur_x = origin_x;
    cur_y = origin_y;
    cur_z = origin_z;

    /* pass them in here */
    ret_val = func_ptr(ei, cur_x+f_base_x, cur_y+f_base_y, cur_z+f_base_z, right, up, forward, tetro_num);
    if(ret_val) {
        return ret_val;
    }
    for(step = 1; step < 4; step++) {
        basis_vector bsv = tetro_def[step];
        f32 step_x = (f32)basis_vector_defs[bsv][0];
        f32 step_y = (f32)basis_vector_defs[bsv][1];
        f32 step_z = (f32)basis_vector_defs[bsv][2];
        multiply_vector_by_interp_mat(&step_x, &step_y, &step_z);
        cur_x += step_x;
        cur_y += step_y;
        cur_z += step_z;
        ret_val = func_ptr(ei, cur_x+f_base_x, cur_y+f_base_y, cur_z+f_base_z, right, up, forward, tetro_num);
        if(ret_val) {
            return ret_val;
        }
    }
    return 0;
}


#define GRID_SZ 5
#define GRID_DEPTH 12
u8 layers[GRID_DEPTH][GRID_SZ * GRID_SZ];

#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)<(b)?(a):(b))

void game_load(ExotiqueInterface *ei) {
    /* initialize palette */
    int c, s, layer,layer_idx;
    for (c = 0; c < NUM_COLORS; c++) {
        for (s = 0; s < NUM_SHADES; s++) {
            f32 t = MAX(0.01f, ((f32)s) / ((f32)(NUM_SHADES-1)));
            /* placeholder: grey ramps. swap in your 8 base colors here */
            f32 r = init_colors[c][0] * t;
            f32 g = init_colors[c][1] * t;
            f32 b = init_colors[c][2] * t;
            ei->palette[c*NUM_SHADES + s] = ((u32)r<<24)|((u32)g<<16)|((u32)b<<8)|0xFF;
        }
    }

    for(layer = 0; layer < GRID_DEPTH; layer++) {
        for(layer_idx = 0; layer_idx < GRID_SZ * GRID_SZ; layer_idx++) {
            layers[layer][layer_idx] = 0;
        }
    }

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

void spawn_new_tetro() {
    u8 i;
    u32 best_score = nextrand();
    u8 selected = 0;

    /* random selection between tetros */

    
    for(i = 0; i < NUM_TETROS; i++) {
        u32 cur_score = nextrand();
        if(cur_score > best_score) {
            selected = i;
            best_score = cur_score;
        }
    }

    falling_tetro = (u8)(selected+1); /* zero is considered NO-TETRO, increment by 1*/
    set_identity_matrix();

    /*falling_tetro = (rand_val % NUM_TETROS)+1;*/
    tetro_pos_x = 1;
    tetro_pos_y = 2;
    tetro_pos_z = 0;

}

int check_block_collision(ExotiqueInterface *ei, int x, int y, int z, u8 color) {
    (void)ei; (void)color;
    if((z < 0 || z >= GRID_DEPTH || x < 0 || y < 0 || x >= GRID_SZ || y >= GRID_SZ || layers[z][y*GRID_SZ+x] != 0)) {
        return 1;
    }
    return 0;
}

int check_collision(ExotiqueInterface *ei, int x, int y, int z) {
    return evaluate_tetro(ei, falling_tetro, x, y, z, check_block_collision);
}

int set_solid_block_at(ExotiqueInterface *ei, int x, int y, int z, u8 color) {
    (void)ei;
    layers[z][y*GRID_SZ+x] = color;
    return 0;
}

int level_is_full(int z) {
    int y,x;
    for(y = 0; y < GRID_SZ; y++) {
        for(x = 0; x < GRID_SZ; x++) {
            if(layers[z][y*GRID_SZ+x] == 0) {
                return 0;
            }
        }
    }
    return 1;
}

void copy_level(int src, int dst) {
    int y,x;
    for(y = 0; y < GRID_SZ; y++) {
        for(x = 0; x < GRID_SZ; x++) {
            layers[dst][y*GRID_SZ+x] = layers[src][y*GRID_SZ+x];
        }
    }
}
void clear_level(int lvl) {    
    int y,x;
    for(y = 0; y < GRID_SZ; y++) {
        for(x = 0; x < GRID_SZ; x++) {
            layers[lvl][y*GRID_SZ+x] = 0;
        }
    }
}

void shift_above_levels_down(int z) {
    int zz;
    for(zz = z-1; zz >= 0; zz--) {
        /* copy from zz to zz+1*/
        copy_level(zz, zz+1);
        clear_level(zz);
    }
}

void handle_collided_tetro(ExotiqueInterface *ei) {
    int g = GRID_DEPTH-1;
    int consecutive_clears = 0;
    evaluate_tetro(ei, falling_tetro, tetro_pos_x, tetro_pos_y, tetro_pos_z, set_solid_block_at);
    falling_tetro = 0;

    while(g >= 0) {
        if(level_is_full(g)) {
            consecutive_clears++;
            shift_above_levels_down(g);
        } else {
            g--;
            consecutive_clears = 0;
        }
    }
}


int handle_input(ExotiqueInterface* ei) {
    PlayerInput* in = &ei->input[0];
    if(cur_rotation != NO_ROTATION) {
        printf("doing %s rotation\n", rotation_names[cur_rotation]);
    } else if(in->a) {
        rotate_matrix_x();
        if(check_collision(ei, tetro_pos_x, tetro_pos_y, tetro_pos_z)) {
            
            if(!check_collision(ei, tetro_pos_x, tetro_pos_y-1, tetro_pos_z)) {
                cur_rotation = X_ROTATION;
                cur_rotate_timer = ROTATE_FRAMES;
                cur_interp_move = MOVE_Y_DOWN;
                tetro_pos_y--;
            } else if(!check_collision(ei, tetro_pos_x, tetro_pos_y+1, tetro_pos_z)) {
                cur_rotation = X_ROTATION;
                cur_rotate_timer = ROTATE_FRAMES;
                cur_interp_move = MOVE_Y_UP;
                tetro_pos_y++;
            } else if(!check_collision(ei, tetro_pos_x, tetro_pos_y-2, tetro_pos_z)) {
                cur_rotation = X_ROTATION;
                cur_rotate_timer = ROTATE_FRAMES;
                cur_interp_move = MOVE_Y_DOWN_2;
                tetro_pos_y -= 2;
            } else if(!check_collision(ei, tetro_pos_x, tetro_pos_y+2, tetro_pos_z)) {
                cur_rotation = X_ROTATION;
                cur_rotate_timer = ROTATE_FRAMES;
                tetro_pos_y += 2;
                cur_interp_move = MOVE_Y_UP_2;
            } else {
                rotate_matrix_x();
                rotate_matrix_x();
                rotate_matrix_x();
            }
        } else {
            cur_rotation = X_ROTATION;
            cur_rotate_timer = ROTATE_FRAMES;
        }
        
        return 1;
    } else if(in->b) {
        rotate_matrix_y();
        if(check_collision(ei, tetro_pos_x, tetro_pos_y, tetro_pos_z)) {
            if(!check_collision(ei, tetro_pos_x-1, tetro_pos_y, tetro_pos_z)) {
                cur_rotation = Y_ROTATION;
                cur_rotate_timer = ROTATE_FRAMES;
                tetro_pos_x--;
            } else if(!check_collision(ei, tetro_pos_x+1, tetro_pos_y, tetro_pos_z)) {
                cur_rotation = Y_ROTATION;
                cur_rotate_timer = ROTATE_FRAMES;
                tetro_pos_x++;
            } else if(!check_collision(ei, tetro_pos_x-2, tetro_pos_y, tetro_pos_z)) {
                cur_rotation = Y_ROTATION;
                cur_rotate_timer = ROTATE_FRAMES;
                tetro_pos_x -= 2;
            } else if(!check_collision(ei, tetro_pos_x+2, tetro_pos_y, tetro_pos_z)) {
                cur_rotation = Y_ROTATION;
                cur_rotate_timer = ROTATE_FRAMES;
                tetro_pos_x += 2;
            } else {
                rotate_matrix_y();
                rotate_matrix_y();
                rotate_matrix_y();
            }
        } else {
            cur_rotation = Y_ROTATION;
            cur_rotate_timer = ROTATE_FRAMES;
        }
        return 1;
    } else if(in->select) {
        rotate_matrix_z();
        if(check_collision(ei, tetro_pos_x, tetro_pos_y, tetro_pos_z)) {
            if(!check_collision(ei, tetro_pos_x-1, tetro_pos_y, tetro_pos_z)) {
                cur_rotation = Z_ROTATION;
                cur_rotate_timer = ROTATE_FRAMES;
                tetro_pos_x--;
            } else if (!check_collision(ei, tetro_pos_x, tetro_pos_y-1, tetro_pos_z)) {
                cur_rotation = Z_ROTATION;
                cur_rotate_timer = ROTATE_FRAMES;
                tetro_pos_y--;
            } else if (!check_collision(ei, tetro_pos_x+1, tetro_pos_y, tetro_pos_z)) {
                cur_rotation = Z_ROTATION;
                cur_rotate_timer = ROTATE_FRAMES;
                tetro_pos_x++;
            } else if(!check_collision(ei, tetro_pos_x, tetro_pos_y+1, tetro_pos_z)) {
                cur_rotation = Z_ROTATION;
                cur_rotate_timer = ROTATE_FRAMES;
                tetro_pos_y++;
            } else if(!check_collision(ei, tetro_pos_x-2, tetro_pos_y, tetro_pos_z)) {
                cur_rotation = Z_ROTATION;
                cur_rotate_timer = ROTATE_FRAMES;
                tetro_pos_x -= 2;
            } else if (!check_collision(ei, tetro_pos_x, tetro_pos_y-2, tetro_pos_z)) {
                cur_rotation = Z_ROTATION;
                cur_rotate_timer = ROTATE_FRAMES;
                tetro_pos_y -= 2;
            } else if (!check_collision(ei, tetro_pos_x+2, tetro_pos_y, tetro_pos_z)) {
                cur_rotation = Z_ROTATION;
                cur_rotate_timer = ROTATE_FRAMES;
                tetro_pos_x += 2;
            } else if(!check_collision(ei, tetro_pos_x, tetro_pos_y+2, tetro_pos_z)) {
                cur_rotation = Z_ROTATION;
                cur_rotate_timer = ROTATE_FRAMES;
                tetro_pos_y += 2;
            } else {
                rotate_matrix_z();
                rotate_matrix_z();
                rotate_matrix_z();
            }
        } else {
            cur_rotation = Z_ROTATION;
            cur_rotate_timer = ROTATE_FRAMES;
        }
        return 1;
    }

    if (in->up) {
        if(!check_collision(ei, tetro_pos_x, tetro_pos_y+1, tetro_pos_z)) {
            tetro_pos_y++;
        }
        return 1;
    }else if (in->down) {
        if(!check_collision(ei, tetro_pos_x, tetro_pos_y-1, tetro_pos_z)) {
            tetro_pos_y--;
        }
        return 1;
    } else if (in->left) {
        if(!check_collision(ei, tetro_pos_x-1, tetro_pos_y, tetro_pos_z)) {
            tetro_pos_x--;
        }
        return 1;
    } else if(in->right) {
        if(!check_collision(ei, tetro_pos_x+1, tetro_pos_y, tetro_pos_z)) {
            tetro_pos_x++;
        }
        return 1;
    } else {
        return 0;
    }
}

int no_inputs(ExotiqueInterface* ei) {
    PlayerInput* in = &ei->input[0];
    return (in->a || in->b || in->up || in->down || in->left || in->right || in->select) == 0;
}



int frame = 0;
int got_input = 0;
int pause = 0;
int last_frame_start = 1;
void game_update(ExotiqueInterface *ei) {
    int fall_timer = (frame & 63);
    int double_fall_timer = (frame & 7);
    
    if(cur_rotate_timer-- == 0) {
        cur_rotation = NO_ROTATION;
        cur_interp_move = NO_MOVE;
    }
    interpolate_matrix();
    interpolate_position();
    if(ei->input->start) {
        if(last_frame_start == 0) {
            pause = !pause;
        }
        last_frame_start = 1;
    } else {
        last_frame_start = 0;
    }


    if(pause) {
        return;
    }

    frame++;

    if(falling_tetro == 0) {
        spawn_new_tetro();
    } else {
        if(got_input == 0) {
            got_input = handle_input(ei);
        } else if (got_input == 1 && no_inputs(ei)) {
            got_input = 0;
        }
        /* separate this if check to allow for inputs on the frame before a tetro falls */
        if(fall_timer == 0 || (ei->input->x && double_fall_timer == 0)) {
            if(check_collision(ei, tetro_pos_x, tetro_pos_y, tetro_pos_z+1)) {
                handle_collided_tetro(ei);
                spawn_new_tetro();
            } else {
                tetro_pos_z++;
            }
        }
    }
}

f32 fast_inv_sqrt( f32 number )
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

f32 my_sqrt(f32 i) {
    return 1.0f / fast_inv_sqrt(i);
}


#define SCREEN_PIXELS (WIDTH*HEIGHT)
static f32 zbuf[SCREEN_PIXELS];

#define FMIN(a,b) ((a)<(b)?(a):(b))
#define FMAX(a,b) ((a)>(b)?(a):(b))
#define EDGE(ax,ay,bx,by,px,py) \
    (((bx)-(ax))*((py)-(ay)) - ((by)-(ay))*((px)-(ax)))

static int m_floor(f32 x) {
    int i = (int)x;
    return (x < 0.0f) ? i - 1 : i;
}

static int m_ceil(f32 x) {
    int i = (int)x;
    return i + (x > 0.0f && (x - (f32)i) > 0.0f);
}

f32 fabsf(f32 f) {
    return (f < 0.0f) ? -f : f;
}

static int is_top_left(f32 ax, f32 ay, f32 bx, f32 by) {
    int a_gt_b = (ay > by);
    int a_lt_b = (ay < by);
    int a_not_gt_or_lt = (!a_gt_b) && (!a_lt_b);
    int top  = (a_not_gt_or_lt && bx > ax); /* horizontal, going right */
    int left = (by > ay);             /* going down */
    return top || left;
}

#define FOV_SCALE  1.45f /* 1.7320f */  /* 60 deg vertical fov */
#define ASPECT     (((f32)WIDTH)/HEIGHT)
#define NEAR       0.1f
#define NEAR_ARENA 2.0f
#define NEAR_Z_SPACING 4.25f
#define FAR_ARENA  (GRID_DEPTH-1)+(NEAR_Z_SPACING)+0.5f

static f32 depth_shade(f32 z_view, f32 brightness)
{
    f32 depth_fade = 1.0f - (z_view - 2.0f) / 40.0f;
    if (depth_fade < 0.0f) depth_fade = 0.0f;
    if (depth_fade > 1.0f) depth_fade = 1.0f;
    return brightness * depth_fade;
}

typedef struct { f32 x, y, z_ndc, z_view; } Projected;

f32 cam_x = 0.0f;
f32 cam_y = 0.0f;
f32 cam_z = 0.0f;
static void project(f32 x, f32 y, f32 z, Projected* p) {
    f32 dx = x-cam_x;
    f32 dy = y-cam_y;
    f32 dz = z-cam_z;
    p->z_view = dz;
    p->x      = ( (dx * FOV_SCALE) / (dz * ASPECT) + 1.0f) * WIDTH_DIV_2;
    p->y      = (-(dy * FOV_SCALE) /  dz            + 1.0f) * HEIGHT_DIV_2;
    p->z_ndc  = 1.0f - (NEAR / dz); /* 0=near 1=far, greater = further away */
}

typedef enum {
    DRAW_EDGE_BLACK,
    DRAW_EDGE_ONLY,
    DRAW_EDGE_ONLY_HALF_BRIGHT,
    DONT_HIGHLIGHT_EDGE
} edge_draw;

static void draw_quad(ExotiqueInterface *ei,
    f32 *v1, f32 *v2, f32 *v3, f32 *v4,
    u8 color, f32 brightness, edge_draw edge, int draw_back_side) {
    int tri, minx, miny, maxx, maxy, x, y, shade;
    f32 ax, ay, az, bx, by, bz, cx, cy, cz;
    f32 total, w0, w1, w2, px, py, wsum, z_interp, avg_z;
    int drawing_w0_edge, drawing_w1_edge, drawing_w2_edge;
    u8 final_color;
    Projected pa, pb, pc;

    f32 v1x = v1[0]; f32 v1y = v1[1]; f32 v1z = v1[2];
    f32 v2x = v2[0]; f32 v2y = v2[1]; f32 v2z = v2[2];
    f32 v3x = v3[0]; f32 v3y = v3[1]; f32 v3z = v3[2];
    f32 v4x = v4[0]; f32 v4y = v4[1]; f32 v4z = v4[2];


    const f32 edge_width_pixels = (edge == DRAW_EDGE_ONLY ? 2.5f : 1.5f);
    
    avg_z      = (v1z + v2z + v3z + v4z) * 0.25f;
    brightness = depth_shade(avg_z, brightness);
    shade      = (int)(brightness * (f32)(NUM_SHADES-1) + 0.5f);

    if (shade <  0) shade =  0;
    if (shade > (NUM_SHADES-1)) shade = NUM_SHADES-1;
    
    final_color =  (u8)(color + (u8)shade);
    if(edge == DRAW_EDGE_ONLY) {
        final_color = (u8)(color + NUM_SHADES-1);
    } else if (edge == DRAW_EDGE_ONLY_HALF_BRIGHT) {
        final_color = (u8)(color + (NUM_SHADES/2));
        edge = DRAW_EDGE_ONLY;
    }

    for (tri = 0; tri < 2; tri++) {
        if (tri == 0) {
            ax=v1x; ay=v1y; az=v1z;
            bx=v2x; by=v2y; bz=v2z;
            cx=v3x; cy=v3y; cz=v3z;
            /* ac edge doesn't need wireframe */
            drawing_w2_edge = 0;
            drawing_w0_edge = 1;
            drawing_w1_edge = 1;
        } else {
            ax=v1x; ay=v1y; az=v1z;
            bx=v3x; by=v3y; bz=v3z;
            cx=v4x; cy=v4y; cz=v4z;
            drawing_w0_edge = 0;
            drawing_w1_edge = 1;
            drawing_w2_edge = 1;
            /* ab edge doesn't need wireframe */
        }

        project(ax, ay, az, &pa);
        project(bx, by, bz, &pb);
        project(cx, cy, cz, &pc);

        /* skip if any vert is behind camera */
        if (pa.z_view < NEAR || pb.z_view < NEAR || pc.z_view < NEAR) continue;

        minx = m_floor(FMIN(pa.x, FMIN(pb.x, pc.x)));
        miny = m_floor(FMIN(pa.y, FMIN(pb.y, pc.y)));
        maxx = m_ceil (FMAX(pa.x, FMAX(pb.x, pc.x)));
        maxy = m_ceil (FMAX(pa.y, FMAX(pb.y, pc.y)));

        if (minx < 0) minx = 0;
        if (miny < 0) miny = 0;
        if (maxx > kScreenWidth  - 1) maxx = kScreenWidth  - 1;
        if (maxy > kScreenHeight - 1) maxy = kScreenHeight - 1;

        total = EDGE(pa.x,pa.y, pb.x,pb.y, pc.x,pc.y);
        if(!draw_back_side && total < 0.0f) {
            continue;
        }


        for (y = miny; y <= maxy; y++) {
            for (x = minx; x <= maxx; x++) {
                int w0_is_edge, w1_is_edge, w2_is_edge, on_edge;

                f32 bias0, bias1, bias2;

                px = (f32)x + 0.5f;
                py = (f32)y + 0.5f;
                w0 = EDGE(pa.x,pa.y, pb.x,pb.y, px,py);
                w1 = EDGE(pb.x,pb.y, pc.x,pc.y, px,py);
                w2 = EDGE(pc.x,pc.y, pa.x,pa.y, px,py);
                if(total < 0.0f) { w0 = -w0; w1 = -w1; w2 = -w2; }

                bias0 = is_top_left(pa.x,pa.y, pb.x,pb.y) ? 0.0f : -0.0001f;
                bias1 = is_top_left(pb.x,pb.y, pc.x,pc.y) ? 0.0f : -0.0001f;
                bias2 = is_top_left(pc.x,pc.y, pa.x,pa.y) ? 0.0f : -0.0001f;


                if (w0 + bias0 < 0 || w1 + bias1 < 0 || w2 + bias2 < 0) continue;

                wsum = w0 + w1 + w2;
                z_interp = (w0*pa.z_ndc + w1*pb.z_ndc + w2*pc.z_ndc) / wsum;

                

                w0_is_edge = w0 < (fabsf(pb.y-pa.y) + fabsf(pb.x-pa.x)) * edge_width_pixels && drawing_w0_edge;
                w1_is_edge = w1 < (fabsf(pc.y-pb.y) + fabsf(pc.x-pb.x)) * edge_width_pixels && drawing_w1_edge;
                w2_is_edge = w2 < (fabsf(pa.y-pc.y) + fabsf(pa.x-pc.x)) * edge_width_pixels && drawing_w2_edge;

                on_edge = (w0_is_edge || w1_is_edge || w2_is_edge);

                if(z_interp < zbuf[y*WIDTH+x]) {
                    if((edge == DRAW_EDGE_ONLY) && on_edge) {
                        zbuf[y * WIDTH + x] = z_interp;
                        ei->screen[y * WIDTH + x] = final_color; /* edges are black */

                    } else if(edge != DRAW_EDGE_ONLY) {
                        if(on_edge && edge != DONT_HIGHLIGHT_EDGE) {
                            zbuf[y * WIDTH + x] = z_interp;
                            ei->screen[y * WIDTH + x] = (edge == DRAW_EDGE_BLACK) ? 0 : NUM_SHADES-1; /* edges are white */
                        } else {
                            zbuf[y * WIDTH + x] = z_interp;
                            ei->screen[y * WIDTH + x] = final_color;
                        }
                    }
                }
            }
        }
    }
}


static void draw_quad_from_verts(ExotiqueInterface *ei,
    f32 v1x, f32 v1y, f32 v1z,
    f32 v2x, f32 v2y, f32 v2z,
    f32 v3x, f32 v3y, f32 v3z,
    f32 v4x, f32 v4y, f32 v4z,
    u8 color, f32 brightness, edge_draw edge, int draw_back_side) {

    f32 v1[3],v2[3],v3[3],v4[3];
    v1[0]=v1x; v1[1]=v1y; v1[2]=v1z;
    v2[0]=v2x; v2[1]=v2y; v2[2]=v2z;
    v3[0]=v3x; v3[1]=v3y; v3[2]=v3z;
    v4[0]=v4x; v4[1]=v4y; v4[2]=v4z;
    draw_quad(ei,v1,v2,v3,v4,color,brightness,edge,draw_back_side);
}


void add_vec(f32 v0[3], f32 v1[3], f32 vo[3]) {
    int i;
    for(i = 0; i < 3; i++) {
        vo[i] = v0[i] + v1[i];
    }
}
void sub_vec(f32 v0[3], f32 v1[3], f32 vo[3]) {
    int i;
    for(i = 0; i < 3; i++) {
        vo[i] = v0[i] - v1[i];
    }
}


void draw_block(ExotiqueInterface *ei, u8 color, f32 x, f32 y, f32 z, f32 half_right_x_vec[3], f32 half_up_y_vec[3], f32 half_forward_z_vec[3], edge_draw edge, int draw_back_side) {
    /* 5 quads to draw */
    u8 scaled_color = (u8)(color * NUM_SHADES);

    f32 cx = x - 2.0f;
    f32 cy = y - 2.0f;
    f32 cz = z + NEAR_Z_SPACING; /* maybe correct? */

    
    f32 lun[3],run[3],rdn[3],ldn[3],rdf[3],ldf[3],luf[3],ruf[3];
    f32 center[3];
    center[0] = cx;
    center[1] = cy;
    center[2] = cz;

    add_vec(center, half_right_x_vec, ruf);
    add_vec(ruf, half_up_y_vec, ruf);
    add_vec(ruf, half_forward_z_vec, ruf);

    add_vec(center, half_right_x_vec, run);
    add_vec(run, half_up_y_vec, run);
    sub_vec(run, half_forward_z_vec, run);

    add_vec(center, half_right_x_vec, rdf);
    sub_vec(rdf, half_up_y_vec, rdf);
    add_vec(rdf, half_forward_z_vec, rdf);

    add_vec(center, half_right_x_vec, rdn);
    sub_vec(rdn, half_up_y_vec, rdn);
    sub_vec(rdn, half_forward_z_vec, rdn);

    sub_vec(center, half_right_x_vec, luf);
    add_vec(luf, half_up_y_vec, luf);
    add_vec(luf, half_forward_z_vec, luf);

    sub_vec(center, half_right_x_vec, lun);
    add_vec(lun, half_up_y_vec, lun);
    sub_vec(lun, half_forward_z_vec, lun);

    sub_vec(center, half_right_x_vec, ldf);
    sub_vec(ldf, half_up_y_vec, ldf);
    add_vec(ldf, half_forward_z_vec, ldf);

    sub_vec(center, half_right_x_vec, ldn);
    sub_vec(ldn, half_up_y_vec, ldn);
    sub_vec(ldn, half_forward_z_vec, ldn);






    /* front */ 
    draw_quad(ei, lun, run, rdn, ldn, scaled_color, 1.0f, edge, draw_back_side);

    /* bot */
    draw_quad(ei, ldn, rdn, rdf, ldf, scaled_color, 0.6f, edge, draw_back_side);
    
    /* top */
    draw_quad(ei, luf, ruf, run, lun, scaled_color, 0.6f, edge, draw_back_side);

    /* left */
    draw_quad(ei, ldf, luf, lun, ldn, scaled_color, 0.6f, edge, draw_back_side);

    /* right */
    draw_quad(ei, rdn, run, ruf, rdf, scaled_color, 0.6f, edge, draw_back_side);
}

void draw_level(ExotiqueInterface *ei) {
    int level,y,x;
    f32 right_vec[3] = {0.5f,0.0f,0.0f};
    f32 up_vec[3] = {0.0f,0.5f,0.0f};
    f32 forward_vec[3] = {0.0f,0.0f,0.5f};

    for(level = GRID_DEPTH-1; level >= 0; level--) {
        for(y = 0; y < GRID_SZ; y++) {
            for(x = 0; x < GRID_SZ; x++) {
                u8 col = layers[level][y*GRID_SZ+x];
                if(col == 0) {
                    continue;
                }
                draw_block(ei, col, (f32)x, (f32)y, (f32)level, right_vec, up_vec, forward_vec, DRAW_EDGE_BLACK, 0);
            }
        }
    }

}

static void zbuf_clear(void) {
    int i;
    for (i = 0; i < SCREEN_PIXELS; i++) zbuf[i] = 1.0f; /* 1.0 = far */
}

int draw_tetro_block(ExotiqueInterface *ei, f32 x, f32 y, f32 z, f32* r, f32* u, f32* f, u8 color) {

    draw_block(ei, color, x, y, z, r, u, f, DRAW_EDGE_ONLY, 1);
    return 0;
}


void draw_tetro(ExotiqueInterface*ei, u8 tetro_num, f32 x, f32 y, f32 z) {
    evaluate_interp_tetro(ei, tetro_num, x, y, z, draw_tetro_block);
}

void game_draw(ExotiqueInterface *ei) {
    u8 white = 0;
    int idx;
    int x,y,z;
    f32 arena_min_coord = -2.5f;
    f32 arena_max_coord = 2.5f;

    for(idx = 0; idx < SCREEN_PIXELS; idx++) {
        ei->screen[idx] = 0;
    }
    zbuf_clear();

    for(x = 0; x < GRID_SZ; x++) {
        for(y = 0; y < GRID_SZ; y++) {
            f32 left_x = arena_min_coord + (f32)x;
            f32 right_x = arena_min_coord + (f32)x + 1.0f;
            f32 bot_y = arena_min_coord + (f32)y;
            f32 top_y = arena_min_coord + (f32)y + 1.0f;
            draw_quad_from_verts(
                ei,
                left_x, top_y, FAR_ARENA,
                right_x, top_y, FAR_ARENA,
                right_x, bot_y, FAR_ARENA,
                left_x, bot_y, FAR_ARENA,
                white, .25f, DRAW_EDGE_ONLY_HALF_BRIGHT, 0
            );
        }
    }

    for(z = 0; z < GRID_DEPTH; z++) {
        f32 nz = (f32)z + NEAR_Z_SPACING - 0.5f;
        f32 fz = (f32)z + NEAR_Z_SPACING + 0.5f;

        for(x = 0; x < GRID_SZ; x++) {
            f32 left_x = arena_min_coord + (f32)x;
            f32 right_x = arena_min_coord + (f32)x + 1.0f;
            draw_quad_from_verts(
                ei,
                left_x, arena_max_coord, nz,
                right_x, arena_max_coord, nz,
                right_x, arena_max_coord, fz,
                left_x, arena_max_coord, fz,
                white, .25f, DRAW_EDGE_ONLY_HALF_BRIGHT, 0
            );
            draw_quad_from_verts(
                ei,
                left_x, arena_min_coord, nz,
                left_x, arena_min_coord, fz,
                right_x, arena_min_coord, fz,
                right_x, arena_min_coord, nz,
                white, .25f, DRAW_EDGE_ONLY_HALF_BRIGHT, 0
            );
        }

        for(y = 0; y < GRID_SZ; y++) {
            f32 bot_y = arena_min_coord + (f32)y;
            f32 top_y = arena_min_coord + (f32)y + 1.0f;
            draw_quad_from_verts(
                ei,
                arena_max_coord, top_y, nz,
                arena_max_coord, bot_y, nz,
                arena_max_coord, bot_y, fz,
                arena_max_coord, top_y, fz,
                white, .25f, DRAW_EDGE_ONLY_HALF_BRIGHT, 0
            );
            draw_quad_from_verts(
                ei,
                arena_min_coord, top_y, fz,
                arena_min_coord, bot_y, fz,
                arena_min_coord, bot_y, nz,
                arena_min_coord, top_y, nz,
                white, .25f, DRAW_EDGE_ONLY_HALF_BRIGHT, 0
            );
        }
    }

    zbuf_clear();
    draw_level(ei);

    if(falling_tetro != 0) {
        draw_tetro(ei, falling_tetro, interp_tetro_pos_x, interp_tetro_pos_y, interp_tetro_pos_z);
    }
}
