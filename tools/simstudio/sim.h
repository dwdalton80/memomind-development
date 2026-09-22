/*
 * simstudio - a host-side stand-in for the glasses Host API.
 *
 * MemoMind does not publish a Linux build of Desktop Studio, so a plugin
 * cannot be run and looked at on this machine. This harness compiles a
 * plugin natively against a mock Host table, drives its lifecycle from a
 * scenario script, and writes out what the plugin drew.
 *
 * What it does check: the lifecycle contract, the state machine, message
 * handling, layout and text. What it does NOT check: the RV32 ABI, real
 * LVGL's own layout and font metrics, firmware timing, and anything about
 * real hardware. A frame that looks right here can still be wrong on glass.
 *
 * The mock records a display list rather than rasterising: the C side has no
 * font, so layout that depends on text metrics is resolved by render.py.
 */

#ifndef SIMSTUDIO_SIM_H
#define SIMSTUDIO_SIM_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "gm_plugin.h"
#include "gm_plugin_lvgl_api.h"
#include "gm_plugin_extensions.h"

#define SIM_MAX_OBJECTS 256
#define SIM_MAX_TEXT 256
#define SIM_MAX_UPLINK 64

typedef struct {
    bool used;
    bool is_label;
    int parent;                 /* index, or -1 for the root */
    int32_t width, height;      /* -1 means size to content */
    bool has_pos;
    int32_t x, y;
    bool has_align;
    uint8_t align;
    int32_t align_dx, align_dy;
    uint32_t flags;
    char text[SIM_MAX_TEXT];
    /* Styles the mock understands; anything else is recorded and ignored. */
    int32_t bg_color, bg_opa;
    int32_t border_color, border_opa, border_width;
    int32_t radius;
    int32_t text_color, text_align;
} sim_object_t;

/* One message the plugin sent back to the phone. */
typedef struct {
    uint16_t channel;
    uint32_t length;
    uint8_t data[64];
} sim_uplink_t;

typedef struct {
    gm_plugin_host_api_t host;
    gm_plugin_descriptor_t descriptor;

    uint16_t display_width, display_height;

    /* Sensor state the scenario script drives. */
    int16_t pitch_degrees;
    int16_t gyro[3];
    int16_t accel[3];
    bool imu_raw_enabled;
    uint8_t battery;
    bool charging, wearing;

    sim_object_t objects[SIM_MAX_OBJECTS];
    int object_count;
    int root;

    sim_uplink_t uplink[SIM_MAX_UPLINK];
    int uplink_count;

    bool exited;
    bool verbose;
} sim_t;

extern sim_t sim;

void sim_init(uint16_t width, uint16_t height);
void sim_dump_json(const char *path, const char *label);
void sim_deliver_bt(uint16_t channel, const uint8_t *data, uint32_t length);
void sim_deliver_button(uint16_t button, uint16_t action);
void sim_advance_clock(uint32_t ms);

#endif
