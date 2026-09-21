/*
 * Star Finder - point the glasses at a star.
 *
 * The phone knows where the sky is; the glasses know where your head is.
 * This plugin turns the difference into a marker on the lens.
 *
 * Why there is an alignment step
 * ------------------------------
 * Altitude comes from gravity: the Host already resolves the accelerometer
 * into gm_plugin_imu_sample_t.pitch_degrees, which is absolute and does not
 * drift. Azimuth is the hard half. There is no magnetometer in the plugin
 * ABI, so absolute heading is not observable from any single sample - it can
 * only be integrated from the gyroscope, whose raw units are undocumented.
 *
 * Both unknowns fall out of a two-star alignment, the trick a telescope GoTo
 * mount uses. The wearer points at two known stars and clicks at each. Two
 * (integrated count, true azimuth) pairs define a line: its slope is the
 * gyro scale and its intercept is the heading offset. After that, azimuth is
 * integration plus a constant.
 *
 * The sensor is mounted rotated with respect to the wearer, so horizontal
 * head motion projects onto gyro X and Y with opposite signs; X - Y recovers
 * it. See the comment in the SDK's game/snake example.
 *
 * The estimator itself lives in orientation.c so it can be compiled and
 * tested on a development host - see tests/orientation_test.c.
 */

#include "gm_plugin_lvgl_api.h"
#include "gm_plugin_libc.h"
#include "star_finder.h"
#include "orientation.h"

#define TICK_MS            33U    /* guidance refresh; the panel runs at 30 Hz */
#define UPLINK_MS         250U    /* state reports to the phone */
#define VIEW_CENTIDEG    6000     /* half-field the marker maps across: 60 deg */
#define ON_TARGET_CENTI   150     /* 1.5 deg counts as found */
#define NEAR_TARGET_CENTI 600     /* 6 deg: close enough to say so */

typedef struct {
    int32_t alt;                 /* centidegrees, signed */
    int32_t az;                  /* centidegrees, 0..35999 */
    char name[SF_NAME_MAX + 1];
    bool valid;
} sky_object_t;

typedef struct {
    const gm_plugin_host_api_t *host;
    const gm_plugin_lvgl_api_t *ui;
    const gm_plugin_libc_extension_api_t *libc;

    gm_plugin_lvgl_obj_t *screen;
    gm_plugin_lvgl_obj_t *title;
    gm_plugin_lvgl_obj_t *reticle;
    gm_plugin_lvgl_obj_t *marker;
    gm_plugin_lvgl_obj_t *hint;
    int16_t half_x;
    int16_t half_y;

    sky_object_t align_ref[2];
    sky_object_t target;

    uint8_t state;
    sf_orientation_t orientation;
    int32_t head_alt;            /* centidegrees, straight from pitch */

    uint32_t tick_ms;
    uint32_t uplink_ms;
} star_finder_t;

static star_finder_t app;

static int32_t head_separation(const star_finder_t *self)
{
    return sf_separation(self->head_alt, self->orientation.head_az,
                         self->target.alt, self->target.az);
}

static void restart_alignment(star_finder_t *self)
{
    sf_orientation_reset(&self->orientation);
    self->state = self->align_ref[0].valid ? SF_STATE_ALIGN_A : SF_STATE_WAIT;
}

/* ------------------------------------------------------------------ ui -- */

#define number gm_plugin_lvgl_style_number
#define color gm_plugin_lvgl_style_color

static void style(star_finder_t *self, gm_plugin_lvgl_obj_t *object,
                  gm_plugin_lvgl_style_prop_t property,
                  gm_plugin_lvgl_style_value_t value)
{
    self->ui->style_set(object, property, value, GM_PLUGIN_LVGL_SELECTOR_MAIN);
}

static gm_plugin_lvgl_obj_t *make_label(star_finder_t *self, const char *text,
                                        uint8_t shade)
{
    gm_plugin_lvgl_obj_t *object = self->ui->label_create(self->screen);
    if (object == 0) return 0;
    self->ui->label_set_text(object, text);
    style(self, object, GM_PLUGIN_LVGL_STYLE_TEXT_COLOR, color(shade));
    style(self, object, GM_PLUGIN_LVGL_STYLE_TEXT_ALIGN,
          number(GM_PLUGIN_LVGL_TEXT_ALIGN_CENTER));
    return object;
}

