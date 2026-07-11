#include "exotique.h"

#define WIDTH 600
#define HEIGHT 600
#define WIDTH_DIV_2 (WIDTH/2)
#define HEIGHT_DIV_2 (HEIGHT/2)
const int kScreenWidth  = WIDTH;
const int kScreenHeight = HEIGHT;

/* ── math ─────────────────────────────────────── */

#define FMIN(a,b) ((a)<(b)?(a):(b))
#define FMAX(a,b) ((a)>(b)?(a):(b))
#define EDGE(ax,ay,bx,by,px,py) \
    (((bx)-(ax))*((py)-(ay)) - ((by)-(ay))*((px)-(ax)))

static int m_floor(float x) {
    int i = (int)x;
    return (x < 0.0f) ? i - 1 : i;
}

static int m_ceil(float x) {
    int i = (int)x;
    return i + (x > 0.0f && (x - (float)i) > 0.0f);
}

/* ── zbuffer ──────────────────────────────────── */

#define SCREEN_PIXELS (WIDTH*HEIGHT)
static float zbuf[SCREEN_PIXELS];

static void zbuf_clear(void) {
    int i;
    for (i = 0; i < SCREEN_PIXELS; i++) zbuf[i] = 1.0f; /* 1.0 = far */
}

/* ── projection ───────────────────────────────── */
/* camera fixed at origin, looking down +Z         */
/* fov_scale = 1/tan(fov_y/2), aspect = W/H        */

#define CAM_Z      0.0f
#define FOV_SCALE  1.7320f   /* 60 deg vertical fov */
#define ASPECT     (((f32)WIDTH)/HEIGHT)
#define NEAR       0.1f

typedef struct { float x, y, z_ndc, z_view; } Projected;

static void project(float x, float y, float z, Projected* p) {
    float dz = z;
    p->z_view = dz;
    p->x      = ( (x * FOV_SCALE) / (dz * ASPECT) + 1.0f) * WIDTH_DIV_2;
    p->y      = (-(y * FOV_SCALE) /  dz            + 1.0f) * HEIGHT_DIV_2;
    p->z_ndc  = 1.0f - (NEAR / dz); /* 0=near 1=far, greater = further away */
}

float project_radius(float r, float z)
{
    return (r * FOV_SCALE / z) * HEIGHT_DIV_2;
}


#define NUM_COLORS 8
#define NUM_SHADES 32

/* ── draw ─────────────────────────────────────── */
static float depth_shade(float z_view, float brightness)
{
    float depth_fade = 1.0f - (z_view - 2.0f) / 26.0f;
    if (depth_fade < 0.0f) depth_fade = 0.0f;
    if (depth_fade > 1.0f) depth_fade = 1.0f;
    return brightness * depth_fade;
}

#define Z_BUFFER 1
#define NO_Z_BUFFER 0
#define TRANSPARENT 1
#define OPAQUE 0

static int is_top_left(float ax, float ay, float bx, float by) {
    int a_gt_b = (ay > by);
    int a_lt_b = (ay < by);
    int a_not_gt_or_lt = (!a_gt_b) && (!a_lt_b);
    int top  = (a_not_gt_or_lt && bx > ax); /* horizontal, going right */
    int left = (by > ay);             /* going down */
    return top || left;
}

