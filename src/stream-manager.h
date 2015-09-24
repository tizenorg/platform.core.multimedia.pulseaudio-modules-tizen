#ifndef foostreammanagerfoo
#define foostreammanagerfoo
#include <pulsecore/core.h>

/* NOTE: A role that has this prefix is only for external audio devices.
 * This is an internal rule for our implementation with stream-map.json file.
 * Before long, we need to improve it for more generalization. */
#define PREFIX_ROLE_FOR_EXTERNAL_DEV "ext-"
#define IS_ROLE_FOR_EXTERNAL_DEV(role) \
      (!strncmp(PREFIX_ROLE_FOR_EXTERNAL_DEV, role, strlen(PREFIX_ROLE_FOR_EXTERNAL_DEV)))
#define PREFIX_ROLE_FOR_FILTER "filter"
#define IS_ROLE_FOR_FILTER(role) \
      (!strncmp(PREFIX_ROLE_FOR_FILTER, role, strlen(PREFIX_ROLE_FOR_FILTER)))

typedef struct _stream_manager pa_stream_manager;

typedef enum _stream_type {
    STREAM_SINK_INPUT,
    STREAM_SOURCE_OUTPUT,
} stream_type_t;

typedef enum stream_route_type {
    STREAM_ROUTE_TYPE_AUTO,     /* the policy of decision device(s) is automatic and it's routing path is particular to one device */
    STREAM_ROUTE_TYPE_AUTO_ALL, /* the policy of decision device(s) is automatic and it's routing path can be several devices */
    STREAM_ROUTE_TYPE_MANUAL,   /* the policy of decision device(s) is manual */
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

pa_stream_manager* pa_stream_manager_init(pa_core *c);
void pa_stream_manager_done(pa_stream_manager* m);

#endif