static gm_plugin_lvgl_obj_t *make_ring(star_finder_t *self, int16_t size,
                                       uint8_t border, uint8_t shade)
{
    gm_plugin_lvgl_obj_t *object = self->ui->obj_create(self->screen);
    if (object == 0) return 0;
    self->ui->obj_set_size(object, size, size);
    style(self, object, GM_PLUGIN_LVGL_STYLE_RADIUS, number(size / 2));
    style(self, object, GM_PLUGIN_LVGL_STYLE_BG_OPA,
          number(GM_PLUGIN_LVGL_OPA_TRANSPARENT));
    style(self, object, GM_PLUGIN_LVGL_STYLE_BORDER_WIDTH, number(border));
    style(self, object, GM_PLUGIN_LVGL_STYLE_BORDER_COLOR, color(shade));
    style(self, object, GM_PLUGIN_LVGL_STYLE_BORDER_OPA,
          number(GM_PLUGIN_LVGL_OPA_COVER));
    self->ui->obj_clear_flag(object, GM_PLUGIN_LVGL_FLAG_SCROLLABLE);
    return object;
}

static void show_marker(star_finder_t *self, bool visible)
{
    if (self->marker == 0) return;
    if (visible)
        self->ui->obj_clear_flag(self->marker, GM_PLUGIN_LVGL_FLAG_HIDDEN);
    else
        self->ui->obj_add_flag(self->marker, GM_PLUGIN_LVGL_FLAG_HIDDEN);
}

/* Map an angular offset onto the panel, pinning it at the edge when the
 * target is outside the mapped field rather than letting it fly off. */
static int16_t project(int32_t delta_centideg, int16_t half)
{
    int32_t offset = (delta_centideg * half) / VIEW_CENTIDEG;
    if (offset > half) offset = half;
    if (offset < -half) offset = -half;
    return (int16_t)offset;
}

static void render_find(star_finder_t *self)
{
    char text[96];
    int32_t delta_alt;
    int32_t delta_az;
    int32_t across;
    int32_t gap;

    if (!self->target.valid) {
        self->ui->label_set_text(self->title, "Star Finder");
        self->ui->label_set_text(self->hint, "Pick a target on your phone");
        show_marker(self, false);
        return;
    }

    delta_alt = self->target.alt - self->head_alt;
    delta_az = sf_shortest_az(self->orientation.head_az, self->target.az);
    across = (delta_az * sf_cos_scaled(self->head_alt)) / 1024;
    gap = head_separation(self);

    self->ui->label_set_text(self->title, self->target.name);
    show_marker(self, true);
    self->ui->obj_align(self->marker, GM_PLUGIN_LVGL_ALIGN_CENTER,
                        project(across, self->half_x),
                        (int16_t)-project(delta_alt, self->half_y));

    if (gap <= ON_TARGET_CENTI) {
        self->ui->label_set_text(self->hint, "ON TARGET");
        return;
    }
    if (gap <= NEAR_TARGET_CENTI) {
        self->libc->snprintf(text, sizeof(text), "close - %d.%d deg",
                             (int)(gap / 100), (int)((gap / 10) % 10));
    } else {
        self->libc->snprintf(text, sizeof(text), "%s %d  %s %d  (%d deg)",
                             delta_az < 0 ? "left" : "right",
                             (int)(sf_abs(delta_az) / 100),
                             delta_alt < 0 ? "down" : "up",
                             (int)(sf_abs(delta_alt) / 100),
                             (int)(gap / 100));
    }
    self->ui->label_set_text(self->hint, text);
}

