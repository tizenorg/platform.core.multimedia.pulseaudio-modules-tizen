/***
  This file is part of PulseAudio.

  Copyright 2015 Sangchul Lee <sc11.lee@samsung.com>

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

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#include <pulse/xmalloc.h>
#include <pulse/proplist.h>

#include <pulsecore/module.h>
#include <pulsecore/log.h>
#include <pulsecore/sink.h>
#include <pulsecore/modargs.h>
#include <pulsecore/macro.h>

#include <json.h>
#include "stream-manager.h"
#include "stream-manager-priv.h"
#include "stream-manager-volume-priv.h"

#ifdef HAVE_DBUS
#define ARR_ARG_MAX  32
#define STREAM_MANAGER_OBJECT_PATH "/org/pulseaudio/StreamManager"
#define STREAM_MANAGER_INTERFACE   "org.pulseaudio.StreamManager"
#define STREAM_MANAGER_METHOD_NAME_GET_STREAM_INFO            "GetStreamInfo"
#define STREAM_MANAGER_METHOD_NAME_GET_STREAM_LIST            "GetStreamList"
#define STREAM_MANAGER_METHOD_NAME_SET_STREAM_ROUTE_DEVICES   "SetStreamRouteDevices"
#define STREAM_MANAGER_METHOD_NAME_SET_STREAM_ROUTE_OPTION    "SetStreamRouteOption"
#define STREAM_MANAGER_METHOD_NAME_SET_VOLUME_LEVEL           "SetVolumeLevel"
#define STREAM_MANAGER_METHOD_NAME_GET_VOLUME_LEVEL           "GetVolumeLevel"
#define STREAM_MANAGER_METHOD_NAME_GET_VOLUME_MAX_LEVEL       "GetVolumeMaxLevel"
#define STREAM_MANAGER_METHOD_NAME_SET_VOLUME_MUTE            "SetVolumeMute"
#define STREAM_MANAGER_METHOD_NAME_GET_VOLUME_MUTE            "GetVolumeMute"
#define STREAM_MANAGER_METHOD_NAME_GET_CURRENT_VOLUME_TYPE    "GetCurrentVolumeType" /* the type that belongs to the stream of the current max priority */
#define STREAM_MANAGER_METHOD_NAME_UPDATE_FOCUS_STATUS        "UpdateFocusStatus"

