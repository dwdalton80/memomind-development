/*
 * The mock Host: a gm_plugin_host_api_t the plugin cannot tell from the real
 * one at the C level, plus enough of the LVGL table to record what was drawn.
 *
 * Deliberately strict where the real Host is strict, because catching a
 * contract violation here is the whole point: objects are only created under
 * a live root, obj_clean frees the subtree, and a NULL object is a hard
 * failure rather than a silent no-op.
 */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "sim.h"

sim_t sim;

static void fail(const char *what)
{
    fprintf(stderr, "simstudio: plugin violated the Host contract: %s\n", what);
    exit(2);
}

/* ------------------------------------------------------------- objects -- */

static int object_index(const gm_plugin_lvgl_obj_t *object)
{
    /* Handles are indices offset by one so that NULL is never valid. */
    intptr_t value = (intptr_t)object;
    int index = (int)(value - 1);
    if (value == 0) fail("used a NULL LVGL object");
    if (index < 0 || index >= SIM_MAX_OBJECTS || !sim.objects[index].used)
        fail("used a stale or unknown LVGL object");
    return index;
}

static gm_plugin_lvgl_obj_t *handle_of(int index)
{
    return (gm_plugin_lvgl_obj_t *)(intptr_t)(index + 1);
}

static int allocate(int parent, bool is_label)
{
    int index;
    for (index = 0; index < SIM_MAX_OBJECTS; index += 1) {
        sim_object_t *object = &sim.objects[index];
        if (object->used) continue;
        memset(object, 0, sizeof(*object));
        object->used = true;
        object->is_label = is_label;
        object->parent = parent;
        object->width = -1;
        object->height = -1;
        object->bg_opa = -1;
        object->border_opa = -1;
        object->text_color = -1;
        object->text_align = -1;
        if (index >= sim.object_count) sim.object_count = index + 1;
        return index;
    }
    fail("created more objects than the harness can hold");
    return -1;
}

static void release_subtree(int index)
{
    int child;
    for (child = 0; child < sim.object_count; child += 1) {
        if (sim.objects[child].used && sim.objects[child].parent == index)
            release_subtree(child);
    }
    sim.objects[index].used = false;
}

static gm_plugin_lvgl_obj_t *lv_root_get(void)
{
    return handle_of(sim.root);
}

static gm_plugin_lvgl_obj_t *lv_obj_create(gm_plugin_lvgl_obj_t *parent)
{
    return handle_of(allocate(object_index(parent), false));
}

static gm_plugin_lvgl_obj_t *lv_label_create(gm_plugin_lvgl_obj_t *parent)
{
    return handle_of(allocate(object_index(parent), true));
}

static void lv_obj_delete(gm_plugin_lvgl_obj_t *object)
{
    int index = object_index(object);
    if (index == sim.root) fail("deleted the Host-owned root");
    release_subtree(index);
}

static void lv_obj_clean(gm_plugin_lvgl_obj_t *object)
{
    int index = object_index(object);
    int child;
    for (child = 0; child < sim.object_count; child += 1) {
        if (sim.objects[child].used && sim.objects[child].parent == index)
            release_subtree(child);
    }
}

static void lv_obj_set_pos(gm_plugin_lvgl_obj_t *object,
                           gm_plugin_lvgl_coord_t x, gm_plugin_lvgl_coord_t y)
{
    sim_object_t *self = &sim.objects[object_index(object)];
    self->has_pos = true;
    self->has_align = false;
    self->x = x;
    self->y = y;
}

static void lv_obj_set_size(gm_plugin_lvgl_obj_t *object,
                            gm_plugin_lvgl_coord_t w, gm_plugin_lvgl_coord_t h)
{
    sim_object_t *self = &sim.objects[object_index(object)];
    self->width = w;
    self->height = h;
}

