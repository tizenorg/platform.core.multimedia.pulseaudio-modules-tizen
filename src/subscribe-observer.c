/***
  This file is part of PulseAudio.

  Copyright 2016 Jeonho Mok <jho.mok@samsung.com>

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

#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <pulse/util.h>
#include <pulsecore/log.h>
#include <pulsecore/core-util.h>
#include <pulsecore/shared.h>

#ifdef HAVE_DBUS
#include <pulsecore/dbus-shared.h>
#include <pulsecore/dbus-util.h>
#include <pulsecore/protocol-dbus.h>
#endif

#include "communicator.h"
#include "device-manager.h"
#include "subscribe-observer.h"

#define SHARED_SUBSCRIBE_OBSERVER "tizen-subscribe-observer"

#define BUS_NAME_AUDIO_CLIENT "org.tizen.AudioClient"
#define OBJECT_AUDIO_CLIENT "/org/tizen/AudioClient1"
#define INTERFACE_AUDIO_CLIENT "org.tizen.AudioClient1"

#define EVENT_NAME_DEVICE_DISCONNECTED "DeviceConnected"

#define FILTER_AUDIO_CLIENT                                \
    "type='signal',"                                       \
    " interface='" INTERFACE_AUDIO_CLIENT "'"

/* A subscriber can be identified with tuple of pid and subs_id,
 * because a client can subscribe same event several times */
struct subscriber {
    uint32_t pid;
    uint32_t subs_id;
};

/* An event can be identified with tuple of event_type and event_id,
 * because same type of events can occurs many times*/
struct event {
    pa_tizen_event_t event_type;
    uint32_t event_id;
};

struct pa_subscribe_observer {
    PA_REFCNT_DECLARE;

    pa_dbus_connection *dbus_conn;
    pa_core *core;
    // key : event-type, value : idxset of struct subscriber.
    pa_hashmap *subscribers;
    // key : struct event, value : idxset of struct subscriber.
    pa_hashmap *handled_events;
    struct {
        pa_communicator *comm;
        pa_hook_slot *comm_hook_device_connection_changed_slot;
    } comm;
};

static int convert_event_name_to_type(const char *event_name, pa_tizen_event_t *event_type) {
    if (!event_name || !event_type)
        return -1;

    if (!strcmp(event_name, EVENT_NAME_DEVICE_DISCONNECTED)) {
        *event_type = PA_TIZEN_EVENT_DEVICE_CONNECTION_CHANGED;
        return 0;
    }

    return -1;
}

static int convert_event_type_to_name(pa_tizen_event_t event_type, const char **event_name) {
    if (event_type < 0 || event_type >= PA_TIZEN_EVENT_MAX)
        return -1;

    if (event_type == PA_TIZEN_EVENT_DEVICE_CONNECTION_CHANGED) {
        *event_name = EVENT_NAME_DEVICE_DISCONNECTED;
        return 0;
    }

    return -1;
}

uint32_t subscriber_hash_func(const void *p) {
    struct subscriber *subscriber_p;

    subscriber_p = (struct subscriber *)p;

    return subscriber_p->pid + subscriber_p->subs_id;
}

static int subscriber_compare_func(const void *a, const void *b) {
    struct subscriber *subscriber_a, *subscriber_b;
    uint32_t pid_a, pid_b, sid_a, sid_b;

    subscriber_a = (struct subscriber *)a;
    subscriber_b = (struct subscriber *)b;

    pid_a = subscriber_a->pid;
    sid_a = subscriber_a->subs_id;
    pid_b = subscriber_b->pid;
    sid_b = subscriber_b->subs_id;

    if (pid_a > pid_b)
        return 1;
    else if (pid_a < pid_b)
        return -1;
    else
        return sid_a < sid_b ? -1 : (sid_a > sid_b ? 1 : 0);
}

static void subscriber_free_func(void *data) {
    struct subscriber *subscriber = (struct subscriber *) data;

    pa_xfree(subscriber);
}

