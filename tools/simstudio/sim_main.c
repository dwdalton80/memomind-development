/*
 * Scenario driver.
 *
 * Reads a small script describing what the wearer and the phone do, runs the
 * plugin's lifecycle against the mock Host, and writes a display-list JSON
 * file at each `snap`. render.py turns those into images.
 *
 * Script commands (one per line, # starts a comment):
 *
 *   display W H            set the panel size before start
 *   start                  gm_plugin_entry, on_load, on_start
 *   imu pitch=N gx=N gy=N gz=N      set the sensor state
 *   wear on|off            wearing detection
 *   battery N [charging]   device state
 *   loop MS                run on_loop for MS of simulated time
 *   button single|double|long|very_long|release
 *   bt CHANNEL HEX...      deliver a phone message
 *   snap NAME              write a frame
 *   expect-uplink N        fail unless at least N messages went to the phone
 *   suspend | resume
 *   stop                   on_stop then on_unload
 */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "sim.h"

#define TICK_MS 33

static const char *output_directory = ".";
static int snapshots;
static int failures;

/* ------------------------------------------------------------- dumping -- */

static void json_string(FILE *out, const char *text)
{
    fputc('"', out);
    for (; *text; text += 1) {
        unsigned char c = (unsigned char)*text;
        if (c == '"' || c == '\\') fprintf(out, "\\%c", c);
        else if (c == '\n') fputs("\\n", out);
        else if (c < 0x20) fprintf(out, "\\u%04x", c);
        else fputc(c, out);
    }
    fputc('"', out);
}

void sim_dump_json(const char *path, const char *label)
{
    FILE *out = fopen(path, "w");
    int index;
    bool first = true;

    if (out == NULL) {
        fprintf(stderr, "simstudio: cannot write %s\n", path);
        failures += 1;
        return;
    }

    fputs("{\n  \"label\": ", out);
    json_string(out, label);
    fprintf(out, ",\n  \"display\": {\"width\": %u, \"height\": %u},\n",
            sim.display_width, sim.display_height);
    fputs("  \"objects\": [\n", out);

    for (index = 0; index < sim.object_count; index += 1) {
        const sim_object_t *object = &sim.objects[index];
        if (!object->used) continue;
        if (!first) fputs(",\n", out);
        first = false;
        fprintf(out, "    {\"i\": %d, \"parent\": %d, \"type\": \"%s\"",
                index, object->parent, object->is_label ? "label" : "box");
        fprintf(out, ", \"w\": %d, \"h\": %d", object->width, object->height);
        if (object->has_pos)
            fprintf(out, ", \"pos\": [%d, %d]", object->x, object->y);
        if (object->has_align)
            fprintf(out, ", \"align\": %u, \"offset\": [%d, %d]",
                    object->align, object->align_dx, object->align_dy);
        fprintf(out, ", \"flags\": %u", object->flags);
        fprintf(out, ", \"bg\": [%d, %d]", object->bg_color, object->bg_opa);
        fprintf(out, ", \"border\": [%d, %d, %d]", object->border_color,
                object->border_opa, object->border_width);
        fprintf(out, ", \"radius\": %d", object->radius);
        fprintf(out, ", \"text_color\": %d, \"text_align\": %d",
                object->text_color, object->text_align);
        if (object->is_label) {
            fputs(", \"text\": ", out);
            json_string(out, object->text);
        }
        fputc('}', out);
    }

    fputs("\n  ],\n  \"uplink\": [\n", out);
    for (index = 0; index < sim.uplink_count; index += 1) {
        uint32_t byte;
        fprintf(out, "    {\"channel\": %u, \"bytes\": \"",
                sim.uplink[index].channel);
        for (byte = 0; byte < sim.uplink[index].length; byte += 1)
            fprintf(out, "%02x", sim.uplink[index].data[byte]);
        fprintf(out, "\"}%s\n",
                index + 1 < sim.uplink_count ? "," : "");
    }
    fputs("  ]\n}\n", out);
    fclose(out);
}