static DBusHandlerResult method_handler_for_vt(DBusConnection *c, DBusMessage *m, void *userdata);
static DBusHandlerResult handle_introspect(DBusConnection *conn, DBusMessage *msg, void *userdata);
static DBusHandlerResult handle_methods(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_get_stream_info(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_get_stream_list(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_set_stream_route_devices(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_set_stream_route_option(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_set_volume_level(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_get_volume_level(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_get_volume_max_level(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_set_volume_mute(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_get_volume_mute(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_get_current_volume_type(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_update_focus_status(DBusConnection *conn, DBusMessage *msg, void *userdata);

enum method_handler_index {
    METHOD_HANDLER_GET_STREAM_INFO,
    METHOD_HANDLER_GET_STREAM_LIST,
    METHOD_HANDLER_SET_STREAM_ROUTE_DEVICES,
    METHOD_HANDLER_SET_STREAM_ROUTE_OPTION,
    METHOD_HANDLER_SET_VOLUME_LEVEL,
    METHOD_HANDLER_GET_VOLUME_LEVEL,
    METHOD_HANDLER_GET_VOLUME_MAX_LEVEL,
    METHOD_HANDLER_SET_VOLUME_MUTE,
    METHOD_HANDLER_GET_VOLUME_MUTE,
    METHOD_HANDLER_GET_CURRENT_VOLUME_TYPE,
    METHOD_HANDLER_UPDATE_FOCUS_STATUS,
    METHOD_HANDLER_MAX
};

static pa_dbus_arg_info get_stream_info_args[]  = { { "stream_type", "s", "in" },
                                                      { "priority", "i", "out" },
                                                    { "route_type", "i", "out" },
                                             { "avail_in_devices", "as", "out" },
                                            { "avail_out_devices", "as", "out" },
                                            { "avail_frameworks", "as", "out"} };
static pa_dbus_arg_info get_stream_list_args[]  = { { "stream_type", "as", "out" },
                                                     { "priority", "ai", "out" } };
static pa_dbus_arg_info set_stream_route_devices_args[]  = { { "parent_id", "u", "in" },
                                                     { "route_in_devices", "au", "in" },
                                                    { "route_out_devices", "au", "in" },
                                                            { "ret_msg", "s", "out" } };
static pa_dbus_arg_info set_stream_route_option_args[]  = { { "parent_id", "u", "in" },
                                                                 { "name", "s", "in" },
                                                                { "value", "i", "in" },
                                                           { "ret_msg", "s", "out" } };
static pa_dbus_arg_info set_volume_level_args[]  = { { "io_direction", "s", "in" },
                                                             { "type", "s", "in" },
                                                            { "level", "u", "in" },
                                                       { "ret_msg", "s", "out" } };
static pa_dbus_arg_info get_volume_level_args[]  = { { "io_direction", "s", "in" },
                                                             { "type", "s", "in" },
                                                           { "level", "u", "out" },
                                                       { "ret_msg", "s", "out" } };
static pa_dbus_arg_info get_volume_max_level_args[]  = { { "io_direction", "s", "in" },
                                                                 { "type", "s", "in" },
                                                               { "level", "u", "out" },
                                                           { "ret_msg", "s", "out" } };
static pa_dbus_arg_info set_volume_mute_args[]  = { { "io_direction", "s", "in" },
                                                            { "type", "s", "in" },
                                                          { "on/off", "u", "in" },
                                                      { "ret_msg", "s", "out" } };
static pa_dbus_arg_info get_volume_mute_args[]  = { { "io_direction", "s", "in" },
                                                            { "type", "s", "in" },
                                                         { "on/off", "u", "out" },
                                                      { "ret_msg", "s", "out" } };
static pa_dbus_arg_info get_current_volume_type_args[]  = { { "io_direction", "s", "in" },
                                                                   { "type", "s", "out" },
                                                              { "ret_msg", "s", "out" } };
static pa_dbus_arg_info update_focus_status_args[]  = { { "parent_id", "u", "in" },
                                                     { "focus_status", "u", "in" },
                                                       { "ret_msg", "s", "out" } };
static const char* signature_args_for_in[] = { "s", "", "uauau", "usi", "ssu", "ss", "ss", "ssu", "ss", "s", "uu"};

static pa_dbus_method_handler method_handlers[METHOD_HANDLER_MAX] = {
    [METHOD_HANDLER_GET_STREAM_INFO] = {
        .method_name = STREAM_MANAGER_METHOD_NAME_GET_STREAM_INFO,
        .arguments = get_stream_info_args,
        .n_arguments = sizeof(get_stream_info_args) / sizeof(pa_dbus_arg_info),
        .receive_cb = handle_get_stream_info },
    [METHOD_HANDLER_GET_STREAM_LIST] = {
        .method_name = STREAM_MANAGER_METHOD_NAME_GET_STREAM_LIST,
        .arguments = get_stream_list_args,
        .n_arguments = sizeof(get_stream_list_args) / sizeof(pa_dbus_arg_info),
        .receive_cb = handle_get_stream_list },
    [METHOD_HANDLER_SET_STREAM_ROUTE_DEVICES] = {
        .method_name = STREAM_MANAGER_METHOD_NAME_SET_STREAM_ROUTE_DEVICES,
        .arguments = set_stream_route_devices_args,
        .n_arguments = sizeof(set_stream_route_devices_args) / sizeof(pa_dbus_arg_info),
        .receive_cb = handle_set_stream_route_devices },
    [METHOD_HANDLER_SET_STREAM_ROUTE_OPTION] = {
        .method_name = STREAM_MANAGER_METHOD_NAME_SET_STREAM_ROUTE_OPTION,
        .arguments = set_stream_route_option_args,
        .n_arguments = sizeof(set_stream_route_option_args) / sizeof(pa_dbus_arg_info),
        .receive_cb = handle_set_stream_route_option },
    [METHOD_HANDLER_SET_VOLUME_LEVEL] = {
        .method_name = STREAM_MANAGER_METHOD_NAME_SET_VOLUME_LEVEL,
        .arguments = set_volume_level_args,
        .n_arguments = sizeof(set_volume_level_args) / sizeof(pa_dbus_arg_info),
        .receive_cb = handle_set_volume_level },
    [METHOD_HANDLER_GET_VOLUME_LEVEL] = {
        .method_name = STREAM_MANAGER_METHOD_NAME_GET_VOLUME_LEVEL,
        .arguments = get_volume_level_args,
        .n_arguments = sizeof(get_volume_level_args) / sizeof(pa_dbus_arg_info),
        .receive_cb = handle_get_volume_level },
    [METHOD_HANDLER_GET_VOLUME_MAX_LEVEL] = {
        .method_name = STREAM_MANAGER_METHOD_NAME_GET_VOLUME_MAX_LEVEL,
        .arguments = get_volume_max_level_args,
        .n_arguments = sizeof(get_volume_max_level_args) / sizeof(pa_dbus_arg_info),
        .receive_cb = handle_get_volume_max_level },
    [METHOD_HANDLER_SET_VOLUME_MUTE] = {
        .method_name = STREAM_MANAGER_METHOD_NAME_SET_VOLUME_MUTE,
        .arguments = set_volume_mute_args,
        .n_arguments = sizeof(set_volume_mute_args) / sizeof(pa_dbus_arg_info),
        .receive_cb = handle_set_volume_mute },
    [METHOD_HANDLER_GET_VOLUME_MUTE] = {
        .method_name = STREAM_MANAGER_METHOD_NAME_GET_VOLUME_MUTE,
        .arguments = get_volume_mute_args,
        .n_arguments = sizeof(get_volume_mute_args) / sizeof(pa_dbus_arg_info),
        .receive_cb = handle_get_volume_mute },
    [METHOD_HANDLER_GET_CURRENT_VOLUME_TYPE] = {
        .method_name = STREAM_MANAGER_METHOD_NAME_GET_CURRENT_VOLUME_TYPE,
        .arguments = get_current_volume_type_args,
        .n_arguments = sizeof(get_current_volume_type_args) / sizeof(pa_dbus_arg_info),
        .receive_cb = handle_get_current_volume_type },
    [METHOD_HANDLER_UPDATE_FOCUS_STATUS] = {
        .method_name = STREAM_MANAGER_METHOD_NAME_UPDATE_FOCUS_STATUS,
        .arguments = update_focus_status_args,
        .n_arguments = sizeof(update_focus_status_args) / sizeof(pa_dbus_arg_info),
        .receive_cb = handle_update_focus_status },
};

const char *dbus_str_none = "none";
const char* stream_manager_dbus_ret_str[] = {"STREAM_MANAGER_RETURN_OK","STREAM_MANAGER_RETURN_ERROR", "STREAM_MANAGER_RETURN_ERROR_NO_STREAM"};
enum {
    RET_MSG_INDEX_OK,
    RET_MSG_INDEX_ERROR,
    RET_MSG_INDEX_ERROR_NO_STREAM,
};

#ifdef USE_DBUS_PROTOCOL

static pa_dbus_interface_info stream_manager_interface_info = {
    .name = STREAM_MANAGER_INTERFACE,
    .method_handlers = method_handlers,
    .n_method_handlers = METHOD_HANDLER_MAX,
    .property_handlers = ,
    .n_property_handlers = ,
    .get_all_properties_cb =,
    .signals =,
    .n_signals =
};

#else

#define STREAM_MGR_INTROSPECT_XML                                            \
    DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE                                \
    "<node>"                                                                 \
    " <interface name=\"STREAM_MANAGER_INTERFACE\">"                         \
    "  <method name=\"STREAM_MANAGER_METHOD_NAME_GET_STREAM_INFO\">"         \
    "   <arg name=\"stream_type\" direction=\"in\" type=\"s\"/>"             \
    "   <arg name=\"priority\" direction=\"out\" type=\"i\"/>"               \
    "   <arg name=\"route_type\" direction=\"out\" type=\"i\"/>"             \
    "   <arg name=\"avail_in_devices\" direction=\"out\" type=\"as\"/>"      \
    "   <arg name=\"avail_out_devices\" direction=\"out\" type=\"as\"/>"     \
    "   <arg name=\"avail_frameworks\" direction=\"out\" type=\"as\"/>"      \
    "  </method>"                                                            \
    "  <method name=\"STREAM_MANAGER_METHOD_NAME_GET_STREAM_LIST\">"         \
    "   <arg name=\"stream_type\" direction=\"in\" type=\"as\"/>"            \
    "   <arg name=\"priority\" direction=\"in\" type=\"ai\"/>"               \
    "  </method>"                                                            \
    "  <method name=\"STREAM_MANAGER_METHOD_NAME_SET_STREAM_ROUTE_DEVICES\">"\
    "   <arg name=\"parent_id\" direction=\"in\" type=\"u\"/>"               \
    "   <arg name=\"route_in_devices\" direction=\"in\" type=\"au\"/>"       \
    "   <arg name=\"route_out_devices\" direction=\"in\" type=\"au\"/>"      \
    "   <arg name=\"ret_msg\" direction=\"out\" type=\"s\"/>"                \
    "  </method>"                                                            \
    "  <method name=\"STREAM_MANAGER_METHOD_NAME_SET_STREAM_ROUTE_OPTION\">" \
    "   <arg name=\"parent_id\" direction=\"in\" type=\"u\"/>"               \
    "   <arg name=\"name\" direction=\"in\" type=\"s\"/>"                    \
    "   <arg name=\"value\" direction=\"in\" type=\"i\"/>"                   \
    "   <arg name=\"ret_msg\" direction=\"out\" type=\"s\"/>"                \
    "  </method>"                                                            \
    "  <method name=\"STREAM_MANAGER_METHOD_NAME_SET_VOLUME_LEVEL\">"        \
    "   <arg name=\"io_direction\" direction=\"in\" type=\"s\"/>"            \
    "   <arg name=\"type\" direction=\"in\" type=\"s\"/>"                    \
    "   <arg name=\"level\" direction=\"in\" type=\"u\"/>"                   \
    "   <arg name=\"ret_msg\" direction=\"out\" type=\"s\"/>"                \
    "  </method>"                                                            \
    "  <method name=\"STREAM_MANAGER_METHOD_NAME_GET_VOLUME_LEVEL\">"        \
    "   <arg name=\"io_direction\" direction=\"in\" type=\"s\"/>"            \
    "   <arg name=\"type\" direction=\"in\" type=\"s\"/>"                    \
    "   <arg name=\"level\" direction=\"out\" type=\"u\"/>"                  \
    "   <arg name=\"ret_msg\" direction=\"out\" type=\"s\"/>"                \
    "  </method>"                                                            \
    "  <method name=\"STREAM_MANAGER_METHOD_NAME_GET_VOLUME_MAX_LEVEL\">"    \
    "   <arg name=\"io_direction\" direction=\"in\" type=\"s\"/>"            \
    "   <arg name=\"type\" direction=\"in\" type=\"s\"/>"                    \
    "   <arg name=\"level\" direction=\"out\" type=\"u\"/>"                  \
    "   <arg name=\"ret_msg\" direction=\"out\" type=\"s\"/>"                \
    "  </method>"                                                            \
    "  <method name=\"STREAM_MANAGER_METHOD_NAME_SET_VOLUME_MUTE\">"         \
    "   <arg name=\"io_direction\" direction=\"in\" type=\"s\"/>"            \
    "   <arg name=\"type\" direction=\"in\" type=\"s\"/>"                    \
    "   <arg name=\"on/off\" direction=\"in\" type=\"u\"/>"                  \
    "   <arg name=\"ret_msg\" direction=\"out\" type=\"s\"/>"                \
    "  </method>"                                                            \
    "  <method name=\"STREAM_MANAGER_METHOD_NAME_GET_VOLUME_MUTE\">"         \
    "   <arg name=\"io_direction\" direction=\"in\" type=\"s\"/>"            \
    "   <arg name=\"type\" direction=\"in\" type=\"s\"/>"                    \
    "   <arg name=\"on/off\" direction=\"out\" type=\"u\"/>"                 \
    "   <arg name=\"ret_msg\" direction=\"out\" type=\"s\"/>"                \
    "  </method>"                                                            \
    "  <method name=\"STREAM_MANAGER_METHOD_NAME_GET_CURRENT_VOLUME_TYPE\">" \
    "   <arg name=\"io_direction\" direction=\"in\" type=\"s\"/>"            \
    "   <arg name=\"type\" direction=\"out\" type=\"s\"/>"                   \
    "   <arg name=\"ret_msg\" direction=\"out\" type=\"s\"/>"                \
    "  </method>"                                                            \
    "  <method name=\"STREAM_MANAGER_METHOD_NAME_UPDATE_FOCUS_STATUS\">"     \
    "   <arg name=\"parent_id\" direction=\"in\" type=\"u\"/>"               \
    "   <arg name=\"focus_status\" direction=\"in\" type=\"u\"/>"            \
    "   <arg name=\"ret_msg\" direction=\"out\" type=\"s\"/>"                \
    "  </method>"                                                            \
    " </interface>"                                                          \
    " <interface name=\"org.freedesktop.DBus.Introspectable\">"              \
    "  <method name=\"Introspect\">"                                         \
    "   <arg name=\"data\" type=\"s\" direction=\"out\"/>"                   \
    "  </method>"                                                            \
    " </interface>"                                                          \
    "</node>"
#endif

#endif

#define STREAM_MANAGER_CLIENT_NAME "SOUND_MANAGER_STREAM_INFO"
#define DEFAULT_ROLE "media"
#define SKIP_ROLE "skip"

/* There are some streams to be skipped.
 * In other words, we do not care about streams that have a name of listed as below */
#define NAME_FOR_SKIP_MAX 1
const char* stream_manager_media_names_for_skip[NAME_FOR_SKIP_MAX] = {"pulsesink probe"};

#define STREAM_FOCUS_NONE     "0"
#define STREAM_FOCUS_PLAYBACK "1"
#define STREAM_FOCUS_CAPTURE  "2"

typedef enum _process_stream_result {
    PROCESS_STREAM_RESULT_OK,
    PROCESS_STREAM_RESULT_STOP,
    PROCESS_STREAM_RESULT_SKIP,
} process_stream_result_t;

typedef enum _process_command_type {
    PROCESS_COMMAND_PREPARE,
    PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED_WITH_NEW_DATA,
    PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED,
    PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_ENDED,
    PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_FOCUS_CHANGED,
    PROCESS_COMMAND_UPDATE_VOLUME,
    PROCESS_COMMAND_ADD_PARENT_ID,
    PROCESS_COMMAND_REMOVE_PARENT_ID,
} process_command_type_t;

typedef enum _notify_command_type {
    NOTIFY_COMMAND_SELECT_PROPER_SINK_OR_SOURCE_FOR_INIT,
    NOTIFY_COMMAND_CHANGE_ROUTE_START,
    NOTIFY_COMMAND_CHANGE_ROUTE_END,
    NOTIFY_COMMAND_UPDATE_ROUTE_OPTION,
    NOTIFY_COMMAND_INFORM_STREAM_CONNECTED,
    NOTIFY_COMMAND_INFORM_STREAM_DISCONNECTED,
} notify_command_type_t;

const char* process_command_type_str[] = {
    "PREPARE",
    "CHANGE_ROUTE_BY_STREAM_STARTED_WITH_NEW_DATA",
    "CHANGE_ROUTE_BY_STREAM_STARTED",
    "CHANGE_ROUTE_BY_STREAM_ENDED",
    "CHANGE_ROUTE_BY_STREAM_FOCUS_CHANGED",
    "UPDATE_VOLUME",
    "ADD_PARENT_ID",
    "REMOVE_PARENT_ID",
};

const char* notify_command_type_str[] = {
    "SELECT_PROPER_SINK_OR_SOURCE_FOR_INIT",
    "CHANGE_ROUTE_START",
    "CHANGE_ROUTE_END",
    "UPDATE_ROUTE_OPTION",
    "INFORM_STREAM_CONNECTED",
    "INFORM_STREAM_DISCONNECTED",
};

#define STREAM_MAP_FILE "/etc/pulse/stream-map.json"
#define STREAM_MAP_VOLUMES "volumes"
#define STREAM_MAP_VOLUME_TYPE "type"
#define STREAM_MAP_VOLUME_IS_FOR_HAL "is-hal-volume"
#define STREAM_MAP_STREAMS "streams"
#define STREAM_MAP_STREAM_ROLE "role"
#define STREAM_MAP_STREAM_PRIORITY "priority"
#define STREAM_MAP_STREAM_ROUTE_TYPE "route-type"
#define STREAM_MAP_STREAM_DIRECTIONS "directions"
#define STREAM_MAP_STREAM_VOLUME_TYPES "volume-types"
#define STREAM_MAP_STREAM_VOLUME_TYPE_IN "in"
#define STREAM_MAP_STREAM_VOLUME_TYPE_OUT "out"
#define STREAM_MAP_STREAM_CAPTURE_VOLUME_TYPE "capture-volume-type"
#define STREAM_MAP_STREAM_PLAYBACK_VOLUME_TYPE "playback-volume-type"
#define STREAM_MAP_STREAM_AVAIL_IN_DEVICES "avail-in-devices"
#define STREAM_MAP_STREAM_AVAIL_OUT_DEVICES "avail-out-devices"
#define STREAM_MAP_STREAM_AVAIL_FRAMEWORKS "avail-frameworks"

typedef struct _stream_parent {
    //char *role;
    pa_idxset *idx_sink_inputs;
    pa_idxset *idx_source_outputs;
    pa_idxset *idx_route_in_devices;
    pa_idxset *idx_route_out_devices;
    focus_acquired_status_t focus_status;
} stream_parent;

#define AVAIL_DEVICES_MAX 16
#define AVAIL_FRAMEWORKS_MAX 16
#define AVAIL_STREAMS_MAX 32
typedef struct _stream_info_per_type {
    int32_t priority;
    int32_t route_type;
    int32_t num_of_in_devices;
    int32_t num_of_out_devices;
    int32_t num_of_frameworks;
    char *avail_in_devices[AVAIL_DEVICES_MAX];
    char *avail_out_devices[AVAIL_DEVICES_MAX];
    char *avail_frameworks[AVAIL_FRAMEWORKS_MAX];
} stream_info_per_type;
typedef struct _stream_list {
    int32_t num_of_streams;
    char* types[AVAIL_STREAMS_MAX];
    int32_t priorities[AVAIL_STREAMS_MAX];
} stream_list;
typedef struct _stream_route_option {
    const char *name;
    int32_t value;
} stream_route_option;

static void do_notify(pa_stream_manager *m, notify_command_type_t command, stream_type_t type, pa_bool_t is_new_data, void *user_data);
static process_stream_result_t process_stream(stream_type_t type, void *stream, process_command_type_t command, pa_stream_manager *m);

static int get_available_streams(pa_stream_manager *m, stream_list *list) {
    void *state = NULL;
    stream_info *s = NULL;
    char *role = NULL;
    int i = 0;

    pa_log_info("get_available_streams");
    if (m->stream_infos) {
        while ((s = pa_hashmap_iterate(m->stream_infos, &state, (const void**)&role))) {
            if (i < AVAIL_STREAMS_MAX) {
                list->priorities[i] = s->priority;
                list->types[i++] = role;
                pa_log_debug("  [%d] stream_type[%s], priority[%d]", i-1, role, s->priority);
            } else {
                pa_log_error("  out of range, [%d]", i);
                break;
            }
        }
        list->num_of_streams = i;
        pa_log_debug("  num_of_streams[%d]",i);
    } else {
        pa_log_error("stream_map is not initialized..");
        return -1;
    }
    return 0;
}

static int get_stream_info(pa_stream_manager *m, const char *stream_role, stream_info_per_type *info) {
    uint32_t idx = 0;
    char *name;
    stream_info *s = NULL;
    int i = 0;
    int j = 0;
    int k = 0;
    pa_log_info("get_stream_info : role[%s]", stream_role);
    if (m->stream_infos) {
        s = pa_hashmap_get(m->stream_infos, stream_role);
        if (s) {
            info->priority = s->priority;
            info->route_type = s->route_type;
            PA_IDXSET_FOREACH(name, s->idx_avail_in_devices, idx) {
                pa_log_debug("  avail-in-device[%d] name  : %s", i, name);
                if (i < AVAIL_DEVICES_MAX)
                    info->avail_in_devices[i++] = name;
                else
                    pa_log_error("  avail-in-devices, out of range, [%d]", i);
            }
            info->num_of_in_devices = i;
            PA_IDXSET_FOREACH(name, s->idx_avail_out_devices, idx) {
                pa_log_debug("  avail-out-device[%d] name  : %s", j, name);
                if (j < AVAIL_DEVICES_MAX)
                    info->avail_out_devices[j++] = name;
                else
                    pa_log_error("  avail-out-devices, out of range, [%d]", j);
            }
            info->num_of_out_devices = j;
            PA_IDXSET_FOREACH(name, s->idx_avail_frameworks, idx) {
                pa_log_debug("  avail-frameworks[%d] name  : %s", k, name);
                if (k < AVAIL_FRAMEWORKS_MAX)
                    info->avail_frameworks[k++] = name;
                else
                    pa_log_error("  avail-frameworks, out of range, [%d]", k);
            }
            info->num_of_frameworks = k;
        }
    } else {
        pa_log_error("stream_map is not initialized..");
        return -1;
    }
    return 0;
}

#ifdef HAVE_DBUS
static DBusHandlerResult handle_introspect(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    const char *xml = STREAM_MGR_INTROSPECT_XML;
    DBusMessage *r = NULL;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(userdata);

    pa_assert_se(r = dbus_message_new_method_return(msg));
    pa_assert_se(dbus_message_append_args(r, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID));

    if (r) {
        pa_assert_se(dbus_connection_send((conn), r, NULL));
        dbus_message_unref(r);
    }

    return DBUS_HANDLER_RESULT_HANDLED;
}

static void handle_get_stream_list(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    stream_list list;
    DBusMessage *reply = NULL;
    DBusMessageIter msg_iter;
    pa_stream_manager *m = (pa_stream_manager*)userdata;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(m);

    pa_assert_se(dbus_message_get_args(msg, NULL,
                                       DBUS_TYPE_INVALID));
    pa_log_info("handle_get_stream_list() dbus method is called");

    memset(&list, 0, sizeof(stream_list));
    pa_assert_se((reply = dbus_message_new_method_return(msg)));
    dbus_message_iter_init_append(reply, &msg_iter);
    if(!get_available_streams(m, &list)) {
        pa_dbus_append_basic_array_variant(&msg_iter, DBUS_TYPE_STRING, &list.types, list.num_of_streams);
        pa_dbus_append_basic_array_variant(&msg_iter, DBUS_TYPE_INT32, &list.priorities, list.num_of_streams);
    } else {
        pa_dbus_append_basic_array_variant(&msg_iter, DBUS_TYPE_STRING, NULL, 0);
        pa_dbus_append_basic_array_variant(&msg_iter, DBUS_TYPE_INT32, NULL, 0);
    }
    pa_assert_se(dbus_connection_send(conn, reply, NULL));
    dbus_message_unref(reply);
}

static void handle_get_stream_info(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    char *type;
    stream_info_per_type info;
    DBusMessage *reply = NULL;
    DBusMessageIter msg_iter;
    pa_stream_manager *m = (pa_stream_manager*)userdata;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(m);

    pa_assert_se(dbus_message_get_args(msg, NULL,
                                       DBUS_TYPE_STRING, &type,
                                       DBUS_TYPE_INVALID));
    pa_log_info("handle_get_stream_info(), type[%s]", type);

    memset(&info, 0, sizeof(stream_info_per_type));
    pa_assert_se((reply = dbus_message_new_method_return(msg)));
    dbus_message_iter_init_append(reply, &msg_iter);
    if(!get_stream_info(m, type, &info)) {
        pa_dbus_append_basic_variant(&msg_iter, DBUS_TYPE_INT32, &info.priority);
        pa_dbus_append_basic_variant(&msg_iter, DBUS_TYPE_INT32, &info.route_type);
        pa_dbus_append_basic_array_variant(&msg_iter, DBUS_TYPE_STRING, &info.avail_in_devices, info.num_of_in_devices);
        pa_dbus_append_basic_array_variant(&msg_iter, DBUS_TYPE_STRING, &info.avail_out_devices, info.num_of_out_devices);
        pa_dbus_append_basic_array_variant(&msg_iter, DBUS_TYPE_STRING, &info.avail_frameworks, info.num_of_frameworks);
    } else {
        pa_dbus_append_basic_variant(&msg_iter, DBUS_TYPE_INT32, 0);
        pa_dbus_append_basic_variant(&msg_iter, DBUS_TYPE_INT32, 0);
        pa_dbus_append_basic_array_variant(&msg_iter, DBUS_TYPE_STRING, NULL, 0);
        pa_dbus_append_basic_array_variant(&msg_iter, DBUS_TYPE_STRING, NULL, 0);
        pa_dbus_append_basic_array_variant(&msg_iter, DBUS_TYPE_STRING, NULL, 0);
    }
    pa_assert_se(dbus_connection_send(conn, reply, NULL));
    dbus_message_unref(reply);
}

static void handle_set_stream_route_devices(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    uint32_t id = 0;
    int i = 0;
    uint32_t *in_device_list = NULL;
    uint32_t *out_device_list = NULL;
    int list_len_in = 0;
    int list_len_out = 0;
    uint32_t idx = 0;
    uint32_t *device_id = NULL;
    void *stream = NULL;
    stream_parent *sp = NULL;
    const char *route_type_str = NULL;
    stream_route_type_t route_type;
    DBusMessage *reply = NULL;
    pa_stream_manager *m = (pa_stream_manager*)userdata;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(m);

    pa_assert_se(dbus_message_get_args(msg, NULL,
                                       DBUS_TYPE_UINT32, &id,
                                       DBUS_TYPE_ARRAY, DBUS_TYPE_UINT32, &in_device_list, &list_len_in,
                                       DBUS_TYPE_ARRAY, DBUS_TYPE_UINT32, &out_device_list, &list_len_out,
                                       DBUS_TYPE_INVALID));
    pa_log_info("handle_set_stream_route_devices(), id[%u], in_device_list[%p]:length[%d], out_device_list[%p]:length[%d]",
            id, in_device_list, list_len_in, out_device_list, list_len_out);

    pa_assert_se((reply = dbus_message_new_method_return(msg)));

    sp = pa_hashmap_get(m->stream_parents, (const void*)id);
    if (sp) {
        if (!in_device_list && !out_device_list) {
            pa_log_error("invalid arguments");
            goto FAILURE;
        }

        if (sp->idx_route_in_devices) {
            PA_IDXSET_FOREACH(device_id, sp->idx_route_in_devices, idx) {
                pa_idxset_remove_by_data(sp->idx_route_in_devices, device_id, NULL);
                pa_xfree(device_id);
            }
            if (in_device_list && list_len_in) {
                for (i = 0; i < list_len_in; i++) {
                    pa_idxset_put(sp->idx_route_in_devices, pa_xmemdup(&in_device_list[i], sizeof(uint32_t)), NULL);
                    pa_log_debug(" -- [in] device id:%u", in_device_list[i]);
                }
            }
            PA_IDXSET_FOREACH(stream, sp->idx_source_outputs, idx) {
                /* find route type of stream */
                route_type_str = pa_proplist_gets(GET_STREAM_PROPLIST(stream, STREAM_SOURCE_OUTPUT), PA_PROP_MEDIA_ROLE_ROUTE_TYPE);
                if (route_type_str) {
                    pa_log_debug(" -- the route type of source_output that belongs to this parent id[%u] is [%s]", id, route_type_str);
                    break;
                }
            }
            /* if any stream that belongs to this id has been activated, do notify right away */
            if (IS_ROUTE_TYPE_FOR_EXTERNAL_DEV(route_type_str, route_type)) {
                PA_IDXSET_FOREACH(stream, sp->idx_source_outputs, idx) {
                    pa_log_debug(" -- source_output[%p] belongs to this parent id[%u], do notify for the select proper source", stream, id);
                    do_notify(m, NOTIFY_COMMAND_SELECT_PROPER_SINK_OR_SOURCE_FOR_INIT, STREAM_SOURCE_OUTPUT, FALSE, stream);
                }
            } else if (m->cur_highest_priority.source_output) {
                if (pa_idxset_get_by_data(sp->idx_source_outputs, m->cur_highest_priority.source_output, NULL)) {
                    pa_log_debug(" -- cur_highest_priority.source_output->index[%u] belongs to this parent id[%u], do notify for the route change",
                            (m->cur_highest_priority.source_output)->index, id);
                    do_notify(m, NOTIFY_COMMAND_CHANGE_ROUTE_START, STREAM_SOURCE_OUTPUT, FALSE, m->cur_highest_priority.source_output);
                }
            }
        } else {
            pa_log_error("failed to update, idx_route_in_devices[%p]", sp->idx_route_in_devices);
            goto FAILURE;
        }

        if (sp->idx_route_out_devices) {
            PA_IDXSET_FOREACH(device_id, sp->idx_route_out_devices, idx) {
                pa_idxset_remove_by_data(sp->idx_route_out_devices, device_id, NULL);
                pa_xfree(device_id);
            }
            if (out_device_list && list_len_out) {
                for (i = 0; i < list_len_out; i++) {
                    pa_idxset_put(sp->idx_route_out_devices, pa_xmemdup(&out_device_list[i], sizeof(uint32_t)), NULL);
                    pa_log_debug(" -- [out] device id:%u", out_device_list[i]);
                }
            }
            PA_IDXSET_FOREACH(stream, sp->idx_sink_inputs, idx) {
                /* find route type of stream */
                route_type_str = pa_proplist_gets(GET_STREAM_PROPLIST(stream, STREAM_SINK_INPUT), PA_PROP_MEDIA_ROLE_ROUTE_TYPE);
                if (route_type_str) {
                    pa_log_debug(" -- the route type of sink_input that belongs to this parent id[%u] is [%s]", id, route_type_str);
                    break;
                }
            }
            /* if any stream that belongs to this id has been activated, do notify right away */
            if (IS_ROUTE_TYPE_FOR_EXTERNAL_DEV(route_type_str, route_type)) {
                PA_IDXSET_FOREACH(stream, sp->idx_sink_inputs, idx) {
                    pa_log_debug(" -- sink_input[%p] belongs to this parent id[%u], do notify for the select proper sink", stream, id);
                    do_notify(m, NOTIFY_COMMAND_SELECT_PROPER_SINK_OR_SOURCE_FOR_INIT, STREAM_SINK_INPUT, FALSE, stream);
                }
            } else if (m->cur_highest_priority.sink_input) {
                if (pa_idxset_get_by_data(sp->idx_sink_inputs, m->cur_highest_priority.sink_input, NULL)) {
                    pa_log_debug(" -- cur_highest_priority.sink_input->index[%u] belongs to this parent id[%u], do notify for the route change",
                            (m->cur_highest_priority.sink_input)->index, id);
                    do_notify(m, NOTIFY_COMMAND_CHANGE_ROUTE_START, STREAM_SINK_INPUT, FALSE, m->cur_highest_priority.sink_input);
                }
            }
        } else {
            pa_log_error("failed to update, idx_route_out_devices[%p]", sp->idx_route_out_devices);
            goto FAILURE;
        }
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_OK], DBUS_TYPE_INVALID));
    } else {
        pa_log_error("could not find matching client for this parent_id[%u]", id);
        goto FAILURE;
    }

    pa_assert_se(dbus_connection_send(conn, reply, NULL));
    dbus_message_unref(reply);
    return;
FAILURE:
    pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR], DBUS_TYPE_INVALID));
    pa_assert_se(dbus_connection_send(conn, reply, NULL));
    dbus_message_unref(reply);
    return;
}

static void handle_set_stream_route_option(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    uint32_t id = 0;
    const char *name = NULL;
    int32_t value = 0;
    pa_bool_t updated = FALSE;
    stream_parent *sp = NULL;
    stream_route_option route_option;
    DBusMessage *reply = NULL;
    pa_stream_manager *m = (pa_stream_manager*)userdata;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(m);

    pa_assert_se(dbus_message_get_args(msg, NULL,
                                       DBUS_TYPE_UINT32, &id,
                                       DBUS_TYPE_STRING, &name,
                                       DBUS_TYPE_INT32, &value,
                                       DBUS_TYPE_INVALID));
    pa_log_info("handle_set_stream_route_option(), name[%s], value[%d]", name, value);

    pa_assert_se((reply = dbus_message_new_method_return(msg)));

    sp = pa_hashmap_get(m->stream_parents, (const void*)id);
    if (sp) {
        if (name) {
            route_option.name = name;
            route_option.value = value;

            /* if any stream that belongs to this id has been activated, do notify right away */
            if (m->cur_highest_priority.sink_input) {
                if (pa_idxset_get_by_data(sp->idx_sink_inputs, m->cur_highest_priority.sink_input, NULL)) {
                    pa_log_debug(" -- cur_highest_priority.sink_input->index[%u] belongs to this parent id[%u], do notify for the options",
                        (m->cur_highest_priority.sink_input)->index, id);
                    do_notify(m, NOTIFY_COMMAND_UPDATE_ROUTE_OPTION, STREAM_SINK_INPUT, FALSE, &route_option);
                    updated = TRUE;
                }
            }
            if (m->cur_highest_priority.source_output) {
                if (pa_idxset_get_by_data(sp->idx_source_outputs, m->cur_highest_priority.source_output, NULL)) {
                    pa_log_debug(" -- cur_highest_priority.source_output->index[%u] belongs to this parent id[%u], do notify for the options",
                        (m->cur_highest_priority.source_output)->index, id);
                    do_notify(m, NOTIFY_COMMAND_UPDATE_ROUTE_OPTION, STREAM_SOURCE_OUTPUT, FALSE, &route_option);
                    updated = TRUE;
                }
            }
            if (!updated) {
                pa_log_error("invalid state");
                pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR_NO_STREAM], DBUS_TYPE_INVALID));
            } else
                pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_OK], DBUS_TYPE_INVALID));
        } else {
            pa_log_error("invalid arguments");
            pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR], DBUS_TYPE_INVALID));
        }

    } else {
        pa_log_error("could not find matching client for this parent_id[%u]", id);
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR], DBUS_TYPE_INVALID));
    }

    pa_assert_se(dbus_connection_send(conn, reply, NULL));
    dbus_message_unref(reply);
    return;
}