static unsigned event_hash_func(const void *p) {
    struct event *event = (struct event*) p;

    return (unsigned)event->event_type + event->event_id;
}

static int event_compare_func(const void *a, const void *b) {
    struct event *event_a, *event_b;
    pa_tizen_event_t type_a, type_b;
    uint32_t id_a, id_b;

    event_a = (struct event *) a;
    event_b = (struct event *) b;

    type_a = event_a->event_type;
    type_b = event_b->event_type;
    id_a = event_a->event_id;
    id_b = event_b->event_id;

    if (id_a > id_b)
        return 1;
    else if (id_a < id_b)
        return -1;
    else
        return type_a < type_b ? -1 : (type_a > type_b ? 1 : 0);
}

/* Get pid of who sent 'got_msg' message. */
static int get_sender_pid(DBusConnection *c, DBusMessage *got_msg, uint32_t *_sender_pid) {
    const char *intf = "org.freedesktop.DBus", *method = "GetConnectionUnixProcessID";
    const char *sender;
    DBusMessage *msg = NULL, *reply = NULL;
    DBusError err;
    dbus_uint32_t sender_pid = 0;

    pa_assert(c);
    pa_assert(got_msg);
    pa_assert(_sender_pid);

    if (!(sender = dbus_message_get_sender(got_msg))) {
        pa_log_error("failed to get sender of msg");
        return -1;
    }

    if (!(msg = dbus_message_new_method_call("org.freedesktop.DBus", "/org/freedesktop/DBus", intf, method))) {
        pa_log_error("dbus method call failed");
        return -1;
    }

    dbus_message_append_args(msg,
                DBUS_TYPE_STRING, &sender,
                DBUS_TYPE_INVALID);

    dbus_error_init(&err);
    if (!(reply = dbus_connection_send_with_reply_and_block(c, msg, -1, &err))) {
        pa_log_error("Failed to method call %s.%s, %s", intf, method, err.message);
        dbus_error_free(&err);
        goto fail;
    }

    if (!dbus_message_get_args(reply, &err, DBUS_TYPE_UINT32, &sender_pid,
                                        DBUS_TYPE_INVALID)) {
        pa_log_error("Failed to get sender_pid from reply : %s", err.message);
        goto fail;
    }

    *_sender_pid = sender_pid;

fail:
    if (msg)
        dbus_message_unref(msg);
    if (reply)
        dbus_message_unref(reply);

    return 0;

}

static void register_subscriber(pa_hashmap *subscribers, pa_tizen_event_t event_type, uint32_t subscriber_pid, uint32_t subscription_id) {
    pa_idxset *event_subscribers;
    const char *event_name;
    struct subscriber *new_subscriber;
    struct subscriber subscriber_tmp = {subscriber_pid, subscription_id};

    pa_assert(subscribers);
    pa_assert(convert_event_type_to_name(event_type, &event_name) == 0);

    event_subscribers = pa_hashmap_get(subscribers, PA_INT_TO_PTR(event_type));

    if (!event_subscribers) {
        pa_log_debug("Create subscribers set for %s", event_name);
        event_subscribers = pa_idxset_new(subscriber_hash_func, subscriber_compare_func);
        pa_hashmap_put(subscribers, PA_INT_TO_PTR(event_type), event_subscribers);
    }

    /* Check that same subscriber exists */
    if (!pa_idxset_get_by_data(event_subscribers, &subscriber_tmp, NULL)) {
        pa_log_debug("New subscriber %d %u", subscriber_pid, subscription_id);
        new_subscriber = (struct subscriber*) pa_xmalloc(sizeof(struct subscriber));
        new_subscriber->pid = subscriber_pid;
        new_subscriber->subs_id = subscription_id;
        pa_idxset_put(event_subscribers, new_subscriber, NULL);
    } else
        pa_log_warn("Same subscriber had been registered, pid : %u, subs_id : %u", subscriber_pid, subscription_id);
}

