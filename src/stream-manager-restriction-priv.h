#ifndef foostreammanagerrestrictionprivfoo
#define foostreammanagerrestrictionprivfoo

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

#include "stream-manager.h"

/* dbus method args */
#define STREAM_MANAGER_METHOD_ARGS_BLOCK_RECORDING_MEDIA     "block_recording_media"

int32_t handle_restrictions(pa_stream_manager *m, const char *name, uint32_t value);
bool check_restrictions(pa_stream_manager *m, void *stream, stream_type_t stream_type);

#endif
