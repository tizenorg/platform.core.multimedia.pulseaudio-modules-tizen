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

#include "stream-manager-priv.h"
#include "stream-manager-restriction-priv.h"

int32_t handle_restrictions(pa_stream_manager *m, const char *name, uint32_t value) {
    const char *role;
    void *s;
    uint32_t idx;

    pa_assert(m);

    if (pa_streq(name, STREAM_MANAGER_METHOD_ARGS_BLOCK_RECORDING_MEDIA) ) {
        if (value == 1) {
            pa_log_info("block recording(media)");
            m->restrictions.block_recording_media = true;
            PA_IDXSET_FOREACH(s, m->core->source_outputs, idx) {
                role = pa_proplist_gets(GET_STREAM_PROPLIST(s, STREAM_SOURCE_OUTPUT), PA_PROP_MEDIA_ROLE);
                if (pa_streq(role, "media")) {
                    pa_log_info("  -- kill source-output(%p, %u)", s, ((pa_source_output*)s)->index);
                    pa_source_output_kill((pa_source_output*)s);
                }
            }
        } else if (value == 0) {
            pa_log_info("recording(media) is now available");
            m->restrictions.block_recording_media = false;
        } else {
            pa_log_error("unknown value");
            return -1;
        }
    } else {
        pa_log_error("unknown name");
        return -1;
    }

    return 0;
}

bool check_restrictions(pa_stream_manager *m, void *stream, stream_type_t type) {
    const char *role;

    pa_assert(m);

    /* check for media recording */
    if (m->restrictions.block_recording_media && type == STREAM_SOURCE_OUTPUT) {
        role = pa_proplist_gets(GET_STREAM_NEW_PROPLIST(stream, type), PA_PROP_MEDIA_ROLE);
        if (pa_streq(role, "media")) {
            pa_log_warn("recording(media) is not allowed");
            return true;
        }
    }

    return false;
}