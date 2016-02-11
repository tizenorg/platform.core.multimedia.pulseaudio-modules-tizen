#ifndef foosubscribeobserverfoo
#define foosubscribeobserverfoo

typedef struct pa_subscribe_observer pa_subscribe_observer;

pa_subscribe_observer* pa_subscribe_observer_get(pa_core *c);
pa_subscribe_observer* pa_subscribe_observer_ref(pa_subscribe_observer *ob);
void pa_subscribe_observer_unref(pa_subscribe_observer *ob);

typedef enum pa_event_type {
    PA_EVENT_TYPE_DEVICE_CONNECTION_CHANGED,
    PA_EVENT_TYPE_MAX,
} pa_event_t;

typedef struct _hook_data_for_event_handled {
    unsigned event_id;
    pa_event_t event_type;
} pa_subscribe_observer_hook_data_for_event_handled;

#endif