/* If 'subscription_id' is less than 0, remove all subscribers which of pid is 'subscriber_pid' */
static void unregister_subscriber(pa_hashmap *subscribers, pa_tizen_event_t event_type, uint32_t subscriber_pid, uint32_t subscription_id) {
    pa_idxset *event_subscribers;
    const char *event_name;
    uint32_t subscriber_idx;

    pa_assert(subscribers);
    pa_assert(convert_event_type_to_name(event_type, &event_name) == 0);

    event_subscribers = pa_hashmap_get(subscribers, PA_INT_TO_PTR(event_type));
    if (!event_subscribers) {
        pa_log_info("No subscribers set for %s", event_name);
        return;
    }

    if (subscription_id > 0) {
        /* Remove subscriber (subscriber_pid, subscription_id) */
        struct subscriber subscriber_tmp = {subscriber_pid, subscription_id};

        if (pa_idxset_remove_by_data(event_subscribers, &subscriber_tmp, NULL))
            pa_log_debug("Subscriber(%u, %u) removed from subscriber set for %s", subscriber_pid, subscription_id, event_name);
        else
            pa_log_error("No subscriber(%u, %u) in subscriber set for %s", subscriber_pid, subscription_id, event_name);

    } else {
        /* Remove subscriber (subscriber_pid, *) */
        struct subscriber *subscriber_item;

        PA_IDXSET_FOREACH(subscriber_item, event_subscribers, subscriber_idx) {
            if (subscriber_item->pid == subscriber_pid) {
                if (pa_idxset_remove_by_index(event_subscribers, subscriber_idx))
                    pa_log_debug("Subscriber(%u, %u) removed from subscriber set for %s", subscriber_pid, subscriber_item->subs_id, event_name);
                else
                    pa_log_error("No subscriber(%u, %u) in subscriber set for %s", subscriber_pid, subscriber_item->subs_id, event_name);
            }
        }
    }
}

static void register_event_handling(pa_hashmap *handled_events, pa_tizen_event_t event_type, uint32_t event_id, uint32_t subscriber_pid, uint32_t subscription_id) {
    pa_idxset *handled_subscribers;
    struct event handled_event = {event_type, event_id};
    struct subscriber *handled_subscriber;

    if (!(handled_subscribers = pa_hashmap_get(handled_events, &handled_event)))
        return;

    handled_subscriber = (struct subscriber*) pa_xmalloc0(sizeof(struct subscriber));
    handled_subscriber->pid = subscriber_pid;
    handled_subscriber->subs_id = subscription_id;

    pa_idxset_put(handled_subscribers, handled_subscriber, NULL);
}

/* If 'subscription_id' is less than 0, remove all subscribers which of pid is 'subscriber_pid' */
static void unregister_event_handling(pa_hashmap* handled_events, pa_tizen_event_t event_type, uint32_t event_id, uint32_t subscriber_pid, uint32_t subscription_id) {
    pa_idxset *handled_subscribers;
    struct subscriber *subscriber_item;
    uint32_t subscriber_idx;
    struct event *handled_event_key;
    void *state;

    pa_assert(handled_events);

    /* Find matching item with event_type, event_id, subscriber_pid, subscription_id
     * if event_id or subscription_id is less than or equal 0, we consider that matches */
    PA_HASHMAP_FOREACH_KV(handled_event_key, handled_subscribers, handled_events, state)
        if (handled_event_key->event_type == event_type)
            if (handled_event_key->event_id == event_id || event_id <= 0)
                PA_IDXSET_FOREACH(subscriber_item, handled_subscribers, subscriber_idx)
                    if (subscriber_item->pid == subscriber_pid)
                        if (subscriber_item->subs_id == subscription_id || subscription_id <= 0)
                            pa_idxset_remove_by_index(handled_subscribers, subscriber_idx);

}

