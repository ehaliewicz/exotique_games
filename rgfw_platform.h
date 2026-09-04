#ifndef RGFW_PLATFORM_H
#define RGFW_PLATFORM_H

#include <stdint.h>

typedef float f32;
typedef double f64;
typedef uint64_t u64;
typedef int64_t i64;
typedef uint32_t u32;
typedef int32_t i32;
typedef uint16_t u16;
typedef int16_t i16;
typedef uint8_t u8;
typedef int8_t i8;

typedef union vec2i_u vec2i_t;
union vec2i_u
{
  struct
  {
    i32 x;
    i32 y;
  } xy;
  struct
  {
    i32 width;
    i32 height;
  } wh;
};

typedef struct PlayerInput PlayerInput;
struct PlayerInput
{
  unsigned up : 1;
  unsigned down : 1;
  unsigned left : 1;
  unsigned right : 1;
  unsigned select : 1;
  unsigned start : 1;
  unsigned a : 1;
  unsigned b : 1;
  unsigned x : 1;
  unsigned y : 1;
  unsigned l1 : 1;
  unsigned r1 : 1;
  unsigned l2 : 1;
  unsigned r2 : 1;
  unsigned l3 : 1;
  unsigned r3 : 1;
  vec2i_t joystick;
};

typedef struct {
    u8* screen;   /* [kScreenPixels] */
    u32* palette; /* [255] */

    vec2i_t mouse;
    PlayerInput input[4];

    u64 ticks;
} PlatformInterface;

typedef struct {
  int screenWidth;
  int screenHeight;
} PlatformOptions;

#endif