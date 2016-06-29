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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "hal-interface.h"
#include <tizen-audio.h>
#include <pulsecore/shared.h>

#define SHARED_HAL_INTF "tizen-hal-interface"

/* Audio HAL library */
#define LIB_TIZEN_AUDIO "libtizen-audio.so"

struct _pa_hal_interface {
    PA_REFCNT_DECLARE;

    pa_core *core;
    void *dl_handle;
    void *ah_handle;
    audio_interface_t intf;
};

pa_hal_interface* pa_hal_interface_get(pa_core *core) {
    pa_hal_interface *h;

    pa_assert(core);

    if ((h = pa_shared_get(core, SHARED_HAL_INTF)))
        return pa_hal_interface_ref(h);

    h = pa_xnew0(pa_hal_interface, 1);
    PA_REFCNT_INIT(h);
    h->core = core;

    /* Load library & init HAL interface */
    h->dl_handle = dlopen(LIB_TIZEN_AUDIO, RTLD_NOW);
    if (h->dl_handle) {
        h->intf.init = dlsym(h->dl_handle, "audio_init");
        h->intf.deinit = dlsym(h->dl_handle, "audio_deinit");
        h->intf.get_volume_level_max = dlsym(h->dl_handle, "audio_get_volume_level_max");
        h->intf.get_volume_level = dlsym(h->dl_handle, "audio_get_volume_level");
        h->intf.set_volume_level = dlsym(h->dl_handle, "audio_set_volume_level");
        h->intf.get_volume_value = dlsym(h->dl_handle, "audio_get_volume_value");
        h->intf.get_volume_mute = dlsym(h->dl_handle, "audio_get_volume_mute");
        h->intf.set_volume_mute = dlsym(h->dl_handle, "audio_set_volume_mute");
        h->intf.update_route = dlsym(h->dl_handle, "audio_update_route");
        h->intf.update_route_option = dlsym(h->dl_handle, "audio_update_route_option");
        h->intf.notify_stream_connection_changed = dlsym(h->dl_handle, "audio_notify_stream_connection_changed");
        h->intf.pcm_open = dlsym(h->dl_handle, "audio_pcm_open");
        h->intf.pcm_start = dlsym(h->dl_handle, "audio_pcm_start");
        h->intf.pcm_stop = dlsym(h->dl_handle, "audio_pcm_stop");
        h->intf.pcm_close = dlsym(h->dl_handle, "audio_pcm_close");
        h->intf.pcm_avail = dlsym(h->dl_handle, "audio_pcm_avail");
        h->intf.pcm_write = dlsym(h->dl_handle, "audio_pcm_write");
        h->intf.pcm_read = dlsym(h->dl_handle, "audio_pcm_read");
        h->intf.pcm_get_fd = dlsym(h->dl_handle, "audio_pcm_get_fd");
        h->intf.pcm_recover = dlsym(h->dl_handle, "audio_pcm_recover");
        h->intf.pcm_get_params = dlsym(h->dl_handle, "audio_pcm_get_params");
        h->intf.pcm_set_params = dlsym(h->dl_handle, "audio_pcm_set_params");
        h->intf.add_message_cb = dlsym(h->dl_handle, "audio_add_message_cb");
        h->intf.remove_message_cb = dlsym(h->dl_handle, "audio_remove_message_cb");
        if (h->intf.init) {
            if (h->intf.init(&h->ah_handle) != AUDIO_RET_OK)
                pa_log_error("hal_interface init failed");
        }

     } else {
         pa_log_error("open hal_interface failed :%s", dlerror());
         return NULL;
     }

    pa_shared_set(core, SHARED_HAL_INTF, h);

    return h;
}

pa_hal_interface* pa_hal_interface_ref(pa_hal_interface *h) {
    pa_assert(h);
    pa_assert(PA_REFCNT_VALUE(h) > 0);

    PA_REFCNT_INC(h);

    return h;
}

void pa_hal_interface_unref(pa_hal_interface *h) {
    pa_assert(h);
    pa_assert(PA_REFCNT_VALUE(h) > 0);

    if (PA_REFCNT_DEC(h) > 0)
        return;

    /* Deinit HAL manager & unload library */
    if (h->intf.deinit) {
        if (h->intf.deinit(h->ah_handle) != AUDIO_RET_OK) {
            pa_log_error("hal_interface deinit failed");
        }
    }
    if (h->dl_handle) {
        dlclose(h->dl_handle);
    }

    if (h->core)
        pa_shared_remove(h->core, SHARED_HAL_INTF);

    pa_xfree(h);
}

