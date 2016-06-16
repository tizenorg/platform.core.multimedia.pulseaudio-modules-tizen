/***
  This file is part of PulseAudio.

  Copyright 2015 Sangchul Lee <sc11.lee@samsung.com>

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

#ifndef foostreammanagervolumeprivfoo
#define foostreammanagervolumeprivfoo

#include "stream-manager.h"

#include <vconf.h>
#include <vconf-keys.h>

#define VCONFKEY_OUT_VOLUME_PREFIX         "file/private/sound/volume/"
#define MASTER_VOLUME_TYPE                 "master"
#define MASTER_VOLUME_LEVEL_MAX            100

typedef enum {
    GET_VOLUME_CURRENT_LEVEL,
    GET_VOLUME_MAX_LEVEL
} pa_volume_get_command_t;

int32_t init_volumes(pa_stream_manager *m);
void deinit_volumes(pa_stream_manager *m);
int32_t set_volume_level_by_type(pa_stream_manager *m, stream_type_t stream_type, const char *volume_type, uint32_t volume_level);
int32_t get_volume_level_by_type(pa_stream_manager *m, pa_volume_get_command_t command, stream_type_t stream_type, const char *volume_type, uint32_t *volume_level);
int32_t set_volume_level_by_idx(pa_stream_manager *m, stream_type_t stream_type, uint32_t idx, uint32_t volume_level);
int32_t set_volume_level_with_new_data(pa_stream_manager *m, void *stream, stream_type_t stream_type, uint32_t volume_level);
int32_t set_volume_mute_by_type(pa_stream_manager *m, stream_type_t stream_type, const char *volume_type, bool volume_mute);
int32_t get_volume_mute_by_type(pa_stream_manager *m, stream_type_t stream_type, const char *volume_type, bool *volume_mute);
int32_t set_volume_mute_by_idx(pa_stream_manager *m, stream_type_t stream_type, uint32_t stream_idx, bool volume_mute);
int32_t set_volume_mute_with_new_data(pa_stream_manager *m, void *stream, stream_type_t stream_type, bool volume_mute);
int32_t get_volume_mute_by_idx(pa_stream_manager *m, stream_type_t stream_type, uint32_t stream_idx, bool *volume_mute);

#endif
