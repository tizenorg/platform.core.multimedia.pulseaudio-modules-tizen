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

#ifndef foostreammanagerfoo
#define foostreammanagerfoo

#include <pulsecore/core.h>

#define IS_ROUTE_TYPE_FOR_EXTERNAL_DEV(route_type_str, route_type) \
      (route_type_str && !pa_atoi(route_type_str, (int32_t*)&route_type) && (route_type == STREAM_ROUTE_TYPE_MANUAL_EXT))

#define IS_ROUTE_TYPE_FOR_AUTO(route_type_str, route_type) \
      (route_type_str && !pa_atoi(route_type_str, (int32_t*)&route_type) && (route_type == STREAM_ROUTE_TYPE_AUTO))

#define GET_STREAM_NEW_PROPLIST(stream, type) \
      (type == STREAM_SINK_INPUT ? ((pa_sink_input_new_data*)stream)->proplist : ((pa_source_output_new_data*)stream)->proplist)

#define GET_STREAM_PROPLIST(stream, type) \
      (type == STREAM_SINK_INPUT ? ((pa_sink_input*)stream)->proplist : ((pa_source_output*)stream)->proplist)

#define STREAM_ROLE_CALL_VOICE        "call-voice"
#define STREAM_ROLE_CALL_VIDEO        "call-video"
#define STREAM_ROLE_VOIP              "voip"
#define STREAM_ROLE_LOOPBACK          "loopback"
#define STREAM_ROLE_RADIO             "radio"

#define SINK_NAME_COMBINED            "sink_combined"
#define SINK_NAME_NULL                "sink_null"
#define SOURCE_NAME_NULL              "source_null"

typedef struct _stream_manager pa_stream_manager;

typedef enum _stream_type {
    STREAM_SINK_INPUT,
    STREAM_SOURCE_OUTPUT,
} stream_type_t;

typedef enum stream_route_type {
    STREAM_ROUTE_TYPE_AUTO,               /* A stream is routed automatically to a particular device which has the highest priority. */
    STREAM_ROUTE_TYPE_AUTO_LAST_CONNECTED,/* A stream is routed automatically to a particular device which has the latest connection time. */
    STREAM_ROUTE_TYPE_AUTO_ALL,           /* A stream is routed automatically to several devices simultaneously. */
    STREAM_ROUTE_TYPE_MANUAL,             /* A stream is routed manually to the device(s) selected by user. */
    STREAM_ROUTE_TYPE_MANUAL_EXT,         /* A stream is routed manually only to the external device(s) selected by user. */
} stream_route_type_t;

typedef struct _hook_call_data_for_select {
    void *stream;
    const char *stream_role;
    const char *device_role;
    const char *occupying_role;
    stream_type_t stream_type;
    stream_route_type_t route_type;
    pa_sink **proper_sink;
    pa_source **proper_source;
    pa_sample_spec sample_spec;
    pa_idxset *idx_avail_devices;
    pa_idxset *idx_manual_devices;
    bool origins_from_new_data;
} pa_stream_manager_hook_data_for_select;

typedef struct _hook_call_data_for_route {
    void *stream;
    const char *stream_role;
    const char *device_role;
    stream_type_t stream_type;
    stream_route_type_t route_type;
    pa_sink **proper_sink;
    pa_source **proper_source;
    pa_sample_spec sample_spec;
    pa_idxset *idx_avail_devices;
    pa_idxset *idx_manual_devices;
    pa_idxset *idx_streams;
    bool origins_from_new_data;
} pa_stream_manager_hook_data_for_route;

typedef struct _hook_call_data_for_update_info {
    const char *stream_role;
    const char *name;
    int32_t value;
} pa_stream_manager_hook_data_for_update_info;

int32_t pa_stream_manager_get_route_type(void *stream, stream_type_t stream_type, bool is_new_data, stream_route_type_t *stream_route_type);
bool pa_stream_manager_check_name_is_vstream(void *stream, stream_type_t type, bool is_new_data);

pa_stream_manager* pa_stream_manager_init(pa_core *c);
void pa_stream_manager_done(pa_stream_manager* m);

#endif
