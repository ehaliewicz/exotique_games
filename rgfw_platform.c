#define RGFW_IMPLEMENTATION
#define RGFW_NO_DWM
#include "RGFW.h"

#include <stdio.h>
#include <windows.h>

#include "rgfw_platform.h"
#include "mahjong.h"

PlatformInterface pi;

int sys_printf( const char * format, ... ) {
  va_list args;
  va_start(args, format);
  int ret = vprintf(format, args);
  va_end(args);
  return ret;
}


uint64_t get_perf_frequency() {
    static int init = 0;
    static LARGE_INTEGER frequency;
    if(!init) {
        QueryPerformanceFrequency(&frequency);
        init = 1;
    }
    return frequency.QuadPart;
}

uint64_t get_perf_counter() {
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return counter.QuadPart;
}

uint64_t get_ticks() {
    uint64_t freq = get_perf_frequency();
    uint64_t counter = get_perf_counter();
    return (u64)(((float)counter * 1000.0f) / (float)freq);
}


void handle_keymod(PlatformInterface *pi, RGFW_event event, int playerNum, int set) {
    PlayerInput *pii = &pi->input[playerNum];
    switch(event.key.value) {
        case RGFW_keyLeft:
            pii->left = set;
            break;
        case RGFW_keyRight:
            pii->right = set;
            break;
        case RGFW_keyA:
            pii->a = set;
            break;
        case RGFW_keyB:
            pii->b = set;
            break;
        case RGFW_keyX:
            pii->x = set;
            break;
        case RGFW_keyY:
            pii->y = set;
            break;
        case RGFW_keySpace:
            pii->select = set;
            break;
        case RGFW_keyReturn:
            pii->start = set;
            break;
    }
}

void handle_keyrelease(PlatformInterface *pi, RGFW_event event, int playerNum) {
    handle_keymod(pi, event, playerNum, 0);
}

void handle_keypress(PlatformInterface *pi, RGFW_event event, int playerNum) {
    handle_keymod(pi, event, playerNum, 1);
}

int main(int argc, const char* argv[]) {

    pi.palette = malloc(sizeof(u32)*256);
    PlatformOptions opts = mahjong_load(&pi, argc, argv);
    int width = opts.screenWidth;
    int height = opts.screenHeight;
    pi.screen = RGFW_ALLOC(sizeof(u8)*width*height);
    
    RGFW_init("East Wind", 0);
    RGFW_window* win = RGFW_createWindow("East Wind", 0, 0, width, height, RGFW_windowCenter | RGFW_windowNoResize);
    RGFW_window_setExitKey(win, RGFW_keyEscape);

    //RGFW_monitor* mon = RGFW_window_getMonitor(win);


	u32* buffer = RGFW_ALLOC((u32)(width * height * sizeof(u32)));
    
	//RGFW_formatRGBA8,   /*!< 8-bit RGBA (4 channels) */
	//RGFW_formatARGB8,   /*!< 8-bit RGBA (4 channels) */
	//RGFW_formatBGRA8,   /*!< 8-bit BGRA (4 channels) */
	//RGFW_formatABGR8,   /* black is green for some reason */

	RGFW_surface* surface = RGFW_createSurface((u8*)buffer, width, height, RGFW_formatABGR8);


    while (RGFW_window_shouldClose(win) == RGFW_FALSE) {
        
        u64 start_ms = get_ticks();


        RGFW_event event;
        while (RGFW_window_checkEvent(win, &event)) { 
            //RGFW_window_getMouse(win, &mouseX, &mouseY);
            switch(event.type) {
                case RGFW_keyPressed:
                    handle_keypress(&pi, event, 0);
                    break;
                case RGFW_keyReleased:
                    handle_keyrelease(&pi, event, 0);
                    break;

            }
        }


        mahjong_update(&pi);
        mahjong_draw(&pi);

        for(int y = 0; y < height; y++) {
            for(int x = 0; x < width; x++) {
                buffer[y*width+x] = pi.palette[pi.screen[y*width+x]];
            }
        }
        //drawBitmap(buffer, width, icon, iconX, iconY, 3, 3);
        RGFW_window_blitSurface(win, surface);
        u64 end_ms = get_ticks();
        i64 rem_time = 16 - (end_ms - start_ms);
        while(rem_time > 0) {
            //sys_printf("waiting for %i ms\n", rem_time);
            RGFW_waitForEvent(rem_time);
            end_ms = get_ticks();
            rem_time = 16 - (end_ms - start_ms);
            //i64 end_ms2 = get_ticks();
            //i64 rem_time2 = 16 - (end_ms - start_ms);
            //sys_printf("waited for %i ms, %i, %i\n", rem_time - rem_time2, end_ms, end_ms2);
        }

	}

    RGFW_surface_free(surface);
	RGFW_FREE(buffer);

    RGFW_window_close(win);
    RGFW_deinit();
    return 0;

    /*
    
    RGFW_init("East Wind", 0); // load OpenGL, Vulkan or EGL functions here with RGFW_initOpenGL, RGFW_initEGL or RGFW_initVulkan
    //RGFW_window* win = RGFW_createWindow("a window", 0, 0, opts.screenWidth, opts.screenHeight, RGFW_windowCenter | RGFW_windowNoResize);
    
    RGFW_window* win = RGFW_createWindow("Basic buffer example", 0, 0, 500, 500, RGFW_windowCenter | RGFW_windowTransparent);
    RGFW_window_setExitKey(win, RGFW_keyEscape);
        
    i32 mouseX, mouseY;

    while (RGFW_window_shouldClose(win) == RGFW_FALSE) {
        RGFW_event event;
        while (RGFW_window_checkEvent(win, &event)) {  // or RGFW_pollEvents(); if you only want callbacks / state checking
            //RGFW_window_getMouse(win, &mouseX, &mouseY);

            //if (event.type == RGFW_mouseButtonPressed && event.button.value == RGFW_mouseLeft) {
            //    printf("You clicked at x: %d, y: %d\n", mouseX, mouseY);
            //}
            if(event.type == RGFW_windowClose) {
                return 0;
            }
        }

        //if (RGFW_isMousePressed(RGFW_mouseRight)) {
        //    printf("The right mouse button was clicked at x: %d, y: %d\n", mouseX, mouseY);
        //}
    }
    */
}