static void render(star_finder_t *self)
{
    char text[96];

    if (self->title == 0 || self->hint == 0) return;

    switch (self->state) {
    case SF_STATE_WAIT:
        self->ui->label_set_text(self->title, "Star Finder");
        self->ui->label_set_text(self->hint, "Open Star Finder on your phone");
        show_marker(self, false);
        break;
    case SF_STATE_ALIGN_A:
    case SF_STATE_ALIGN_B: {
        uint8_t slot = self->state == SF_STATE_ALIGN_A ? 0U : 1U;
        const sky_object_t *reference = &self->align_ref[slot];
        self->libc->snprintf(text, sizeof(text), "Align %u of 2: %s",
                             (unsigned int)(slot + 1U),
                             reference->valid ? reference->name : "waiting");
        self->ui->label_set_text(self->title, text);
        self->ui->label_set_text(self->hint,
                                 reference->valid
                                     ? "Centre the star, then click"
                                     : "Waiting for the phone");
        show_marker(self, false);
        break;
    }
    default:
        render_find(self);
        break;
    }
}

/* ------------------------------------------------------------ messages -- */

static void report_state(star_finder_t *self)
{
    uint8_t message[SF_UP_SIZE];
    uint32_t head_az = SF_UNKNOWN;
    uint32_t gap = SF_UNKNOWN;
    uint32_t alt = (uint32_t)self->head_alt;

    if (sf_orientation_calibrated(&self->orientation)) {
        head_az = (uint32_t)self->orientation.head_az;
        if (self->target.valid) gap = (uint32_t)head_separation(self);
    }

    message[0] = SF_MAGIC;
    message[1] = SF_VERSION;
    message[2] = (uint8_t)SF_MSG_STATE;
    message[3] = self->state;
    message[4] = (uint8_t)(alt & 0xFFU);
    message[5] = (uint8_t)((alt >> 8) & 0xFFU);
    message[6] = (uint8_t)(head_az & 0xFFU);
    message[7] = (uint8_t)((head_az >> 8) & 0xFFU);
    message[8] = (uint8_t)(gap & 0xFFU);
    message[9] = (uint8_t)((gap >> 8) & 0xFFU);
    (void)self->host->bt_send(SF_CHANNEL_TO_PHONE, message, SF_UP_SIZE);
}

static void load_object(star_finder_t *self, sky_object_t *object,
                        const uint8_t *data, uint32_t length)
{
    uint32_t name_length = length - SF_DOWN_HEADER;
    int32_t alt = (int32_t)(int16_t)((uint16_t)data[4] |
                                     (uint16_t)((uint16_t)data[5] << 8));
    int32_t az = (int32_t)((uint16_t)data[6] |
                           (uint16_t)((uint16_t)data[7] << 8));

    if (name_length > SF_NAME_MAX) name_length = SF_NAME_MAX;
    if (alt < -9000) alt = -9000;
    if (alt > 9000) alt = 9000;

    object->alt = alt;
    object->az = sf_wrap_az(az);
    self->libc->memcpy(object->name, data + SF_DOWN_HEADER, name_length);
    object->name[name_length] = '\0';
    object->valid = true;
}

/* The payload is borrowed for the duration of this callback only. */
static void on_message(star_finder_t *self, const uint8_t *data,
                       uint32_t length)
{
    if (data == 0 || length < SF_DOWN_HEADER || length > SF_DOWN_MAX) return;
    if (data[0] != SF_MAGIC || data[1] != SF_VERSION) return;

    switch (data[2]) {
    case SF_MSG_ALIGN_REF:
        if (data[3] > 1U) return;
        load_object(self, &self->align_ref[data[3]], data, length);
        if (self->state == SF_STATE_WAIT && self->align_ref[0].valid)
            self->state = SF_STATE_ALIGN_A;
        break;
    case SF_MSG_TARGET:
        load_object(self, &self->target, data, length);
        break;
    case SF_MSG_RESET:
        restart_alignment(self);
        break;
    default:
        return;
    }
    render(self);
}