int32_t pa_hal_interface_get_volume_level_max(pa_hal_interface *h, const char *volume_type, io_direction_t direction, uint32_t *level) {
    int32_t ret = 0;
    audio_return_t hal_ret = AUDIO_RET_OK;
    audio_volume_info_t info = {NULL, NULL, 0};

    pa_assert(h);
    pa_assert(volume_type);
    pa_assert(level);

    info.type = volume_type;
    info.direction = direction;

    if (AUDIO_RET_OK != (hal_ret = h->intf.get_volume_level_max(h->ah_handle, &info, level))) {
        pa_log_error("get_volume_level_max returns error:0x%x", hal_ret);
        ret = -1;
    }
    return ret;
}

int32_t pa_hal_interface_get_volume_level(pa_hal_interface *h, const char *volume_type, io_direction_t direction, uint32_t *level) {
    int32_t ret = 0;
    audio_return_t hal_ret = AUDIO_RET_OK;
    audio_volume_info_t info = {NULL, NULL, 0};

    pa_assert(h);
    pa_assert(volume_type);
    pa_assert(level);

    info.type = volume_type;
    info.direction = direction;

    if (AUDIO_RET_OK != (hal_ret = h->intf.get_volume_level(h->ah_handle, &info, level))) {
        pa_log_error("get_volume_level returns error:0x%x", hal_ret);
        ret = -1;
    }
    return ret;
}

int32_t pa_hal_interface_set_volume_level(pa_hal_interface *h, const char *volume_type, io_direction_t direction, uint32_t level) {
    int32_t ret = 0;
    audio_return_t hal_ret = AUDIO_RET_OK;
    audio_volume_info_t info = {NULL, NULL, 0};

    pa_assert(h);
    pa_assert(volume_type);

    info.type = volume_type;
    info.direction = direction;

    if (AUDIO_RET_OK != (hal_ret = h->intf.set_volume_level(h->ah_handle, &info, level))) {
        pa_log_error("set_volume_level returns error:0x%x", hal_ret);
        ret = -1;
    }

    return ret;
}

int32_t pa_hal_interface_get_volume_value(pa_hal_interface *h, const char *volume_type, const char *gain_type, io_direction_t direction, uint32_t level, double *value) {
    int32_t ret = 0;
    audio_return_t hal_ret = AUDIO_RET_OK;
    audio_volume_info_t info = {NULL, NULL, 0};

    pa_assert(h);
    pa_assert(volume_type);
    pa_assert(value);

    info.type = volume_type;
    info.gain = gain_type;
    info.direction = direction;

    if (AUDIO_RET_OK != (hal_ret = h->intf.get_volume_value(h->ah_handle, &info, level, value))) {
        pa_log_error("get_volume_value returns error:0x%x", hal_ret);
        ret = -1;
    }

    return ret;
}

int32_t pa_hal_interface_get_volume_mute(pa_hal_interface *h, const char *volume_type, io_direction_t direction, uint32_t *mute) {
    int32_t ret = 0;
    audio_return_t hal_ret = AUDIO_RET_OK;
    audio_volume_info_t info = {NULL, NULL, 0};

    pa_assert(h);
    pa_assert(volume_type);
    pa_assert(mute);

    info.type = volume_type;
    info.direction = direction;

    if (AUDIO_RET_OK != (hal_ret = h->intf.get_volume_mute(h->ah_handle, &info, mute))) {
        pa_log_error("get_mute returns error:0x%x", hal_ret);
        ret = -1;
    }
    return ret;
}

int32_t pa_hal_interface_set_volume_mute(pa_hal_interface *h, const char *volume_type, io_direction_t direction, uint32_t mute) {
    int32_t ret = 0;
    audio_return_t hal_ret = AUDIO_RET_OK;
    audio_volume_info_t info = {NULL, NULL, 0};

    pa_assert(h);
    pa_assert(volume_type);

    info.type = volume_type;
    info.direction = direction;

    if (AUDIO_RET_OK != (hal_ret = h->intf.set_volume_mute(h->ah_handle, &info, mute))) {
        pa_log_error("set_mute returns error:0x%x", hal_ret);
        ret = -1;
    }
    return ret;
}

int32_t pa_hal_interface_update_route(pa_hal_interface *h, hal_route_info *info) {
    int32_t ret = 0;
    audio_return_t hal_ret = AUDIO_RET_OK;

    pa_assert(h);
    pa_assert(info);

    if (AUDIO_RET_OK != (hal_ret = h->intf.update_route(h->ah_handle, (audio_route_info_t*)info))) {
        pa_log_error("update_route returns error:0x%x", hal_ret);
        ret = -1;
    }
    return ret;
}

