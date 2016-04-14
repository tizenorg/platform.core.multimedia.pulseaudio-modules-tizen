#ifndef foocommunicatorfoo
#define foocommunicatorfoo
#include <pulsecore/core.h>

typedef enum pa_communicator_hook {
    PA_COMMUNICATOR_HOOK_SELECT_INIT_SINK_OR_SOURCE, /* It is fired when a stream is created and needs to be set to sink or source */
    PA_COMMUNICATOR_HOOK_CHANGE_ROUTE,               /* It is fired when routing using internal codec should be processed */
    PA_COMMUNICATOR_HOOK_DEVICE_CONNECTION_CHANGED,  /* It is fired when a device is connected or disconnected */
    PA_COMMUNICATOR_HOOK_DEVICE_INFORMATION_CHANGED, /* It is fired when a device's property is changed */
    PA_COMMUNICATOR_HOOK_EVENT_FULLY_HANDLED,        /* It is fired when a event is handled by all subscribers */
    PA_COMMUNICATOR_HOOK_UPDATE_INFORMATION,         /* It is fired when routing information should be updated */
    PA_COMMUNICATOR_HOOK_MAX
} pa_communicator_hook_t;

typedef struct _pa_communicator pa_communicator;

pa_communicator* pa_communicator_get(pa_core *c);
pa_communicator* pa_communicator_ref(pa_communicator *c);
void pa_communicator_unref(pa_communicator *c);
pa_hook* pa_communicator_hook(pa_communicator *c, pa_communicator_hook_t hook);

#endif
