#ifndef MAHJONG_H
#define MAHJONG_H
void mahjong_draw(PlatformInterface* i);
void mahjong_update(PlatformInterface* i);
PlatformOptions mahjong_load(PlatformInterface* ei, int argc, const char* argv[]);

#endif