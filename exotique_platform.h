#ifndef EXOTIQUE_PLATFORM
#define EXOTIQUE_PLATFORM

typedef ExotiqueOptions PlatformOptions;
typedef ExotiqueInterface PlatformInterface;

#define sys_printf(fmt, ...) exotique_printf(fmt __VA_OPT__(,) __VA_ARGS__)

#endif