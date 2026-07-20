#include "miniaudio.h"
#include "tile_click.h"
#include <stdio.h>
typedef unsigned short u16;
typedef signed short s16;

int idx = 0;
void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    // In playback mode copy data to pOutput. In capture mode read data from pInput. In full-duplex mode, both
    // pOutput and pInput will be valid and you can move data from pInput into pOutput. Never process more than
    // frameCount frames.
    s16* s16ptr = (s16*)pOutput;
    s16* s16in = (s16*)pDevice->pUserData;
    for(int i = 0; i < frameCount; i++) {
        *s16ptr++ = s16in[idx];
        *s16ptr++ = s16in[idx++];
        
        
        if(idx >= (sizeof(tile_click_raw_data)/sizeof(s16))) {
            idx = 0;
        }
    }
    printf("%i frames\n", frameCount);
}


void stop_device_callback(ma_device* pDevice) {

}

int main() {
        
    ma_device_config config = ma_device_config_init(ma_device_type_playback);

    config.playback.format   = ma_format_s16;   // Set to ma_format_unknown to use the device's native format.
    config.playback.channels = 2;               // Set to 0 to use the device's native channel count.
    config.sampleRate        = 22050;           // Set to 0 to use the device's native sample rate.
    config.dataCallback      = &data_callback;   // This function will be called when miniaudio needs more data.
    config.stopCallback      = &stop_device_callback;
    config.pUserData         = tile_click_raw_data;   // Can be accessed from the device object (device.pUserData).

    //ma_device
    ma_device device;
    
    if (ma_device_init(NULL, &config, &device) != MA_SUCCESS) {
        return 1;  // Failed to initialize the device.
    }
    
    ma_result res = ma_device_start(&device);     // The device is sleeping by default so you'll need to start it manually.
    if(res != MA_SUCCESS) {
        printf("wtf %i\n", res);
        return 1;
    }

    while(1) {

    }
}