#ifndef PLATFORM_H
#define PLATFORM_H

int sys_printf( const char * format, ... );

u64 get_ticks();

u64 get_perf_counter();

u64 get_perf_frequency();

#endif