static void handle_client_subscribed(pa_subscribe_observer *ob, pa_tizen_event_t event_type, uint32_t sender_pid, uint32_t subs_id, bool is_subscribe) {
    const char *event_name;

    pa_assert(ob);
    pa_assert(convert_event_type_to_name(event_type, &event_name) == 0);

    pa_log_info("Audio Client (pid : %u) has %s our signal '%s', subscription id : %u", sender_pid, is_subscribe ? "subscribed" : "unsubscribed", event_name, subs_id);

    if (is_subscribe)
        /* register new subscriber */
        register_subscriber(ob->subscribers, event_type, sender_pid, subs_id);
    else
        /* unregister subscriber */
        unregister_subscriber(ob->subscribers, event_type, sender_pid, subs_id);
}

/* When client exits emergently, we should unregister them to count subscribers
 * correctly */
static void handle_client_emergent_exit(pa_subscribe_observer *ob, int client_pid) {
    int event_type_idx;

    pa_log_warn("Audio Client(pid : %u) exited, clean up registered things", client_pid);

    for (event_type_idx = 0; event_type_idx < PA_TIZEN_EVENT_MAX; event_type_idx++) {
        unregister_subscriber(ob->subscribers, event_type_idx, client_pid, 0);
        unregister_event_handling(ob->handled_events, event_type_idx, 0, client_pid, 0);
    }
}

/* Return true, if all subscribers have handled event or there is no subscriber for this event.
 * Just compare the number of all subscribers with the number of subscribers who handled it. */
static bool is_event_handled_all(pa_subscribe_observer *ob, pa_tizen_event_t event_type, uint32_t event_id) {
    struct event event = {event_type, event_id};
    pa_idxset *subscribers;
    pa_idxset *handled_subscribers;
    unsigned subs_cnt, handled_subs_cnt;
    const char *event_name;

    pa_assert(ob);
    pa_assert(convert_event_type_to_name(event_type, &event_name) == 0);

    /* If there is no subscriber, always return true */
    if (!(subscribers = pa_hashmap_get(ob->subscribers, PA_INT_TO_PTR(event_type)))) {
        pa_log_info("No subscribers for event : %s", event_name);
        return true;
    }

    if ((subs_cnt = pa_idxset_size(subscribers)) == 0) {
        pa_log_info("Subscriber counts for event %s is 0", event_name);
        return true;
    }

    /* Now there is at least one subscriber */
    if (!(handled_subscribers = pa_hashmap_get(ob->handled_events, &event))) {
        pa_log_info("No handled_subscribers for event : %s, id : %u", event_name, event_id);
        return false;
    }

    handled_subs_cnt = pa_idxset_size(handled_subscribers);

    pa_log_debug("subscriber count : %u, handled subscriber count : %u", subs_cnt, handled_subs_cnt);

    if (subs_cnt == handled_subs_cnt)
        return true;
    else
        return false;
}

static void handle_all_subscribers_handled_event(pa_subscribe_observer *ob, pa_tizen_event_t event_type, uint32_t event_id) {
    pa_subscribe_observer_hook_data_for_event_handled event_handled_data;
    pa_idxset *handled_subscribers;
    struct event handled_event = {event_type, event_id};
    const char *event_name;

    pa_assert(ob);
    pa_assert(convert_event_type_to_name(event_type, &event_name) == 0);

    pa_log_info("All subscribers handled event(event name:%s, event id:%u)", event_name, event_id);

    pa_log_info("Fire event fully handled hook");
    event_handled_data.event_id = event_id;
    event_handled_data.event_type= event_type;
    pa_hook_fire(pa_communicator_hook(ob->comm.comm, PA_COMMUNICATOR_HOOK_EVENT_FULLY_HANDLED), &event_handled_data);

    /* handled_subscriber for this event is now expired, remove it. */
    if (!(handled_subscribers = pa_hashmap_remove(ob->handled_events, &handled_event))) {
        pa_log_warn("No handled subscribers for event[%s/%u]", event_name, event_id);
        return;
    }

    pa_idxset_free(handled_subscribers, subscriber_free_func);
}

