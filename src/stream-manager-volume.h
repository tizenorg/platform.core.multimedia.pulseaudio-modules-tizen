#ifndef foostreammanagervolumefoo
#define foostreammanagervolumefoo

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

#include "stream-manager.h"

int32_t pa_stream_manager_volume_get_level(pa_stream_manager *m, stream_type_t stream_type, const char *volume_type, uint32_t *volume_level);
int32_t pa_stream_manager_volume_set_level(pa_stream_manager *m, stream_type_t stream_type, const char *volume_type, uint32_t volume_level);
int32_t pa_stream_manager_volume_get_mute(pa_stream_manager *m, stream_type_t stream_type, const char *volume_type, bool *volume_mute);
int32_t pa_stream_manager_volume_set_mute(pa_stream_manager *m, stream_type_t stream_type, const char *volume_type, bool volume_mute);
int32_t pa_stream_manager_volume_get_max_level(pa_stream_manager *m, stream_type_t stream_type, const char *volume_type, uint32_t *volume_level);
int32_t pa_stream_manager_volume_get_mute_by_idx(pa_stream_manager *m, stream_type_t stream_type, uint32_t stream_idx, bool *volume_mute);
int32_t pa_stream_manager_volume_set_mute_by_idx(pa_stream_manager *m, stream_type_t stream_type, uint32_t stream_idx, bool volume_mute);

#endif
