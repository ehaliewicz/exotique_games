#include "exotique.h"
#include "exotique_platform.h"
#include "mahjong.h"


void game_draw(ExotiqueInterface* ei) {
    mahjong_draw(ei);
}
void game_update(ExotiqueInterface* ei) {
    mahjong_update(ei);
}

ExotiqueOptions game_load(ExotiqueInterface* ei, int argc, const char* argv[]) {
    return mahjong_load(ei, argc, argv);
}


u64 get_ticks() {
  return exotique_get_ticks();
}

u64 get_perf_counter() {
  return exotique_get_perf_counter();
}

u64 get_perf_frequency() {
  return exotique_get_perf_frequency();
}