static void send_volume_changed_signal(DBusConnection *conn, const char *direction, const char *volume_type, const uint32_t volume_level) {
    DBusMessage *signal_msg;
    DBusMessageIter msg_iter;

    pa_assert(conn);
    pa_assert(volume_type);

    pa_log_debug("Send volume changed signal : direction %s, type %s, level %d", direction, volume_type, volume_level);

    pa_assert_se(signal_msg = dbus_message_new_signal(STREAM_MANAGER_OBJECT_PATH, STREAM_MANAGER_INTERFACE, "VolumeChanged"));
    dbus_message_iter_init_append(signal_msg, &msg_iter);

    dbus_message_iter_append_basic(&msg_iter, DBUS_TYPE_STRING, &direction);
    dbus_message_iter_append_basic(&msg_iter, DBUS_TYPE_STRING, &volume_type);
    dbus_message_iter_append_basic(&msg_iter, DBUS_TYPE_UINT32, &volume_level);

    pa_assert_se(dbus_connection_send(conn, signal_msg, NULL));
    dbus_message_unref(signal_msg);
    return;
}

static void handle_set_volume_level(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    const char *direction = NULL;
    const char *type = NULL;
    uint32_t level = 0;
    stream_type_t stream_type = STREAM_SINK_INPUT;
    DBusMessage *reply = NULL;
    pa_stream_manager *m = (pa_stream_manager*)userdata;
    int ret = 0;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(m);

    pa_assert_se(dbus_message_get_args(msg, NULL,
                                       DBUS_TYPE_STRING, &direction,
                                       DBUS_TYPE_STRING, &type,
                                       DBUS_TYPE_UINT32, &level,
                                       DBUS_TYPE_INVALID));
    pa_log_info("handle_set_volume_level(), direction[%s], type[%s], level[%u]", direction, type, level);

    pa_assert_se((reply = dbus_message_new_method_return(msg)));

    if (pa_streq(direction, "in"))
        stream_type = STREAM_SOURCE_OUTPUT;
    else if (pa_streq(direction, "out"))
        stream_type = STREAM_SINK_INPUT;
    else {
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR], DBUS_TYPE_INVALID));
        goto FAILURE;
    }

    if ((ret = set_volume_level_by_type(m, stream_type, type, level)))
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR], DBUS_TYPE_INVALID));
    else
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_OK], DBUS_TYPE_INVALID));

FAILURE:
    pa_assert_se(dbus_connection_send(conn, reply, NULL));
    dbus_message_unref(reply);

    if (!ret)
        send_volume_changed_signal(conn, direction, type, level);
    return;
}

static void handle_get_volume_level(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    const char *direction = NULL;
    const char *type = NULL;
    uint32_t level = 0;
    stream_type_t stream_type = STREAM_SINK_INPUT;
    DBusMessage *reply = NULL;
    pa_stream_manager *m = (pa_stream_manager*)userdata;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(m);

    pa_assert_se(dbus_message_get_args(msg, NULL,
                                       DBUS_TYPE_STRING, &direction,
                                       DBUS_TYPE_STRING, &type,
                                       DBUS_TYPE_INVALID));
    pa_log_info("handle_get_volume_level(), direction(%s), type(%s)", direction, type);

    pa_assert_se((reply = dbus_message_new_method_return(msg)));

    if (pa_streq(direction, "in"))
        stream_type = STREAM_SOURCE_OUTPUT;
    else if (pa_streq(direction, "out"))
        stream_type = STREAM_SINK_INPUT;
    else {
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_UINT32, 0, DBUS_TYPE_INVALID));
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR], DBUS_TYPE_INVALID));
        goto FAILURE;
    }

    if (get_volume_level_by_type(m, GET_VOLUME_CURRENT_LEVEL, stream_type, type, &level)) {
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_UINT32, 0, DBUS_TYPE_INVALID));
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR], DBUS_TYPE_INVALID));
    } else {
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_UINT32, &level, DBUS_TYPE_INVALID));
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_OK], DBUS_TYPE_INVALID));
    }

FAILURE:
    pa_assert_se(dbus_connection_send(conn, reply, NULL));
    dbus_message_unref(reply);
    return;
}

static void handle_get_volume_max_level(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    const char *direction = NULL;
    const char *type = NULL;
    uint32_t level = 0;
    stream_type_t stream_type = STREAM_SINK_INPUT;
    DBusMessage *reply = NULL;
    pa_stream_manager *m = (pa_stream_manager*)userdata;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(m);

    pa_assert_se(dbus_message_get_args(msg, NULL,
                                       DBUS_TYPE_STRING, &direction,
                                       DBUS_TYPE_STRING, &type,
                                       DBUS_TYPE_INVALID));
    pa_log_info("handle_get_volume_max_level(), direction[%s], type[%s]", direction, type);

    pa_assert_se((reply = dbus_message_new_method_return(msg)));

    if (pa_streq(direction, "in"))
        stream_type = STREAM_SOURCE_OUTPUT;
    else if (pa_streq(direction, "out"))
        stream_type = STREAM_SINK_INPUT;
    else {
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_UINT32, 0, DBUS_TYPE_INVALID));
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR], DBUS_TYPE_INVALID));
        goto FAILURE;
    }

    if (get_volume_level_by_type(m, GET_VOLUME_MAX_LEVEL, stream_type, type, &level)) {
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_UINT32, 0, DBUS_TYPE_INVALID));
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR], DBUS_TYPE_INVALID));
    } else {
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_UINT32, &level, DBUS_TYPE_INVALID));
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_OK], DBUS_TYPE_INVALID));
    }
FAILURE:
    pa_assert_se(dbus_connection_send(conn, reply, NULL));
    dbus_message_unref(reply);
    return;
}

static void handle_set_volume_mute(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    const char *direction = NULL;
    const char *type = NULL;
    uint32_t do_mute = 0;
    stream_type_t stream_type = STREAM_SINK_INPUT;
    DBusMessage *reply = NULL;
    pa_stream_manager *m = (pa_stream_manager*)userdata;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(m);

    pa_assert_se(dbus_message_get_args(msg, NULL,
                                       DBUS_TYPE_STRING, &direction,
                                       DBUS_TYPE_STRING, &type,
                                       DBUS_TYPE_UINT32, &do_mute,
                                       DBUS_TYPE_INVALID));
    pa_log_info("handle_set_volume_mute(), direction[%s], type[%s], do_mute[%u]", direction, type, do_mute);

    pa_assert_se((reply = dbus_message_new_method_return(msg)));

    if (pa_streq(direction, "in"))
        stream_type = STREAM_SOURCE_OUTPUT;
    else if (pa_streq(direction, "out"))
        stream_type = STREAM_SINK_INPUT;
    else {
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR], DBUS_TYPE_INVALID));
        goto FAILURE;
    }

    if (set_volume_mute_by_type(m, stream_type, type, (pa_bool_t)do_mute))
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR], DBUS_TYPE_INVALID));
    else
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_OK], DBUS_TYPE_INVALID));

FAILURE:
    pa_assert_se(dbus_connection_send(conn, reply, NULL));
    dbus_message_unref(reply);
    return;
}

static void handle_get_volume_mute(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    const char *direction = NULL;
    const char *type = NULL;
    uint32_t is_muted = 0;
    stream_type_t stream_type = STREAM_SINK_INPUT;
    DBusMessage *reply = NULL;
    pa_stream_manager *m = (pa_stream_manager*)userdata;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(m);

    pa_assert_se(dbus_message_get_args(msg, NULL,
                                       DBUS_TYPE_STRING, &direction,
                                       DBUS_TYPE_STRING, &type,
                                       DBUS_TYPE_INVALID));
    pa_log_info("handle_get_volume_mute(), direction[%s], type[%s]", direction, type);

    pa_assert_se((reply = dbus_message_new_method_return(msg)));

    if (pa_streq(direction, "in"))
        stream_type = STREAM_SOURCE_OUTPUT;
    else if (pa_streq(direction, "out"))
        stream_type = STREAM_SINK_INPUT;
    else {
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_UINT32, 0, DBUS_TYPE_INVALID));
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR], DBUS_TYPE_INVALID));
        goto FAILURE;
    }

    if (get_volume_mute_by_type(m, stream_type, type, (pa_bool_t*)&is_muted)) {
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_UINT32, 0, DBUS_TYPE_INVALID));
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR], DBUS_TYPE_INVALID));
    } else {
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_UINT32, &is_muted, DBUS_TYPE_INVALID));
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_OK], DBUS_TYPE_INVALID));
    }

FAILURE:
    pa_assert_se(dbus_connection_send(conn, reply, NULL));
    dbus_message_unref(reply);
    return;
}

static void handle_get_current_volume_type(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    const char *direction = NULL;
    const char *type = NULL;
    void *s = NULL;
    stream_type_t stream_type = STREAM_SINK_INPUT;
    DBusMessage *reply = NULL;
    pa_stream_manager *m = (pa_stream_manager*)userdata;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(m);

    pa_assert_se(dbus_message_get_args(msg, NULL,
                                       DBUS_TYPE_STRING, &direction,
                                       DBUS_TYPE_INVALID));
    pa_log_info("handle_get_current_volume_type(), direction[%s]", direction);

    pa_assert_se((reply = dbus_message_new_method_return(msg)));

    if (pa_streq(direction, "in"))
        stream_type = STREAM_SOURCE_OUTPUT;
    else if (pa_streq(direction, "out"))
        stream_type = STREAM_SINK_INPUT;
    else {
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &dbus_str_none, DBUS_TYPE_INVALID));
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR], DBUS_TYPE_INVALID));
        goto FAILURE;
    }

    s = (stream_type == STREAM_SINK_INPUT)?(void*)(m->cur_highest_priority.sink_input):(void*)(m->cur_highest_priority.source_output);
    if (s) {
        type = pa_proplist_gets(GET_STREAM_PROPLIST(s, stream_type), PA_PROP_MEDIA_TIZEN_VOLUME_TYPE);
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &type, DBUS_TYPE_INVALID));
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_OK], DBUS_TYPE_INVALID));
    } else {
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &dbus_str_none, DBUS_TYPE_INVALID));
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR_NO_STREAM], DBUS_TYPE_INVALID));
    }

FAILURE:
    pa_assert_se(dbus_connection_send(conn, reply, NULL));
    dbus_message_unref(reply);
    return;
}

static void handle_update_focus_status(DBusConnection *conn, DBusMessage *msg, void *userdata) {
    uint32_t id = 0;
    uint32_t idx = 0;
    uint32_t count = 0;
    uint32_t acquired_focus_status = 0;
    stream_parent *sp = NULL;
    void *stream = NULL;
    DBusMessage *reply = NULL;
    pa_stream_manager *m = (pa_stream_manager*)userdata;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(m);

    pa_assert_se(dbus_message_get_args(msg, NULL,
                                       DBUS_TYPE_UINT32, &id,
                                       DBUS_TYPE_UINT32, &acquired_focus_status,
                                       DBUS_TYPE_INVALID));
    pa_log_info("handle_update_focus_status(), id[%u], acquired_focus_status[0x%x]", id, acquired_focus_status);

    pa_assert_se((reply = dbus_message_new_method_return(msg)));

    sp = pa_hashmap_get(m->stream_parents, (const void*)id);
    if (sp) {
        if (sp->focus_status != (acquired_focus_status & (STREAM_FOCUS_ACQUIRED_PLAYBACK|STREAM_FOCUS_ACQUIRED_CAPTURE))) {
            /* need to update */
            sp->focus_status = acquired_focus_status & (STREAM_FOCUS_ACQUIRED_PLAYBACK|STREAM_FOCUS_ACQUIRED_CAPTURE);
            if (sp->idx_sink_inputs)
                count = pa_idxset_size(sp->idx_sink_inputs);
               PA_IDXSET_FOREACH(stream, sp->idx_sink_inputs, idx) {
                   pa_proplist_sets(GET_STREAM_PROPLIST(stream, STREAM_SINK_INPUT), PA_PROP_MEDIA_FOCUS_STATUS,
                        IS_FOCUS_ACQUIRED(sp->focus_status, STREAM_SINK_INPUT)?STREAM_FOCUS_PLAYBACK:STREAM_FOCUS_NONE);
                    if (--count == 0)
                        process_stream(STREAM_SINK_INPUT, stream, PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_FOCUS_CHANGED, m);
                }
            if (sp->idx_source_outputs)
                count = pa_idxset_size(sp->idx_source_outputs);
                PA_IDXSET_FOREACH(stream, sp->idx_source_outputs, idx) {
                    pa_proplist_sets(GET_STREAM_PROPLIST(stream, STREAM_SOURCE_OUTPUT), PA_PROP_MEDIA_FOCUS_STATUS,
                        IS_FOCUS_ACQUIRED(sp->focus_status, STREAM_SOURCE_OUTPUT)?STREAM_FOCUS_CAPTURE:STREAM_FOCUS_NONE);
                    if (--count == 0)
                        process_stream(STREAM_SOURCE_OUTPUT, stream, PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_FOCUS_CHANGED, m);
                }
        } else
            pa_log_debug("same as before, skip updating focus status[0x%x]", acquired_focus_status);

    } else {
        pa_log_error("could not find matching client for this parent_id[%u]", id);
        pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_ERROR], DBUS_TYPE_INVALID));
        goto FAILURE;
    }
    pa_assert_se(dbus_message_append_args(reply, DBUS_TYPE_STRING, &stream_manager_dbus_ret_str[RET_MSG_INDEX_OK], DBUS_TYPE_INVALID));