static void lv_obj_align(gm_plugin_lvgl_obj_t *object,
                         gm_plugin_lvgl_align_t align,
                         gm_plugin_lvgl_coord_t dx, gm_plugin_lvgl_coord_t dy)
{
    sim_object_t *self = &sim.objects[object_index(object)];
    self->has_align = true;
    self->has_pos = false;
    self->align = align;
    self->align_dx = dx;
    self->align_dy = dy;
}

static void lv_obj_add_flag(gm_plugin_lvgl_obj_t *object,
                            gm_plugin_lvgl_flag_t flag)
{
    sim.objects[object_index(object)].flags |= flag;
}

static void lv_obj_clear_flag(gm_plugin_lvgl_obj_t *object,
                              gm_plugin_lvgl_flag_t flag)
{
    sim.objects[object_index(object)].flags &= ~flag;
}

static void lv_obj_invalidate(const gm_plugin_lvgl_obj_t *object)
{
    (void)object_index(object);
}

static gm_plugin_lvgl_coord_t lv_obj_get_width(const gm_plugin_lvgl_obj_t *o)
{
    return sim.objects[object_index(o)].width;
}

static gm_plugin_lvgl_coord_t lv_obj_get_height(const gm_plugin_lvgl_obj_t *o)
{
    return sim.objects[object_index(o)].height;
}

static void lv_label_set_text(gm_plugin_lvgl_obj_t *label, const char *utf8)
{
    sim_object_t *self = &sim.objects[object_index(label)];
    if (!self->is_label) fail("set text on a non-label object");
    if (utf8 == NULL) fail("set NULL label text");
    snprintf(self->text, sizeof(self->text), "%s", utf8);
}

static void lv_label_set_long_mode(gm_plugin_lvgl_obj_t *label,
                                   gm_plugin_lvgl_label_mode_t mode)
{
    (void)object_index(label);
    (void)mode;
}

static void lv_style_set(gm_plugin_lvgl_obj_t *object,
                         gm_plugin_lvgl_style_prop_t property,
                         gm_plugin_lvgl_style_value_t value,
                         gm_plugin_lvgl_selector_t selector)
{
    sim_object_t *self = &sim.objects[object_index(object)];
    (void)selector;
    switch (property) {
    case GM_PLUGIN_LVGL_STYLE_BG_COLOR:     self->bg_color = value.color.full; break;
    case GM_PLUGIN_LVGL_STYLE_BG_OPA:       self->bg_opa = value.num; break;
    case GM_PLUGIN_LVGL_STYLE_BORDER_COLOR: self->border_color = value.color.full; break;
    case GM_PLUGIN_LVGL_STYLE_BORDER_OPA:   self->border_opa = value.num; break;
    case GM_PLUGIN_LVGL_STYLE_BORDER_WIDTH: self->border_width = value.num; break;
    case GM_PLUGIN_LVGL_STYLE_RADIUS:       self->radius = value.num; break;
    case GM_PLUGIN_LVGL_STYLE_TEXT_COLOR:   self->text_color = value.color.full; break;
    case GM_PLUGIN_LVGL_STYLE_TEXT_ALIGN:   self->text_align = value.num; break;
    default: break; /* padding and the rest do not change what we draw */
    }
}

static const gm_plugin_lvgl_api_t lvgl_table = {
    .struct_size = (uint16_t)sizeof(gm_plugin_lvgl_api_t),
    .api_version = GM_PLUGIN_LVGL_API_VERSION,
    .root_get = lv_root_get,
    .obj_create = lv_obj_create,
    .obj_delete = lv_obj_delete,
    .obj_clean = lv_obj_clean,
    .obj_set_pos = lv_obj_set_pos,
    .obj_set_size = lv_obj_set_size,
    .obj_align = lv_obj_align,
    .obj_add_flag = lv_obj_add_flag,
    .obj_clear_flag = lv_obj_clear_flag,
    .obj_invalidate = lv_obj_invalidate,
    .obj_get_width = lv_obj_get_width,
    .obj_get_height = lv_obj_get_height,
    .style_set = lv_style_set,
    .label_create = lv_label_create,
    .label_set_text = lv_label_set_text,
    .label_set_long_mode = lv_label_set_long_mode,
};