int32_t pa_hal_interface_update_route_option(pa_hal_interface *h, hal_route_option *option) {
    int32_t ret = 0;
    audio_return_t hal_ret = AUDIO_RET_OK;

    pa_assert(h);
    pa_assert(option);

    if (AUDIO_RET_OK != (hal_ret = h->intf.update_route_option(h->ah_handle, (audio_route_option_t*)option))) {
        pa_log_error("update_route_option returns error:0x%x", hal_ret);
        ret = -1;
    }
    return ret;
}

int32_t pa_hal_interface_notify_stream_connection_changed(pa_hal_interface *h, hal_stream_connection_info *info) {
    int32_t ret = 0;
    audio_return_t hal_ret = AUDIO_RET_OK;
    audio_stream_info_t hal_info;

    pa_assert(h);
    pa_assert(info);

    hal_info.role = info->role;
    hal_info.direction = info->direction;
    hal_info.idx = info->idx;

    if (AUDIO_RET_OK != (hal_ret = h->intf.notify_stream_connection_changed(h->ah_handle, &hal_info, (uint32_t)info->is_connected))) {
        pa_log_error("notify_tream_connection_changed returns error:0x%x", hal_ret);
        ret = -1;
    }
    return ret;
}

int32_t pa_hal_interface_pcm_open(pa_hal_interface *h, pcm_handle *pcm_h, io_direction_t direction, pa_sample_spec *sample_spec, uint32_t period_size, uint32_t periods) {
    int32_t ret = 0;
    audio_return_t hal_ret = AUDIO_RET_OK;

    pa_assert(h);
    pa_assert(pcm_h);
    pa_assert(sample_spec);

    if (AUDIO_RET_OK != (hal_ret = h->intf.pcm_open(h->ah_handle, pcm_h, direction, sample_spec, period_size, periods))) {
        pa_log_error("pcm_open returns error:0x%x", hal_ret);
        ret = -1;
    }
    return ret;
}

int32_t pa_hal_interface_pcm_start(pa_hal_interface *h, pcm_handle pcm_h) {
    int32_t ret = 0;
    audio_return_t hal_ret = AUDIO_RET_OK;

    pa_assert(h);
    pa_assert(pcm_h);

    if (AUDIO_RET_OK != (hal_ret = h->intf.pcm_start(h->ah_handle, pcm_h))) {
        pa_log_error("pcm_start returns error:0x%x", hal_ret);
        ret = -1;
    }
    return ret;
}

int32_t pa_hal_interface_pcm_stop(pa_hal_interface *h, pcm_handle pcm_h) {
    int32_t ret = 0;
    audio_return_t hal_ret = AUDIO_RET_OK;

    pa_assert(h);
    pa_assert(pcm_h);

    if (AUDIO_RET_OK != (hal_ret = h->intf.pcm_stop(h->ah_handle, pcm_h))) {
        pa_log_error("pcm_stop returns error:0x%x", hal_ret);
        ret = -1;
    }
    return ret;
}

int32_t pa_hal_interface_pcm_close(pa_hal_interface *h, pcm_handle pcm_h) {
    int32_t ret = 0;
    audio_return_t hal_ret = AUDIO_RET_OK;

    pa_assert(h);
    pa_assert(pcm_h);

    if (AUDIO_RET_OK != (hal_ret = h->intf.pcm_close(h->ah_handle, pcm_h))) {
        pa_log_error("pcm_close returns error:0x%x", hal_ret);
        ret = -1;
    }
    return ret;
}

int32_t pa_hal_interface_pcm_available(pa_hal_interface *h, pcm_handle pcm_h, uint32_t *available) {
    int32_t ret = 0;
    audio_return_t hal_ret = AUDIO_RET_OK;

    pa_assert(h);
    pa_assert(pcm_h);
    pa_assert(available);

    if (AUDIO_RET_OK != (hal_ret = h->intf.pcm_avail(h->ah_handle, pcm_h, available))) {
        pa_log_error("pcm_avail returns error:0x%x", hal_ret);
        ret = -1;
    }
    return ret;
}

int32_t pa_hal_interface_pcm_write(pa_hal_interface *h, pcm_handle pcm_h, const void *buffer, uint32_t frames) {
    int32_t ret = 0;
    audio_return_t hal_ret = AUDIO_RET_OK;

    pa_assert(h);
    pa_assert(pcm_h);
    pa_assert(buffer);

    if (AUDIO_RET_OK != (hal_ret = h->intf.pcm_write(h->ah_handle, pcm_h, buffer, frames))) {
        pa_log_error("pcm_write returns error:0x%x", hal_ret);
        ret = -1;
    }
    return ret;
}