FAILURE:
    pa_assert_se(dbus_connection_send(conn, reply, NULL));
    dbus_message_unref(reply);
    return;
}

static DBusHandlerResult handle_methods(DBusConnection *conn, DBusMessage *msg, void *userdata) {
	int idx = 0;
    pa_stream_manager *m = (pa_stream_manager*)userdata;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(m);

    for (idx = 0; idx < METHOD_HANDLER_MAX; idx++) {
        if (dbus_message_is_method_call(msg, STREAM_MANAGER_INTERFACE, method_handlers[idx].method_name )) {
            pa_log_debug("Message signature [%s] (Expected [%s])", dbus_message_get_signature(msg), signature_args_for_in[idx]);
            if (pa_streq(dbus_message_get_signature(msg), signature_args_for_in[idx])) {
                method_handlers[idx].receive_cb(conn, msg, userdata);
                return DBUS_HANDLER_RESULT_HANDLED;
            } else {
                pa_log_warn("Wrong Argument Signature");
                pa_dbus_send_error(conn, msg, DBUS_ERROR_INVALID_SIGNATURE,  "Wrong Signature, Expected %s", signature_args_for_in[idx]);
                return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
            }
        }
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusHandlerResult method_handler_for_vt(DBusConnection *c, DBusMessage *m, void *userdata) {
    pa_stream_manager *u = (pa_stream_manager*)userdata;
    const char *path, *interface, *member;

    pa_assert(c);
    pa_assert(m);
    pa_assert(u);

    path = dbus_message_get_path(m);
    interface = dbus_message_get_interface(m);
    member = dbus_message_get_member(m);

    pa_log_debug("dbus: path=%s, interface=%s, member=%s", path, interface, member);

    if (!pa_streq(path, STREAM_MANAGER_OBJECT_PATH))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (dbus_message_is_method_call(m, "org.freedesktop.DBus.Introspectable", "Introspect")) {
        return handle_introspect(c, m, u);
    } else {
        return handle_methods(c, m, u);
    }

    return DBUS_HANDLER_RESULT_HANDLED;
}
#endif

static int convert_route_type(stream_route_type_t *route_type, const char *route_type_string) {
    int ret = 0;

    pa_assert(route_type);
    pa_assert(route_type_string);

    if (pa_streq("auto", route_type_string))
        *route_type = STREAM_ROUTE_TYPE_AUTO;
    else if (pa_streq("auto-all", route_type_string))
        *route_type = STREAM_ROUTE_TYPE_AUTO_ALL;
    else if (pa_streq("manual", route_type_string))
        *route_type = STREAM_ROUTE_TYPE_MANUAL;
    else if (pa_streq("manual-ext", route_type_string))
        *route_type = STREAM_ROUTE_TYPE_MANUAL_EXT;
    else {
        ret = -1;
        pa_log_error("Not supported route_type(%s)", route_type_string);
    }

    return ret;
}

static void dump_stream_map (pa_stream_manager *m) {
    stream_info *s = NULL;
    const char *role = NULL;
    char *name = NULL;
    void *state = NULL;
    uint32_t idx = 0;
    pa_assert(m);
    pa_log_debug("==========[START stream-map dump]==========");
    while (m->stream_infos && (s = pa_hashmap_iterate(m->stream_infos, &state, (const void **)&role))) {
        pa_log_debug("[role : %s]", role);
        pa_log_debug("  - priority   : %d", s->priority);
        pa_log_debug("  - route-type : %d (0:auto,1:auto-all,2:manual,3:manual-ext)", s->route_type);
        pa_log_debug("  - volume-types : in[%s], out[%s]", s->volume_type[STREAM_DIRECTION_IN], s->volume_type[STREAM_DIRECTION_OUT]);
        pa_log_debug("  - avail-in-devices");
        PA_IDXSET_FOREACH(name, s->idx_avail_in_devices, idx)
            pa_log_debug("      name[%d]  : %s", idx, name);
        pa_log_debug("  - avail-out-devices");
        PA_IDXSET_FOREACH(name, s->idx_avail_out_devices, idx)
            pa_log_debug("      name[%d]  : %s", idx, name);
        pa_log_debug("  - avail-frameworks");
        PA_IDXSET_FOREACH(name, s->idx_avail_frameworks, idx)
            pa_log_debug("      name[%d]  : %s", idx, name);
    }
    pa_log_debug("===========[END stream-map dump]===========");
    return;
}

static int init_stream_map (pa_stream_manager *m) {
    volume_info *v;
    stream_info *s;
    json_object *o;
    json_object *volume_array_o;
    json_object *stream_array_o;
    json_object *volume_type_o;
    json_object *is_hal_volume_o;
    json_object *role_o;
    json_object *priority_o;
    json_object *route_type_o;
    json_object *volume_types_o;
    json_object *avail_in_devices_o;
    json_object *avail_out_devices_o;
    json_object *avail_frameworks_o;
    int num_of_volume_types = 0;
    int num_of_stream_types = 0;
    const char *volume_type = NULL;
    const char *role = NULL;
    int i = 0, j = 0;
    int num_of_avail_in_devices;
    int num_of_avail_out_devices;
    int num_of_avail_frameworks;
    json_object *out_device_o;
    json_object *in_device_o;
    json_object *framework_o;
    json_object *volume_o;
    json_object *stream_o;
    const char *volume_type_in_str = NULL;
    const char *volume_type_out_str = NULL;
    json_object *volume_type_in_o;
    json_object *volume_type_out_o;
    void *state = NULL;

    pa_assert(m);

    o = json_object_from_file(STREAM_MAP_FILE);
    if(is_error(o)) {
        pa_log_error("Read stream-map file(%s) failed", STREAM_MAP_FILE);
        return -1;
    }

    /* Volumes */
    m->volume_infos = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
    if((volume_array_o = json_object_object_get(o, STREAM_MAP_VOLUMES)) && json_object_is_type(volume_array_o, json_type_array)) {
        num_of_volume_types = json_object_array_length(volume_array_o);
        for (i = 0; i < num_of_volume_types; i++) {
            if((volume_o = json_object_array_get_idx(volume_array_o, i)) && json_object_is_type(volume_o, json_type_object)) {
                v = pa_xmalloc0(sizeof(volume_info));
                pa_log_debug("volume found [%d]", i);
                if((volume_type_o = json_object_object_get(volume_o, STREAM_MAP_VOLUME_TYPE)) && json_object_is_type(volume_type_o, json_type_string)) {
                    volume_type = json_object_get_string(volume_type_o);
                    pa_log_debug(" - type : %s", volume_type);
                } else {
                    pa_log_error("Get volume type failed");
                    goto failed;
                }
                if((is_hal_volume_o = json_object_object_get(volume_o, STREAM_MAP_VOLUME_IS_FOR_HAL)) && json_object_is_type(is_hal_volume_o, json_type_int)) {
                    v->is_hal_volume_type = (pa_bool_t)json_object_get_int(is_hal_volume_o);
                    pa_log_debug(" - is-hal-volume : %d", v->is_hal_volume_type);
                } else {
                    pa_log_error("Get is-hal-volume failed");
                    goto failed;
                }
                pa_hashmap_put(m->volume_infos,(void*)volume_type, v);
            }
        }
    }

    /* Streams */
    m->stream_infos = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
    if((stream_array_o = json_object_object_get(o, STREAM_MAP_STREAMS)) && json_object_is_type(stream_array_o, json_type_array)) {
        num_of_stream_types = json_object_array_length(stream_array_o);
        for (i = 0; i < num_of_stream_types; i++) {

            if((stream_o = json_object_array_get_idx(stream_array_o, i)) && json_object_is_type(stream_o, json_type_object)) {
                s = pa_xmalloc0(sizeof(stream_info));
                pa_log_debug("stream found [%d]", i);
                if((role_o = json_object_object_get(stream_o, STREAM_MAP_STREAM_ROLE)) && json_object_is_type(role_o, json_type_string)) {
                    role = json_object_get_string(role_o);
                    pa_log_debug(" - role : %s", role);
                } else {
                    pa_log_error("Get stream role failed");
                    goto failed;
                }
                if((priority_o = json_object_object_get(stream_o, STREAM_MAP_STREAM_PRIORITY)) && json_object_is_type(priority_o, json_type_int)) {
                    s->priority = json_object_get_int(priority_o);
                    pa_log_debug(" - priority : %d", s->priority);
                } else {
                    pa_log_error("Get stream priority failed");
                    goto failed;
                }
                if((route_type_o = json_object_object_get(stream_o, STREAM_MAP_STREAM_ROUTE_TYPE)) && json_object_is_type(route_type_o, json_type_string)) {
                    if (convert_route_type(&(s->route_type), json_object_get_string(route_type_o))) {
                        pa_log_error("convert stream route-type failed");
                        goto failed;
                    }
                    pa_log_debug(" - route-type : %d", s->route_type);
                } else {
                    pa_log_error("Get stream route-type failed");
                    goto failed;
                }
                if((volume_types_o = json_object_object_get(stream_o, STREAM_MAP_STREAM_VOLUME_TYPES)) && json_object_is_type(volume_types_o, json_type_object)) {
                    if((volume_type_in_o = json_object_object_get(volume_types_o, STREAM_MAP_STREAM_VOLUME_TYPE_IN)) && json_object_is_type(volume_type_in_o, json_type_string)) {
                        volume_type_in_str = json_object_get_string(volume_type_in_o);
                        if (!pa_streq(volume_type_in_str, "none"))
                            s->volume_type[STREAM_DIRECTION_IN] = volume_type_in_str;
                    } else {
                        pa_log_error("Get stream volume-type-in failed");
                        goto failed;
                    }
                    if((volume_type_out_o = json_object_object_get(volume_types_o, STREAM_MAP_STREAM_VOLUME_TYPE_OUT)) && json_object_is_type(volume_type_out_o, json_type_string)) {
                        volume_type_out_str = json_object_get_string(volume_type_out_o);
                        if (!pa_streq(volume_type_out_str, "none"))
                            s->volume_type[STREAM_DIRECTION_OUT] = volume_type_out_str;
                    } else {
                        pa_log_error("Get stream volume-type-out failed");
                        goto failed;
                    }
                    pa_log_debug(" - volume-types : in[%s], out[%s]", s->volume_type[STREAM_DIRECTION_IN], s->volume_type[STREAM_DIRECTION_OUT]);
                } else {
                    pa_log_error("Get stream volume-types failed");
                    goto failed;
                }
                if((avail_in_devices_o = json_object_object_get(stream_o, STREAM_MAP_STREAM_AVAIL_IN_DEVICES)) && json_object_is_type(avail_in_devices_o, json_type_array)) {
                    j = 0;
                    s->idx_avail_in_devices = pa_idxset_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
                    num_of_avail_in_devices = json_object_array_length(avail_in_devices_o);
                    pa_log_debug(" - avail-in-devices");
                    for (j = 0; j < num_of_avail_in_devices; j++) {
                        if((in_device_o = json_object_array_get_idx(avail_in_devices_o, j)) && json_object_is_type(in_device_o, json_type_string)) {
                            pa_idxset_put(s->idx_avail_in_devices, (void*)json_object_get_string(in_device_o), NULL);
                            pa_log_debug("      device[%d] : %s", j, json_object_get_string(in_device_o));
                           }
                       }
                } else {
                    pa_log_error("Get stream avail-in-devices failed");
                    goto failed;
                }
                if((avail_out_devices_o = json_object_object_get(stream_o, STREAM_MAP_STREAM_AVAIL_OUT_DEVICES)) && json_object_is_type(avail_out_devices_o, json_type_array)) {
                    j = 0;
                    s->idx_avail_out_devices = pa_idxset_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
                    num_of_avail_out_devices = json_object_array_length(avail_out_devices_o);
                    pa_log_debug(" - avail-out-devices");
                    for (j = 0; j < num_of_avail_out_devices; j++) {
                        if((out_device_o = json_object_array_get_idx(avail_out_devices_o, j)) && json_object_is_type(out_device_o, json_type_string)) {
                            pa_idxset_put(s->idx_avail_out_devices, (void*)json_object_get_string(out_device_o), NULL);
                            pa_log_debug("      device[%d] : %s", j, json_object_get_string(out_device_o));
                           }
                       }
                } else {
                    pa_log_error("Get stream avail-out-devices failed");
                    goto failed;
                }
                if((avail_frameworks_o = json_object_object_get(stream_o, STREAM_MAP_STREAM_AVAIL_FRAMEWORKS)) && json_object_is_type(avail_frameworks_o, json_type_array)) {
                    j = 0;
                    s->idx_avail_frameworks = pa_idxset_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
                    num_of_avail_frameworks = json_object_array_length(avail_frameworks_o);
                    pa_log_debug(" - avail-frameworks");
                    for (j = 0; j < num_of_avail_frameworks; j++) {
                        if((framework_o = json_object_array_get_idx(avail_frameworks_o, j)) && json_object_is_type(framework_o, json_type_string)) {
                            pa_idxset_put(s->idx_avail_frameworks, (void*)json_object_get_string(framework_o), NULL);
                            pa_log_debug("      framework[%d] : %s", j, json_object_get_string(framework_o));
                           }
                       }
                } else {
                    pa_log_error("Get stream avail-frameworks failed");
                    goto failed;
                }
                pa_hashmap_put(m->stream_infos,(void*)role, s);
            }
        }
    } else {
        pa_log_error("Get streams object failed");
        goto failed;
    }

    dump_stream_map(m);

    return 0;
failed:
    pa_log_error("failed to initialize stream-map");
    if (m->stream_infos) {
        PA_HASHMAP_FOREACH(s, m->stream_infos, state) {
            if (s->idx_avail_in_devices)
                pa_idxset_free(s->idx_avail_in_devices, NULL);
            if (s->idx_avail_out_devices)
                pa_idxset_free(s->idx_avail_out_devices, NULL);
            if (s->idx_avail_frameworks)
                pa_idxset_free(s->idx_avail_frameworks, NULL);
            pa_xfree(s);
        }
        pa_hashmap_free(m->stream_infos);
    }
    if (m->volume_infos) {
        PA_HASHMAP_FOREACH(v, m->volume_infos, state) {
            pa_xfree(v);
        }
        pa_hashmap_free(m->volume_infos);
    }
    return -1;
}

static void deinit_stream_map (pa_stream_manager *m) {
    stream_info *s = NULL;
    volume_info *v = NULL;
    void *state = NULL;
    pa_assert(m);

    if (m->stream_infos) {
        PA_HASHMAP_FOREACH(s, m->stream_infos, state) {
            if (s->idx_avail_in_devices)
                pa_idxset_free(s->idx_avail_in_devices, NULL);
            if (s->idx_avail_out_devices)
                pa_idxset_free(s->idx_avail_out_devices, NULL);
            if (s->idx_avail_frameworks)
                pa_idxset_free(s->idx_avail_frameworks, NULL);
            pa_xfree(s);
        }
        pa_hashmap_free(m->stream_infos);
    }
    if (m->volume_infos) {
        PA_HASHMAP_FOREACH(v, m->volume_infos, state) {
            pa_xfree(v);
        }
        pa_hashmap_free(m->volume_infos);
    }

    return;
}

static pa_bool_t check_name_to_skip(pa_stream_manager *m, process_command_type_t command, stream_type_t type, void *stream) {
    pa_bool_t ret = FALSE;
    const char *name = NULL;
    const char *role = NULL;
    int i = 0;

    pa_assert(m);
    pa_assert(stream);

    if (command == PROCESS_COMMAND_PREPARE) {
        name = pa_proplist_gets(GET_STREAM_NEW_PROPLIST(stream, type), PA_PROP_MEDIA_NAME);
        if (name) {
            for (i = 0; i < NAME_FOR_SKIP_MAX; i++)
                if (pa_streq(name, stream_manager_media_names_for_skip[i])) {
                    ret = TRUE;
                    pa_proplist_sets(GET_STREAM_NEW_PROPLIST(stream, type), PA_PROP_MEDIA_ROLE, SKIP_ROLE);
                    break;
                }
            pa_log_info("name is [%s], skip(%d)", name, ret);
        }
    } else {
        if (command == PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED_WITH_NEW_DATA ||
            command == PROCESS_COMMAND_UPDATE_VOLUME)
            role = pa_proplist_gets(GET_STREAM_NEW_PROPLIST(stream, type), PA_PROP_MEDIA_ROLE);
        else
            role = pa_proplist_gets(GET_STREAM_PROPLIST(stream, type), PA_PROP_MEDIA_ROLE);

        if (role && pa_streq(role, "skip"))
            ret = TRUE;
    }

    return ret;
}

static pa_bool_t check_role_to_skip(pa_stream_manager *m, process_command_type_t command, const char *role) {
    pa_bool_t ret = FALSE;
    stream_info *s = NULL;

    pa_assert(m);
    pa_assert(role);

    if (m->stream_infos) {
        s = pa_hashmap_get(m->stream_infos, role);
        if (s)
            ret = FALSE;
        else
            ret = TRUE;
    }
    pa_log_info("role is [%s], skip(%d) for %s", role, ret, process_command_type_str[command]);

    return ret;
}

static pa_bool_t check_route_type_to_skip(process_command_type_t command, const char *route_type_str) {
    pa_bool_t ret = FALSE;
    stream_route_type_t route_type;
    pa_assert(route_type_str);

    if (!pa_atoi(route_type_str, (int32_t*)&route_type)) {
        if ((route_type == STREAM_ROUTE_TYPE_MANUAL_EXT) &&
            (command > PROCESS_COMMAND_PREPARE && command < PROCESS_COMMAND_UPDATE_VOLUME))
            ret = TRUE; /* this route route is for external device, need to skip */
    } else {
        pa_log_error("could not convert route_type_str(%s) to int", route_type_str);
        ret = TRUE;
    }
    pa_log_debug("command is [%s], skip(%d) for route_type(%d)", process_command_type_str[command], ret, route_type);

    return ret;
}

static pa_bool_t update_priority_of_stream(pa_stream_manager *m, process_command_type_t command, stream_type_t type, void *stream, const char *role) {
    stream_info *s = NULL;

    pa_assert(m);
    pa_assert(role);

    if (m->stream_infos)
        s = pa_hashmap_get(m->stream_infos, role);
    else
        return FALSE;

    if (s) {
        if (command == PROCESS_COMMAND_PREPARE)
            pa_proplist_set(GET_STREAM_NEW_PROPLIST(stream, type), PA_PROP_MEDIA_ROLE_PRIORITY, (const void*)&(s->priority), sizeof(s->priority));
        else
            pa_proplist_set(GET_STREAM_PROPLIST(stream, type), PA_PROP_MEDIA_ROLE_PRIORITY, (const void*)&(s->priority), sizeof(s->priority));
    }

    return TRUE;
}

static pa_bool_t update_route_type_of_stream(pa_stream_manager *m, void *stream, stream_type_t type, const char *role) {
    stream_route_type_t route_type = STREAM_ROUTE_TYPE_AUTO;
    stream_info *s = NULL;

    pa_assert(m);
    pa_assert(role);

    if (m->stream_infos) {
        s = pa_hashmap_get(m->stream_infos, role);
        if (s)
            route_type = s->route_type;
    } else
        return FALSE;

    pa_proplist_setf(GET_STREAM_NEW_PROPLIST(stream, type), PA_PROP_MEDIA_ROLE_ROUTE_TYPE, "%d", route_type);

    return TRUE;
}

static pa_bool_t update_volume_type_of_stream(pa_stream_manager *m, stream_type_t type, void *stream, const char *role) {
    const char *volume_type = NULL;
    stream_info *s = NULL;

    pa_assert(m);
    pa_assert(role);

    if (m->stream_infos) {
        s = pa_hashmap_get(m->stream_infos, role);
        if (s)
            volume_type = s->volume_type[!type];
    } else
        return FALSE;

    if (volume_type)
        pa_proplist_sets(GET_STREAM_NEW_PROPLIST(stream, type), PA_PROP_MEDIA_TIZEN_VOLUME_TYPE, volume_type);
    else
        pa_log_warn("this stream[%p] does not have any volume type, skip updating volume type. stream_type[%d], role[%s]", stream, type, role);

    return TRUE;
}

static pa_bool_t update_focus_status_of_stream(pa_stream_manager *m, process_command_type_t command, void *stream, stream_type_t type) {
    const char *p_idx;
    uint32_t parent_idx;
    stream_parent *sp = NULL;

    pa_assert(m);
    pa_assert(stream);

    if (command == PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED_WITH_NEW_DATA)
        p_idx = pa_proplist_gets(GET_STREAM_NEW_PROPLIST(stream, type), PA_PROP_MEDIA_PARENT_ID);
    else
        p_idx = pa_proplist_gets(GET_STREAM_PROPLIST(stream, type), PA_PROP_MEDIA_PARENT_ID);
    if (p_idx && !pa_atou(p_idx, &parent_idx)) {
        pa_log_debug("p_idx(%s), idx(%u)", p_idx, parent_idx);
        sp = pa_hashmap_get(m->stream_parents, (const void*)parent_idx);
        if (sp) {
            if (command == PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED_WITH_NEW_DATA)
                pa_proplist_setf(GET_STREAM_NEW_PROPLIST(stream, type), PA_PROP_MEDIA_FOCUS_STATUS, "%u", sp->focus_status);
            else
                pa_proplist_setf(GET_STREAM_PROPLIST(stream, type), PA_PROP_MEDIA_FOCUS_STATUS, "%u", sp->focus_status);
        } else {
            pa_log_error("could not find matching client for this parent_id(%u)", parent_idx);
            return FALSE;
        }
    } else {
        pa_log_warn("p_idx(%s) or idx(%u) is not valid", p_idx, parent_idx);
        return FALSE;
    }

    return TRUE;
}

static pa_bool_t update_stream_parent_info(pa_stream_manager *m, process_command_type_t command, stream_type_t type, void *stream) {
    const char *p_idx;
    uint32_t parent_idx;
    stream_parent *sp = NULL;

    pa_assert(m);
    pa_assert(stream);

    p_idx = pa_proplist_gets(GET_STREAM_PROPLIST(stream, type), PA_PROP_MEDIA_PARENT_ID);
    if (p_idx && !pa_atou(p_idx, &parent_idx)) {
        pa_log_debug("p_idx(%s), idx(%u)", p_idx, parent_idx);
        sp = pa_hashmap_get(m->stream_parents, (const void*)parent_idx);
        if (sp) {
            uint32_t idx = (type==STREAM_SINK_INPUT)?((pa_sink_input*)stream)->index:((pa_source_output*)stream)->index;
            if (command == PROCESS_COMMAND_ADD_PARENT_ID) {
                /* append this stream to the parent stream info. */
                pa_log_debug(" - append this stream(%p, %u) to the list. sp(%p), stream_type(%d)", stream, idx, sp, type);
                pa_idxset_put(type==STREAM_SINK_INPUT?(sp->idx_sink_inputs):(sp->idx_source_outputs), stream, NULL);
                return TRUE;
            } else if (command == PROCESS_COMMAND_REMOVE_PARENT_ID) {
                /* remove this stream from the parent stream info. */
                pa_log_debug(" - remove this stream(%p, %u) from the list. sp(%p), stream_type(%d)", stream, idx, sp, type);
                pa_idxset_remove_by_data(type==STREAM_SINK_INPUT?(sp->idx_sink_inputs):(sp->idx_source_outputs), stream, NULL);
                return TRUE;
            } else {
                pa_log_error("invalid command(%d)", command);
                return FALSE;
            }
        } else {
            pa_log_error("could not find matching client for this parent_id(%u)", parent_idx);
            return FALSE;
        }
    } else {
        pa_log_warn("p_idx(%s) or idx(%u) is not valid", p_idx, parent_idx);
        return FALSE;
    }
    return TRUE;
}

static pa_bool_t update_the_highest_priority_stream(pa_stream_manager *m, process_command_type_t command, void *mine,
                                                    stream_type_t type, const char *role, pa_bool_t *need_to_update) {
    uint32_t idx = 0;
    size_t size = 0;
    const int32_t *priority = NULL;
    const char *focus_status = NULL;
    void *cur_max_stream = NULL;
    void *cur_max_stream_tmp = NULL;
    const int32_t *cur_max_priority = NULL;
    const char *cur_max_role = NULL;
    int32_t cur_max_focus_status_int = 0;
    int32_t focus_status_int = 0;
    void *i = NULL;
    const char *_role = NULL;
    pa_idxset *streams = NULL;

    pa_assert(m);
    pa_assert(mine);
    if (!role) {
        pa_log_error("invalid input, role(%s)", role);
        return FALSE;
    }

    *need_to_update = FALSE;

    if (type == STREAM_SINK_INPUT) {
        cur_max_stream = m->cur_highest_priority.sink_input;
    } else if (type == STREAM_SOURCE_OUTPUT) {
        cur_max_stream = m->cur_highest_priority.source_output;
    }

    pa_log_info("update_the_highest_priority_stream(), stream_type(%d), role(%s), command(%d)", type, role, command);
    if (command == PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED_WITH_NEW_DATA ||
        command == PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED) {
        /* get focus status */
        if (command == PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED_WITH_NEW_DATA)
            focus_status = pa_proplist_gets(GET_STREAM_NEW_PROPLIST(mine, type), PA_PROP_MEDIA_FOCUS_STATUS);
        else
            focus_status = pa_proplist_gets(GET_STREAM_PROPLIST(mine, type), PA_PROP_MEDIA_FOCUS_STATUS);
        if (focus_status && !pa_atoi(focus_status, &focus_status_int)) {
            pa_log_debug("focus status(0x%x)", focus_status_int);
        }
        if (cur_max_stream == NULL) {
            *need_to_update = TRUE;
            pa_log_debug("set cur_highest to mine");
        } else {
            /* TODO : need to check if this stream should be played to external devices */
            if (command == PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED_WITH_NEW_DATA) {
                if (pa_proplist_get(GET_STREAM_NEW_PROPLIST(mine, type), PA_PROP_MEDIA_ROLE_PRIORITY, (const void**)&priority, &size))
                    pa_log_error("failed to pa_proplist_get() for priority");
            } else {
                if (pa_proplist_get(GET_STREAM_PROPLIST(mine, type), PA_PROP_MEDIA_ROLE_PRIORITY, (const void**)&priority, &size))
                    pa_log_error("failed to pa_proplist_get() for priority");
            }
            if (pa_proplist_get(GET_STREAM_PROPLIST(cur_max_stream, type), PA_PROP_MEDIA_ROLE_PRIORITY, (const void**)&cur_max_priority, &size))
                pa_log_error("failed to pa_proplist_get() for priority");
            focus_status = pa_proplist_gets(GET_STREAM_PROPLIST(cur_max_stream, type), PA_PROP_MEDIA_FOCUS_STATUS);
            if (focus_status && !pa_atoi(focus_status, &cur_max_focus_status_int)) {
                pa_log_debug("cur_max_focus status(0x%x)", cur_max_focus_status_int);
            }
            cur_max_role = pa_proplist_gets(GET_STREAM_PROPLIST(cur_max_stream, type), PA_PROP_MEDIA_ROLE);
            if (!cur_max_priority || !cur_max_role) {
                pa_log_error("failed to pa_proplist_gets() for getting current max priority(%p) and it's role(%s)", cur_max_priority, cur_max_role);
                return FALSE;
            } else {
                if (priority && cur_max_priority) {
                    if (IS_FOCUS_ACQUIRED(focus_status_int, type) ||
                        (!IS_FOCUS_ACQUIRED(cur_max_focus_status_int, type) && *priority >= *cur_max_priority)) {
                        *need_to_update = TRUE;
                        pa_log_debug("update cur_highest to mine(%s)", role);
                    } else {
                        /* no need to trigger */
                        return TRUE;
                    }
                }
            }
        }
    } else if (command == PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_ENDED ||
            command == PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_FOCUS_CHANGED) {
        if (cur_max_stream == mine || command == PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_FOCUS_CHANGED) {
            if (type == STREAM_SINK_INPUT) {
                streams = ((pa_sink_input*)mine)->sink->inputs;
            } else if (type == STREAM_SOURCE_OUTPUT) {
                streams = ((pa_source_output*)mine)->source->outputs;
            }
            /* find the next highest priority input */
            PA_IDXSET_FOREACH(i, streams, idx) {
                if (!(_role = pa_proplist_gets(GET_STREAM_PROPLIST(i, type), PA_PROP_MEDIA_ROLE))) {
                    pa_log_error("failed to pa_proplist_gets() for role");
                    continue;
                }
                if (pa_proplist_get(GET_STREAM_PROPLIST(i, type), PA_PROP_MEDIA_ROLE_PRIORITY, (const void**)&priority, &size)) {
                    pa_log_warn("failed to pa_proplist_get() for priority, skip it");
                    continue;
                }
                if (!(focus_status = pa_proplist_gets(GET_STREAM_PROPLIST(i, type), PA_PROP_MEDIA_FOCUS_STATUS))) {
                    pa_log_warn("failed to pa_proplist_gets() for focus status");
                } else {
                    if (!pa_atoi(focus_status, &focus_status_int)) {
                        pa_log_debug("focus status(0x%x)", focus_status_int);
                        }
                }
                pa_log_debug("role(%s)/priority(%p)/focus_state(0x%x)/stream(%p)", _role, priority, focus_status_int, i);
                if (cur_max_priority == NULL) {
                    cur_max_priority = priority;
                    cur_max_focus_status_int = focus_status_int;
                    cur_max_stream_tmp = i;
                }
                if (cur_max_priority && priority) {
                    if (IS_FOCUS_ACQUIRED(cur_max_focus_status_int, type) ||
                        (!IS_FOCUS_ACQUIRED(focus_status_int, type) && (*cur_max_priority > *priority))) {
                        /* skip */
                    } else  {
                        cur_max_priority = priority;
                        cur_max_focus_status_int = focus_status_int;
                        cur_max_stream_tmp = i;
                    }
                }
            }
            pa_log_debug("updated max priority(%p)/stream(%p)", cur_max_priority, cur_max_stream_tmp);
            if (cur_max_stream_tmp) {
                if (type == STREAM_SINK_INPUT) {
                    m->cur_highest_priority.sink_input = cur_max_stream_tmp;
                } else if (type == STREAM_SOURCE_OUTPUT) {
                    m->cur_highest_priority.source_output = cur_max_stream_tmp;
                }
            } else {
                if (type == STREAM_SINK_INPUT) {
                    m->cur_highest_priority.sink_input = NULL;
                } else if (type == STREAM_SOURCE_OUTPUT) {
                    m->cur_highest_priority.source_output = NULL;
                }
            }
            *need_to_update = TRUE;
            pa_log_info("need to update: type(%d), cur_highest_priority(sink_input=%p/source_output=%p)",
                        type, (void*)m->cur_highest_priority.sink_input, (void*)m->cur_highest_priority.sink_input);
        } else {
            /* no need to trigger */
            return TRUE;
        }
    }
    return TRUE;
}

static void fill_device_info_to_hook_data(void *hook_data, notify_command_type_t command, stream_type_t type, void *stream, pa_bool_t is_new_data, pa_stream_manager *m) {
    char *device_none = NULL;
    const char *p_idx = NULL;
    uint32_t parent_idx = 0;
    stream_parent *sp = NULL;
    pa_stream_manager_hook_data_for_select *select_data = NULL;
    pa_stream_manager_hook_data_for_route *route_data = NULL;
    stream_info *si;
    pa_idxset *avail_devices;
    uint32_t list_len = 0;

    pa_assert(hook_data);
    pa_assert(m);
    pa_log_debug("fill_device_info_to_hook_data() for %s", notify_command_type_str[command]);

    switch (command) {
    case NOTIFY_COMMAND_SELECT_PROPER_SINK_OR_SOURCE_FOR_INIT: {
        select_data = (pa_stream_manager_hook_data_for_select*)hook_data;
        si = pa_hashmap_get(m->stream_infos, select_data->stream_role);
        select_data->route_type = si->route_type;
        avail_devices = (type==STREAM_SINK_INPUT)?si->idx_avail_out_devices:si->idx_avail_in_devices;
        list_len = pa_idxset_size(avail_devices);
        device_none = pa_idxset_get_by_data(avail_devices, "none", NULL);
        if (list_len == 0 || device_none) {
            pa_log_warn("  -- there is no available device, stream_type(%d)", type);
            break;
        }
        select_data->idx_avail_devices = avail_devices;
        select_data->origins_from_new_data = is_new_data;
        if (si->route_type >= STREAM_ROUTE_TYPE_MANUAL) {
            if (is_new_data)
                p_idx = pa_proplist_gets(GET_STREAM_NEW_PROPLIST(stream, type), PA_PROP_MEDIA_PARENT_ID);
            else
                p_idx = pa_proplist_gets(GET_STREAM_PROPLIST(stream, type), PA_PROP_MEDIA_PARENT_ID);
            if (p_idx && !pa_atou(p_idx, &parent_idx)) {
                /* find parent idx, it's device info. and it's stream idxs */
                sp = pa_hashmap_get(m->stream_parents, (const void*)parent_idx);
                if (sp)
                    select_data->idx_manual_devices = (type==STREAM_SINK_INPUT)?(sp->idx_route_out_devices):(sp->idx_route_in_devices);
                else
                    pa_log_warn("  -- failed to get the stream parent of idx(%u)", parent_idx);
            } else
                pa_log_warn("  -- could not get the parent id of this stream, but keep going...");
        }
        break;
    }
    case NOTIFY_COMMAND_CHANGE_ROUTE_START:
    case NOTIFY_COMMAND_CHANGE_ROUTE_END: {
        route_data = (pa_stream_manager_hook_data_for_route*)hook_data;
        si = pa_hashmap_get(m->stream_infos, route_data->stream_role);
        avail_devices = (type==STREAM_SINK_INPUT)?si->idx_avail_out_devices:si->idx_avail_in_devices;
        route_data->route_type = si->route_type;
        list_len = pa_idxset_size(avail_devices);
        device_none = pa_idxset_get_by_data(avail_devices, "none", NULL);

        if (is_new_data)
            p_idx = pa_proplist_gets(GET_STREAM_NEW_PROPLIST(stream, type), PA_PROP_MEDIA_PARENT_ID);
        else
            p_idx = pa_proplist_gets(GET_STREAM_PROPLIST(stream, type), PA_PROP_MEDIA_PARENT_ID);
        if (p_idx && !pa_atou(p_idx, &parent_idx)) {
            sp = pa_hashmap_get(m->stream_parents, (const void*)parent_idx);
            if (!sp)
                pa_log_warn("  -- failed to get the stream parent of idx(%u)", parent_idx);
        } else
           pa_log_warn("  -- could not get the parent id of this stream, but keep going...");

        if (list_len == 0 || device_none) {
            pa_log_warn("  -- there is no available device, stream_type(%d)", type);
            break;
        }
        route_data->idx_avail_devices = avail_devices;
        if (si->route_type >= STREAM_ROUTE_TYPE_MANUAL) {
            if (sp) {
                route_data->idx_manual_devices = (type==STREAM_SINK_INPUT)?(sp->idx_route_out_devices):(sp->idx_route_in_devices);
                route_data->idx_streams = (type==STREAM_SINK_INPUT)?(sp->idx_sink_inputs):(sp->idx_source_outputs);
            } else
                pa_log_warn("  -- failed to get the stream parent of idx(%u)", parent_idx);
        }
        break;
    }
    default:
        break;
    }
    return;
}

static void do_notify(pa_stream_manager *m, notify_command_type_t command, stream_type_t type, pa_bool_t is_new_data, void *user_data) {
    pa_stream_manager_hook_data_for_select hook_call_select_data;
    pa_stream_manager_hook_data_for_route hook_call_route_data;
    pa_stream_manager_hook_data_for_option hook_call_option_data;
    hal_stream_connection_info stream_conn_info;
    const char *role = NULL;
    void *s = NULL;

    pa_assert(m);
    pa_log_debug("do_notify(%s): type(%d), is_new_data(%d), user_data(%p)", notify_command_type_str[command], type, is_new_data, user_data);

    switch (command) {
    case NOTIFY_COMMAND_SELECT_PROPER_SINK_OR_SOURCE_FOR_INIT: {
        pa_assert(user_data);
        memset(&hook_call_select_data, 0, sizeof(pa_stream_manager_hook_data_for_select));
        hook_call_select_data.stream = s = user_data;
        if (s) {
            hook_call_select_data.stream_type = type;
            hook_call_select_data.origins_from_new_data = is_new_data;
            if (is_new_data) {
                hook_call_select_data.stream_role = pa_proplist_gets(GET_STREAM_NEW_PROPLIST(s, type), PA_PROP_MEDIA_ROLE);
                fill_device_info_to_hook_data(&hook_call_select_data, command, type, s, is_new_data, m);
                hook_call_select_data.sample_spec = GET_STREAM_NEW_SAMPLE_SPEC(s, type);
                if (type == STREAM_SINK_INPUT)
                    hook_call_select_data.proper_sink = &(((pa_sink_input_new_data*)s)->sink);
                else if (type == STREAM_SOURCE_OUTPUT)
                    hook_call_select_data.proper_source = &(((pa_source_output_new_data*)s)->source);
            } else {
                hook_call_select_data.stream_role = pa_proplist_gets(GET_STREAM_PROPLIST(s, type), PA_PROP_MEDIA_ROLE);
                fill_device_info_to_hook_data(&hook_call_select_data, command, type, s, is_new_data ,m);
                hook_call_select_data.sample_spec = GET_STREAM_SAMPLE_SPEC(s, type);
                if (type == STREAM_SINK_INPUT)
                    hook_call_select_data.proper_sink = &(((pa_sink_input*)s)->sink);
                else if (type == STREAM_SOURCE_OUTPUT)
                    hook_call_select_data.proper_source = &(((pa_source_output*)s)->source);
            }
            pa_hook_fire(pa_communicator_hook(m->comm.comm, PA_COMMUNICATOR_HOOK_SELECT_INIT_SINK_OR_SOURCE), &hook_call_select_data);
        }
        break;
    }
    case NOTIFY_COMMAND_CHANGE_ROUTE_START: {
        pa_assert(user_data);
        memset(&hook_call_route_data, 0, sizeof(pa_stream_manager_hook_data_for_route));
        hook_call_route_data.stream = s = user_data;
        if (s) {
            if (is_new_data) {
                hook_call_route_data.origins_from_new_data = TRUE;
                role = pa_proplist_gets(GET_STREAM_NEW_PROPLIST(s, type), PA_PROP_MEDIA_ROLE);
                hook_call_route_data.sample_spec = GET_STREAM_NEW_SAMPLE_SPEC(s, type);
                if (type == STREAM_SINK_INPUT) {
                    hook_call_route_data.proper_sink = &(((pa_sink_input_new_data*)s)->sink);
                } else if (type == STREAM_SOURCE_OUTPUT) {
                    hook_call_route_data.proper_source = &(((pa_source_output_new_data*)s)->source);
                }
            } else {
                role = pa_proplist_gets(GET_STREAM_PROPLIST(s, type), PA_PROP_MEDIA_ROLE);
                hook_call_route_data.sample_spec = GET_STREAM_SAMPLE_SPEC(s, type);
                hook_call_route_data.idx_streams = (type==STREAM_SINK_INPUT)?((pa_sink_input*)s)->sink->inputs:((pa_source_output*)s)->source->outputs;
            }
            hook_call_route_data.stream_type = type;
            hook_call_route_data.stream_role = role;
            fill_device_info_to_hook_data(&hook_call_route_data, command, type, s, is_new_data, m);
            if (hook_call_route_data.route_type >= STREAM_ROUTE_TYPE_MANUAL) {
                if (hook_call_route_data.idx_manual_devices && !pa_idxset_size(hook_call_route_data.idx_manual_devices)) {
                    pa_log_warn("no manual device for this type(%d), skip it..", type);
                    break;
                }
            }
            pa_hook_fire(pa_communicator_hook(m->comm.comm, PA_COMMUNICATOR_HOOK_CHANGE_ROUTE), &hook_call_route_data);
        }
        break;
    }
    case NOTIFY_COMMAND_CHANGE_ROUTE_END: {
        memset(&hook_call_route_data, 0, sizeof(pa_stream_manager_hook_data_for_route));
        s = (type==STREAM_SINK_INPUT)?(void*)(m->cur_highest_priority.sink_input):(void*)(m->cur_highest_priority.source_output);
        if (s) {
            role = pa_proplist_gets(GET_STREAM_PROPLIST(s, type), PA_PROP_MEDIA_ROLE);
            hook_call_route_data.stream_type = type;
            hook_call_route_data.stream_role = role;
            hook_call_route_data.sample_spec = GET_STREAM_SAMPLE_SPEC(s, type);
            hook_call_route_data.idx_streams = (type==STREAM_SINK_INPUT)?((pa_sink_input*)s)->sink->inputs:((pa_source_output*)s)->source->outputs; // need to check it
            fill_device_info_to_hook_data(&hook_call_route_data, command, type, s, is_new_data, m);
        } else {
            pa_log_info("no stream for this type(%d), need to unset route", type);
            hook_call_route_data.stream_type = type;
            hook_call_route_data.stream_role = "reset";
        }
        pa_hook_fire(pa_communicator_hook(m->comm.comm, PA_COMMUNICATOR_HOOK_CHANGE_ROUTE), &hook_call_route_data);
        break;
    }
    case NOTIFY_COMMAND_UPDATE_ROUTE_OPTION: {
        pa_assert(user_data);
        memset(&hook_call_option_data, 0, sizeof(pa_stream_manager_hook_data_for_option));
        s = (type==STREAM_SINK_INPUT)?(void*)(m->cur_highest_priority.sink_input):(void*)(m->cur_highest_priority.source_output);
        if (s) {
            role = pa_proplist_gets(GET_STREAM_PROPLIST(s, type), PA_PROP_MEDIA_ROLE);
            hook_call_option_data.stream_role = role;
            hook_call_option_data.name = ((stream_route_option*)user_data)->name;
            hook_call_option_data.value = ((stream_route_option*)user_data)->value;
            pa_hook_fire(pa_communicator_hook(m->comm.comm, PA_COMMUNICATOR_HOOK_UPDATE_ROUTE_OPTION), &hook_call_option_data);
        }
        break;
    }
    case NOTIFY_COMMAND_INFORM_STREAM_CONNECTED:
    case NOTIFY_COMMAND_INFORM_STREAM_DISCONNECTED: {
        pa_assert(user_data);
        memset(&stream_conn_info, 0, sizeof(hal_stream_connection_info));
        s = user_data;
        if (s) {
            stream_conn_info.role = pa_proplist_gets(GET_STREAM_PROPLIST(s, type), PA_PROP_MEDIA_ROLE);
            stream_conn_info.direction = (type==STREAM_SINK_INPUT)?DIRECTION_OUT:DIRECTION_IN;
            stream_conn_info.idx = (type==STREAM_SINK_INPUT)?((pa_sink_input*)s)->index:((pa_source_output*)s)->index;
            stream_conn_info.is_connected = (command == NOTIFY_COMMAND_INFORM_STREAM_CONNECTED)?TRUE:FALSE;
            pa_hal_manager_update_stream_connection_info(m->hal, &stream_conn_info);
        }
        break;
    }
    }
    return;
}

static process_stream_result_t process_stream(stream_type_t type, void *stream, process_command_type_t command, pa_stream_manager *m) {
    process_stream_result_t result = PROCESS_STREAM_RESULT_OK;
    const char *role = NULL;
    const char *route_type_str = NULL;
    stream_route_type_t route_type;
    pa_bool_t ret = TRUE;
    pa_bool_t need_update = FALSE;
    int32_t volume_ret = 0;
    volume_info *v = NULL;
    const char *si_volume_type_str = NULL;
    const int32_t *prior_priority = NULL;
    size_t size = 0;
    pa_format_info *req_format = NULL;
    char *format_str = NULL;
    const char *rate_str = NULL;
    const char *ch_str = NULL;

    pa_log_info(">>> process_stream(%s): stream_type(%d), stream(%p), m(%p)", process_command_type_str[command], type, stream, m);
    pa_assert(stream);
    pa_assert(m);

    if (check_name_to_skip(m, command, type, stream)) {
        result = PROCESS_STREAM_RESULT_SKIP;
        goto FAILURE;
    }

    if (command == PROCESS_COMMAND_PREPARE) {
        if (type == STREAM_SINK_INPUT) {
            /* Parse request formats for samplerate, channel, format infomation */
            if (((pa_sink_input_new_data*)stream)->req_formats) {
                req_format = pa_idxset_first(((pa_sink_input_new_data*)stream)->req_formats, NULL);
                if (req_format && req_format->plist) {
                    /* set sample_spec */
                    rate_str = pa_proplist_gets(req_format->plist, PA_PROP_FORMAT_RATE);
                    ch_str = pa_proplist_gets(req_format->plist, PA_PROP_FORMAT_CHANNELS);
                    if (pa_format_info_get_prop_string(req_format, PA_PROP_FORMAT_SAMPLE_FORMAT, &format_str)==0)
                        ((pa_sink_input_new_data*)stream)->sample_spec.format = pa_parse_sample_format((const char*)format_str);
                    pa_log_info("req rate(%s), req ch(%s), req format(%s)", rate_str, ch_str, format_str);
                    if (ch_str)
                        ((pa_sink_input_new_data*)stream)->sample_spec.channels = atoi (ch_str);
                    if (rate_str)
                        ((pa_sink_input_new_data*)stream)->sample_spec.rate = atoi (rate_str);
                    /* set channel map if it is not set by client */
                    if (!((pa_sink_input_new_data*)stream)->channel_map_is_set) {
                        pa_channel_map_init_auto(&(((pa_sink_input_new_data*)stream)->channel_map), ((pa_sink_input_new_data*)stream)->sample_spec.channels, PA_CHANNEL_MAP_ALSA);
                        pa_log_info("set default channel_map: channels(%u)",((pa_sink_input_new_data*)stream)->channel_map.channels);
                        ((pa_sink_input_new_data*)stream)->channel_map_is_set = TRUE;
                    }
                }
            } else {
                pa_log_debug("no request formats available");
            }
        }
        role = pa_proplist_gets(GET_STREAM_NEW_PROPLIST(stream, type), PA_PROP_MEDIA_ROLE);
        if (!role) {
            /* set default value for role and priority */
            role = DEFAULT_ROLE;
            pa_proplist_sets(GET_STREAM_NEW_PROPLIST(stream, type), PA_PROP_MEDIA_ROLE, role);
            pa_log_warn("role is null, set default to [%s]", role);
        } else {
            /* skip roles */
            if (check_role_to_skip(m, command, role)) {
                result = PROCESS_STREAM_RESULT_SKIP;
                goto FAILURE;
            }
        }
        /* update the priority of this stream */
        ret = update_priority_of_stream(m, command, type, stream, role);
        if (ret == FALSE) {
            pa_log_error("could not update the priority of '%s' role.", role);
            result = PROCESS_STREAM_RESULT_STOP;
            goto FAILURE;
        }
        /* update the volume type of this stream */
        ret = update_volume_type_of_stream(m, type, stream, role);
        if (ret == FALSE) {
            pa_log_error("could not update the volume type of '%s' role.", role);
            result = PROCESS_STREAM_RESULT_STOP;
            goto FAILURE;
        }
        /* update the route type of this stream */
        ret = update_route_type_of_stream(m, stream, type, role);
        if (ret == FALSE) {
            pa_log_error("could not update the route type of '%s' role.", role);
            result = PROCESS_STREAM_RESULT_STOP;
            goto FAILURE;
        }

        /* skip route types */
        if ((route_type_str = pa_proplist_gets(GET_STREAM_NEW_PROPLIST(stream, type), PA_PROP_MEDIA_ROLE_ROUTE_TYPE))) {
            if (check_route_type_to_skip(command, route_type_str)) {
                result = PROCESS_STREAM_RESULT_SKIP;
                goto FAILURE;
            }
        }

        /* notify to select sink or source */
        do_notify(m, NOTIFY_COMMAND_SELECT_PROPER_SINK_OR_SOURCE_FOR_INIT, type, TRUE, stream);

    } else if (command == PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED_WITH_NEW_DATA ||
            command == PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED) {
        if (command == PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED_WITH_NEW_DATA) {
            role = pa_proplist_gets(GET_STREAM_NEW_PROPLIST(stream, type), PA_PROP_MEDIA_ROLE);
            route_type_str = pa_proplist_gets(GET_STREAM_NEW_PROPLIST(stream, type), PA_PROP_MEDIA_ROLE_ROUTE_TYPE);
        } else {
            role = pa_proplist_gets(GET_STREAM_PROPLIST(stream, type), PA_PROP_MEDIA_ROLE);
            route_type_str = pa_proplist_gets(GET_STREAM_PROPLIST(stream, type), PA_PROP_MEDIA_ROLE_ROUTE_TYPE);
        }

        /* skip roles */
        if (check_role_to_skip(m, command, role)) {
            result = PROCESS_STREAM_RESULT_SKIP;
            goto FAILURE;
        }

        /* skip route types */
        if (check_route_type_to_skip(command, route_type_str)) {
            result = PROCESS_STREAM_RESULT_SKIP;
            goto FAILURE;
        }

        if (command == PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED ) {
            /* update the priority of this stream */
            ret = update_priority_of_stream(m, command, type, stream, role);
            if (ret == FALSE) {
                pa_log_error("could not update the priority of '%s' role.", role);
                result = PROCESS_STREAM_RESULT_STOP;
                goto FAILURE;
            }
        }

        /* update the focus status */
        ret = update_focus_status_of_stream(m, command, stream, type);
        if (ret == FALSE)
            pa_log_warn("could not update focus status");

        /* update the highest priority */
        ret = update_the_highest_priority_stream(m, command, stream, type,  role, &need_update);
        if (ret == FALSE) {
            pa_log_error("could not update the highest priority stream");
            result = PROCESS_STREAM_RESULT_SKIP;
            goto FAILURE;
        }

        /* need to skip if this stream does not belong to internal device */
        /* if needed, notify to update */
        if (need_update) {
            if (command == PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED_WITH_NEW_DATA) {
                do_notify(m, NOTIFY_COMMAND_CHANGE_ROUTE_START, type, TRUE, stream);
                if (type==STREAM_SINK_INPUT)
                    m->cur_highest_priority.need_to_update_si = TRUE;
                else
                    m->cur_highest_priority.need_to_update_so = TRUE;
            } else {
                do_notify(m, NOTIFY_COMMAND_CHANGE_ROUTE_START, type, FALSE, stream);
                if (type==STREAM_SINK_INPUT)
                    m->cur_highest_priority.sink_input = stream;
                else
                    m->cur_highest_priority.source_output = stream;
            }
        }
        if (command == PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED)
            do_notify(m, NOTIFY_COMMAND_INFORM_STREAM_CONNECTED, type, FALSE, stream);

    } else if (command == PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_ENDED) {
        role = pa_proplist_gets(GET_STREAM_PROPLIST(stream, type), PA_PROP_MEDIA_ROLE);
        if (role) {
            /* skip roles */
            if (check_role_to_skip(m, command, role)) {
                result = PROCESS_STREAM_RESULT_SKIP;
                goto FAILURE;
            }

            /* skip route types */
            if ((route_type_str = pa_proplist_gets(GET_STREAM_PROPLIST(stream, type), PA_PROP_MEDIA_ROLE_ROUTE_TYPE))) {
                if (check_route_type_to_skip(command, route_type_str)) {
                    result = PROCESS_STREAM_RESULT_SKIP;
                    goto FAILURE;
                }
            }

            /* check if it has already been processed (unlink or state_changed_cb) */
            if (pa_proplist_get(GET_STREAM_PROPLIST(stream, type), PA_PROP_MEDIA_ROLE_PRIORITY, (const void**)&prior_priority, &size)) {
                pa_log_debug("it has already been processed for PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_ENDED, skip it..");
                result = PROCESS_STREAM_RESULT_SKIP;
                goto FAILURE;
            }

            pa_proplist_unset(GET_STREAM_PROPLIST(stream, type), PA_PROP_MEDIA_ROLE_PRIORITY);
            ret = update_the_highest_priority_stream(m, command, stream, type, role, &need_update);
            if (ret == FALSE) {
                pa_log_error("could not update the highest priority stream");
                result = PROCESS_STREAM_RESULT_STOP;
                goto FAILURE;
            }

            do_notify(m, NOTIFY_COMMAND_INFORM_STREAM_DISCONNECTED, type, FALSE, stream);

            /* need to skip if this stream does not belong to internal device */
            /* if needed, notify to update */
            if (need_update)
                do_notify(m, NOTIFY_COMMAND_CHANGE_ROUTE_END, type, FALSE, NULL);

        } else {
            pa_log_error("role is null, skip it");
        }

    } else if (command == PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_FOCUS_CHANGED) {
        role = pa_proplist_gets(GET_STREAM_PROPLIST(stream, type), PA_PROP_MEDIA_ROLE);
        if (role) {
            /* skip roles */
            if (check_role_to_skip(m, command, role)) {
                result = PROCESS_STREAM_RESULT_SKIP;
                goto FAILURE;
            }

            /* skip route types */
            if ((route_type_str = pa_proplist_gets(GET_STREAM_PROPLIST(stream, type), PA_PROP_MEDIA_ROLE_ROUTE_TYPE))) {
                if (check_route_type_to_skip(command, route_type_str)) {
                    result = PROCESS_STREAM_RESULT_SKIP;
                    goto FAILURE;
                }
            }

            ret = update_the_highest_priority_stream(m, command, stream, type, role, &need_update);
            if (ret == FALSE) {
                pa_log_error("could not update the highest priority stream");
                result = PROCESS_STREAM_RESULT_STOP;
                goto FAILURE;
            }

            /* need to skip if this stream does not belong to internal device */
            /* if needed, notify to update */
            if (need_update)
                do_notify(m, NOTIFY_COMMAND_CHANGE_ROUTE_END, type, FALSE, NULL);

        } else {
            pa_log_error("role is null, skip it");
        }

    } else if (command == PROCESS_COMMAND_UPDATE_VOLUME) {
        if ((si_volume_type_str = pa_proplist_gets(GET_STREAM_NEW_PROPLIST(stream, type), PA_PROP_MEDIA_TIZEN_VOLUME_TYPE))) {
            v = pa_hashmap_get(m->volume_infos, si_volume_type_str);
            if (v && v->values[(type==STREAM_SINK_INPUT)?STREAM_DIRECTION_OUT:STREAM_DIRECTION_IN].idx_volume_values) {
                /* Update volume-level */
                volume_ret = set_volume_level_with_new_data(m, type, stream, v->values[(type==STREAM_SINK_INPUT)?STREAM_DIRECTION_OUT:STREAM_DIRECTION_IN].current_level);
                if (volume_ret)
                    pa_log_error("failed to set_volume_level_by_idx(), stream_type(%d), level(%u), ret(0x%x)",
                            STREAM_SINK_INPUT, v->values[(type==STREAM_SINK_INPUT)?STREAM_DIRECTION_OUT:STREAM_DIRECTION_IN].current_level, volume_ret);
                /* Update volume-mute */
                volume_ret = set_volume_mute_with_new_data(m, type, stream, v->values[(type==STREAM_SINK_INPUT)?STREAM_DIRECTION_OUT:STREAM_DIRECTION_IN].is_muted);
                if (volume_ret)
                    pa_log_error("failed to set_volume_mute_by_idx(), stream_type(%d), mute(%d), ret(0x%x)",
                            STREAM_SINK_INPUT, v->values[(type==STREAM_SINK_INPUT)?STREAM_DIRECTION_OUT:STREAM_DIRECTION_IN].is_muted, volume_ret);
            }
        }

    } else if (command == PROCESS_COMMAND_ADD_PARENT_ID || command == PROCESS_COMMAND_REMOVE_PARENT_ID) {
        role = pa_proplist_gets(GET_STREAM_PROPLIST(stream, type), PA_PROP_MEDIA_ROLE);
        if (role) {
            /* skip roles */
            if (check_role_to_skip(m, command, role)) {
                result = PROCESS_STREAM_RESULT_SKIP;
                goto FAILURE;
            }

            /* skip route types */
            if ((route_type_str = pa_proplist_gets(GET_STREAM_PROPLIST(stream, type), PA_PROP_MEDIA_ROLE_ROUTE_TYPE))) {
                if (check_route_type_to_skip(command, route_type_str)) {
                    result = PROCESS_STREAM_RESULT_SKIP;
                    goto FAILURE;
                }
            }

            if (!IS_ROUTE_TYPE_FOR_EXTERNAL_DEV(route_type_str, route_type)) {
                if (command == PROCESS_COMMAND_ADD_PARENT_ID) {
                    if (type == STREAM_SINK_INPUT && m->cur_highest_priority.need_to_update_si) {
                        m->cur_highest_priority.sink_input = stream;
                        m->cur_highest_priority.need_to_update_si = FALSE;
                    }
                    if (type == STREAM_SOURCE_OUTPUT && m->cur_highest_priority.need_to_update_so) {
                        m->cur_highest_priority.source_output = stream;
                        m->cur_highest_priority.need_to_update_so = FALSE;
                    }
                    do_notify(m, NOTIFY_COMMAND_INFORM_STREAM_CONNECTED, type, FALSE, stream);
                }
            }

            /* update parent stream info. */
            ret = update_stream_parent_info(m, command, type, stream);
            if (ret == FALSE) {
                pa_log_warn("could not update the parent information of this stream");
                //return PROCESS_STREAM_RESULT_STOP;
            }
        }
    }

FAILURE:
    pa_log_info("<<< process_stream(%s): result(%d), stream(%p)", process_command_type_str[command], result, stream);
    return result;
}

static void update_buffer_attribute(stream_type_t stream_type, void *new_data, pa_stream_manager *m) {
    int32_t maxlength = -1;
    int32_t tlength = -1;
    int32_t prebuf = -1;
    int32_t minreq = -1;
    int32_t fragsize = -1;
    const char* audio_latency = NULL;

    pa_assert(m);
    pa_assert(new_data);

    if (m->hal == NULL)
        return;

    audio_latency = pa_proplist_gets(GET_STREAM_NEW_PROPLIST(new_data, stream_type), PA_PROP_MEDIA_TIZEN_AUDIO_LATENCY);
    pa_log_info("audio_latency : %s", audio_latency);
    if (audio_latency == NULL)
        return;

    if (!pa_hal_manager_get_buffer_attribute(m->hal, (io_direction_t)!stream_type, audio_latency, new_data, (uint32_t*)&maxlength, (uint32_t*)&tlength, (uint32_t*)&prebuf, (uint32_t*)&minreq, (uint32_t*)&fragsize)) {
        pa_log_info(" - maxlength:%d, tlength:%d, prebuf:%d, minreq:%d, fragsize:%d", maxlength, tlength, prebuf, minreq, fragsize);
        pa_proplist_setf(GET_STREAM_NEW_PROPLIST(new_data, stream_type), "maxlength", "%d", maxlength);
        pa_proplist_setf(GET_STREAM_NEW_PROPLIST(new_data, stream_type), "tlength",   "%d", tlength);
        pa_proplist_setf(GET_STREAM_NEW_PROPLIST(new_data, stream_type), "prebuf",    "%d", prebuf);
        pa_proplist_setf(GET_STREAM_NEW_PROPLIST(new_data, stream_type), "minreq",    "%d", minreq);
        pa_proplist_setf(GET_STREAM_NEW_PROPLIST(new_data, stream_type), "fragsize",  "%d", fragsize);
    }

    return;
}

static pa_hook_result_t sink_input_new_cb(pa_core *core, pa_sink_input_new_data *new_data, pa_stream_manager *m) {
    pa_core_assert_ref(core);

    pa_log_info("start sink_input_new_cb");

    process_stream(STREAM_SINK_INPUT, new_data, PROCESS_COMMAND_PREPARE, m);
    /* Update buffer attributes from HAL */
    update_buffer_attribute(STREAM_SINK_INPUT, new_data, m);
    process_stream(STREAM_SINK_INPUT, new_data, PROCESS_COMMAND_UPDATE_VOLUME, m);
    process_stream(STREAM_SINK_INPUT, new_data, PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED_WITH_NEW_DATA, m);

    return PA_HOOK_OK;
}

static pa_hook_result_t sink_input_put_cb(pa_core *core, pa_sink_input *i, pa_stream_manager *m) {
    pa_core_assert_ref(core);
    pa_sink_input_assert_ref(i);

    pa_log_info("start sink_input_put_cb, i(%p, index:%u)", i, i->index);

    process_stream(STREAM_SINK_INPUT, i, PROCESS_COMMAND_ADD_PARENT_ID, m);

    return PA_HOOK_OK;
}

static pa_hook_result_t sink_input_unlink_cb(pa_core *core, pa_sink_input *i, pa_stream_manager *m) {
    pa_core_assert_ref(core);
    pa_sink_input_assert_ref(i);

    pa_log_info("start sink_input_unlink_cb, i(%p, index:%u)", i, i->index);
    process_stream(STREAM_SINK_INPUT, i, PROCESS_COMMAND_REMOVE_PARENT_ID, m);
    process_stream(STREAM_SINK_INPUT, i, PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_ENDED, m);

    return PA_HOOK_OK;
}

static pa_hook_result_t sink_input_state_changed_cb(pa_core *core, pa_sink_input *i, pa_stream_manager *m) {
    pa_sink_input_state_t state;

    pa_assert(i);
    pa_assert(m);

    state = pa_sink_input_get_state(i);
    pa_log_info("start sink_input_state_changed_cb(), sink-input(%p), state(%d)", i, state);

    switch(state) {
    case PA_SINK_INPUT_CORKED: {
        process_stream(STREAM_SINK_INPUT, i, PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_ENDED, m);
        break;
    }
    case PA_SINK_INPUT_DRAINED:
    case PA_SINK_INPUT_RUNNING: {
        process_stream(STREAM_SINK_INPUT, i, PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED, m);
        break;
    }
    default:
        break;
    }

    return PA_HOOK_OK;
}

static pa_hook_result_t sink_input_move_start_cb(pa_core *core, pa_sink_input *i, pa_stream_manager *m) {
    pa_core_assert_ref(core);
    pa_sink_input_assert_ref(i);

    /* There's no point in doing anything if the core is shut down anyway */
    if (core->state == PA_CORE_SHUTDOWN)
        return PA_HOOK_OK;

    pa_log_debug ("sink_input_move_start_cb, i(%p, index:%u)", i, i->index);

    set_volume_mute_by_idx(m, STREAM_SINK_INPUT, i->index, TRUE);

    return PA_HOOK_OK;
}

static pa_hook_result_t sink_input_move_finish_cb(pa_core *core, pa_sink_input *i, pa_stream_manager *m) {
    pa_core_assert_ref(core);
    pa_sink_input_assert_ref(i);

    /* There's no point in doing anything if the core is shut down anyway */
    if (core->state == PA_CORE_SHUTDOWN)
        return PA_HOOK_OK;

    pa_log_debug ("sink_input_move_finish_cb, i(%p, index:%u)", i, i->index);

    set_volume_mute_by_idx(m, STREAM_SINK_INPUT, i->index, FALSE);

    return PA_HOOK_OK;
}

static pa_hook_result_t source_output_new_cb(pa_core *core, pa_source_output_new_data *new_data, pa_stream_manager *m) {
    pa_core_assert_ref(core);

    pa_log_info("start source_output_new_new_cb");

    process_stream(STREAM_SOURCE_OUTPUT, new_data, PROCESS_COMMAND_PREPARE, m);
    /* Update buffer attributes from HAL */
    update_buffer_attribute(STREAM_SOURCE_OUTPUT, new_data, m);
    process_stream(STREAM_SOURCE_OUTPUT, new_data, PROCESS_COMMAND_UPDATE_VOLUME, m);
    process_stream(STREAM_SOURCE_OUTPUT, new_data, PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED_WITH_NEW_DATA, m);

    return PA_HOOK_OK;
}

static pa_hook_result_t source_output_put_cb(pa_core *core, pa_source_output *o, pa_stream_manager *m) {
    pa_core_assert_ref(core);
    pa_source_output_assert_ref(o);

    pa_log_info("start source_output_put_cb, o(%p, index:%u)", o, o->index);

    process_stream(STREAM_SOURCE_OUTPUT, o, PROCESS_COMMAND_ADD_PARENT_ID, m);

    return PA_HOOK_OK;
}

static pa_hook_result_t source_output_unlink_cb(pa_core *core, pa_source_output *o, pa_stream_manager *m) {
    pa_core_assert_ref(core);
    pa_source_output_assert_ref(o);

    pa_log_info("start source_output_unlink_cb, o(%p, index:%u)", o, o->index);

    process_stream(STREAM_SOURCE_OUTPUT, o, PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_ENDED, m);
    process_stream(STREAM_SOURCE_OUTPUT, o, PROCESS_COMMAND_REMOVE_PARENT_ID, m);

    return PA_HOOK_OK;
}

static pa_hook_result_t source_output_state_changed_cb(pa_core *core, pa_source_output *o, pa_stream_manager *m) {
    pa_source_output_state_t state;

    pa_assert(o);
    pa_assert(m);

    state = pa_source_output_get_state(o);
    pa_log_debug("start source_output_state_changed_cb(), source-output(%p), state(%d)", o, state);

    switch(state) {
    case PA_SOURCE_OUTPUT_CORKED: {
        process_stream(STREAM_SOURCE_OUTPUT, o, PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_ENDED, m);
        break;
    }
    case PA_SOURCE_OUTPUT_RUNNING: {
        process_stream(STREAM_SOURCE_OUTPUT, o, PROCESS_COMMAND_CHANGE_ROUTE_BY_STREAM_STARTED, m);
        break;
    }
    default:
        break;
    }

    return PA_HOOK_OK;
}

/* Reorganize routing when a device has been connected or disconnected */
static pa_hook_result_t device_connection_changed_hook_cb(pa_core *c, pa_device_manager_hook_data_for_conn_changed *conn, pa_stream_manager *m) {
    const char *active_dev = NULL;
    const char *device_type = NULL;
    stream_route_type_t route_type;
    dm_device_direction_t device_direction = DM_DEVICE_DIRECTION_OUT;
    pa_bool_t use_internal_codec = FALSE;

    device_direction = pa_device_manager_get_device_direction(conn->device);
    device_type = pa_device_manager_get_device_type(conn->device);
    pa_device_manager_use_internal_codec(conn->device, device_direction, DEVICE_ROLE_NORMAL, &use_internal_codec);

    pa_log_info("device_connection_changed_hook_cb is called. conn(%p), is_connected(%d), device(%p, %s), direction(0x%x), use_internal_codec(%d)",
            conn, conn->is_connected, conn->device, device_type, device_direction, use_internal_codec);

    /* If the route type of the stream is not manual, notify again */
    if (m->cur_highest_priority.source_output && (device_direction & DM_DEVICE_DIRECTION_IN)) {
        if (!pa_stream_manager_get_route_type(m->cur_highest_priority.source_output, FALSE, STREAM_SOURCE_OUTPUT, &route_type)) {
            if (route_type < STREAM_ROUTE_TYPE_MANUAL) {
                if (use_internal_codec) {
                    if (route_type == STREAM_ROUTE_TYPE_AUTO && !conn->is_connected) {
                        /* remove activated device info. if it has the AUTO route type */
                        active_dev = pa_proplist_gets(GET_STREAM_PROPLIST(m->cur_highest_priority.source_output, STREAM_SOURCE_OUTPUT), PA_PROP_MEDIA_ROUTE_AUTO_ACTIVE_DEV);
                        if (pa_streq(active_dev, device_type))
                            pa_proplist_sets(GET_STREAM_PROPLIST(m->cur_highest_priority.source_output, STREAM_SOURCE_OUTPUT), PA_PROP_MEDIA_ROUTE_AUTO_ACTIVE_DEV, "removed");
                    }
                    do_notify(m, NOTIFY_COMMAND_CHANGE_ROUTE_START, STREAM_SOURCE_OUTPUT, FALSE, m->cur_highest_priority.source_output);
                }
            }
        }
    }
    if (m->cur_highest_priority.sink_input && (device_direction & DM_DEVICE_DIRECTION_OUT)) {
        if (!pa_stream_manager_get_route_type(m->cur_highest_priority.sink_input, FALSE, STREAM_SINK_INPUT, &route_type)) {
            if (route_type < STREAM_ROUTE_TYPE_MANUAL) {
                if (use_internal_codec || (!use_internal_codec && (route_type == STREAM_ROUTE_TYPE_AUTO_ALL))) {
                    if (route_type == STREAM_ROUTE_TYPE_AUTO && !conn->is_connected) {
                        /* remove activated device info. if it has the AUTO route type */
                        active_dev = pa_proplist_gets(GET_STREAM_PROPLIST(m->cur_highest_priority.sink_input, STREAM_SINK_INPUT), PA_PROP_MEDIA_ROUTE_AUTO_ACTIVE_DEV);
                        if (pa_streq(active_dev, device_type))
                            pa_proplist_sets(GET_STREAM_PROPLIST(m->cur_highest_priority.sink_input, STREAM_SINK_INPUT), PA_PROP_MEDIA_ROUTE_AUTO_ACTIVE_DEV, "removed");
                    }
                    do_notify(m, NOTIFY_COMMAND_CHANGE_ROUTE_START, STREAM_SINK_INPUT, FALSE, m->cur_highest_priority.sink_input);
                }
            }
        }
    }

    return PA_HOOK_OK;
}

/* Reorganize routing when device information has been changed */
static pa_hook_result_t device_information_changed_hook_cb(pa_core *c, pa_device_manager_hook_data_for_info_changed *info, pa_stream_manager *m) {
#if 0
    pa_log_info("device_information_changed_hook_cb is called. info(%p), changed_info(%d), device(%p)",
            info, info->changed_info, info->device);
#endif

    return PA_HOOK_OK;
}

static void subscribe_cb(pa_core *core, pa_subscription_event_type_t t, uint32_t idx, pa_stream_manager *m) {
    pa_client *client = NULL;
    stream_parent *sp = NULL;
    const char *name = NULL;
    uint32_t *device_id = NULL;
    uint32_t _idx = 0;
    pa_core_assert_ref(core);
    pa_assert(m);
    pa_log_info("subscribe_cb() is called, t(%x), idx(%u)", t, idx);

    if (t == (PA_SUBSCRIPTION_EVENT_CLIENT|PA_SUBSCRIPTION_EVENT_CHANGE)) {
        client = pa_idxset_get_by_index(core->clients, idx);
        if (client == NULL) {
            pa_log_error(" - could not find any client that has idx(%u)", idx);
            return;
        }
        name = pa_proplist_gets(client->proplist, PA_PROP_APPLICATION_NAME);
        if (strncmp (name, STREAM_MANAGER_CLIENT_NAME, strlen(STREAM_MANAGER_CLIENT_NAME))) {
            pa_log_warn(" - this is not a client(%s) that we should take care of, skip it", name);
            return;
        }
        /* add a stream parent */
        sp = pa_xmalloc0(sizeof(stream_parent));
        sp->idx_sink_inputs = pa_idxset_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);
        sp->idx_source_outputs = pa_idxset_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);
        sp->idx_route_in_devices = pa_idxset_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
        sp->idx_route_out_devices = pa_idxset_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
        pa_hashmap_put(m->stream_parents, (void*)idx, sp);
        pa_log_debug(" - add sp(%p), idx(%u)", sp, idx);
     } else if (t == (PA_SUBSCRIPTION_EVENT_CLIENT|PA_SUBSCRIPTION_EVENT_REMOVE)) {
        /* remove the stream parent */
        sp = pa_hashmap_get(m->stream_parents, (const void*)idx);
        if (sp) {
            pa_log_debug(" - remove sp(%p), idx(%u)", sp, idx);
            pa_hashmap_remove(m->stream_parents, (const void*)idx);
            if (sp->idx_route_in_devices)
                PA_IDXSET_FOREACH(device_id, sp->idx_route_in_devices, _idx)
                    pa_xfree(device_id);
            if (sp->idx_route_out_devices)
                PA_IDXSET_FOREACH(device_id, sp->idx_route_out_devices, _idx)
                    pa_xfree(device_id);
            pa_idxset_free(sp->idx_sink_inputs, NULL);
            pa_idxset_free(sp->idx_source_outputs, NULL);
            pa_idxset_free(sp->idx_route_in_devices, NULL);
            pa_idxset_free(sp->idx_route_out_devices, NULL);
            pa_xfree(sp);
        } else {
            pa_log_error(" - could not find any stream_parent that has idx(%u)", idx);
        }
    }
}