/* ---------------------------------------------------------- core table -- */

static void host_log(const char *format, ...)
{
    va_list arguments;
    if (!sim.verbose) return;
    fputs("  [plugin] ", stderr);
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
}

static uint32_t monotonic;
static uint32_t host_monotonic_ms(void) { return monotonic; }
void sim_advance_clock(uint32_t ms) { monotonic += ms; }

static void *host_alloc(size_t size) { return malloc(size); }
static void host_free(void *memory) { free(memory); }

static gm_plugin_result_t host_display_get_info(gm_plugin_display_info_t *info)
{
    if (info == NULL) return GM_PLUGIN_EINVAL;
    info->width = sim.display_width;
    info->height = sim.display_height;
    info->refresh_hz = 30;
    info->pixel_format = GM_PLUGIN_PIXEL_GRAY_4;
    info->logical_display_count = 1;
    return GM_PLUGIN_OK;
}

static gm_plugin_result_t host_bt_send(gm_plugin_bt_channel_t channel,
                                       const void *data, uint32_t length)
{
    sim_uplink_t *slot;
    if (data == NULL || length == 0) return GM_PLUGIN_EINVAL;
    if (sim.uplink_count >= SIM_MAX_UPLINK)
        sim.uplink_count = 0; /* ring: only the latest matter */
    slot = &sim.uplink[sim.uplink_count++];
    slot->channel = channel;
    slot->length = length > sizeof(slot->data) ? sizeof(slot->data) : length;
    memcpy(slot->data, data, slot->length);
    return GM_PLUGIN_OK;
}

static gm_plugin_result_t host_imu_enable(gm_plugin_imu_modes_t modes)
{
    sim.imu_raw_enabled = (modes & GM_PLUGIN_IMU_ENABLE_RAW) != 0;
    return GM_PLUGIN_OK;
}

static gm_plugin_result_t host_imu_read(gm_plugin_imu_sample_t *sample)
{
    if (sample == NULL) return GM_PLUGIN_EINVAL;
    if (!sim.imu_raw_enabled) return GM_PLUGIN_ESTATE;
    memcpy(sample->accel_raw, sim.accel, sizeof(sample->accel_raw));
    memcpy(sample->gyro_raw, sim.gyro, sizeof(sample->gyro_raw));
    sample->temperature_raw = 0;
    sample->pitch_degrees = sim.pitch_degrees;
    return GM_PLUGIN_OK;
}

static uint8_t host_battery_percent(void) { return sim.battery; }
static bool host_battery_charging(void) { return sim.charging; }
static bool host_wearing(void) { return sim.wearing; }

static gm_plugin_result_t host_locale_get(char tag[GM_PLUGIN_LOCALE_TAG_MAX])
{
    if (tag == NULL) return GM_PLUGIN_EINVAL;
    snprintf(tag, GM_PLUGIN_LOCALE_TAG_MAX, "en-GB");
    return GM_PLUGIN_OK;
}

static void host_app_exit(void) { sim.exited = true; }

/* The libc extension, wired to the host's own C runtime. */
static const gm_plugin_libc_extension_api_t libc_table = {
    .memcpy = memcpy, .memmove = memmove, .memset = memset,
    .memcmp = memcmp, .memchr = memchr,
    .strlen = strlen, .strnlen = strnlen, .strcpy = strcpy,
    .strncpy = strncpy, .strcat = strcat, .strncat = strncat,
    .strcmp = strcmp, .strncmp = strncmp, .strchr = strchr,
    .strrchr = strrchr, .strstr = strstr, .strnstr = NULL,
    .strspn = strspn, .strcspn = strcspn, .strpbrk = strpbrk,
    .strtok_r = strtok_r,
    .snprintf = snprintf, .vsnprintf = vsnprintf,
};