static void draw_quad(ExotiqueInterface *ei,
    float v1x, float v1y, float v1z,
    float v2x, float v2y, float v2z,
    float v3x, float v3y, float v3z,
    float v4x, float v4y, float v4z,
    u8 color, float brightness, int z_buffer, int transparent)
{
    int shade, tri, minx, miny, maxx, maxy, x, y;
    float ax, ay, az, bx, by, bz, cx, cy, cz;
    float total, w0, w1, w2, wsum, px, py, z_interp, avg_z;
    u8 final_color;
    Projected pa, pb, pc;

    avg_z      = (v1z + v2z + v3z + v4z) * 0.25f;
    brightness = depth_shade(avg_z, brightness);
    shade      = (int)(brightness * 31.0f + 0.5f);

    if (shade <  0) shade =  0;
    if (shade > 31) shade = 31;
    final_color =  (u8)(color + (u8)shade);

    for (tri = 0; tri < 2; tri++) {
        if (tri == 0) {
            ax=v1x; ay=v1y; az=v1z;
            bx=v2x; by=v2y; bz=v2z;
            cx=v3x; cy=v3y; cz=v3z;
        } else {
            ax=v1x; ay=v1y; az=v1z;
            bx=v3x; by=v3y; bz=v3z;
            cx=v4x; cy=v4y; cz=v4z;
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
        if (total < 0.0f) continue;

        for (y = miny; y <= maxy; y++) {
            for (x = minx; x <= maxx; x++) {
                float bias0, bias1, bias2;
                px = (float)x + 0.5f;
                py = (float)y + 0.5f;
                w0 = EDGE(pa.x,pa.y, pb.x,pb.y, px,py);
                w1 = EDGE(pb.x,pb.y, pc.x,pc.y, px,py);
                w2 = EDGE(pc.x,pc.y, pa.x,pa.y, px,py);

                bias0 = is_top_left(pa.x,pa.y, pb.x,pb.y) ? 0.0f : -0.0001f;
                bias1 = is_top_left(pb.x,pb.y, pc.x,pc.y) ? 0.0f : -0.0001f;
                bias2 = is_top_left(pc.x,pc.y, pa.x,pa.y) ? 0.0f : -0.0001f;

                if (w0 + bias0 < 0 || w1 + bias1 < 0 || w2 + bias2 < 0) continue;
                /*if (!((w0 >= 0 && w1 >= 0 && w2 >= 0) ||
                      (w0 <= 0 && w1 <= 0 && w2 <= 0))) continue;*/

                wsum = w0 + w1 + w2;
                z_interp = (w0*pa.z_ndc + w1*pb.z_ndc + w2*pc.z_ndc) / wsum;

                if ((z_interp < zbuf[y * WIDTH + x])) {
                    if(z_buffer) { zbuf[y * WIDTH + x] = z_interp; }
                    
                    if(transparent) {
                        u8 idx = ei->screen[y*WIDTH+x];
                        ei->screen[y*WIDTH+x] = (u8)((idx & 0x1F) < 6 ? (idx & 0xF8) : idx-6);
                    } else {
                        ei->screen[y * WIDTH + x] = final_color;
                    }
                }
            }
        }
    }
}

/* ── game ─────────────────────────────────────── */


#define GRID_SZ 5

typedef struct {
    int cells[GRID_SZ*GRID_SZ];
} layer;

#define MAX_NUM_LAYERS 5
typedef struct {
    layer layers[MAX_NUM_LAYERS];
} level;

u8 colors[NUM_COLORS][3]= {
    { 220,  50,  50 }, /* red */
    { 220, 130,  40 }, /* orange */
    { 210, 200,  50 }, /* yellow */
    {  50, 180,  70 }, /* green */
    {  40, 190, 210 }, /* cyan */
    {  50,  90, 220 }, /* blue */
    { 150,  60, 220 }, /* purple */
    { 178, 190, 181 }  /* grey */
};

level level0;

int level0_empty_layer_cells[] = {
    -1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,
};

int level0_layer_5_cells[] = {

     0, 1, 2, 3, 4,
     5, 6, 5, 4, 3,
     2, 1, 0, 1, 2,
     3, 4, 5, 6, 5,
     3, 2, 1, 0, 1
     
};

float ball_x = 0.0f;
float ball_y = 0.0f;
float ball_z = 4.0f;
float ball_dz = 0.15f;
float ball_dy = -0.01f;
float ball_dx = 0.02f;

#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)>(b)?(b):(a))

#define BRICK_SZ 1.0f
#define BRICK_LENGTH 0.5f
#define NEAR_ARENA 3.255f
#define FAR_ARENA 20.0f

#define PUCK_SIZE 0.75f
#define PUCK_LENGTH 0.15f
float puck_pos_x = 0.0f;
float puck_pos_y = 0.0f;
const float puck_pos_z = NEAR_ARENA+0.5f;
int bricks_left;

int hard_mode = 0;

void game_load(ExotiqueInterface *ei) {
    int c, s, l;
    int y,x;
    bricks_left = 0;
    puck_pos_x = 0.0f;
    puck_pos_y = 0.0f;
    ball_x = 0.0f;
    ball_y = 0.0f;
    ball_z = 4.0f;
    ball_dz = 0.1f;
    ball_dy = 0.01f;
    ball_dx = 0.008f;


    for (c = 0; c < NUM_COLORS; c++) {
        for (s = 0; s < NUM_SHADES; s++) {
            float t = MAX(0.01f, ((f32)s) / ((f32)(NUM_SHADES-1)));
            /* placeholder: grey ramps. swap in your 8 base colors here */
            float r = colors[c][0] * t;
            float g = colors[c][1] * t;
            float b = colors[c][2] * t;
            ei->palette[c*NUM_SHADES + s] = ((u32)r<<24)|((u32)g<<16)|((u32)b<<8)|0xFF;
        }
    }

    for(l = 0; l < MAX_NUM_LAYERS; l++) {
        if ( l == 4 ) {
            for(y = 0; y < GRID_SZ; y++) {
                for(x = 0; x < GRID_SZ; x++) {
                    level0.layers[l].cells[y*GRID_SZ+x] = level0_layer_5_cells[y*GRID_SZ+x];
                    bricks_left += (level0.layers[l].cells[y*GRID_SZ+x] == -1) ? 0 : 1;
                }
            }
        } else if (l == 3) {
            for(y = 0; y < GRID_SZ; y++) {
                for(x = 0; x < GRID_SZ; x++) {
                    level0.layers[l].cells[y*GRID_SZ+x] = ((x & 1) || (y & 1)) ? -1 : level0_layer_5_cells[(4-y)*GRID_SZ+(4-x)];
                    bricks_left += (level0.layers[l].cells[y*GRID_SZ+x] == -1) ? 0 : 1;
                }
            }
        } else if (l == 2) {
            for(y = 0; y < GRID_SZ; y++) {
                for(x = 0; x < GRID_SZ; x++) {
                    level0.layers[l].cells[y*GRID_SZ+x] = ((x & 1) || (y & 1)) ? level0_layer_5_cells[(4-y)*GRID_SZ+(4-x)] : -1;
                    bricks_left += (level0.layers[l].cells[y*GRID_SZ+x] == -1) ? 0 : 1;
                }
            }
        } else if (l == 1 && hard_mode) {
            for(y = 0; y < GRID_SZ; y++) {
                for(x = 0; x < GRID_SZ; x++) {
                    level0.layers[l].cells[y*GRID_SZ+x] = -2;
                    bricks_left++;
                }
            }
        } else {
            for(y = 0; y < GRID_SZ; y++) {
                for(x = 0; x < GRID_SZ; x++) {
                    level0.layers[l].cells[y*GRID_SZ+x] = -1;
                }
            }
        }
    }
}


const float layer_depths[5] = {
    7.0f, 9.0f, 11.0f, 13.0f, 15.0f
};

#define BALL_RADIUS 0.3f
#define ABS(a) ((a) < 0 ? (-a) : (a))


float fast_inv_sqrt( float number )
{
	long i;
	float x2, y;
	const float threehalfs = 1.5F;

	x2 = number * 0.5F;
	y  = number;
	i  = * ( long * ) &y;      
	i  = 0x5f3759df - ( i >> 1 );
	y  = * ( float * ) &i;
	y  = y * ( threehalfs - ( x2 * y * y ) ); 

	return y;
}

float my_sqrt(float i) {
    return 1.0f / fast_inv_sqrt(i);
}


float dist_sqr(float x1, float y1, float x2, float y2) {
    float dx = x2-x1;
    float dy = y2-y1;
    return my_sqrt(dx*dx+dy*dy);
}


int ball_within_cell(float x, float y, float z) {
    float left_x = x - BRICK_SZ * 0.5f;
    float right_x = x + BRICK_SZ * 0.5f;
    float up_y = y - BRICK_SZ * 0.5f;
    float down_y = y + BRICK_SZ * 0.5f;
    float fz = z-BRICK_LENGTH*0.5f;
    float bz = z+BRICK_LENGTH*0.5f;

    return (
        x >= left_x && x < right_x && y >= up_y && y < down_y && z >= fz && z < bz
    );

}



typedef enum { NO_HIT, LEFT, RIGHT, TOP, BOTTOM, FRONT, BACK } box_side;

box_side sphere_vs_box(
    float sx, float sy, float sz, float sr,
    float bx, float by, float bz,        /* box corner */
    float bw, float bh, float bd
) {

    float box_cx = bx + bw/2.0f;
    float box_cy = by + bh/2.0f;
    float box_cz = bz + bd/2.0f;

    float cx = sx < bx ? bx : sx > bx + bw ? bx + bw : sx;
    float cy = sy < by ? by : sy > by + bh ? by + bh : sy;
    float cz = sz < bz ? bz : sz > bz + bd ? bz + bd : sz;

    float dx = sx - cx;
    float dy = sy - cy;
    float dz = sz - cz;


    if ((dx*dx + dy*dy + dz*dz) > (sr*sr)) return NO_HIT;

    /* penetration depth on each axis */
    {
        float px = bw/2.0f - (sx > box_cx ? sx - box_cx : box_cx - sx);
        float py = bh/2.0f - (sy > box_cy ? sy - box_cy : box_cy - sy);
        float pz = bd/2.0f - (sz > box_cz ? sz - box_cz : box_cz - sz);

        if (px < py && px < pz) {
            return sx > box_cx ? RIGHT : LEFT;
        } else if (py < pz) {
            return sy > box_cy ? BOTTOM : TOP;
        } else {
            return sz > box_cz ? BACK : FRONT;
        }
    }
}

int pause = 0;
int last_frame_start = 0;

void game_update(ExotiqueInterface *ei) {
    int lvl;
    box_side puck_hit;
    (void)ei;

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

    ball_z += ball_dz;
    ball_y += ball_dy;
    ball_x += ball_dx;
    if(ei->input->up) {
        puck_pos_y = MIN(puck_pos_y+0.05f, 2.5f-PUCK_SIZE*0.5f);
    }
    if(ei->input->down) {
        puck_pos_y = MAX(puck_pos_y-0.05f, -2.5f+PUCK_SIZE*0.5f);
    }
    if(ei->input->right) {
        puck_pos_x = MIN(puck_pos_x+0.05f, 2.5f-PUCK_SIZE*0.5f);
    }
    if(ei->input->left) {
        puck_pos_x = MAX(puck_pos_x-0.05f, -2.5f+PUCK_SIZE*0.5f);
    }


    for(lvl = 0; lvl < 5; lvl++) {
        int y,x;
        float bz = layer_depths[lvl];
        float fz = bz - BRICK_LENGTH/2.0f;
        if(ball_z+BALL_RADIUS < fz) { goto skip_collision; }
        if(ball_z-BALL_RADIUS > fz + BRICK_LENGTH) { continue; }

        for(y = 0; y < GRID_SZ; y++) {
            for(x = 0; x < GRID_SZ; x++) {
                box_side hit_result;
                int idx = y * GRID_SZ + x;
                float lx = ((f32)x)-2.0f - BRICK_SZ/2.0f;
                float dy = ((f32)y)-2.0f - BRICK_SZ/2.0f;
                if(level0.layers[lvl].cells[idx] == -1) { continue; }
                hit_result = sphere_vs_box(ball_x, ball_y, ball_z, BALL_RADIUS,
                    lx, dy, fz, BRICK_SZ, BRICK_SZ, BRICK_LENGTH
                );
                
                if(hit_result == NO_HIT) { continue; }
                switch(hit_result) {
                    case NO_HIT: 
                        continue;
                        break;
                    case LEFT:
                    case RIGHT:
                        ball_dx = -ball_dx;
                        level0.layers[lvl].cells[idx] = -1;
                        bricks_left--;
                        break;
                    case TOP:
                    case BOTTOM:
                        ball_dy = -ball_dy;
                        level0.layers[lvl].cells[idx] = -1;
                        bricks_left--;
                        break;
                    case FRONT:
                    case BACK:
                        ball_dz = -ball_dz;
                        level0.layers[lvl].cells[idx] = -1;
                        bricks_left--;
                        break;
                    default:
                        break;
                }

                if(bricks_left == 0) {
                    hard_mode = !hard_mode;
                    game_load(ei);
                    return;
                }

                goto after_collision;
            }
        }
    }
    skip_collision:;

    
    puck_hit = sphere_vs_box(
        ball_x, ball_y, ball_z, BALL_RADIUS,
        puck_pos_x-PUCK_SIZE*0.5f, puck_pos_y-PUCK_SIZE*0.5f, puck_pos_z-PUCK_LENGTH*0.5f,
        PUCK_SIZE, PUCK_SIZE, PUCK_LENGTH
    );

    

    /* reflect against back wall */
    if(ball_dz < 0) {
        if(puck_hit != NO_HIT) {
            float hit_x  = ball_x - puck_pos_x;
            float hit_y = ball_y - puck_pos_y;
            float nx = hit_x / PUCK_SIZE*0.5f;
            float ny = hit_y / PUCK_SIZE*0.5f;

            float speed = 1.15f*my_sqrt(ball_dx*ball_dx + ball_dy*ball_dy + ball_dz*ball_dz);

            ball_dx = nx * speed * 0.6f;
            ball_dy = ny * speed * 0.6f;
            ball_dz = -ball_dz; /* still reflect Z */
        } else if (ball_z < (NEAR_ARENA+BALL_RADIUS)) {
            game_load(ei);
            return;
        }
    }

    if (ball_z >= (FAR_ARENA-BALL_RADIUS) && ball_dz > 0.0f) {
        ball_dz = -ball_dz;
    }
    after_collision:;
    /* reflect against left or right walls*/
    if((ball_x < (-2.5f+BALL_RADIUS) && ball_dx < 0.0f) || (ball_x >= (2.5f-BALL_RADIUS)&& ball_dx > 0.0f)) {
        ball_dx = -ball_dx;
    }
    /* reflect against top or bottom walls */
    if((ball_y < (-2.5f+BALL_RADIUS) && ball_dy < 0.0f) || ((ball_y >= 2.5f-BALL_RADIUS) && ball_dy > 0.0f)) {
        ball_dy = -ball_dy;
    }
    
}

void draw_brick(ExotiqueInterface *ei, float cx, float cy, float cz, float size, float length, int color, u8 z_buffer, u8 transparent) {
    /* 5 quads to draw */

    float lx = cx - size/2.0f;
    float rx = cx + size/2.0f;
    float dy = cy - size/2.0f;
    float uy = cy + size/2.0f;
    float fz = cz - length/2.0f;
    float bz = cz + length/2.0f;

    draw_quad(ei,  lx,uy,fz, rx,uy,fz, rx,dy,fz, lx,dy,fz, (u8)color, 1.0f, z_buffer, transparent);

    /* bot */
    draw_quad(ei, lx,dy,fz, rx,dy,fz, rx,dy,bz, lx,dy,bz, (u8)color, 0.5f, z_buffer, transparent);

    /* top */
    draw_quad(ei, lx,uy,bz, rx,uy,bz, rx,uy,fz, lx,uy,fz, (u8)color, 0.5f, z_buffer, transparent);

    /* left */
    draw_quad(ei, lx,dy,bz, lx,uy,bz, lx,uy,fz, lx,dy,fz, (u8)color, 0.5f, z_buffer, transparent);

    /* right */
    draw_quad(ei, rx,dy,fz, rx,uy,fz, rx,uy,bz, rx,dy,bz, (u8)color, 0.5f, z_buffer, transparent);
}

void draw_layer(ExotiqueInterface *ei, level* lvl, int lay) {
    int y,x;
    float layer_depth = layer_depths[lay];
    for(y = 0; y < GRID_SZ; y++) {
        for(x = 0; x < GRID_SZ; x++) {
            int c = lvl->layers[lay].cells[y*GRID_SZ+x];
            u8 transparent = (c == -2) ? TRANSPARENT : OPAQUE;
            if(c == -1) {
                continue;
            }
            draw_brick(ei, ((f32)x)-2.0f, ((f32)y)-2.0f, layer_depth, BRICK_SZ, BRICK_LENGTH, c == -2 ? -2 : (c*NUM_SHADES), transparent ? NO_Z_BUFFER : Z_BUFFER, transparent);
        }
    }
}

void draw_ball(ExotiqueInterface *ei) {
    Projected p;
    int x, y;
    int left_x, right_x, up_y, down_y;
    float radius;
    float ball_w = 1.0f - (NEAR / ball_z);
    project(ball_x, ball_y, ball_z, &p);

    if (p.z_view < NEAR) { return; }

    radius = project_radius(BALL_RADIUS, ball_z);

    left_x = MAX((int)(p.x-radius), 0),
    right_x = MIN((int)(p.x+radius), WIDTH-1);
    up_y = MAX((int)(p.y - radius), 0);
    down_y = MIN((int)(p.y + radius), HEIGHT-1);

    for(y = up_y; y <= down_y; y++) {
        for(x = left_x; x <= right_x; x++) {
            float dx = ((f32)x-p.x);
            float dy = ((f32)y-p.y);
            if(my_sqrt(dx*dx+dy*dy) < radius) {
                if (ball_w < zbuf[y*WIDTH+x]) {
                    /*u8 idx = ei->screen[y*WIDTH+x];*/
                    ei->screen[y*WIDTH+x] = 191;/*(u8)((idx & 0x1F) < 4 ? (idx & 0xF8) : idx-4);*/
                }
            }
        }
    }
}



void draw_level(ExotiqueInterface *ei, level* l) {
    int lvl;
    for(lvl = 4; lvl >= 0; lvl--) {
        draw_layer(ei, l, lvl);
    }
}



void game_draw(ExotiqueInterface *ei) {
    int y,x;
    u8 gray = (NUM_COLORS-1)*NUM_SHADES;
    zbuf_clear();

    for (y = 0; y < kScreenHeight; y++) {
        for (x = 0; x < kScreenWidth; x++) {
            ei->screen[y*kScreenWidth + x] = 0;
        }
    }
    

    /* draw back of box arena */
    draw_quad( 
        ei, 
        -5.0f, -5.0f, 20.0f,      
        -5.0f, 5.0f, 20.0f,      
        5.0f, 5.0f, 20.0f,      
        5.0f, -5.0f, 20.0f, 
        gray, 0.8f, NO_Z_BUFFER, OPAQUE
    );

    /* left side of box arena */
    draw_quad(
        ei,
        -2.5f, -2.5f, NEAR_ARENA,
        -2.5f, 2.5f, NEAR_ARENA,
        -2.5f, 2.5f, FAR_ARENA,
        -2.5f, -2.5f, FAR_ARENA,
        gray, .6f, NO_Z_BUFFER, OPAQUE
    );
    /* right side */
    draw_quad(
        ei,
        2.5f, -2.5f, FAR_ARENA,
        2.5f, 2.5f, FAR_ARENA,
        2.5f, 2.5f, NEAR_ARENA,
        2.5f, -2.5f, NEAR_ARENA,
        gray, .6f, NO_Z_BUFFER, OPAQUE
    );
    /* top */
    draw_quad(
        ei,
        2.5f, 2.5f, FAR_ARENA,
        -2.5f, 2.5f, FAR_ARENA,
        -2.5f, 2.5f, NEAR_ARENA,
        2.5f, 2.5f, NEAR_ARENA,
        gray, .5f, NO_Z_BUFFER, OPAQUE
    );
    /* bottom */
    draw_quad(
        ei,
        2.5f, -2.5f, NEAR_ARENA,
        -2.5f, -2.5f, NEAR_ARENA,
        -2.5f, -2.5f, FAR_ARENA,
        2.5f, -2.5f, FAR_ARENA,
        gray, .5f, NO_Z_BUFFER, OPAQUE
    );

    draw_level(ei, &level0);

    draw_ball(ei);

    draw_brick(
        ei, puck_pos_x, puck_pos_y, puck_pos_z, PUCK_SIZE, PUCK_LENGTH, -2, NO_Z_BUFFER, TRANSPARENT
    );

}