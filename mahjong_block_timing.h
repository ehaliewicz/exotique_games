#ifndef MAHJONG_BLOCK_TIMING_H
#define MAHJONG_BLOCK_TIMING_H


typedef struct block block;

struct block {
    const char* name;
    u64 count, cumulative_count;
    u64 cur_start_ticks, total_ticks, cumulative_total_ticks;
    int num_children;
    block *children[16];
};

void enter_block(block* blk) {
    blk->cur_start_ticks = exotique_get_perf_counter();

}

void exit_block(block* blk) {
    blk->count++;
    blk->total_ticks = exotique_get_perf_counter() - blk->cur_start_ticks;
    blk->cumulative_count++;
    blk->cumulative_total_ticks += blk->total_ticks;
}

block new_timed_block(const char* name) {
    return (block){name,0,0,0,0,0,0,{NULL}};
}

#define ROOT_TIMED_BLOCK(var, name) {name, 0, 0, 0, 0, 0, 0, {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}}; do {   \
    enter_block(&var);                           \

#define START_TIMED_BLOCK(var, name, parent) {name, 0, 0, 0, 0, 0, 0, {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}}; static int init ## var = 0; \
    if(! init ## var ) { block_add_child(&parent, &var); init ## var = 1; }     \
    enter_block(&var);    do {          \

#define END_TIMED_BLOCK(blk)    \
    exit_block(&blk);           \
} while(0);                     \

void print_prefix(int depth) {
    for(int i = 0; i < depth; i++) {
        exotique_printf("  ");
    }
    exotique_printf("-");
}

f64 perf_ticks_to_ms(f64 ticks) {
    static u64 freq = 0;
    if(freq == 0) {
        freq = exotique_get_perf_frequency();
    }
    return (ticks / (f64)freq)*1000.0;
}

void print_and_reset_block(block *blk, int depth) {
    f64 total_per_tick = (f64)blk->cumulative_total_ticks/(f64)blk->cumulative_count;
    u64 children_total = 0;
    u64 children_cumulative_total = 0;
    for(int i = 0; i < blk->num_children; i++) {
        children_total += blk->children[i]->total_ticks;
        children_cumulative_total += blk->children[i]->cumulative_total_ticks;
    }
    u64 exclusive_total = blk->total_ticks - children_total;
    u64 exclusive_cumulative_total = blk->cumulative_total_ticks - children_cumulative_total;
    f64 exclusive_per_tick = (f64)exclusive_cumulative_total / (f64)blk->cumulative_count;

    print_prefix(depth); exotique_printf("%11s %5.3fms   %5.3fms  %5.3fms   %5.3fms\n", blk->name, perf_ticks_to_ms((f64)blk->total_ticks), perf_ticks_to_ms(total_per_tick), perf_ticks_to_ms((f64)exclusive_total), perf_ticks_to_ms(exclusive_per_tick));
        
    for(int i = 0; i < blk->num_children; i++) {
        print_and_reset_block(blk->children[i], depth+1);
    }
    //print_prefix(depth);
    //exotique_printf("END %s\n", blk->name);

    blk->count = 0;
    blk->total_ticks = 0;
    blk->cumulative_total_ticks = 0;
    blk->cumulative_count = 0;
}

void print_and_reset_root_block(block *blk) {
    exotique_printf("                 inc   inc per      exc   exc per\n", blk->name);
    print_and_reset_block(blk, 0);
}

void block_add_child(block *parent, block *child) {
    parent->children[parent->num_children++] = child;
}



#endif