/* ------------------------------------------------------------ commands -- */

static void run_loop(uint32_t total_ms)
{
    uint32_t elapsed = 0;
    while (elapsed < total_ms) {
        uint32_t slice = total_ms - elapsed < TICK_MS ? total_ms - elapsed : TICK_MS;
        sim_advance_clock(slice);
        if (sim.descriptor.on_loop)
            sim.descriptor.on_loop(sim.descriptor.context, slice);
        elapsed += slice;
    }
}

static int parse_keyed(const char *line, const char *key, int fallback)
{
    const char *found = strstr(line, key);
    if (found == NULL) return fallback;
    return atoi(found + strlen(key));
}

static uint16_t button_action(const char *word)
{
    if (strncmp(word, "single", 6) == 0) return GM_PLUGIN_BUTTON_ACTION_SINGLE;
    if (strncmp(word, "double", 6) == 0) return GM_PLUGIN_BUTTON_ACTION_DOUBLE;
    if (strncmp(word, "very_long", 9) == 0) return GM_PLUGIN_BUTTON_ACTION_VERY_LONG;
    if (strncmp(word, "long", 4) == 0) return GM_PLUGIN_BUTTON_ACTION_LONG;
    if (strncmp(word, "release", 7) == 0) return GM_PLUGIN_BUTTON_ACTION_RELEASE;
    return GM_PLUGIN_BUTTON_ACTION_UNKNOWN;
}

static void command_bt(char *arguments)
{
    uint8_t payload[64];
    uint32_t length = 0;
    char *token = strtok(arguments, " \t");
    unsigned channel;

    if (token == NULL) return;
    channel = (unsigned)strtoul(token, NULL, 16);
    while ((token = strtok(NULL, " \t")) != NULL && length < sizeof(payload))
        payload[length++] = (uint8_t)strtoul(token, NULL, 16);
    sim_deliver_bt((uint16_t)channel, payload, length);
}

static void command_snap(const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%02d-%s.json", output_directory,
             ++snapshots, name);
    sim_dump_json(path, name);
    printf("  snap %-18s -> %s\n", name, path);
}

static void command_start(void)
{
    gm_plugin_result_t result =
        gm_plugin_entry(&sim.host, &sim.descriptor);
    if (result != GM_PLUGIN_OK) {
        fprintf(stderr, "simstudio: gm_plugin_entry failed: %d\n", result);
        exit(2);
    }
    if (sim.descriptor.struct_size != sizeof(gm_plugin_descriptor_t)) {
        fprintf(stderr, "simstudio: plugin overwrote descriptor struct_size\n");
        exit(2);
    }
    if (sim.descriptor.on_load) {
        result = sim.descriptor.on_load(sim.descriptor.context);
        if (result != GM_PLUGIN_OK) {
            fprintf(stderr, "simstudio: on_load failed: %d\n", result);
            if (sim.descriptor.on_unload)
                sim.descriptor.on_unload(sim.descriptor.context);
            exit(2);
        }
    }
    if (sim.descriptor.on_start) {
        result = sim.descriptor.on_start(sim.descriptor.context);
        if (result != GM_PLUGIN_OK) {
            fprintf(stderr, "simstudio: on_start failed: %d\n", result);
            exit(2);
        }
    }
}

static void command_stop(void)
{
    if (sim.descriptor.on_stop) sim.descriptor.on_stop(sim.descriptor.context);
    if (sim.descriptor.on_unload)
        sim.descriptor.on_unload(sim.descriptor.context);
    /* on_stop must leave the Host root clean apart from the root itself. */
    {
        int index, leaked = 0;
        for (index = 0; index < sim.object_count; index += 1)
            if (sim.objects[index].used && index != sim.root) leaked += 1;
        if (leaked > 0) {
            fprintf(stderr,
                    "simstudio: %d object(s) survived on_stop; the Host root "
                    "should have been cleaned\n", leaked);
            failures += 1;
        }
    }
}