static void confirm_alignment(star_finder_t *self)
{
    uint8_t slot = self->state == SF_STATE_ALIGN_A ? 0U : 1U;

    if (!self->align_ref[slot].valid) return;
    sf_orientation_mark(&self->orientation, slot, self->align_ref[slot].az);

    if (slot == 0U) {
        self->state = SF_STATE_ALIGN_B;
    } else if (sf_orientation_calibrate(&self->orientation)) {
        self->state = SF_STATE_FIND;
    } else {
        /* The two sightings were too close together, or the head barely
         * moved between them. Neither yields a usable slope. */
        restart_alignment(self);
    }
    render(self);
    report_state(self);
}

/* ----------------------------------------------------------- lifecycle -- */

static gm_plugin_result_t star_finder_start(void *context)
{
    star_finder_t *self = context;
    gm_plugin_display_info_t display;
    gm_plugin_lvgl_obj_t *root = self->ui->root_get();

    if (root == 0 ||
        self->host->display_get_info(&display) != GM_PLUGIN_OK ||
        display.width < 200U || display.height < 160U)
        return GM_PLUGIN_ESTATE;

    self->ui->obj_clean(root);
    self->screen = self->ui->obj_create(root);
    if (self->screen == 0) return GM_PLUGIN_ENOMEM;
    self->ui->obj_set_size(self->screen, display.width, display.height);
    self->ui->obj_align(self->screen, GM_PLUGIN_LVGL_ALIGN_CENTER, 0, 0);
    style(self, self->screen, GM_PLUGIN_LVGL_STYLE_BG_COLOR, color(0x00));
    style(self, self->screen, GM_PLUGIN_LVGL_STYLE_BG_OPA,
          number(GM_PLUGIN_LVGL_OPA_COVER));
    style(self, self->screen, GM_PLUGIN_LVGL_STYLE_BORDER_WIDTH, number(0));
    self->ui->obj_clear_flag(self->screen, GM_PLUGIN_LVGL_FLAG_SCROLLABLE);

    self->half_x = (int16_t)(display.width / 2U - 24U);
    self->half_y = (int16_t)(display.height / 2U - 52U);

    self->title = make_label(self, "Star Finder", 0xF0);
    self->reticle = make_ring(self, 56, 2, 0x70);
    self->marker = make_ring(self, 22, 3, 0xF0);
    self->hint = make_label(self, "", 0xA0);
    if (self->title == 0 || self->reticle == 0 || self->marker == 0 ||
        self->hint == 0) {
        /* on_stop is not called after a failed start. */
        self->title = self->reticle = self->marker = self->hint = 0;
        self->screen = 0;
        self->ui->obj_clean(root);
        return GM_PLUGIN_ENOMEM;
    }
    self->ui->obj_align(self->title, GM_PLUGIN_LVGL_ALIGN_TOP_MID, 0, 22);
    self->ui->obj_align(self->reticle, GM_PLUGIN_LVGL_ALIGN_CENTER, 0, 0);
    self->ui->obj_align(self->marker, GM_PLUGIN_LVGL_ALIGN_CENTER, 0, 0);
    self->ui->obj_align(self->hint, GM_PLUGIN_LVGL_ALIGN_BOTTOM_MID, 0, -24);

    if (self->host->imu_enable(GM_PLUGIN_IMU_ENABLE_RAW) != GM_PLUGIN_OK) {
        self->title = self->reticle = self->marker = self->hint = 0;
        self->screen = 0;
        self->ui->obj_clean(root);
        return GM_PLUGIN_EIO;
    }

    self->tick_ms = 0U;
    self->uplink_ms = 0U;
    render(self);
    return GM_PLUGIN_OK;
}

static void star_finder_loop(void *context, uint32_t elapsed_ms)
{
    star_finder_t *self = context;
    gm_plugin_imu_sample_t sample;

    if (elapsed_ms > 1000U) elapsed_ms = 1000U;

    if (self->host->imu_read(&sample) == GM_PLUGIN_OK) {
        self->head_alt = (int32_t)sample.pitch_degrees * 100;
        if (!sf_orientation_integrate(&self->orientation,
                                      (int32_t)sample.gyro_raw[0] -
                                          (int32_t)sample.gyro_raw[1],
                                      elapsed_ms)) {
            /* Drift outran what the estimate can justify. */
            restart_alignment(self);
        }
    }

    self->tick_ms += elapsed_ms;
    if (self->tick_ms >= TICK_MS) {
        self->tick_ms = 0U;
        render(self);
    }

    self->uplink_ms += elapsed_ms;
    if (self->uplink_ms >= UPLINK_MS) {
        self->uplink_ms = 0U;
        report_state(self);
    }
}

