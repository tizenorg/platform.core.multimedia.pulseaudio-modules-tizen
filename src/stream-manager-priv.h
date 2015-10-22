#ifndef foostreammanagerprivfoo
#define foostreammanagerprivfoo

#include "stream-manager.h"
#include "hal-manager.h"
#include "communicator.h"
#include "device-manager.h"

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

enum stream_direction {
    STREAM_DIRECTION_IN,
    STREAM_DIRECTION_OUT,
    STREAM_DIRECTION_MAX,
};

#define GET_STREAM_NEW_SAMPLE_SPEC(stream, type) \
      (type == STREAM_SINK_INPUT? ((pa_sink_input_new_data*)stream)->sample_spec : ((pa_source_output_new_data*)stream)->sample_spec)

#define GET_STREAM_SAMPLE_SPEC(stream, type) \
      (type == STREAM_SINK_INPUT? ((pa_sink_input*)stream)->sample_spec : ((pa_source_output*)stream)->sample_spec)

#define IS_FOCUS_ACQUIRED(focus, type) \
      (type == STREAM_SINK_INPUT? (focus & STREAM_FOCUS_ACQUIRED_PLAYBACK) : (focus & STREAM_FOCUS_ACQUIRED_CAPTURE))


typedef struct _stream_info {
    int32_t priority;
    const char *volume_type[STREAM_DIRECTION_MAX];
    stream_route_type_t route_type;
    pa_idxset *idx_avail_in_devices;
    pa_idxset *idx_avail_out_devices;
    pa_idxset *idx_avail_frameworks;
} stream_info;

typedef struct _volume_info {
    pa_bool_t is_hal_volume_type;
    struct _values {
        pa_bool_t is_muted;
        uint32_t current_level;
        pa_idxset *idx_volume_values;
    } values[STREAM_DIRECTION_MAX];
} volume_info;

typedef struct _prior_max_priority_stream {
    pa_sink_input *sink_input;
    pa_source_output *source_output;
    pa_bool_t need_to_update_si;
    pa_bool_t need_to_update_so;
} cur_max_priority_stream;

struct _stream_manager {
    pa_core *core;
    pa_hal_manager *hal;
    pa_hashmap *volume_infos;
    pa_hashmap *volume_modifiers;
    pa_hashmap *stream_infos;
    pa_hashmap *stream_parents;
    cur_max_priority_stream cur_highest_priority;
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
        *source_output_state_changed_slot;
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
        pa_hook_slot *comm_hook_need_update_route_slot;
    } comm;
};


#endif