static int init_ipc (pa_stream_manager *m) {
#ifdef HAVE_DBUS
#ifdef USE_DBUS_PROTOCOL
    pa_assert(m);
    pa_log_info("Initialization for IPC");
    m->dbus_protocol = pa_dbus_protocol_get(m->core);
    pa_assert_se(pa_dbus_protocol_add_interface(m->dbus_protocol, STREAM_MANAGER_OBJECT_PATH, &stream_manager_interface_info, m) >= 0);
    pa_assert_se(pa_dbus_protocol_register_extension(m->dbus_protocol, STREAM_MANAGER_INTERFACE) >= 0);
#else
    DBusError err;
    pa_dbus_connection *conn = NULL;
    static const DBusObjectPathVTable vtable = {
        .message_function = method_handler_for_vt,
    };

    pa_assert(m);
    pa_log_info("Initialization for IPC");

    dbus_error_init(&err);

    if (!(conn = pa_dbus_bus_get(m->core, DBUS_BUS_SYSTEM, &err)) || dbus_error_is_set(&err)) {
        if (conn) {
            pa_dbus_connection_unref(conn);
        }
        pa_log_error("Unable to contact D-Bus system bus: %s: %s", err.name, err.message);
        goto fail;
    } else {
        pa_log_notice("Got dbus connection");
    }
    m->dbus_conn = conn;
    pa_assert_se(dbus_connection_register_object_path(pa_dbus_connection_get(conn), STREAM_MANAGER_OBJECT_PATH, &vtable, m));
#endif
#else
    pa_log_error("DBUS is not supported\n");
    goto fail;
#endif

    return 0;
fail:
    pa_log_error("failed to initialize stream manager ipc");
    return -1;
}

