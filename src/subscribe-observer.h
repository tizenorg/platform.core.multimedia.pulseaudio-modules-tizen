#ifndef foosubscribeobserverfoo
#define foosubscribeobserverfoo

typedef struct pa_subscribe_observer pa_subscribe_observer;

pa_subscribe_observer* pa_subscribe_observer_get(pa_core *c);
pa_subscribe_observer* pa_subscribe_observer_ref(pa_subscribe_observer *ob);
void pa_subscribe_observer_unref(pa_subscribe_observer *ob);

/* Now only support device connection changed event.
 * This can be move out to general header like 'tizen-def.h' when other use-cases come out. */
typedef enum pa_tizen_event_type {
    PA_TIZEN_EVENT_DEVICE_CONNECTION_CHANGED,
    PA_TIZEN_EVENT_MAX,
} pa_tizen_event_t;

/* This hook can be got with PA_COMMUNICATOR_HOOK_EVENT_FULLY_HANDLED */
typedef struct _hook_data_for_event_handled {
    /* Unique id of event, you can determine what event this is about
     * by comparing this with real event's id.
     * For example, event_id of pa_device_manager_hook_data_for_conn_changed */
    uint32_t event_id;
    pa_tizen_event_t event_type;
} pa_subscribe_observer_hook_data_for_event_handled;

#endif
