/*
 * hello_hud - MemoMind glasses plugin.
 *
 * Lifecycle contract (see .sdk/GlassSDK/docs/ABI.md):
 *   gm_plugin_entry  validate the Host, publish callbacks, acquire nothing
 *   on_load          non-UI resources for the loaded image
 *   on_start         create UI, begin one visible cycle
 *   on_loop          one short non-blocking update
 *   on_event         one borrowed Host event, handled synchronously
 *   on_stop          release the visible cycle
 *   on_unload        release what on_load acquired
 *
 * Budgets: 500 KiB Flash, <100 KiB static RAM, 1024 B per function frame,
 * 8 KiB display-task stack shared with the firmware. Never block or sleep.
 */

#include "gm_plugin_lvgl_api.h"
#include "gm_plugin_libc.h"

#define STATUS_INTERVAL_MS 1000U
#define SCREEN_MARGIN 20

typedef struct {
    const gm_plugin_host_api_t *host;
    const gm_plugin_lvgl_api_t *ui;
    const gm_plugin_libc_extension_api_t *libc;
    gm_plugin_lvgl_obj_t *screen;
    gm_plugin_lvgl_obj_t *status;
    uint32_t uptime_ms;
    uint32_t since_status_ms;
    uint32_t clicks;
} app_t;

static app_t app;

static void style(app_t *self, gm_plugin_lvgl_obj_t *object,
                  gm_plugin_lvgl_style_prop_t property,
                  gm_plugin_lvgl_style_value_t value)
{
    self->ui->style_set(object, property, value, GM_PLUGIN_LVGL_SELECTOR_MAIN);
}

static gm_plugin_lvgl_obj_t *label(app_t *self, gm_plugin_lvgl_obj_t *parent,
                                   const char *text, uint8_t shade)
{
    gm_plugin_lvgl_obj_t *object = self->ui->label_create(parent);
    if (object == 0) return 0;
    self->ui->label_set_text(object, text);
    style(self, object, GM_PLUGIN_LVGL_STYLE_TEXT_COLOR,
          gm_plugin_lvgl_style_color(shade));
    style(self, object, GM_PLUGIN_LVGL_STYLE_TEXT_ALIGN,
          gm_plugin_lvgl_style_number(GM_PLUGIN_LVGL_TEXT_ALIGN_CENTER));
    return object;
}

/* Rebuild the status line. Keep the buffer small: it lives on the shared
 * 8 KiB display-task stack and the build caps each frame at 1024 B. */
static void refresh_status(app_t *self)
{
    char text[96];
    if (self->status == 0) return;
    self->libc->snprintf(text, sizeof(text),
                         "%lus  -  battery %u%%%s  -  %s\nclicks: %lu",
                         (unsigned long)(self->uptime_ms / 1000U),
                         (unsigned int)self->host->battery_percent(),
                         self->host->battery_charging() ? " (charging)" : "",
                         self->host->wearing() ? "worn" : "off head",
                         (unsigned long)self->clicks);
    self->ui->label_set_text(self->status, text);
}

static gm_plugin_result_t app_start(void *context)
{
    app_t *self = context;
    gm_plugin_display_info_t display;
    gm_plugin_lvgl_obj_t *root = self->ui->root_get();
    gm_plugin_lvgl_obj_t *title;

    if (root == 0 ||
        self->host->display_get_info(&display) != GM_PLUGIN_OK ||
        display.width <= SCREEN_MARGIN * 2U)
        return GM_PLUGIN_ESTATE;

    self->ui->obj_clean(root);
    self->screen = self->ui->obj_create(root);
    if (self->screen == 0) return GM_PLUGIN_ENOMEM;
    self->ui->obj_set_size(self->screen, display.width, display.height);
    self->ui->obj_align(self->screen, GM_PLUGIN_LVGL_ALIGN_CENTER, 0, 0);
    style(self, self->screen, GM_PLUGIN_LVGL_STYLE_BG_COLOR,
          gm_plugin_lvgl_style_color(0x00));
    style(self, self->screen, GM_PLUGIN_LVGL_STYLE_BG_OPA,
          gm_plugin_lvgl_style_number(255));
    style(self, self->screen, GM_PLUGIN_LVGL_STYLE_BORDER_WIDTH,
          gm_plugin_lvgl_style_number(0));
    self->ui->obj_clear_flag(self->screen, GM_PLUGIN_LVGL_FLAG_SCROLLABLE);

    title = label(self, self->screen, "hello_hud", 0xF0);
    self->status = label(self, self->screen, "starting...", 0xA0);
    if (title == 0 || self->status == 0) {
        /* on_stop() never runs after a failed start: clean up here. */
        self->status = 0;
        self->screen = 0;
        self->ui->obj_clean(root);
        return GM_PLUGIN_ENOMEM;
    }
    self->ui->obj_align(title, GM_PLUGIN_LVGL_ALIGN_TOP_MID, 0, 40);
    self->ui->obj_align(self->status, GM_PLUGIN_LVGL_ALIGN_CENTER, 0, 20);

    self->uptime_ms = 0U;
    self->since_status_ms = STATUS_INTERVAL_MS;
    self->clicks = 0U;
    return GM_PLUGIN_OK;
}