static void deinit_ipc (pa_stream_manager *m) {
    pa_assert(m);

#ifdef HAVE_DBUS
#ifdef USE_DBUS_PROTOCOL
    if (m->dbus_protocol) {
        pa_assert_se(pa_dbus_protocol_unregister_extension(m->dbus_protocol, STREAM_MANAGER_INTERFACE) >= 0);
        pa_assert_se(pa_dbus_protocol_remove_interface(m->dbus_protocol, STREAM_MANAGER_OBJECT_PATH, stream_manager_interface_info.name) >= 0);
        pa_dbus_protocol_unref(m->dbus_protocol);
        m->dbus_protocol = NULL;
    }
#else
    if (m->dbus_conn) {
        if(!dbus_connection_unregister_object_path(pa_dbus_connection_get(m->dbus_conn), STREAM_MANAGER_OBJECT_PATH))
            pa_log_error("failed to unregister object path");
        m->dbus_conn = NULL;
    }
#endif
#endif
    return;
}

int32_t pa_stream_manager_get_route_type(void *stream, pa_bool_t origins_from_new_data, stream_type_t stream_type, stream_route_type_t *stream_route_type) {
    const char *route_type_str = NULL;

    pa_assert(stream);
    pa_assert(stream_route_type);

    if (origins_from_new_data)
        route_type_str = pa_proplist_gets(GET_STREAM_NEW_PROPLIST(stream, stream_type), PA_PROP_MEDIA_ROLE_ROUTE_TYPE);
    else
        route_type_str = pa_proplist_gets(GET_STREAM_PROPLIST(stream, stream_type), PA_PROP_MEDIA_ROLE_ROUTE_TYPE);

    if (route_type_str) {
        if (!pa_atoi(route_type_str, (int32_t*)stream_route_type)) {
            pa_log_debug("stream(%p), origins_from_new_data(%d), stream_type(%d): route_type(%d)", stream, origins_from_new_data, stream_type, *stream_route_type);
        } else {
            pa_log_error("could not convert route_type_str(%s) to int", route_type_str);
            return -1;
        }
    } else {
        pa_log_warn("could not get route type from the stream(%p)", stream);
        return -1;
    }
}

