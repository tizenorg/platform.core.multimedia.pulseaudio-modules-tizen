#ifndef foostreammanagerprivfoo
#define foostreammanagerprivfoo

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

#include "stream-manager.h"
#include "hal-interface.h"
#include "communicator.h"
#include "device-manager.h"
#include "subscribe-observer.h"

#include <pulsecore/core-util.h>
#include <pulsecore/namereg.h>
#ifdef HAVE_DBUS
#include <pulsecore/dbus-shared.h>
#include <pulsecore/protocol-dbus.h>
#include <pulsecore/dbus-util.h>
#endif

typedef enum _focus_acquired_status {
    STREAM_FOCUS_ACQUIRED_NONE     = 0x00,
    STREAM_FOCUS_ACQUIRED_PLAYBACK = 0x01,
    STREAM_FOCUS_ACQUIRED_CAPTURE  = 0x02,
} focus_acquired_status_t;

typedef enum _stream_direction {
    STREAM_DIRECTION_OUT,
    STREAM_DIRECTION_IN,
    STREAM_DIRECTION_MAX,
} stream_direction_t;

#define GET_STREAM_NEW_SAMPLE_SPEC_PTR(stream, type) \
      (type == STREAM_SINK_INPUT ? &(((pa_sink_input_new_data*)stream)->sample_spec) : &(((pa_source_output_new_data*)stream)->sample_spec))

#define GET_STREAM_NEW_SAMPLE_SPEC(stream, type) \
      (type == STREAM_SINK_INPUT ? ((pa_sink_input_new_data*)stream)->sample_spec : ((pa_source_output_new_data*)stream)->sample_spec)

#define GET_STREAM_SAMPLE_SPEC(stream, type) \
      (type == STREAM_SINK_INPUT ? ((pa_sink_input*)stream)->sample_spec : ((pa_source_output*)stream)->sample_spec)

#define IS_FOCUS_ACQUIRED(focus, type) \
      (type == STREAM_SINK_INPUT ? (focus & STREAM_FOCUS_ACQUIRED_PLAYBACK) : (focus & STREAM_FOCUS_ACQUIRED_CAPTURE))

typedef struct _volume_info {
    bool is_hal_volume_type;
    struct _values {
        bool is_muted;
        uint32_t current_level;
        pa_idxset *idx_volume_values;
    } values[STREAM_DIRECTION_MAX];
} volume_info;

typedef struct _latency_info {
    int32_t maxlength;
    int32_t tlength_ms;
    int32_t minreq_ms;
    int32_t prebuf_ms;
    int32_t fragsize_ms;
} latency_info;

typedef struct _stream_info {
    int32_t priority;
    stream_route_type_t route_type;
    const char *volume_types[STREAM_DIRECTION_MAX];
    pa_idxset *idx_avail_in_devices;
    pa_idxset *idx_avail_out_devices;
    pa_idxset *idx_avail_frameworks;
} stream_info;

typedef struct _prior_max_priority_stream {
    pa_sink_input *sink_input;
    pa_source_output *source_output;
    bool need_to_update_si;
    bool need_to_update_so;
    const char *role_si;
    const char *role_so;
} cur_max_priority_stream;

typedef struct _stream_restrictions {
    bool block_recording_media;
} stream_restrictions;

struct _stream_manager {
    pa_core *core;
    pa_hal_interface *hal;
    pa_device_manager *dm;
    pa_subscribe_observer *subs_ob;

    pa_hashmap *volume_infos;
    pa_hashmap *volume_modifiers;
    pa_hashmap *stream_infos;
    pa_hashmap *latency_infos;
    pa_hashmap *stream_parents;
    pa_hashmap *muted_streams;
    cur_max_priority_stream cur_highest_priority;
    stream_restrictions restrictions;

    pa_hook_slot
        *sink_input_new_slot,
        *sink_input_put_slot,
        *sink_input_unlink_slot,
        *sink_input_state_changed_slot,
        *sink_input_move_start_slot,
        *sink_input_move_finish_slot,
        *source_output_new_slot,
        *source_output_put_slot,
        *source_output_unlink_slot,
        *source_output_state_changed_slot,
        *source_output_move_start_slot,
        *source_output_move_finish_slot;

#ifdef HAVE_DBUS
#ifdef USE_DBUS_PROTOCOL
    pa_dbus_protocol *dbus_protocol;
#else
    pa_dbus_connection *dbus_conn;
#endif
#endif
    pa_subscription *subscription;

    struct {
        pa_communicator *comm;
        pa_hook_slot *comm_hook_device_connection_changed_slot;
        pa_hook_slot *comm_hook_event_fully_handled_slot;
    } comm;
};

#endif
