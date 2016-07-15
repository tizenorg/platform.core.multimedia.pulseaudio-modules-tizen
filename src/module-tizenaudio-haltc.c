/***
  This file is part of PulseAudio.

  Copyright 2016 Sangchul Lee <sc11.lee@samsung.com>

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2.1 of the License,
  or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulsecore/modargs.h>
#include "module-tizenaudio-haltc-symdef.h"
#include "hal-interface.h"

PA_MODULE_AUTHOR("Sangchul Lee");
PA_MODULE_DESCRIPTION("Tizen Audio HAL TC module");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(true);
PA_MODULE_USAGE(" ");

struct userdata;

struct userdata {
    pa_core *core;
    pa_hal_interface *hal_interface;
};

/* pointer to testcase functions */
typedef int32_t (*tc_fun_ptr)(pa_hal_interface *h);

/* struct describing specific testcase */
typedef struct testcase_s {
    const char* name;
    tc_fun_ptr function;
} testcase;


static int32_t notify_stream_connection_changed_p(pa_hal_interface *h)
{
    int32_t ret = 0;
    hal_stream_connection_info info;

    info.role = "media";
    info.direction = DIRECTION_OUT;
    info.idx = 100;
    info.is_connected = true;

    ret = pa_hal_interface_notify_stream_connection_changed(h, &info);

    return ret;
}

static int32_t notify_stream_connection_changed_n(pa_hal_interface *h)
{
    int32_t ret = 0;
    hal_stream_connection_info info;

    info.role = NULL;
    info.direction = DIRECTION_OUT;
    info.idx = 100;
    info.is_connected = true;

    ret = pa_hal_interface_notify_stream_connection_changed(h, &info);

    return !ret;
}

static int32_t update_route_p(pa_hal_interface *h)
{
    int32_t ret = 0;
    hal_route_info info;
    int32_t num_of_devices = 1;

    info.role = "media";
    info.device_infos = pa_xmalloc0(sizeof(hal_device_info) * num_of_devices);
    info.device_infos[0].direction = DIRECTION_OUT;
    info.device_infos[0].type = "builtin-speaker";
    info.device_infos[0].id = 100;
    info.num_of_devices = num_of_devices;

    ret = pa_hal_interface_update_route(h, &info);
    pa_xfree(info.device_infos);

    return ret;
}

static int32_t update_route_n(pa_hal_interface *h)
{
    int32_t ret = 0;
    hal_route_info info;

    info.role = NULL;

    ret = pa_hal_interface_update_route(h, &info);

    return !ret;
}

static int32_t set_get_volume_level_p(pa_hal_interface *h)
{
    int32_t ret = 0;
    uint32_t set_level = 10;
    uint32_t get_level = 0;
    const char *volume_type = "media";

    if ((ret = pa_hal_interface_set_volume_level(h, volume_type, DIRECTION_OUT, set_level)))
        return ret;
    if ((ret = pa_hal_interface_get_volume_level(h, volume_type, DIRECTION_OUT, &get_level)))
        return ret;
    if (set_level != get_level)
        return -1;

    return ret;
}

static int32_t set_get_volume_level_n(pa_hal_interface *h)
{
    int32_t ret = 0;
    uint32_t set_level = 20;
    uint32_t get_level = 0;
    const char *volume_type = "media";

    if ((ret = pa_hal_interface_set_volume_level(h, volume_type, DIRECTION_OUT, set_level)))
        return !ret;
    if ((ret = pa_hal_interface_get_volume_level(h, volume_type, DIRECTION_OUT, &get_level)))
        return !ret;
    if (set_level != get_level)
        return 0;

    return !ret;
}

static int32_t pcm_open_close_p(pa_hal_interface *h)
{
    int32_t ret = 0;
    pcm_handle pcm_h = NULL;
    pa_sample_spec sample_spec;
    uint32_t period_size = 0;
    uint32_t periods = 0;

    sample_spec.format = PA_SAMPLE_S16LE;
    sample_spec.rate = 44100;
    sample_spec.channels = 2;

    periods = 5;
    period_size = 6400 / pa_frame_size(&sample_spec);

    if ((ret = pa_hal_interface_pcm_open(h, &pcm_h, DIRECTION_OUT, &sample_spec, period_size, periods)))
        return ret;
    if ((ret = pa_hal_interface_pcm_close(h, pcm_h)))
        return ret;

    return ret;
}

static int32_t pcm_open_close_n(pa_hal_interface *h)
{
    int32_t ret = 0;
    pcm_handle pcm_h = NULL;
    pa_sample_spec sample_spec;
    uint32_t period_size = 0;
    uint32_t periods = 0;

    if ((ret = pa_hal_interface_pcm_open(h, &pcm_h, DIRECTION_OUT, &sample_spec, period_size, periods)))
        return !ret;
    if ((ret = pa_hal_interface_pcm_close(h, pcm_h)))
        return !ret;

    return !ret;
}


testcase tc_array[] = {
    {"notify_stream_connection_changed_p", notify_stream_connection_changed_p},
    {"notify_stream_connection_changed_n", notify_stream_connection_changed_n},
    {"update_route_p", update_route_p},
    {"update_route_n", update_route_n},
    {"set_get_volume_level_p", set_get_volume_level_p},
    {"set_get_volume_level_n", set_get_volume_level_n},
    {"pcm_open_close_p", pcm_open_close_p},
    {"pcm_open_close_n", pcm_open_close_n},
    {NULL, NULL}
};

static void run_test_cases(pa_hal_interface *h)
{
    int32_t i = 0;
    int32_t success = 0;
    int32_t result = 0;

    pa_log_info("[TizenAudioHAL_TC][START]====================================================");

    for (i = 0; tc_array[i].name; i++) {
        if (!(result = tc_array[i].function(h))) {
            pa_log_info("[TizenAudioHAL_TC][SUCCESS][%s]", tc_array[i].name);
            success++;
        } else
            pa_log_error("[TizenAudioHAL_TC][FAILURE][%s][Error:%x]", tc_array[i].name, result);
    }

    pa_log_info("[TizenAudioHAL_TC][END][%d/%d] SUCCESS ======================================", success, i);

    return;
}

int pa__init(pa_module *m)
{
    struct userdata *u;

    pa_assert(m);

    m->userdata = u = pa_xnew0(struct userdata, 1);
    u->core = m->core;

    if (!(u->hal_interface = pa_hal_interface_get(u->core))) {
        pa_log_error("Failed to get hal interface");
        goto fail;
    }

    pa_log_info("Tizen Audio HAL TC module is loaded\n");

    /* run TC */
    run_test_cases(u->hal_interface);

    return 0;

fail:
    pa__done(m);

    return -1;
}

void pa__done(pa_module *m)
{
    struct userdata* u;

    pa_assert(m);

    if (!(u = m->userdata))
        return;

    if (u->hal_interface)
        pa_hal_interface_unref(u->hal_interface);

    pa_xfree(u);

    pa_log_info("Tizen Audio HAL TC module is unloaded\n");
}