void pa_stream_manager_is_available_device_for_auto_route(const char *cur_device_type, const char *new_device_type, const char *role, stream_type_t stream_type, pa_bool_t *available, pa_stream_manager *m) {
    stream_info *si = NULL;
    pa_idxset *devices = NULL;
    uint32_t idx = 0;
    char *device_type = NULL;

    pa_assert(m);
    pa_assert(role);
    pa_assert(cur_device_type);
    pa_assert(new_device_type);
    pa_assert(available);

    if (m->stream_infos) {
        si = pa_hashmap_get(m->stream_infos, role);
        if (si) {
            *available = FALSE;
            devices = (stream_type==STREAM_SINK_INPUT)?si->idx_avail_out_devices:si->idx_avail_in_devices;
            if (devices) {
                PA_IDXSET_FOREACH(device_type, devices, idx) {
                    if (cur_device_type && pa_streq(device_type, cur_device_type)) {
                        pa_log_debug("cur_device[%s]'s priority is more higher than new_device[%s]", cur_device_type, new_device_type);
                        break;
                    }
                    if (pa_streq(device_type, new_device_type)) {
                        *available = TRUE;
                        break;
                    }
                }
            } else {
                pa_log_error("could not found a device list for this role[%s], stream type[%d]", role, stream_type);
                *available = FALSE;
            }
        } else {
            pa_log_warn("not support this role[%s]", role);
            *available = FALSE;
        }
    } else {
        pa_log_error("stream_infos is null");
        *available = FALSE;
    }
    pa_log_debug("is new_device[%s] available for role[%s]/stream_type[%d]:%d", new_device_type, role, stream_type, *available);

    return;
}

