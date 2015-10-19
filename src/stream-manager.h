#ifndef foostreammanagerfoo
#define foostreammanagerfoo
#include <pulsecore/core.h>

#define IS_ROUTE_TYPE_FOR_EXTERNAL_DEV(route_type_str, route_type) \
      (route_type_str && !pa_atoi(route_type_str, (int32_t*)&route_type) && (route_type == STREAM_ROUTE_TYPE_MANUAL_EXT))

#define IS_ROUTE_TYPE_FOR_AUTO(route_type_str, route_type) \
      (route_type_str && !pa_atoi(route_type_str, (int32_t*)&route_type) && (route_type == STREAM_ROUTE_TYPE_AUTO))

#define GET_STREAM_NEW_PROPLIST(stream, type) \
      (type == STREAM_SINK_INPUT? ((pa_sink_input_new_data*)stream)->proplist : ((pa_source_output_new_data*)stream)->proplist)

#define GET_STREAM_PROPLIST(stream, type) \
      (type == STREAM_SINK_INPUT? ((pa_sink_input*)stream)->proplist : ((pa_source_output*)stream)->proplist)

typedef struct _stream_manager pa_stream_manager;

typedef enum _stream_type {
    STREAM_SINK_INPUT,
    STREAM_SOURCE_OUTPUT,
} stream_type_t;

typedef enum stream_route_type {
    STREAM_ROUTE_TYPE_AUTO,       /* the policy of decision device(s) is automatic and it's routing path is particular to one device */
    STREAM_ROUTE_TYPE_AUTO_ALL,   /* the policy of decision device(s) is automatic and it's routing path can be several devices */
    STREAM_ROUTE_TYPE_MANUAL,     /* the policy of decision device(s) is manual */
    STREAM_ROUTE_TYPE_MANUAL_EXT, /* the policy of decision device(s) is manual and it's routing path is for only external devices */
} stream_route_type_t;

typedef struct _hook_call_data_for_select {
    void *stream;
    const char *stream_role;
    stream_type_t stream_type;
    stream_route_type_t route_type;
    pa_sink **proper_sink;
    pa_source **proper_source;
    pa_sample_spec sample_spec;
    pa_idxset *idx_avail_devices;
    pa_idxset *idx_manual_devices;
    pa_bool_t origins_from_new_data;
} pa_stream_manager_hook_data_for_select;

typedef struct _hook_call_data_for_route {
    void *stream;
    const char *stream_role;
    stream_type_t stream_type;
    stream_route_type_t route_type;
    pa_sink **proper_sink;
    pa_source **proper_source;
    pa_sample_spec sample_spec;
    pa_idxset *idx_avail_devices;
    pa_idxset *idx_manual_devices;
    pa_idxset *idx_streams;
    pa_bool_t origins_from_new_data;
} pa_stream_manager_hook_data_for_route;

typedef struct _hook_call_data_for_option {
    const char *stream_role;
    const char *name;
    int32_t value;
} pa_stream_manager_hook_data_for_option;

typedef struct _hook_call_data_for_update_route {
    void *stream;
    stream_type_t stream_type;
    pa_bool_t is_device_connected;
    pa_bool_t use_internal_codec;
} pa_stream_manager_hook_data_for_update_route;

int32_t pa_stream_manager_get_route_type(void *stream, pa_bool_t origins_from_new_data, stream_type_t stream_type, stream_route_type_t *stream_route_type);
void pa_stream_manager_is_current_highest_priority(void *stream, stream_type_t stream_type, pa_bool_t *highest_priority, pa_stream_manager *m);
void pa_stream_manager_is_available_device_for_auto_route(const char *cur_device_type, const char *new_device_type, const char *role, stream_type_t stream_type, pa_bool_t *available, pa_stream_manager *m);
void pa_stream_manager_find_next_priority_device_for_auto_route(const char *cur_device_type, const char *role, stream_type_t stream_type, char **next_device_type, pa_stream_manager *m);

pa_stream_manager* pa_stream_manager_init(pa_core *c);
void pa_stream_manager_done(pa_stream_manager* m);

#endif