static void handle_subscriber_handled_event(pa_subscribe_observer *ob, pa_tizen_event_t event_type, uint32_t event_id,  uint32_t sender_pid, uint32_t subs_id, DBusMessageIter *var_i) {
    const char *event_name;

    pa_assert(ob);
    pa_assert(convert_event_type_to_name(event_type, &event_name) == 0);

    pa_log_info("Audio Client (Pid : %u) has handled our signal '%s'(event id : %u), subscription id : %u", sender_pid, event_name, event_id, subs_id);

    register_event_handling(ob->handled_events, event_type, event_id, sender_pid, subs_id);

    if (is_event_handled_all(ob, event_type, event_id))
        handle_all_subscribers_handled_event(ob, event_type, event_id);
}

/* Get dbus signal from audio clients */
static DBusHandlerResult dbus_filter_audio_client_handler(DBusConnection *c, DBusMessage *m, void *userdata) {
    DBusError error;
    pa_subscribe_observer *ob= (pa_subscribe_observer*)userdata;
    uint32_t sender_pid;

    pa_assert(userdata);

    if (dbus_message_get_type(m) != DBUS_MESSAGE_TYPE_SIGNAL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    pa_log_info("Dbus audio subscribe observer received msg");

    dbus_error_init(&error);

    if (!pa_streq(dbus_message_get_interface(m), INTERFACE_AUDIO_CLIENT))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;


    if (dbus_message_is_signal(m, INTERFACE_AUDIO_CLIENT, "ClientSubscribed")) {
        /* Client subscribed or unsubscribed our signal(callback) */
        char *event_name;
        dbus_uint32_t subs_id;
        dbus_bool_t is_subscribe;
        pa_tizen_event_t event_type;

        pa_log_debug("Got ClientSubscribed signal");

        if (get_sender_pid(c, m, &sender_pid) < 0)
            goto fail;

        if (!pa_streq(dbus_message_get_signature(m), "sub")) {
            pa_log_error("Invalid signature for ClientSubscribed, %s", dbus_message_get_signature(m));
            goto fail;
        }

        if (!dbus_message_get_args(m, NULL, DBUS_TYPE_STRING, &event_name,
                                            DBUS_TYPE_UINT32, &subs_id,
                                            DBUS_TYPE_BOOLEAN, &is_subscribe, /* True on subscription, False on unsubscription */
                                            DBUS_TYPE_INVALID)) {
            goto fail;
        } else {
            if (convert_event_name_to_type(event_name, &event_type) < 0) {
                pa_log_error("convert event name failed : %s", event_name);
                goto fail;
            }
            handle_client_subscribed(ob, event_type, sender_pid, subs_id, is_subscribe);
        }
    } else if (dbus_message_is_signal(m, INTERFACE_AUDIO_CLIENT, "ClientSignalHandled")) {
        /* Client handled out signal(callback), this is sended right after
         * User's callback is finished */
        char *event_name;
        dbus_uint32_t subs_id, event_id;
        DBusMessageIter arg_i, var_i;
        pa_tizen_event_t event_type;

        pa_log_debug("Got ClientSignalHandled signal");

        if (get_sender_pid(c, m, &sender_pid) < 0)
            goto fail;

        if (!dbus_message_iter_init(m, &arg_i) || !pa_streq(dbus_message_get_signature(m), "usuv")) {
            pa_log_error("Invalid signature for ClientSignalHandled, %s", dbus_message_get_signature(m));
            goto fail;
        }

        pa_assert(dbus_message_iter_get_arg_type(&arg_i) == DBUS_TYPE_UINT32);
        dbus_message_iter_get_basic(&arg_i, &event_id);
        pa_assert_se(dbus_message_iter_next(&arg_i));

        pa_assert(dbus_message_iter_get_arg_type(&arg_i) == DBUS_TYPE_STRING);
        dbus_message_iter_get_basic(&arg_i, &event_name);
        pa_assert_se(dbus_message_iter_next(&arg_i));

        pa_assert(dbus_message_iter_get_arg_type(&arg_i) == DBUS_TYPE_UINT32);
        dbus_message_iter_get_basic(&arg_i, &subs_id);
        pa_assert_se(dbus_message_iter_next(&arg_i));

        pa_assert(dbus_message_iter_get_arg_type(&arg_i) == DBUS_TYPE_VARIANT);
        dbus_message_iter_recurse(&arg_i, &var_i);

        if (convert_event_name_to_type(event_name, &event_type) < 0) {
            pa_log_error("convert event name failed : %s", event_name);
            goto fail;
        }
        handle_subscriber_handled_event(ob, event_type, event_id, sender_pid, subs_id, &var_i);

    } else if (dbus_message_is_signal(m, INTERFACE_AUDIO_CLIENT, "EmergentExit")) {
        /* Client will be exit emergently, we should care about that */
        dbus_int32_t client_pid;
        if (!pa_streq(dbus_message_get_signature(m), "i")) {
            pa_log_error("Invalid signature for EmergentExit, %s", dbus_message_get_signature(m));
            goto fail;
        }
        if (!dbus_message_get_args(m, NULL, DBUS_TYPE_INT32, &client_pid,
                                            DBUS_TYPE_INVALID))
            goto fail;
        else
            handle_client_emergent_exit(ob, client_pid);
    } else {
        pa_log_info("Unknown message , not handle it");

        dbus_error_free(&error);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    pa_log_debug("Dbus Message handled");

    dbus_error_free(&error);
    return DBUS_HANDLER_RESULT_HANDLED;

fail:
    pa_log_error("Fail to handle dbus signal");
    dbus_error_free(&error);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static int watch_signals(pa_subscribe_observer *ob) {
    DBusError error;

    pa_assert(ob);
    pa_assert(ob->dbus_conn);

    dbus_error_init(&error);

    pa_log_debug("Watch Audio Client Dbus signals");

    if (!dbus_connection_add_filter(pa_dbus_connection_get(ob->dbus_conn), dbus_filter_audio_client_handler, ob, NULL)) {
        pa_log_error("Unable to add D-Bus filter : %s: %s", error.name, error.message);
        goto fail;
    }

    if (pa_dbus_add_matches(pa_dbus_connection_get(ob->dbus_conn), &error, FILTER_AUDIO_CLIENT, NULL) < 0) {
        pa_log_error("Unable to subscribe to signals: %s: %s", error.name, error.message);
        goto fail;
    }
    return 0;

fail:
    dbus_error_free(&error);
    return -1;
}

static void unwatch_signals(pa_subscribe_observer *ob) {
    pa_log_debug("Unwatch Audio Client Dbus signals");

    pa_assert(ob);
    pa_assert(ob->dbus_conn);

    pa_dbus_remove_matches(pa_dbus_connection_get(ob->dbus_conn), FILTER_AUDIO_CLIENT, NULL);
    dbus_connection_remove_filter(pa_dbus_connection_get(ob->dbus_conn), dbus_filter_audio_client_handler, ob);
}

static void dbus_init(pa_subscribe_observer *ob) {
    DBusError error;
    pa_dbus_connection *connection = NULL;

    pa_assert(ob);
    pa_log_debug("Dbus init");
    dbus_error_init(&error);

    if (!(connection = pa_dbus_bus_get(ob->core, DBUS_BUS_SYSTEM, &error)) || dbus_error_is_set(&error)) {
        if (connection) {
            pa_dbus_connection_unref(connection);
        }
        pa_log_error("Unable to contact D-Bus system bus: %s: %s", error.name, error.message);
        goto fail;
    } else {
        pa_log_debug("Got dbus connection");
    }

    ob->dbus_conn = connection;

    if (watch_signals(ob) < 0)
        pa_log_error("dbus watch signals failed");
    else
        pa_log_debug("dbus ready to get signals");

fail:
    dbus_error_free(&error);
}

static void dbus_deinit(pa_subscribe_observer *ob) {
    pa_assert(ob);

    pa_log_debug("Dbus deinit");

    unwatch_signals(ob);

    if (ob->dbus_conn) {
        pa_dbus_connection_unref(ob->dbus_conn);
        ob->dbus_conn = NULL;
    }
}

static pa_hook_result_t device_connection_changed_hook_cb(pa_core *c, pa_device_manager_hook_data_for_conn_changed *data, pa_subscribe_observer *ob) {
    struct event *new_event;
    pa_idxset *handled_subscribers;

    new_event = pa_xmalloc0(sizeof(struct event));
    new_event->event_type = PA_TIZEN_EVENT_DEVICE_CONNECTION_CHANGED;
    new_event->event_id = data->event_id;

    pa_log_debug("Got device connection changed hook : event-id(%u), device(%p), is_connected(%d)", data->event_id, data->device, data->is_connected);
    /* We assume that we got connection changed hook earlier than 'ClientSignalHandled' signal */
    handled_subscribers = pa_idxset_new(subscriber_hash_func, subscriber_compare_func);
    pa_hashmap_put(ob->handled_events, new_event, handled_subscribers);
    if (is_event_handled_all(ob, new_event->event_type, new_event->event_id))
        handle_all_subscribers_handled_event(ob, new_event->event_type, new_event->event_id);

    return PA_HOOK_OK;
}

pa_subscribe_observer* pa_subscribe_observer_ref(pa_subscribe_observer *ob) {
    pa_assert(ob);
    pa_assert(PA_REFCNT_VALUE(ob) > 0);

    PA_REFCNT_INC(ob);

    return ob;
}

void pa_subscribe_observer_unref(pa_subscribe_observer *ob) {
    pa_assert(ob);
    pa_assert(PA_REFCNT_VALUE(ob) > 0);

    if (PA_REFCNT_DEC(ob) > 0)
        return;

    if (ob->comm.comm)
        pa_communicator_unref(ob->comm.comm);
    if (ob->comm.comm_hook_device_connection_changed_slot)
        pa_hook_slot_free(ob->comm.comm_hook_device_connection_changed_slot);

    dbus_deinit(ob);
    pa_hashmap_free(ob->subscribers);

    if (ob->core)
        pa_shared_remove(ob->core, SHARED_SUBSCRIBE_OBSERVER);

    pa_xfree(ob);
}

pa_subscribe_observer* pa_subscribe_observer_get(pa_core *c) {
    pa_subscribe_observer *ob;
    pa_assert(c);

    pa_log_debug("pa_subscribe_observer_get");

    if ((ob = pa_shared_get(c, SHARED_SUBSCRIBE_OBSERVER)))
        return pa_subscribe_observer_ref(ob);

    /* Subscribe-Observer init logic from here */
    pa_log_debug("Make new pa_subscribe_observer");

    ob = pa_xnew0(pa_subscribe_observer, 1);
    PA_REFCNT_INIT(ob);
    ob->core = c;
    ob->subscribers = pa_hashmap_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);
    ob->handled_events = pa_hashmap_new(event_hash_func, event_compare_func);

    dbus_init(ob);

    ob->comm.comm = pa_communicator_get(c);
    ob->comm.comm_hook_device_connection_changed_slot = pa_hook_connect(pa_communicator_hook(ob->comm.comm, PA_COMMUNICATOR_HOOK_DEVICE_CONNECTION_CHANGED),
            PA_HOOK_LATE, (pa_hook_cb_t)device_connection_changed_hook_cb, ob);

    pa_shared_set(c, SHARED_SUBSCRIBE_OBSERVER, ob);

    return ob;
}