static bool star_finder_event(void *context, const gm_plugin_event_t *event)
{
    star_finder_t *self = context;

    if (event == 0) return false;

    if (event->type == GM_PLUGIN_EVENT_BT_MESSAGE) {
        if (event->data.bt.channel != SF_CHANNEL_TO_GLASSES) return false;
        on_message(self, event->data.bt.data, event->data.bt.length);
        return true;
    }

    if (event->type != GM_PLUGIN_EVENT_BUTTON) return false;
    if (event->data.button.button != GM_PLUGIN_BUTTON_PRIMARY) return false;

    switch (event->data.button.action) {
    case GM_PLUGIN_BUTTON_ACTION_SINGLE:
        if (self->state == SF_STATE_ALIGN_A || self->state == SF_STATE_ALIGN_B)
            confirm_alignment(self);
        return true;
    case GM_PLUGIN_BUTTON_ACTION_DOUBLE:
        /* Drift accumulates, and the wearer can feel it before we can. */
        restart_alignment(self);
        render(self);
        return true;
    case GM_PLUGIN_BUTTON_ACTION_LONG:
        self->host->app_exit();
        return true;
    default:
        return false;
    }
}

static void star_finder_stop(void *context)
{
    star_finder_t *self = context;
    gm_plugin_lvgl_obj_t *root = self->ui->root_get();

    (void)self->host->imu_enable(GM_PLUGIN_IMU_ENABLE_NONE);
    if (root != 0) self->ui->obj_clean(root);
    self->title = 0;
    self->reticle = 0;
    self->marker = 0;
    self->hint = 0;
    self->screen = 0;
}

gm_plugin_result_t gm_plugin_entry(const gm_plugin_host_api_t *host,
                                   gm_plugin_descriptor_t *plugin)
{
    const gm_plugin_capabilities_t required =
        GM_PLUGIN_CAP_BUTTON | GM_PLUGIN_CAP_IMU_RAW | GM_PLUGIN_CAP_BLUETOOTH;

    if (host == 0 || plugin == 0 || host->log == 0 || host->app_exit == 0 ||
        host->display_get_info == 0 || host->imu_enable == 0 ||
        host->imu_read == 0 || host->bt_send == 0 ||
        host->graphics.lvgl == 0 ||
        !GM_PLUGIN_VERSION_COMPATIBLE(host->abi_version,
                                      GM_PLUGIN_ABI_MIN_VERSION) ||
        host->struct_size < GM_PLUGIN_HOST_API_MIN_SIZE ||
        plugin->struct_size < GM_PLUGIN_DESCRIPTOR_MIN_SIZE ||
        (host->capabilities & required) != required)
        return GM_PLUGIN_ENOTSUP;

    app.ui = host->graphics.lvgl;
    if (app.ui->struct_size < GM_PLUGIN_LVGL_API_MIN_SIZE ||
        !GM_PLUGIN_VERSION_COMPATIBLE(app.ui->api_version,
                                      GM_PLUGIN_LVGL_API_MIN_VERSION))
        return GM_PLUGIN_EVERSION;
    if (gm_plugin_libc_get(host, &app.libc) != GM_PLUGIN_OK)
        return GM_PLUGIN_ENOTSUP;

    app.host = host;
    app.state = SF_STATE_WAIT;
    plugin->abi_version = GM_PLUGIN_ABI_MIN_VERSION;
    plugin->context = &app;
    plugin->on_start = star_finder_start;
    plugin->on_loop = star_finder_loop;
    plugin->on_event = star_finder_event;
    plugin->on_stop = star_finder_stop;
    return GM_PLUGIN_OK;
}