int32_t pa_hal_interface_pcm_read(pa_hal_interface *h, pcm_handle pcm_h, void *buffer, uint32_t frames) {
    int32_t ret = 0;
    audio_return_t hal_ret = AUDIO_RET_OK;

    pa_assert(h);
    pa_assert(pcm_h);
    pa_assert(buffer);

    if (AUDIO_RET_OK != (hal_ret = h->intf.pcm_read(h->ah_handle, pcm_h, buffer, frames))) {
        pa_log_error("pcm_read returns error:0x%x", hal_ret);
        ret = -1;
    }
    return ret;
}

int32_t pa_hal_interface_pcm_get_fd(pa_hal_interface *h, pcm_handle pcm_h, int *fd) {
    int32_t ret = 0;
    audio_return_t hal_ret = AUDIO_RET_OK;

    pa_assert(h);
    pa_assert(pcm_h);
    pa_assert(fd);

    if (AUDIO_RET_OK != (hal_ret = h->intf.pcm_get_fd(h->ah_handle, pcm_h, fd))) {
        pa_log_error("pcm_get_fd returns error:0x%x", hal_ret);
        ret = -1;
    }
    return ret;
}

int32_t pa_hal_interface_pcm_recover(pa_hal_interface *h, pcm_handle pcm_h, int err) {
    int32_t ret = 0;
    audio_return_t hal_ret = AUDIO_RET_OK;

    pa_assert(h);
    pa_assert(pcm_h);

    if (AUDIO_RET_OK != (hal_ret = h->intf.pcm_recover(h->ah_handle, pcm_h, err))) {
        pa_log_error("pcm_recover returns error:0x%x", hal_ret);
        ret = -1;
    }
    return ret;
}

int32_t pa_hal_interface_pcm_get_params(pa_hal_interface *h, pcm_handle pcm_h, uint32_t direction, void **sample_spec, uint32_t *period_size, uint32_t *periods) {
    int32_t ret = 0;
    audio_return_t hal_ret = AUDIO_RET_OK;

    pa_assert(h);
    pa_assert(*sample_spec);
    pa_assert(period_size);
    pa_assert(periods);

    if (AUDIO_RET_OK != (hal_ret = h->intf.pcm_get_params(h->ah_handle, pcm_h, direction, sample_spec, period_size, periods))) {
        pa_log_error("pcm_get_params returns error:0x%x", hal_ret);
        ret = -1;
    }
    return ret;
}

int32_t pa_hal_interface_pcm_set_params(pa_hal_interface *h, pcm_handle pcm_h, uint32_t direction, void *sample_spec, uint32_t period_size, uint32_t periods) {
    int32_t ret = 0;
    audio_return_t hal_ret = AUDIO_RET_OK;

    pa_assert(h);
    pa_assert(sample_spec);

    if (AUDIO_RET_OK != (hal_ret = h->intf.pcm_set_params(h->ah_handle, pcm_h, direction, sample_spec, period_size, periods))) {
        pa_log_error("pcm_set_params returns error:0x%x", hal_ret);
        ret = -1;
    }
    return ret;
}

int32_t pa_hal_interface_add_message_callback(pa_hal_interface *h, hal_message_callback callback, void *user_data) {
    int32_t ret = 0;
    audio_return_t hal_ret = AUDIO_RET_OK;

    pa_assert(h);
    pa_assert(callback);

    if (h->intf.add_message_cb == NULL) {
        pa_log_error("there is no add_message_cb symbol in this audio hal");
        ret = -1;
    } else if (AUDIO_RET_OK != (hal_ret = h->intf.add_message_cb(h->ah_handle, (message_cb)callback, user_data))) {
        pa_log_error("add_message_cb returns error:0x%x", hal_ret);
        ret = -1;
    }

    return ret;
}

int32_t pa_hal_interface_remove_message_callback(pa_hal_interface *h, hal_message_callback callback) {
    int32_t ret = 0;
    audio_return_t hal_ret = AUDIO_RET_OK;

    pa_assert(h);
    pa_assert(callback);

    if (h->intf.remove_message_cb == NULL) {
        pa_log_error("there is no remove_message_cb symbol in this audio hal");
        ret = -1;
    } else if (AUDIO_RET_OK != (hal_ret = h->intf.remove_message_cb(h->ah_handle, (message_cb)callback))) {
        pa_log_error("remove_message_cb returns error:0x%x", hal_ret);
        ret = -1;
    }

    return ret;
}
