#ifndef foohalinterfacefoo
#define foohalinterfacefoo

/***
  This file is part of PulseAudio.

  Copyright 2015-2016 Sangchul Lee <sc11.lee@samsung.com>

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

#include <dlfcn.h>
#include <pulsecore/core.h>

typedef struct _pa_hal_interface pa_hal_interface;

typedef enum _io_direction {
    DIRECTION_IN,
    DIRECTION_OUT,
} io_direction_t;

typedef struct _hal_device_info {
    const char *type;
    uint32_t direction;
    uint32_t id;
} hal_device_info;

typedef struct _hal_route_info {
    const char *role;
    hal_device_info *device_infos;
    uint32_t num_of_devices;
} hal_route_info;

typedef struct _hal_route_option {
    const char *role;
    const char *name;
    int32_t value;
} hal_route_option;

typedef struct _hal_stream_connection_info {
    const char *role;
    uint32_t direction;
    uint32_t idx;
    bool is_connected;
} hal_stream_connection_info;

typedef struct _hal_stream_info {
    io_direction_t direction;
    const char *latency;
    pa_sample_spec *sample_spec;
} hal_stream_info;

typedef void* pcm_handle;

#define MSG_FOR_LOOPBACK_ARG_LATENCY      "loopback::latency"
#define MSG_FOR_LOOPBACK_ARG_ADJUST_TIME  "loopback::adjust_time"
typedef void (*hal_message_callback)(const char *name, int value, void *user_data);

pa_hal_interface* pa_hal_interface_get(pa_core *core);
pa_hal_interface* pa_hal_interface_ref(pa_hal_interface *h);
void pa_hal_interface_unref(pa_hal_interface *h);
int32_t pa_hal_interface_get_volume_level_max(pa_hal_interface *h, const char *volume_type, io_direction_t direction, uint32_t *level);
int32_t pa_hal_interface_get_volume_level(pa_hal_interface *h, const char *volume_type, io_direction_t direction, uint32_t *level);
int32_t pa_hal_interface_set_volume_level(pa_hal_interface *h, const char *volume_type, io_direction_t direction, uint32_t level);
int32_t pa_hal_interface_get_volume_value(pa_hal_interface *h, const char *volume_type, const char *gain_type, io_direction_t direction, uint32_t level, double *value);
int32_t pa_hal_interface_get_volume_mute(pa_hal_interface *h, const char *volume_type, io_direction_t direction, uint32_t *mute);
int32_t pa_hal_interface_set_volume_mute(pa_hal_interface *h, const char *volume_type, io_direction_t direction, uint32_t mute);
int32_t pa_hal_interface_update_route(pa_hal_interface *h, hal_route_info *info);
int32_t pa_hal_interface_update_route_option(pa_hal_interface *h, hal_route_option *option);
int32_t pa_hal_interface_notify_stream_connection_changed(pa_hal_interface *h, hal_stream_connection_info *info);
int32_t pa_hal_interface_pcm_open(pa_hal_interface *h, pcm_handle *pcm_h, io_direction_t direction, pa_sample_spec *sample_spec, uint32_t period_size, uint32_t periods);
int32_t pa_hal_interface_pcm_start(pa_hal_interface *h, pcm_handle pcm_h);
int32_t pa_hal_interface_pcm_stop(pa_hal_interface *h, pcm_handle pcm_h);
int32_t pa_hal_interface_pcm_close(pa_hal_interface *h, pcm_handle pcm_h);
int32_t pa_hal_interface_pcm_available(pa_hal_interface *h, pcm_handle pcm_h, uint32_t *available);
int32_t pa_hal_interface_pcm_write(pa_hal_interface *h, pcm_handle pcm_h, const void *buffer, uint32_t frames);
int32_t pa_hal_interface_pcm_read(pa_hal_interface *h, pcm_handle pcm_h, void *buffer, uint32_t frames);
int32_t pa_hal_interface_pcm_get_fd(pa_hal_interface *h, pcm_handle pcm_h, int *fd);
int32_t pa_hal_interface_pcm_recover(pa_hal_interface *h, pcm_handle pcm_h, int err);
int32_t pa_hal_interface_pcm_get_params(pa_hal_interface *h, pcm_handle pcm_h, uint32_t direction, void **sample_spec, uint32_t *period_size, uint32_t *periods);
int32_t pa_hal_interface_pcm_set_params(pa_hal_interface *h, pcm_handle pcm_h, uint32_t direction, void *sample_spec, uint32_t period_size, uint32_t periods);
int32_t pa_hal_interface_add_message_callback(pa_hal_interface *h, hal_message_callback callback, void *user_data);
int32_t pa_hal_interface_remove_message_callback(pa_hal_interface *h, hal_message_callback callback);

#endif