static uint32_t random_u32(void) { return (uint32_t)rand(); }
static const gm_plugin_random_extension_api_t random_table = {
    .get_u32 = random_u32,
};

static gm_plugin_result_t host_extension_get(gm_plugin_extension_id_t id,
                                             const void **api)
{
    if (api == NULL) return GM_PLUGIN_EINVAL;
    *api = NULL;
    if (id == GM_PLUGIN_EXTENSION_LIBC) { *api = &libc_table; return GM_PLUGIN_OK; }
    if (id == GM_PLUGIN_EXTENSION_RANDOM) { *api = &random_table; return GM_PLUGIN_OK; }
    return GM_PLUGIN_ENOTSUP; /* LZ4 is not mocked */
}

/* ---------------------------------------------------------------- init -- */

void sim_init(uint16_t width, uint16_t height)
{
    memset(&sim, 0, sizeof(sim));
    monotonic = 0;
    sim.display_width = width;
    sim.display_height = height;
    sim.battery = 78;
    sim.wearing = true;

    sim.root = allocate(-1, false);
    sim.objects[sim.root].width = width;
    sim.objects[sim.root].height = height;
    sim.objects[sim.root].has_pos = true;

    sim.host.struct_size = (uint16_t)sizeof(gm_plugin_host_api_t);
    sim.host.abi_version = GM_PLUGIN_ABI_VERSION;
    sim.host.capabilities =
        GM_PLUGIN_CAP_DISPLAY_BITMAP | GM_PLUGIN_CAP_BUTTON |
        GM_PLUGIN_CAP_IMU_EVENTS | GM_PLUGIN_CAP_IMU_RAW |
        GM_PLUGIN_CAP_BLUETOOTH | GM_PLUGIN_CAP_DEVICE_STATE |
        GM_PLUGIN_CAP_DISPLAY_CONTROL | GM_PLUGIN_CAP_LOCALE;
    sim.host.log = host_log;
    sim.host.monotonic_ms = host_monotonic_ms;
    sim.host.alloc = host_alloc;
    sim.host.free = host_free;
    sim.host.display_get_info = host_display_get_info;
    sim.host.graphics.lvgl = &lvgl_table;
    sim.host.bt_send = host_bt_send;
    sim.host.imu_enable = host_imu_enable;
    sim.host.imu_read = host_imu_read;
    sim.host.battery_percent = host_battery_percent;
    sim.host.battery_charging = host_battery_charging;
    sim.host.wearing = host_wearing;
    sim.host.locale_get = host_locale_get;
    sim.host.app_exit = host_app_exit;
    sim.host.extension_get = host_extension_get;

    /* The Host zeroes the descriptor and sets its capacity before entry. */
    memset(&sim.descriptor, 0, sizeof(sim.descriptor));
    sim.descriptor.struct_size = (uint16_t)sizeof(gm_plugin_descriptor_t);
}

void sim_deliver_bt(uint16_t channel, const uint8_t *data, uint32_t length)
{
    gm_plugin_event_t event;
    if (sim.descriptor.on_event == NULL) return;
    memset(&event, 0, sizeof(event));
    event.struct_size = (uint16_t)sizeof(event);
    event.type = GM_PLUGIN_EVENT_BT_MESSAGE;
    event.timestamp_ms = monotonic;
    event.data.bt.channel = channel;
    event.data.bt.data = data;
    event.data.bt.length = length;
    (void)sim.descriptor.on_event(sim.descriptor.context, &event);
}

void sim_deliver_button(uint16_t button, uint16_t action)
{
    gm_plugin_event_t event;
    if (sim.descriptor.on_event == NULL) return;
    memset(&event, 0, sizeof(event));
    event.struct_size = (uint16_t)sizeof(event);
    event.type = GM_PLUGIN_EVENT_BUTTON;
    event.timestamp_ms = monotonic;
    event.data.button.button = button;
    event.data.button.action = action;
    (void)sim.descriptor.on_event(sim.descriptor.context, &event);
}