static void app_loop(void *context, uint32_t elapsed_ms)
{
    app_t *self = context;
    if (elapsed_ms > 1000U) elapsed_ms = 1000U;
    self->uptime_ms += elapsed_ms;
    self->since_status_ms += elapsed_ms;
    if (self->since_status_ms < STATUS_INTERVAL_MS) return;
    self->since_status_ms = 0U;
    refresh_status(self);
}

static bool app_event(void *context, const gm_plugin_event_t *event)
{
    app_t *self = context;
    if (event == 0 || event->type != GM_PLUGIN_EVENT_BUTTON) return false;
    if (event->data.button.button != GM_PLUGIN_BUTTON_PRIMARY) return false;

    switch (event->data.button.action) {
    case GM_PLUGIN_BUTTON_ACTION_SINGLE:
        self->clicks++;
        refresh_status(self);
        return true;
    case GM_PLUGIN_BUTTON_ACTION_LONG:
        /* Return promptly: app_exit() is asynchronous. */
        self->host->app_exit();
        return true;
    default:
        return false;
    }
}

static void app_stop(void *context)
{
    app_t *self = context;
    gm_plugin_lvgl_obj_t *root = self->ui->root_get();
    if (root != 0) self->ui->obj_clean(root);
    self->status = 0;
    self->screen = 0;
}

gm_plugin_result_t gm_plugin_entry(const gm_plugin_host_api_t *host,
                                   gm_plugin_descriptor_t *plugin)
{
    if (host == 0 || plugin == 0 || host->log == 0 ||
        host->app_exit == 0 || host->display_get_info == 0 ||
        host->battery_percent == 0 || host->battery_charging == 0 ||
        host->wearing == 0 || host->graphics.lvgl == 0 ||
        !GM_PLUGIN_VERSION_COMPATIBLE(host->abi_version,
                                      GM_PLUGIN_ABI_MIN_VERSION) ||
        host->struct_size < GM_PLUGIN_HOST_API_MIN_SIZE ||
        plugin->struct_size < GM_PLUGIN_DESCRIPTOR_MIN_SIZE ||
        (host->capabilities & GM_PLUGIN_CAP_BUTTON) == 0U)
        return GM_PLUGIN_ENOTSUP;

    app.ui = host->graphics.lvgl;
    if (app.ui->struct_size < GM_PLUGIN_LVGL_API_MIN_SIZE ||
        !GM_PLUGIN_VERSION_COMPATIBLE(app.ui->api_version,
                                      GM_PLUGIN_LVGL_API_MIN_VERSION))
        return GM_PLUGIN_EVERSION;
    if (gm_plugin_libc_get(host, &app.libc) != GM_PLUGIN_OK)
        return GM_PLUGIN_ENOTSUP;

    app.host = host;
    plugin->abi_version = GM_PLUGIN_ABI_MIN_VERSION;
    plugin->context = &app;
    plugin->on_start = app_start;
    plugin->on_loop = app_loop;
    plugin->on_event = app_event;
    plugin->on_stop = app_stop;
    return GM_PLUGIN_OK;
}