int main(int argc, char **argv)
{
    char line[512];
    FILE *script;
    int width = 600, height = 350;
    bool started = false;

    if (argc < 3) {
        fprintf(stderr, "usage: %s <scenario> <output-dir> [--verbose]\n", argv[0]);
        return 64;
    }
    output_directory = argv[2];
    script = fopen(argv[1], "r");
    if (script == NULL) {
        fprintf(stderr, "simstudio: cannot read %s\n", argv[1]);
        return 66;
    }

    sim_init((uint16_t)width, (uint16_t)height);
    sim.verbose = argc > 3 && strcmp(argv[3], "--verbose") == 0;

    while (fgets(line, sizeof(line), script)) {
        char *hash = strchr(line, '#');
        char *command;
        char *arguments;

        if (hash) *hash = '\0';
        command = line;
        while (isspace((unsigned char)*command)) command += 1;
        if (*command == '\0') continue;
        arguments = command;
        while (*arguments && !isspace((unsigned char)*arguments)) arguments += 1;
        if (*arguments) *arguments++ = '\0';
        while (isspace((unsigned char)*arguments)) arguments += 1;
        {
            size_t length = strlen(arguments);
            while (length > 0 && isspace((unsigned char)arguments[length - 1]))
                arguments[--length] = '\0';
        }

        if (strcmp(command, "display") == 0) {
            if (started) { fprintf(stderr, "simstudio: display must precede start\n"); return 2; }
            sscanf(arguments, "%d %d", &width, &height);
            sim_init((uint16_t)width, (uint16_t)height);
        } else if (strcmp(command, "start") == 0) {
            command_start();
            started = true;
        } else if (strcmp(command, "imu") == 0) {
            sim.pitch_degrees = (int16_t)parse_keyed(arguments, "pitch=", sim.pitch_degrees);
            sim.gyro[0] = (int16_t)parse_keyed(arguments, "gx=", 0);
            sim.gyro[1] = (int16_t)parse_keyed(arguments, "gy=", 0);
            sim.gyro[2] = (int16_t)parse_keyed(arguments, "gz=", 0);
        } else if (strcmp(command, "wear") == 0) {
            sim.wearing = strcmp(arguments, "on") == 0;
        } else if (strcmp(command, "battery") == 0) {
            sim.battery = (uint8_t)atoi(arguments);
            sim.charging = strstr(arguments, "charging") != NULL;
        } else if (strcmp(command, "loop") == 0) {
            run_loop((uint32_t)atoi(arguments));
        } else if (strcmp(command, "button") == 0) {
            sim_deliver_button(GM_PLUGIN_BUTTON_PRIMARY, button_action(arguments));
        } else if (strcmp(command, "bt") == 0) {
            command_bt(arguments);
        } else if (strcmp(command, "snap") == 0) {
            command_snap(arguments);
        } else if (strcmp(command, "expect-uplink") == 0) {
            int wanted = atoi(arguments);
            if (sim.uplink_count < wanted) {
                fprintf(stderr, "simstudio: expected >=%d uplink messages, saw %d\n",
                        wanted, sim.uplink_count);
                failures += 1;
            }
        } else if (strcmp(command, "suspend") == 0) {
            if (sim.descriptor.on_suspend)
                sim.descriptor.on_suspend(sim.descriptor.context);
        } else if (strcmp(command, "resume") == 0) {
            if (sim.descriptor.on_resume)
                sim.descriptor.on_resume(sim.descriptor.context);
        } else if (strcmp(command, "stop") == 0) {
            command_stop();
            started = false;
        } else if (strcmp(command, "echo") == 0) {
            printf("  %s\n", arguments);
        } else {
            fprintf(stderr, "simstudio: unknown command '%s'\n", command);
            failures += 1;
        }
    }
    fclose(script);

    if (started) command_stop();
    printf("  %d frame(s), %d problem(s)\n", snapshots, failures);
    return failures == 0 ? 0 : 1;
}
