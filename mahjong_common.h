#ifndef MAHJONG_COMMON
#define MAHJONG_COMMON

#define OUTPUT_TILE_SIZE 64
#define RENDER_TILE_SIZE (2*OUTPUT_TILE_SIZE)
#define TILE_ROUND(x) ((x+OUTPUT_TILE_SIZE-1)&(~(OUTPUT_TILE_SIZE-1)))
#define OUTPUT_WIDTH TILE_ROUND(1280)
#define OUTPUT_HEIGHT TILE_ROUND(720)
#define RENDER_WIDTH (2*OUTPUT_WIDTH)
#define RENDER_HEIGHT (2*OUTPUT_HEIGHT)
const int kScreenWidth = OUTPUT_WIDTH;
const int kScreenHeight = OUTPUT_HEIGHT;


// number of tiles to fill the screen

#define TILES_WIDE (RENDER_WIDTH/RENDER_TILE_SIZE)
#define TILES_HIGH (RENDER_HEIGHT/RENDER_TILE_SIZE)


#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define CLAMP(a, mi, ma) MIN(MAX(a, mi), ma)

void exit(int);

#endif