void pa_stream_manager_find_next_priority_device_for_auto_route(const char *cur_device_type, const char *role, stream_type_t stream_type, char **next_device_type, pa_stream_manager *m) {
    stream_info *si = NULL;
    pa_idxset *devices = NULL;
    uint32_t idx = 0;
    char *device_type = NULL;
    pa_bool_t ret_next = FALSE;

    pa_assert(m);
    pa_assert(role);
    pa_assert(cur_device_type);
    pa_assert(next_device_type);

    if (m->stream_infos) {
        si = pa_hashmap_get(m->stream_infos, role);
        if (si) {
            devices = (stream_type==STREAM_SINK_INPUT)?si->idx_avail_out_devices:si->idx_avail_in_devices;
            if (devices) {
                PA_IDXSET_FOREACH(device_type, devices, idx) {
                    if (ret_next) {
                        *next_device_type = device_type;
                        pa_log_debug("found next device[%s]", *next_device_type);
                        break;
                    }
                    if (pa_streq(device_type, cur_device_type)) {
                        ret_next = TRUE;
                        continue;
                    }
                }
            } else {
                pa_log_error("could not found a device list for this role[%s], stream type[%d]", role, stream_type);
                *next_device_type = NULL;
            }
        } else {
            pa_log_warn("not support this role[%s]", role);
            *next_device_type = NULL;
        }
    } else {
        pa_log_error("stream_infos is null");
        *next_device_type = NULL;
    }
    pa_log_debug("next_device_type is [%s] for role[%s]/stream_type[%d]", *next_device_type, role, stream_type);

    return;
}

pa_stream_manager* pa_stream_manager_init(pa_core *c) {
    pa_stream_manager *m;

    pa_assert(c);

    m = pa_xnew0(pa_stream_manager, 1);
    m->core = c;

    m->hal = pa_hal_manager_get(c, NULL);

#ifdef HAVE_DBUS
#ifdef USE_DBUS_PROTOCOL
    m->dbus_protocol = NULL;
#else
    m->dbus_conn = NULL;
#endif
#endif
    if (init_ipc(m))
        goto fail;

    if (init_stream_map(m))
        goto fail;

    if (init_volumes(m))
        goto fail;

    m->stream_parents = pa_hashmap_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);

    m->sink_input_new_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_INPUT_NEW], PA_HOOK_EARLY, (pa_hook_cb_t) sink_input_new_cb, m);
    m->sink_input_put_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_INPUT_PUT], PA_HOOK_EARLY, (pa_hook_cb_t) sink_input_put_cb, m);
    m->sink_input_unlink_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_INPUT_UNLINK], PA_HOOK_EARLY, (pa_hook_cb_t) sink_input_unlink_cb, m);
    m->sink_input_state_changed_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_INPUT_STATE_CHANGED], PA_HOOK_EARLY, (pa_hook_cb_t) sink_input_state_changed_cb, m);
    m->sink_input_move_start_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_INPUT_MOVE_START], PA_HOOK_EARLY, (pa_hook_cb_t) sink_input_move_start_cb, m);
    m->sink_input_move_finish_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_INPUT_MOVE_FINISH], PA_HOOK_EARLY, (pa_hook_cb_t) sink_input_move_finish_cb, m);
    m->source_output_new_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SOURCE_OUTPUT_NEW], PA_HOOK_EARLY, (pa_hook_cb_t) source_output_new_cb, m);
    m->source_output_put_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SOURCE_OUTPUT_PUT], PA_HOOK_EARLY, (pa_hook_cb_t) source_output_put_cb, m);
    m->source_output_unlink_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SOURCE_OUTPUT_UNLINK], PA_HOOK_EARLY, (pa_hook_cb_t) source_output_unlink_cb, m);
    m->source_output_state_changed_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SOURCE_OUTPUT_STATE_CHANGED], PA_HOOK_EARLY, (pa_hook_cb_t) source_output_state_changed_cb, m);


    m->subscription = pa_subscription_new(m->core, PA_SUBSCRIPTION_MASK_CLIENT | PA_SUBSCRIPTION_MASK_SAMPLE_CACHE, (pa_subscription_cb_t)subscribe_cb, m);

    m->comm.comm = pa_communicator_get(c);
    m->comm.comm_hook_device_connection_changed_slot = pa_hook_connect(pa_communicator_hook(m->comm.comm,PA_COMMUNICATOR_HOOK_DEVICE_CONNECTION_CHANGED),
            PA_HOOK_EARLY + 10, (pa_hook_cb_t) device_connection_changed_hook_cb, m);
    m->comm.comm_hook_device_information_changed_slot = pa_hook_connect(pa_communicator_hook(m->comm.comm,PA_COMMUNICATOR_HOOK_DEVICE_INFORMATION_CHANGED),
            PA_HOOK_EARLY, (pa_hook_cb_t) device_information_changed_hook_cb, m);

    return m;

fail:
    pa_log_error("failed to initialize stream-manager");
    deinit_volumes(m);
    deinit_stream_map(m);
    deinit_ipc(m);
    if (m->hal)
        pa_hal_manager_unref(m->hal);
    pa_xfree(m);
    return 0;
}

void pa_stream_manager_done(pa_stream_manager *m) {
    pa_assert(m);

    if (m->comm.comm) {
        if (m->comm.comm_hook_device_connection_changed_slot)
            pa_hook_slot_free(m->comm.comm_hook_device_connection_changed_slot);
        if (m->comm.comm_hook_device_information_changed_slot)
            pa_hook_slot_free(m->comm.comm_hook_device_information_changed_slot);
        pa_communicator_unref(m->comm.comm);
    }

    if (m->subscription)
        pa_subscription_free(m->subscription);

    if (m->sink_input_new_slot)
        pa_hook_slot_free(m->sink_input_new_slot);
    if (m->sink_input_put_slot)
        pa_hook_slot_free(m->sink_input_put_slot);
    if (m->sink_input_unlink_slot)
        pa_hook_slot_free(m->sink_input_unlink_slot);
    if (m->sink_input_state_changed_slot)
        pa_hook_slot_free(m->sink_input_state_changed_slot);
    if (m->sink_input_move_start_slot)
        pa_hook_slot_free(m->sink_input_move_start_slot);
    if (m->sink_input_move_finish_slot)
        pa_hook_slot_free(m->sink_input_move_finish_slot);
    if (m->source_output_new_slot)
        pa_hook_slot_free(m->source_output_new_slot);
    if (m->source_output_put_slot)
        pa_hook_slot_free(m->source_output_put_slot);
    if (m->source_output_unlink_slot)
        pa_hook_slot_free(m->source_output_unlink_slot);
    if (m->source_output_state_changed_slot)
        pa_hook_slot_free(m->source_output_state_changed_slot);

    if (m->stream_parents)
        pa_hashmap_free(m->stream_parents);

    deinit_volumes(m);
    deinit_stream_map(m);
    deinit_ipc(m);

    if (m->hal)
        pa_hal_manager_unref(m->hal);

    pa_xfree(m);
}
