#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdbool.h>
#include <strings.h>
#include <vconf.h> // for mono
#include <iniparser.h>
#include <unistd.h>
#include <pthread.h>

#include <pulse/proplist.h>
#include <pulse/timeval.h>
#include <pulse/util.h>
#include <pulse/rtclock.h>

#include <pulsecore/core.h>
#include <pulsecore/module.h>
#include <pulsecore/modargs.h>
#include <pulsecore/core-error.h>
#include <pulsecore/core-rtclock.h>
#include <pulsecore/core-scache.h>
#include <pulsecore/core-subscribe.h>
#include <pulsecore/core-util.h>
#include <pulsecore/mutex.h>
#include <pulsecore/log.h>
#include <pulsecore/namereg.h>
#include <pulsecore/sink-input.h>
#include <pulsecore/source-output.h>
#include <pulsecore/pstream-util.h>
#include <pulsecore/strbuf.h>
#include <pulsecore/sink-input.h>
#include <pulsecore/sound-file.h>
#include <pulsecore/play-memblockq.h>
#include <pulsecore/shared.h>
#ifdef HAVE_DBUS
#include <pulsecore/dbus-shared.h>
#include <pulsecore/dbus-util.h>
#include <pulsecore/protocol-dbus.h>
#endif

#include "module-policy-symdef.h"
#include "communicator.h"
#include "hal-manager.h"
#include "stream-manager.h"
#include "device-manager.h"

PA_MODULE_AUTHOR("Seungbae Shin");
PA_MODULE_DESCRIPTION("Media Policy module");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(TRUE);
PA_MODULE_USAGE(" ");

static const char* const valid_modargs[] = {
    NULL
};

struct userdata;

#ifdef HAVE_DBUS

/*** Defines for module policy dbus interface ***/
#define OBJECT_PATH "/org/pulseaudio/policy1"
#define INTERFACE_POLICY "org.PulseAudio.Ext.Policy1"
#define POLICY_INTROSPECT_XML                                               \
    DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE                               \
    "<node>"                                                                \
    " <interface name=\"" INTERFACE_POLICY "\">"                            \
    "  <method name=\"MethodTest1\">"                                       \
    "   <arg name=\"arg1\" direction=\"in\" type=\"s\"/>"                   \
    "   <arg name=\"arg2\" direction=\"out\" type=\"u\"/>"                  \
    "  </method>"                                                           \
    "  <method name=\"MethodTest2\">"                                       \
    "   <arg name=\"arg1\" direction=\"in\" type=\"i\"/>"                   \
    "   <arg name=\"arg2\" direction=\"in\" type=\"i\"/>"                   \
    "   <arg name=\"arg3\" direction=\"out\" type=\"i\"/>"                  \
    "  </method>"                                                           \
    "  <property name=\"PropertyTest1\" type=\"i\" access=\"readwrite\"/>"  \
    "  <property name=\"PropertyTest2\" type=\"s\" access=\"read\"/>"       \
    "  <signal name=\"PropertyTest1Changed\">"                              \
    "   <arg name=\"arg1\" type=\"i\"/>"                                    \
    "  </signal>"                                                           \
    "  <signal name=\"SignalTest2\">"                                       \
    "   <arg name=\"arg1\" type=\"s\"/>"                                    \
    "  </signal>"                                                           \
    " </interface>"                                                         \
    " <interface name=\"" DBUS_INTERFACE_INTROSPECTABLE "\">\n"             \
    "  <method name=\"Introspect\">\n"                                      \
    "   <arg name=\"data\" type=\"s\" direction=\"out\"/>\n"                \
    "  </method>\n"                                                         \
    " </interface>\n"                                                       \
    " <interface name=\"" DBUS_INTERFACE_PROPERTIES "\">\n"                 \
    "  <method name=\"Get\">\n"                                             \
    "   <arg name=\"interface_name\" type=\"s\" direction=\"in\"/>\n"       \
    "   <arg name=\"property_name\" type=\"s\" direction=\"in\"/>\n"        \
    "   <arg name=\"value\" type=\"v\" direction=\"out\"/>\n"               \
    "  </method>\n"                                                         \
    "  <method name=\"Set\">\n"                                             \
    "   <arg name=\"interface_name\" type=\"s\" direction=\"in\"/>\n"       \
    "   <arg name=\"property_name\" type=\"s\" direction=\"in\"/>\n"        \
    "   <arg name=\"value\" type=\"v\" direction=\"in\"/>\n"                \
    "  </method>\n"                                                         \
    "  <method name=\"GetAll\">\n"                                          \
    "   <arg name=\"interface_name\" type=\"s\" direction=\"in\"/>\n"       \
    "   <arg name=\"props\" type=\"a{sv}\" direction=\"out\"/>\n"           \
    "  </method>\n"                                                         \
    " </interface>\n"                                                       \
    "</node>"


static DBusHandlerResult handle_get_property(DBusConnection *conn, DBusMessage *msg, void *userdata);
static DBusHandlerResult handle_get_all_property(DBusConnection *conn, DBusMessage *msg, void *userdata);
static DBusHandlerResult handle_set_property(DBusConnection *conn, DBusMessage *msg, void *userdata);
static DBusHandlerResult handle_policy_methods(DBusConnection *conn, DBusMessage *msg, void *userdata);
static DBusHandlerResult handle_introspect(DBusConnection *conn, DBusMessage *msg, void *userdata);
static DBusHandlerResult method_call_handler(DBusConnection *c, DBusMessage *m, void *userdata);
static void endpoint_init(struct userdata *u);
static void endpoint_done(struct userdata* u);

/*** Called when module-policy load/unload ***/
static void dbus_init(struct userdata* u);
static void dbus_deinit(struct userdata* u);

/*** Defines for Property handle ***/
/* property handlers */
static void handle_get_property_test1(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_set_property_test1(DBusConnection *conn, DBusMessage *msg, DBusMessageIter *iter, void *userdata);
static void handle_get_property_test2(DBusConnection *conn, DBusMessage *msg, void *userdata);

enum property_index {
    PROPERTY_TEST1,
    PROPERTY_TEST2,
    PROPERTY_MAX
};

static pa_dbus_property_handler property_handlers[PROPERTY_MAX] = {
    [PROPERTY_TEST1] = { .property_name = "PropertyTest1", .type = "i",
                                 .get_cb = handle_get_property_test1,
                                 .set_cb = handle_set_property_test1 },
    [PROPERTY_TEST2] = { .property_name = "PropertyTest2", .type = "s",
                                 .get_cb = handle_get_property_test2,
                                 .set_cb = NULL },
};


/*** Defines for method handle ***/
/* method handlers */
static void handle_method_test1(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_method_test2(DBusConnection *conn, DBusMessage *msg, void *userdata);
static void handle_get_all(DBusConnection *conn, DBusMessage *msg, void *userdata);

enum method_handler_index {
    METHOD_HANDLER_TEST1,
    METHOD_HANDLER_TEST2,
    METHOD_HANDLER_MAX
};

static pa_dbus_arg_info method1_args[] = { { "arg1",              "s",     "in" },
                                             { "arg2",             "u",     "out" } };

static pa_dbus_arg_info method2_args[] = { { "arg1",              "i",     "in" },
                                             { "arg2",             "i",     "in" },
                                             { "arg3",             "i",     "out" } };

static const char* method_arg_signatures[] = { "s", "ii" };

static pa_dbus_method_handler method_handlers[METHOD_HANDLER_MAX] = {
    [METHOD_HANDLER_TEST1] = {
        .method_name = "MethodTest1",
        .arguments = method1_args,
        .n_arguments = sizeof(method1_args) / sizeof(pa_dbus_arg_info),
        .receive_cb = handle_method_test1 },
    [METHOD_HANDLER_TEST2] = {
        .method_name = "MethodTest2",
        .arguments = method2_args,
        .n_arguments = sizeof(method2_args) / sizeof(pa_dbus_arg_info),
        .receive_cb = handle_method_test2 }
};

/*** Defines for signal send ***/
static int watch_signals(struct userdata* u);
static void unwatch_signals(struct userdata* u);
static void send_prop1_changed_signal(struct userdata* u);

enum signal_index {
    SIGNAL_PROP_CHANGED,
    SIGNAL_TEST2,
    SIGNAL_MAX
};

/*** Defines for get signal ***/
#define SOUND_SERVER_INTERFACE_NAME "org.tizen.soundserver.service"
#define AUDIO_CLIENT_INTERFACE_NAME "org.tizen.audioclient.service"

#define SOUND_SERVER_FILTER              \
    "type='signal',"                    \
    " interface='" SOUND_SERVER_INTERFACE_NAME "'"
#define AUDIO_CLIENT_FILTER              \
    "type='signal',"                    \
    " interface='" AUDIO_CLIENT_INTERFACE_NAME "'"

#endif

/* Modules for dynamic loading */
#define MODULE_COMBINE_SINK           "module-combine-sink"
#define MODULE_NULL_SINK              "module-null-sink"
#define MODULE_NULL_SOURCE            "module-null-source"

/* Name of combine sink for external route type */
#define SINK_NAME_COMBINED_EX         SINK_NAME_COMBINED"_ex"

/* Macros */
#define CONVERT_TO_DEVICE_DIRECTION(stream_type)\
    ((stream_type == STREAM_SINK_INPUT) ? DM_DEVICE_DIRECTION_OUT : DM_DEVICE_DIRECTION_IN)
#define IS_AVAILABLE_DIRECTION(stream_type, device_direction) \
    ((stream_type == STREAM_SINK_INPUT) ? (device_direction & DM_DEVICE_DIRECTION_OUT) : (device_direction & DM_DEVICE_DIRECTION_IN))

/* PCM Dump */
#define PA_DUMP_INI_DEFAULT_PATH                "/usr/etc/mmfw_audio_pcm_dump.ini"
#define PA_DUMP_INI_TEMP_PATH                   "/opt/system/mmfw_audio_pcm_dump.ini"
#define PA_DUMP_VCONF_KEY                       "memory/private/sound/pcm_dump"
#define PA_DUMP_PLAYBACK_DECODER_OUT            0x00000001
#define PA_DUMP_PLAYBACK_RESAMPLER_IN           0x00000008
#define PA_DUMP_PLAYBACK_RESAMPLER_OUT          0x00000010
#define PA_DUMP_CAPTURE_ENCODER_IN              0x80000000

struct userdata {
    pa_core *core;
    pa_module *module;

#ifdef HAVE_DBUS
    pa_dbus_connection *dbus_conn;
    int32_t test_property1;
#endif

    struct {
        pa_communicator *comm;
        pa_hook_slot *comm_hook_select_proper_sink_or_source_slot;
        pa_hook_slot *comm_hook_change_route_slot;
        pa_hook_slot *comm_hook_update_route_option_slot;
        pa_hook_slot *comm_hook_device_connection_changed_slot;
    } communicator;

    pa_hal_manager *hal_manager;
    pa_stream_manager *stream_manager;
    pa_device_manager *device_manager;

    pa_module *module_combine_sink;
    pa_module *module_combine_sink_for_ex;
    pa_module *module_null_sink;
    pa_module *module_null_source;
};

static void __load_dump_config(struct userdata *u)
{
    dictionary * dict = NULL;
    int vconf_dump = 0;

    dict = iniparser_load(PA_DUMP_INI_DEFAULT_PATH);
    if (!dict) {
        pa_log_debug("%s load failed. Use temporary file", PA_DUMP_INI_DEFAULT_PATH);
        dict = iniparser_load(PA_DUMP_INI_TEMP_PATH);
        if (!dict) {
            pa_log_warn("%s load failed", PA_DUMP_INI_TEMP_PATH);
            return;
        }
    }

    vconf_dump |= iniparser_getboolean(dict, "pcm_dump:decoder_out", 0) ? PA_DUMP_PLAYBACK_DECODER_OUT : 0;
    vconf_dump |= iniparser_getboolean(dict, "pcm_dump:resampler_in", 0) ? PA_DUMP_PLAYBACK_RESAMPLER_IN : 0;
    vconf_dump |= iniparser_getboolean(dict, "pcm_dump:resampler_out", 0) ? PA_DUMP_PLAYBACK_RESAMPLER_OUT : 0;
    vconf_dump |= iniparser_getboolean(dict, "pcm_dump:encoder_in", 0) ? PA_DUMP_CAPTURE_ENCODER_IN : 0;
    u->core->dump_sink = (pa_bool_t)iniparser_getboolean(dict, "pcm_dump:pa_sink", 0);
    u->core->dump_sink_input = (pa_bool_t)iniparser_getboolean(dict, "pcm_dump:pa_sink_input", 0);
    u->core->dump_source = (pa_bool_t)iniparser_getboolean(dict, "pcm_dump:pa_source", 0);
    u->core->dump_source_output = (pa_bool_t)iniparser_getboolean(dict, "pcm_dump:pa_source_output", 0);

    iniparser_freedict(dict);

    if (vconf_set_int(PA_DUMP_VCONF_KEY, vconf_dump)) {
        pa_log_warn("vconf_set_int %s=%x failed", PA_DUMP_VCONF_KEY, vconf_dump);
    }
}

/* Set the proper sink(source) according to the data of the parameter.
 * - ROUTE_TYPE_AUTO(_ALL)
 *     1. Find the proper sink/source comparing between avail_devices
 *       and current connected devices.
 *     2. If not found, set it to null sink/source.
 * - ROUTE_TYPE_MANUAL(_EXT)
 *     1. Find the proper sink/source comparing between avail_devices
 *        and manual_devices that have been set by user.
 *     2. If not found, set it to null sink/source. */
static pa_hook_result_t select_proper_sink_or_source_hook_cb(pa_core *c, pa_stream_manager_hook_data_for_select *data, struct userdata *u) {
    uint32_t idx = 0;
    uint32_t m_idx = 0;
    uint32_t conn_idx = 0;
    uint32_t *device_id = NULL;
    uint32_t dm_device_id = 0;
    const char *device_type = NULL;
    const char *dm_device_type = NULL;
    const char *dm_device_subtype = NULL;
    dm_device_direction_t dm_device_direction = DM_DEVICE_DIRECTION_NONE;
    dm_device *device = NULL;
    dm_device *latest_device = NULL;
    pa_idxset *conn_devices = NULL;
    pa_sink *sink = NULL;
    pa_sink *null_sink = NULL;
    pa_sink *combine_sink_arg1 = NULL;
    pa_sink *combine_sink_arg2 = NULL;
    pa_source *source = NULL;
    pa_source *null_source = NULL;
    char *args = NULL;
    void *s = NULL;
    uint32_t s_idx = 0;
    pa_bool_t is_found = 0;
    pa_usec_t creation_time = 0;
    pa_usec_t latest_creation_time = 0;

    pa_assert(c);
    pa_assert(data);
    pa_assert(u);

    pa_log_info("[SELECT] select_proper_sink_or_source_hook_cb is called. (%p), stream_type(%d), stream_role(%s), route_type(%d)",
                data, data->stream_type, data->stream_role, data->route_type);

    null_sink = (pa_sink*)pa_namereg_get(u->core, SINK_NAME_NULL, PA_NAMEREG_SINK);
    null_source = (pa_source*)pa_namereg_get(u->core, SOURCE_NAME_NULL, PA_NAMEREG_SOURCE);
    if (!null_sink || !null_source) {
        pa_log_error("[SELECT] could not get null_sink(%p) or null_source(%p)", null_sink, null_source);
        return PA_HOOK_OK;
    }

    if ((data->route_type <= STREAM_ROUTE_TYPE_AUTO_ALL) && data->idx_avail_devices) {
        /* get current connected devices */
        conn_devices = pa_device_manager_get_device_list(u->device_manager);
        if (data->route_type == STREAM_ROUTE_TYPE_AUTO || data->route_type == STREAM_ROUTE_TYPE_AUTO_ALL) {
            PA_IDXSET_FOREACH(device_type, data->idx_avail_devices, idx) {
                pa_log_info("[SELECT][AUTO(_ALL)] avail_device[%u] for this role[%-16s]: type[%-16s]", idx, data->stream_role, device_type);
                PA_IDXSET_FOREACH(device, conn_devices, conn_idx) {
                    dm_device_type = pa_device_manager_get_device_type(device);
                    dm_device_subtype = pa_device_manager_get_device_subtype(device);
                    dm_device_direction = pa_device_manager_get_device_direction(device);
                    dm_device_id = pa_device_manager_get_device_id(device);
                    pa_log_debug("  -- type[%-16s], subtype[%-5s], direction[0x%x], id[%u]",
                                 dm_device_type, dm_device_subtype, dm_device_direction, dm_device_id);
                    if (pa_streq(device_type, dm_device_type) && IS_AVAILABLE_DIRECTION(data->stream_type, dm_device_direction)) {
                        is_found = TRUE;
                        pa_log_info("  ** found a matched device: type[%-16s], direction[0x%x]", dm_device_type, dm_device_direction);
                        if (data->stream_type == STREAM_SINK_INPUT) {
                            if (data->route_type == STREAM_ROUTE_TYPE_AUTO_ALL && u->module_combine_sink) {
                                *(data->proper_sink) = (pa_sink*)pa_namereg_get(u->core, SINK_NAME_COMBINED, PA_NAMEREG_SINK);
                                pa_log_debug("  -- found the combine-sink, set it to the sink");
                            } else
                                *(data->proper_sink) = pa_device_manager_get_sink(device, DEVICE_ROLE_NORMAL);
                        } else
                            *(data->proper_source) = pa_device_manager_get_source(device, DEVICE_ROLE_NORMAL);
                        break;
                    }
                }
                if (is_found && data->route_type == STREAM_ROUTE_TYPE_AUTO)
                    break;
            }
        } else if (data->route_type == STREAM_ROUTE_TYPE_AUTO_LAST_CONNECTED) {
            PA_IDXSET_FOREACH(device_type, data->idx_avail_devices, idx) {
                pa_log_info("[SELECT][AUTO_LAST_CONN] avail_device[%u] for this role[%-16s]: type[%-16s]", idx, data->stream_role, device_type);
                PA_IDXSET_FOREACH(device, conn_devices, conn_idx) {
                    dm_device_type = pa_device_manager_get_device_type(device);
                    dm_device_subtype = pa_device_manager_get_device_subtype(device);
                    dm_device_direction = pa_device_manager_get_device_direction(device);
                    dm_device_id = pa_device_manager_get_device_id(device);
                    creation_time = pa_device_manager_get_device_creation_time(device);
                    pa_log_debug("  -- type[%-16s], subtype[%-5s], direction[0x%x], id[%u], creation_time[%llu]",
                                 dm_device_type, dm_device_subtype, dm_device_direction, dm_device_id, creation_time);
                    if (pa_streq(device_type, dm_device_type) && IS_AVAILABLE_DIRECTION(data->stream_type, dm_device_direction)) {
                        if (!latest_device || (latest_creation_time <= creation_time)) {
                            latest_device = device;
                            latest_creation_time = creation_time;
                            pa_log_info("  ** updated the last connected device: type[%-16s], direction[0x%x]", dm_device_type, dm_device_direction);
                        }
                    }
                }
            }
            /* update active device info. */
            if (latest_device) {
                if (data->stream_type == STREAM_SINK_INPUT)
                    *(data->proper_sink) = pa_device_manager_get_sink(latest_device, DEVICE_ROLE_NORMAL);
                else
                    *(data->proper_source) = pa_device_manager_get_source(latest_device, DEVICE_ROLE_NORMAL);

                pa_proplist_sets(GET_STREAM_NEW_PROPLIST(data->stream, data->stream_type), PA_PROP_MEDIA_ROUTE_AUTO_ACTIVE_DEV, dm_device_type);
            }
        }

    } else if (data->route_type == STREAM_ROUTE_TYPE_MANUAL && data->idx_manual_devices && data->idx_avail_devices) {
        PA_IDXSET_FOREACH(device_type, data->idx_avail_devices, idx) {
            pa_log_info("[SELECT][MANUAL] avail_device[%u] for this role[%-16s]: type[%-16s]", idx, data->stream_role, device_type);
            PA_IDXSET_FOREACH(device_id, data->idx_manual_devices, m_idx) {
                if ((device = pa_device_manager_get_device_by_id(u->device_manager, *device_id))) {
                    dm_device_type = pa_device_manager_get_device_type(device);
                    dm_device_subtype = pa_device_manager_get_device_subtype(device);
                    dm_device_direction = pa_device_manager_get_device_direction(device);
                    pa_log_debug("  -- type[%-16s], subtype[%-5s], direction[0x%x], device id[%u]",
                            dm_device_type, dm_device_subtype, dm_device_direction, *device_id);
                    if (pa_streq(device_type, dm_device_type) && IS_AVAILABLE_DIRECTION(data->stream_type, dm_device_direction)) {
                        pa_log_info("  ** found a matched device: type[%-16s], direction[0x%x]", device_type, dm_device_direction);
                        if (data->stream_type == STREAM_SINK_INPUT) {
                            if ((*(data->proper_sink)) == null_sink)
                                pa_sink_input_move_to((pa_sink_input*)(data->stream), pa_device_manager_get_sink(device, DEVICE_ROLE_NORMAL), FALSE);
                            else
                                *(data->proper_sink) = pa_device_manager_get_sink(device, DEVICE_ROLE_NORMAL);
                        } else {
                            if ((*(data->proper_source)) == null_source)
                                pa_source_output_move_to((pa_source_output*)(data->stream), pa_device_manager_get_source(device, DEVICE_ROLE_NORMAL), FALSE);
                            else
                                *(data->proper_source) = pa_device_manager_get_source(device, DEVICE_ROLE_NORMAL);
                        }
                    }
                }
            }
        }

    } else if (data->route_type == STREAM_ROUTE_TYPE_MANUAL_EXT && data->idx_manual_devices && data->idx_avail_devices) {
        PA_IDXSET_FOREACH(device_type, data->idx_avail_devices, idx) {
            pa_log_info("[SELECT][MANUAL_EXT] avail_device[%u] for this role[%-16s]: type[%-16s]", idx, data->stream_role, device_type);
            PA_IDXSET_FOREACH(device_id, data->idx_manual_devices, m_idx) {
                if ((device = pa_device_manager_get_device_by_id(u->device_manager, *device_id))) {
                    dm_device_type = pa_device_manager_get_device_type(device);
                    dm_device_subtype = pa_device_manager_get_device_subtype(device);
                    dm_device_direction = pa_device_manager_get_device_direction(device);
                    pa_log_debug("  -- type[%-16s], subtype[%-5s], direction[0x%x], device id[%u]",
                            dm_device_type, dm_device_subtype, dm_device_direction, *device_id);
                    if (pa_streq(device_type, dm_device_type) && IS_AVAILABLE_DIRECTION(data->stream_type, dm_device_direction)) {
                        pa_log_info("  ** found a matched device: type[%-16s], direction[0x%x]", device_type, dm_device_direction);
                        /* currently, we support two sinks for combining */
                        if (data->stream_type == STREAM_SINK_INPUT) {
                            if (!combine_sink_arg1) {
                                if ((sink = combine_sink_arg1 = pa_device_manager_get_sink(device, DEVICE_ROLE_NORMAL)))
                                    pa_log_debug("  -- combine_sink_arg1[%s], combine_sink_arg2[%p]", sink->name, combine_sink_arg2);
                                else
                                    pa_log_warn("  -- could not get combine_sink_arg1");
                            } else if (!combine_sink_arg2) {
                                sink = combine_sink_arg2 = pa_device_manager_get_sink(device, DEVICE_ROLE_NORMAL);
                                if (sink && !pa_streq(sink->name, combine_sink_arg1->name)) {
                                    pa_log_debug("  -- combine_sink_arg2[%s]", sink->name);
                                    /* load combine sink */
                                    if (!u->module_combine_sink_for_ex) {
                                        args = pa_sprintf_malloc("sink_name=%s slaves=\"%s,%s\"", SINK_NAME_COMBINED_EX, combine_sink_arg1->name, combine_sink_arg2->name);
                                        pa_log_info("  -- combined sink is not prepared, now load module[%s]", args);
                                        u->module_combine_sink_for_ex = pa_module_load(u->core, MODULE_COMBINE_SINK, args);
                                        pa_xfree(args);
                                    }
                                    sink = (pa_sink*)pa_namereg_get(u->core, SINK_NAME_COMBINED_EX, PA_NAMEREG_SINK);
                                    PA_IDXSET_FOREACH(s, combine_sink_arg1->inputs, s_idx) {
                                        if (s == data->stream) {
                                            pa_sink_input_move_to(s, sink, FALSE);
                                            pa_log_debug("  -- *** sink-input(%p,%u) moves to sink(%p,%s)", s, ((pa_sink_input*)s)->index, sink, sink->name);
                                            break;
                                        }
                                    }
                                } else if (!sink) {
                                    pa_log_warn("  -- could not get combine_sink_arg2");
                                }
                            }
                            if (data->origins_from_new_data)
                                *(data->proper_sink) = sink;
                            else {
                                if (((pa_sink_input*)(data->stream))->sink != sink)
                                    pa_sink_input_move_to(data->stream, sink, FALSE);
                                }

                        } else if (data->stream_type == STREAM_SOURCE_OUTPUT) {
                            if ((source = pa_device_manager_get_source(device, DEVICE_ROLE_NORMAL))) {
                                if (data->origins_from_new_data)
                                    *(data->proper_source) = source;
                                else {
                                    if (((pa_source_output*)(data->stream))->source != source)
                                        pa_source_output_move_to(data->stream, source, FALSE);
                                }
                            } else
                                pa_log_warn("  -- could not get source");
                        }
                    }
                }
            }
        }
    }

    if ((data->stream_type == STREAM_SINK_INPUT) ? !(*(data->proper_sink)) : !(*(data->proper_source))) {
        pa_log_warn("[SELECT] could not find a proper sink/source, set it to null sink/source");
        if (data->stream_type == STREAM_SINK_INPUT)
            *(data->proper_sink) = null_sink;
        else
            *(data->proper_source) = null_source;
    }

    return PA_HOOK_OK;
}

/* The state of a device using internal audio codec is handled here.
 * Regarding the state of an external device, those is handled in device-manager.c */
static void set_device_state_if_using_internal_codec(dm_device *device, stream_type_t stream_type, dm_device_state_t device_state) {
    pa_bool_t use_internal_codec = FALSE;
    dm_device_direction_t direction;

    pa_assert(device);

    direction = pa_device_manager_get_device_direction(device);
    if (IS_AVAILABLE_DIRECTION(stream_type, direction))
        if ((use_internal_codec = pa_device_manager_is_device_use_internal_codec(device, CONVERT_TO_DEVICE_DIRECTION(stream_type), DEVICE_ROLE_NORMAL)))
            pa_device_manager_set_device_state(device, CONVERT_TO_DEVICE_DIRECTION(stream_type), device_state);

    return;
}

/* Open/Close BT SCO if it is possible */
static int set_bt_sco_state(pa_device_manager *dm, pa_bool_t open) {
    dm_device_bt_sco_status_t sco_status;

    pa_assert(dm);

    pa_device_manager_bt_sco_get_status(dm, &sco_status);

    if (!open && (sco_status == DM_DEVICE_BT_SCO_STATUS_OPENED)) {
        /* close BT SCO */
        if (pa_device_manager_bt_sco_close(dm)) {
            pa_log_error("BT SCO was opened, but failed to close SCO");
            return -1;
        } else
            pa_log_debug("BT SCO is now closed");

    } else if (open) {
        if (sco_status == DM_DEVICE_BT_SCO_STATUS_DISCONNECTED) {
            pa_log_error("BT SCO is not available for this BT device");
            return -1;
        } else if (sco_status == DM_DEVICE_BT_SCO_STATUS_CONNECTED) {
            /* open BT SCO */
            if (pa_device_manager_bt_sco_open(dm)) {
                pa_log_error("failed to open BT SCO");
                return -1;
            } else
                pa_log_debug("BT SCO is now opened");
        }
    }

    return 0;
}

/* Change the route setting according to the data from argument.
 * This function is called only when it needs to change routing path via HAL.
 * - role is "reset"
 *     1. It will be received when it is needed to terminate playback
 *       or capture routing path.
 *     2. Update the state of the device to be deactivated.
 *     3. Call HAL API to reset routing.
 * - ROUTE_TYPE_AUTO
 *     1. Find the proper sink/source comparing between avail_devices
 *       and current connected devices.
 *      : Need to check the priority of the device list by order of receipt.
 *     2. Update the state of devices.
 *     3. Call HAL API to apply the routing setting
 * - ROUTE_TYPE_AUTO_ALL
 *     1. Find the proper sink/source comparing between avail_devices
 *       and current connected devices.
 *      : Might use combine-sink according to the conditions.
 *     2. Update the state of devices.
 *     3. Call HAL API to apply the routing setting
 * - ROUTE_TYPE_MANUAL
 *     1. Find the proper sink/source comparing between avail_devices
 *        and manual_devices that have been set by user.
 *     2. Update the state of devices.
 *     3. Call HAL API to apply the routing setting. */
static pa_hook_result_t route_change_hook_cb(pa_core *c, pa_stream_manager_hook_data_for_route *data, struct userdata *u) {
    uint32_t i = 0;
    uint32_t idx = 0;
    uint32_t d_idx = 0;
    uint32_t s_idx = 0;
    hal_route_info route_info = {NULL, NULL, 0};
    uint32_t conn_idx = 0;
    uint32_t *device_id = NULL;
    uint32_t dm_device_id = 0;
    stream_route_type_t route_type;
    const char *device_type = NULL;
    dm_device *device = NULL;
    dm_device *_device = NULL;
    dm_device *latest_device = NULL;
    const char *dm_device_type = NULL;
    const char *dm_device_subtype = NULL;
    dm_device_state_t dm_device_state = DM_DEVICE_STATE_DEACTIVATED;
    dm_device_direction_t dm_device_direction = DM_DEVICE_DIRECTION_NONE;
    io_direction_t hal_direction;
    void *s = NULL;
    pa_sink *sink = NULL;
    pa_source *source = NULL;
    pa_idxset *conn_devices = NULL;
    pa_sink *combine_sink_arg1 = NULL;
    pa_sink *combine_sink_arg2 = NULL;
    pa_sink *combine_sink = NULL;
    pa_sink *null_sink = NULL;
    char *args = NULL;
    pa_bool_t use_internal_codec = FALSE;
    pa_usec_t creation_time = 0;
    pa_usec_t latest_creation_time = 0;

    pa_assert(c);
    pa_assert(data);
    pa_assert(u);

    pa_log_info("[ROUTE] route_change_hook_cb is called. (%p), stream_type(%d), stream_role(%s), route_type(%d)",
            data, data->stream_type, data->stream_role, data->route_type);

    route_info.role = data->stream_role;

    if (pa_streq(data->stream_role, "reset")) {
        /* update BT SCO: close */
        set_bt_sco_state(u->device_manager, FALSE);

        /* get current connected devices */
        conn_devices = pa_device_manager_get_device_list(u->device_manager);
        /* set device state to deactivate */
        PA_IDXSET_FOREACH(device, conn_devices, conn_idx) {
            dm_device_type = pa_device_manager_get_device_type(device);
            dm_device_state = pa_device_manager_get_device_state(device, CONVERT_TO_DEVICE_DIRECTION(data->stream_type));
            dm_device_direction = pa_device_manager_get_device_direction(device);
            if (dm_device_state == DM_DEVICE_STATE_ACTIVATED && IS_AVAILABLE_DIRECTION(data->stream_type, dm_device_direction)) {
                pa_log_info("[ROUTE][RESET] found a matched device and set state to DE-ACTIVATED: type[%s], direction[0x%x]", dm_device_type, dm_device_direction);
                set_device_state_if_using_internal_codec(device, data->stream_type, DM_DEVICE_STATE_DEACTIVATED);
            }
        }
        route_info.num_of_devices = 1;
        route_info.device_infos = pa_xmalloc0(sizeof(hal_device_info)*route_info.num_of_devices);
        route_info.device_infos[0].direction = (data->stream_type == STREAM_SINK_INPUT) ? DIRECTION_OUT : DIRECTION_IN;

        /* unload combine sink */
        if (data->stream_type == STREAM_SINK_INPUT && u->module_combine_sink) {
            pa_log_info("[ROUTE][RESET] unload module[%s]", SINK_NAME_COMBINED);
            combine_sink = (pa_sink*)pa_namereg_get(u->core, SINK_NAME_COMBINED, PA_NAMEREG_SINK);
            null_sink = (pa_sink*)pa_namereg_get(u->core, SINK_NAME_NULL, PA_NAMEREG_SINK);
            if (!combine_sink || !null_sink)
                pa_log_error("[ROUTE][RESET] could not get combine_sink(%p) or null_sink(%p)", combine_sink, null_sink);
            else {
                PA_IDXSET_FOREACH(s, combine_sink->inputs, s_idx) {
                    pa_sink_input_move_to(s, null_sink, FALSE);
                    pa_log_debug("[ROUTE][RESET] *** sink-input(%p,%u) moves to sink(%p,%s)", s, ((pa_sink_input*)s)->index, null_sink, null_sink->name);
                }
                pa_sink_suspend(combine_sink, TRUE, PA_SUSPEND_USER);
            }
            pa_module_unload(u->core, u->module_combine_sink, TRUE);
            u->module_combine_sink = NULL;
        }

    } else if ((data->route_type <= STREAM_ROUTE_TYPE_AUTO_ALL) && data->idx_avail_devices) {
        /* update BT SCO: close */
        set_bt_sco_state(u->device_manager, FALSE);

        /* get current connected devices */
        conn_devices = pa_device_manager_get_device_list(u->device_manager);
        if (data->route_type == STREAM_ROUTE_TYPE_AUTO || data->route_type == STREAM_ROUTE_TYPE_AUTO_ALL) {
            PA_IDXSET_FOREACH(device_type, data->idx_avail_devices, idx) {
                pa_log_info("[ROUTE][AUTO(_ALL)] avail_device[%u] for this role[%-16s]: type[%-16s]", idx, route_info.role, device_type);
                PA_IDXSET_FOREACH(device, conn_devices, conn_idx) {
                    dm_device_type = pa_device_manager_get_device_type(device);
                    dm_device_subtype = pa_device_manager_get_device_subtype(device);
                    dm_device_direction = pa_device_manager_get_device_direction(device);
                    dm_device_id = pa_device_manager_get_device_id(device);
                    pa_log_debug("  -- type[%-16s], subtype[%-5s], direction[0x%x], id[%u]",
                                 dm_device_type, dm_device_subtype, dm_device_direction, dm_device_id);
                    if (pa_streq(device_type, dm_device_type) && IS_AVAILABLE_DIRECTION(data->stream_type, dm_device_direction)) {
                        pa_log_debug("  ** found a matched device: type[%-16s], direction[0x%x]", dm_device_type, dm_device_direction);
                        use_internal_codec = pa_device_manager_is_device_use_internal_codec(device, CONVERT_TO_DEVICE_DIRECTION(data->stream_type), DEVICE_ROLE_NORMAL);
                        if (use_internal_codec) {
                            hal_direction = (data->stream_type == STREAM_SINK_INPUT) ? DIRECTION_OUT : DIRECTION_IN;
                            route_info.num_of_devices++;
                            route_info.device_infos = pa_xrealloc(route_info.device_infos, sizeof(hal_device_info)*route_info.num_of_devices);
                            route_info.device_infos[route_info.num_of_devices-1].type = dm_device_type;
                            route_info.device_infos[route_info.num_of_devices-1].direction = hal_direction;
                            route_info.device_infos[route_info.num_of_devices-1].id = dm_device_id;
                            pa_log_info("  ** found a matched device and set state to ACTIVATED: type[%-16s], direction[0x%x], id[%u]",
                            route_info.device_infos[route_info.num_of_devices-1].type, hal_direction, dm_device_id);
                            /* set device state to activated */
                            set_device_state_if_using_internal_codec(device, data->stream_type, DM_DEVICE_STATE_ACTIVATED);
                        } else
                            pa_log_debug("  -- it does not use internal audio codec, skip it");
                        break;
                    }
                }
                if (device == NULL)
                    continue;

                if (data->route_type == STREAM_ROUTE_TYPE_AUTO) {
                    /* check if this device uses internal codec */
                    use_internal_codec = pa_device_manager_is_device_use_internal_codec(device, CONVERT_TO_DEVICE_DIRECTION(data->stream_type), DEVICE_ROLE_NORMAL);
                    if (use_internal_codec) {
                        /* set other device's state to deactivated */
                        PA_IDXSET_FOREACH(_device, conn_devices, conn_idx) {
                            if (device == _device)
                                continue;
                            set_device_state_if_using_internal_codec(_device, data->stream_type, DM_DEVICE_STATE_DEACTIVATED);
                        }
                    }
                    if (data->origins_from_new_data)
                        pa_proplist_sets(GET_STREAM_NEW_PROPLIST(data->stream, data->stream_type), PA_PROP_MEDIA_ROUTE_AUTO_ACTIVE_DEV, dm_device_type);
                    else
                        pa_proplist_sets(GET_STREAM_PROPLIST(data->stream, data->stream_type), PA_PROP_MEDIA_ROUTE_AUTO_ACTIVE_DEV, dm_device_type);

                /* move sink-inputs/source-outputs if needed */
                if (data->stream_type == STREAM_SINK_INPUT)
                    sink = pa_device_manager_get_sink(device, DEVICE_ROLE_NORMAL);

                    /* unload combine sink */
                    if (data->stream_type == STREAM_SINK_INPUT && u->module_combine_sink) {
                        if ((combine_sink = (pa_sink*)pa_namereg_get(u->core, SINK_NAME_COMBINED, PA_NAMEREG_SINK))) {
                            PA_IDXSET_FOREACH(s, combine_sink->inputs, s_idx) {
                                pa_sink_input_move_to(s, sink, FALSE);
                                pa_log_debug("[ROUTE][AUTO] *** sink-input(%p,%u) moves to sink(%p,%s)", s, ((pa_sink_input*)s)->index, sink, sink->name);
                            }
                            pa_sink_suspend(combine_sink, TRUE, PA_SUSPEND_USER);
                        } else
                            pa_log_error("[ROUTE][AUTO] could not get combine_sink");

                        pa_log_debug("[ROUTE][AUTO] unload module[%s]", SINK_NAME_COMBINED);
                        pa_module_unload(u->core, u->module_combine_sink, TRUE);
                        u->module_combine_sink = NULL;
                    }
                    break;

                } else if (data->route_type == STREAM_ROUTE_TYPE_AUTO_ALL) {
                    /* find the proper sink/source */
                    /* currently, we support two sinks for combining */
                    if (data->stream_type == STREAM_SINK_INPUT && u->module_combine_sink) {
                        sink = (pa_sink*)pa_namereg_get(u->core, SINK_NAME_COMBINED, PA_NAMEREG_SINK);
                        pa_log_info("[ROUTE][AUTO_ALL] found the combine_sink already existed");
                    } else if (data->stream_type == STREAM_SINK_INPUT && !combine_sink_arg1) {
                        sink = combine_sink_arg1 = pa_device_manager_get_sink(device, DEVICE_ROLE_NORMAL);
                        pa_log_debug("[ROUTE][AUTO_ALL] combine_sink_arg1[%s], combine_sink_arg2[%p]", sink->name, combine_sink_arg2);
                    } else if (data->stream_type == STREAM_SINK_INPUT && !combine_sink_arg2) {
                        sink = combine_sink_arg2 = pa_device_manager_get_sink(device, DEVICE_ROLE_NORMAL);
                        if (sink && !pa_streq(sink->name, combine_sink_arg1->name)) {
                            pa_log_debug("[ROUTE][AUTO_ALL] combine_sink_arg2[%s]", sink->name);
                            /* load combine sink */
                            if (!u->module_combine_sink) {
                                args = pa_sprintf_malloc("sink_name=%s slaves=\"%s,%s\"", SINK_NAME_COMBINED, combine_sink_arg1->name, combine_sink_arg2->name);
                                pa_log_info("[ROUTE][AUTO_ALL] combined sink is not prepared, now load module[%s]", args);
                                u->module_combine_sink = pa_module_load(u->core, MODULE_COMBINE_SINK, args);
                                pa_xfree(args);
                            }
                            if ((sink = (pa_sink*)pa_namereg_get(u->core, SINK_NAME_COMBINED, PA_NAMEREG_SINK))) {
                                PA_IDXSET_FOREACH(s, combine_sink_arg1->inputs, s_idx) {
                                    if (s == data->stream) {
                                        pa_sink_input_move_to(s, sink, FALSE);
                                        pa_log_debug("[ROUTE][AUTO_ALL] *** sink-nput(%p,%u) moves to sink(%p,%s)",
                                                     s, ((pa_sink_input*)s)->index, sink, sink->name);
                                    }
                                }
                            } else
                                pa_log_error("[ROUTE][AUTO_ALL] could not get combine_sink");
                        }
                    } else if (data->stream_type == STREAM_SOURCE_OUTPUT)
                        source = pa_device_manager_get_source(device, DEVICE_ROLE_NORMAL);

                    if (data->origins_from_new_data) {
                        if (data->stream_type == STREAM_SINK_INPUT)
                            *(data->proper_sink) = sink;
                        else
                            *(data->proper_source) = source;
                    } else {
                        /* move sink-inputs/source-outputs if needed */
                        if (data->idx_streams) {
                            PA_IDXSET_FOREACH(s, data->idx_streams, s_idx) { /* data->idx_streams: null_sink */
                                if (!pa_stream_manager_get_route_type(s, FALSE, data->stream_type, &route_type) &&
                                    (route_type == STREAM_ROUTE_TYPE_AUTO_ALL)) {
                                    if ((data->stream_type == STREAM_SINK_INPUT) && (sink && (sink != ((pa_sink_input*)s)->sink))) {
                                        pa_sink_input_move_to(s, sink, FALSE);
                                        pa_log_debug("[ROUTE][AUTO_ALL] *** sink-input(%p,%u) moves to sink(%p,%s)",
                                                     s, ((pa_sink_input*)s)->index, sink, sink->name);
                                    } else if ((data->stream_type == STREAM_SOURCE_OUTPUT) && (source && (source != ((pa_source_output*)s)->source))) {
                                        pa_source_output_move_to(s, source, FALSE);
                                        pa_log_debug("[ROUTE][AUTO_ALL] *** source-output(%p,%u) moves to source(%p,%s)",
                                                     s, ((pa_source_output*)s)->index, source, source->name);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        } else if (data->route_type == STREAM_ROUTE_TYPE_AUTO_LAST_CONNECTED) {
            PA_IDXSET_FOREACH(device_type, data->idx_avail_devices, idx) {
                pa_log_info("[ROUTE][AUTO_LAST_CONN] avail_device[%u] for this role[%-16s]: type[%-16s]", idx, data->stream_role, device_type);
                PA_IDXSET_FOREACH(device, conn_devices, conn_idx) {
                    dm_device_type = pa_device_manager_get_device_type(device);
                    dm_device_subtype = pa_device_manager_get_device_subtype(device);
                    dm_device_direction = pa_device_manager_get_device_direction(device);
                    dm_device_id = pa_device_manager_get_device_id(device);
                    creation_time = pa_device_manager_get_device_creation_time(device);
                    pa_log_debug("  -- type[%-16s], subtype[%-5s], direction[0x%x], id[%u], creation_time[%llu]",
                                 dm_device_type, dm_device_subtype, dm_device_direction, dm_device_id, creation_time);
                    if (pa_streq(device_type, dm_device_type) && IS_AVAILABLE_DIRECTION(data->stream_type, dm_device_direction)) {
                        if (!latest_device || (latest_creation_time <= creation_time)) {
                            latest_device = device;
                            latest_creation_time = creation_time;
                            pa_log_info("  ** updated the last connected device: type[%-16s], direction[0x%x]", dm_device_type, dm_device_direction);
                        }
                    }
                }
            }
            /* update activated device if it is found */
            if (latest_device) {
                dm_device_type = pa_device_manager_get_device_type(latest_device);
                dm_device_id = pa_device_manager_get_device_id(latest_device);
                use_internal_codec = pa_device_manager_is_device_use_internal_codec(latest_device, CONVERT_TO_DEVICE_DIRECTION(data->stream_type), DEVICE_ROLE_NORMAL);
                if (use_internal_codec) {
                    hal_direction = (data->stream_type == STREAM_SINK_INPUT) ? DIRECTION_OUT : DIRECTION_IN;
                    route_info.num_of_devices++;
                    route_info.device_infos = pa_xrealloc(route_info.device_infos, sizeof(hal_device_info)*route_info.num_of_devices);
                    route_info.device_infos[route_info.num_of_devices-1].type = dm_device_type;
                    route_info.device_infos[route_info.num_of_devices-1].direction = hal_direction;
                    route_info.device_infos[route_info.num_of_devices-1].id = dm_device_id;
                    pa_log_info("  ** found a matched device and set state to ACTIVATED: type[%-16s], direction[0x%x], id[%u]",
                    route_info.device_infos[route_info.num_of_devices-1].type, hal_direction, dm_device_id);
                    /* set device state to activated */
                    set_device_state_if_using_internal_codec(latest_device, data->stream_type, DM_DEVICE_STATE_ACTIVATED);

                    /* set other device's state to deactivated */
                    PA_IDXSET_FOREACH(device, conn_devices, conn_idx) {
                        if (latest_device == device)
                            continue;
                        set_device_state_if_using_internal_codec(device, data->stream_type, DM_DEVICE_STATE_DEACTIVATED);
                    }
                } else
                    pa_log_debug("  -- it does not use internal audio codec, skip it");

                if (data->origins_from_new_data)
                    pa_proplist_sets(GET_STREAM_NEW_PROPLIST(data->stream, data->stream_type), PA_PROP_MEDIA_ROUTE_AUTO_ACTIVE_DEV, dm_device_type);
                else
                    pa_proplist_sets(GET_STREAM_PROPLIST(data->stream, data->stream_type), PA_PROP_MEDIA_ROUTE_AUTO_ACTIVE_DEV, dm_device_type);

                /* unload combine sink */
                if (data->stream_type == STREAM_SINK_INPUT && u->module_combine_sink) {
                    if ((combine_sink = (pa_sink*)pa_namereg_get(u->core, SINK_NAME_COMBINED, PA_NAMEREG_SINK))) {
                        PA_IDXSET_FOREACH(s, combine_sink->inputs, s_idx) {
                            pa_sink_input_move_to(s, sink, FALSE);
                            pa_log_debug("[ROUTE][AUTO_LAST_CONN] *** sink-input(%p,%u) moves to sink(%p,%s)", s, ((pa_sink_input*)s)->index, sink, sink->name);
                        }
                        pa_sink_suspend(combine_sink, TRUE, PA_SUSPEND_USER);
                    } else
                        pa_log_error("[ROUTE][AUTO_LAST_CONN] could not get combine_sink");

                    pa_log_info("[ROUTE][AUTO_LAST_CONN] unload module[%s]", SINK_NAME_COMBINED);
                    pa_module_unload(u->core, u->module_combine_sink, TRUE);
                    u->module_combine_sink = NULL;
                }
            }
        }

        if (data->route_type == STREAM_ROUTE_TYPE_AUTO_ALL && route_info.num_of_devices) {
            /* set other device's state to deactivated */
            PA_IDXSET_FOREACH(_device, conn_devices, conn_idx) {
                pa_bool_t need_to_deactive = TRUE;
                dm_device_id = pa_device_manager_get_device_id(_device);
                for (i = 0; i < route_info.num_of_devices; i++) {
                    if (dm_device_id == route_info.device_infos[i].id) {
                        need_to_deactive = FALSE;
                        break;
                    }
                }
                if (need_to_deactive)
                    set_device_state_if_using_internal_codec(_device, data->stream_type, DM_DEVICE_STATE_DEACTIVATED);
            }
        }

    } else if (data->route_type == STREAM_ROUTE_TYPE_MANUAL && data->idx_manual_devices && data->idx_avail_devices) {
        PA_IDXSET_FOREACH(device_type, data->idx_avail_devices, idx) {
            pa_log_info("[ROUTE][MANUAL] avail_device[%u] for this role[%-16s]: type[%-16s]", idx, data->stream_role, device_type);
            PA_IDXSET_FOREACH(device_id, data->idx_manual_devices, d_idx) {
                pa_log_debug("  -- manual_device[%u] for this role[%-16s]: device_id(%u)", idx, data->stream_role, *device_id);
                if ((device = pa_device_manager_get_device_by_id(u->device_manager, *device_id))) {
                    dm_device_type = pa_device_manager_get_device_type(device);
                    if (pa_streq(device_type, dm_device_type)) {
                        pa_log_debug("  ** found a matched device: type[%-16s]", device_type);
                        if (pa_streq(dm_device_type, DEVICE_TYPE_BT)) {
                            /* update BT SCO: open */
                            if (set_bt_sco_state(u->device_manager, TRUE)) {
                                pa_log_error("  ** could not open BT SCO");
                                continue;
                            }
                        } else {
                            /* update BT SCO: close */
                            set_bt_sco_state(u->device_manager, FALSE);
                        }
                        dm_device_direction = pa_device_manager_get_device_direction(device);
                        dm_device_subtype = pa_device_manager_get_device_subtype(device);
                        pa_log_debug("  -- manual_device, type[%-16s], subtype[%-5s], direction[0x%x]", dm_device_type, dm_device_subtype, dm_device_direction);
                        if (IS_AVAILABLE_DIRECTION(data->stream_type, dm_device_direction)) {
                            use_internal_codec = pa_device_manager_is_device_use_internal_codec(device, CONVERT_TO_DEVICE_DIRECTION(data->stream_type), DEVICE_ROLE_NORMAL);
                            if (use_internal_codec) {
                                route_info.num_of_devices++;
                                route_info.device_infos = pa_xrealloc(route_info.device_infos, sizeof(hal_device_info)*route_info.num_of_devices);
                                route_info.device_infos[route_info.num_of_devices-1].type = dm_device_type;
                                route_info.device_infos[route_info.num_of_devices-1].direction = (data->stream_type == STREAM_SINK_INPUT) ? DIRECTION_OUT : DIRECTION_IN;
                                pa_log_info("  ** found a matched device and set state to ACTIVATED: type[%-16s], direction[0x%x]",
                                    route_info.device_infos[route_info.num_of_devices-1].type, dm_device_direction);
                                /* set device state to activated */
                                set_device_state_if_using_internal_codec(device, data->stream_type, DM_DEVICE_STATE_ACTIVATED);
                            } else
                                pa_log_debug("  -- it does not use internal audio codec, skip it");
                        }
                    }
                }
            }
        }

        /* move sink-inputs/source-outputs if needed */
        if (device && !data->origins_from_new_data) {
            if (data->stream_type == STREAM_SINK_INPUT)
                sink = pa_device_manager_get_sink(device, DEVICE_ROLE_NORMAL);
            else if (data->stream_type == STREAM_SOURCE_OUTPUT)
                source = pa_device_manager_get_source(device, DEVICE_ROLE_NORMAL);
            if (data->idx_streams) {
                PA_IDXSET_FOREACH(s, data->idx_streams, idx) {
                    if (sink && (sink != ((pa_sink_input*)s)->sink)) {
                        pa_sink_input_move_to(s, sink, FALSE);
                        pa_log_debug("[ROUTE][MANUAL] *** sink-input(%p,%u) moves to sink(%p,%s)", s, ((pa_sink_input*)s)->index, sink, sink->name);
                    } else if (source && (source != ((pa_source_output*)s)->source)) {
                        pa_source_output_move_to(s, source, FALSE);
                        pa_log_debug("[ROUTE][MANUAL] *** source-output(%p,%u) moves to source(%p,%s)", s, ((pa_source_output*)s)->index, source, source->name);
                    }
                }
            }
        }
    }

    if (route_info.device_infos) {
        /* send information to HAL to set routing */
        if (pa_hal_manager_do_route(u->hal_manager, &route_info))
            pa_log_error("[ROUTE] Failed to pa_hal_manager_do_route()");
        pa_xfree(route_info.device_infos);
    }
    return PA_HOOK_OK;
}

/* Forward routing option to HAL */
static pa_hook_result_t route_option_update_hook_cb(pa_core *c, pa_stream_manager_hook_data_for_option *data, struct userdata *u) {
    hal_route_option route_option;

    pa_assert(c);
    pa_assert(data);
    pa_assert(u);

    pa_log_info("[ROUTE_OPT] route_option_update_hook_cb is called. (%p), stream_role(%s), option[name(%s)/value(%d)]",
            data, data->stream_role, data->name, data->value);
    route_option.role = data->stream_role;
    route_option.name = data->name;
    route_option.value = data->value;

    /* send information to HAL to update routing option */
    if (pa_hal_manager_update_route_option(u->hal_manager, &route_option))
        pa_log_error("[ROUTE_OPT] Failed to pa_hal_manager_update_route_option()");

    return PA_HOOK_OK;
}

/* Reorganize routing when a device has been connected or disconnected */
static pa_hook_result_t device_connection_changed_hook_cb(pa_core *c, pa_device_manager_hook_data_for_conn_changed *conn, struct userdata *u) {
    uint32_t idx = 0;
    pa_sink_input *s = NULL;
    dm_device_direction_t device_direction = DM_DEVICE_DIRECTION_OUT;
    pa_sink *sink = NULL;
    pa_sink *null_sink = NULL;
    pa_sink *combine_sink = NULL;
    pa_bool_t use_internal_codec = FALSE;
    pa_idxset* conn_devices = NULL;
    dm_device* device = NULL;

    pa_assert(c);
    pa_assert(conn);
    pa_assert(u);

    device_direction = pa_device_manager_get_device_direction(conn->device);

    pa_log_info("[CONN] device_connection_changed_hook_cb is called. conn(%p), is_connected(%d), device(%p), direction(0x%x)",
            conn, conn->is_connected, conn->device, device_direction);

    null_sink = (pa_sink*)pa_namereg_get(u->core, SINK_NAME_NULL, PA_NAMEREG_SINK);
    if (!null_sink) {
        pa_log_error("[CONN] could not get null_sink(%p)", null_sink);
        return PA_HOOK_OK;
    }

    use_internal_codec = pa_device_manager_is_device_use_internal_codec(conn->device, device_direction, DEVICE_ROLE_NORMAL);
    /* update for using combine sink case when external device is disconnected */
    if (!use_internal_codec && !conn->is_connected && (device_direction & DM_DEVICE_DIRECTION_OUT)) {
        if (u->module_combine_sink) {
            /* unload combine sink */
            if ((combine_sink = (pa_sink*)pa_namereg_get(u->core, SINK_NAME_COMBINED, PA_NAMEREG_SINK))) {
                conn_devices = pa_device_manager_get_device_list(u->device_manager);
                PA_IDXSET_FOREACH(device, conn_devices, idx) {
                    device_direction = pa_device_manager_get_device_direction(device);
                    if (device_direction == DM_DEVICE_DIRECTION_OUT) {
                        use_internal_codec = pa_device_manager_is_device_use_internal_codec(device, device_direction, DEVICE_ROLE_NORMAL);
                        if (use_internal_codec) {
                            sink = pa_device_manager_get_sink(device, DEVICE_ROLE_NORMAL);
                            break;
                        }
                    }
                }
                if (combine_sink->inputs) {
                    if (!sink)
                        sink = null_sink;
                    PA_IDXSET_FOREACH(s, combine_sink->inputs, idx) {
                        /* re-route this stream to the remaining device using internal codec */
                        pa_sink_input_move_to(s, sink, FALSE);
                        pa_log_debug("[CONN] *** sink-input(%p,%u) moves to sink(%p,%s)", s, ((pa_sink_input*)s)->index, sink, sink->name);
                    }
                }
                pa_sink_suspend(combine_sink, TRUE, PA_SUSPEND_USER);
                pa_module_unload(u->core, u->module_combine_sink, TRUE);
                u->module_combine_sink = NULL;
            } else
                pa_log_error("[CONN] could not get combine_sink");
        }
        if (u->module_combine_sink_for_ex) {
            /* unload combine sink for external devices */
            if ((combine_sink = (pa_sink*)pa_namereg_get(u->core, SINK_NAME_COMBINED_EX, PA_NAMEREG_SINK))) {
                if (combine_sink->inputs) {
                    PA_IDXSET_FOREACH(s, combine_sink->inputs, idx) {
                        pa_sink_input_move_to(s, null_sink, FALSE);
                        pa_log_debug("[CONN] *** sink-input(%p,%u) moves to sink(%p,%s)", s, ((pa_sink_input*)s)->index, null_sink, null_sink->name);
                    }
                }
                pa_sink_suspend(combine_sink, TRUE, PA_SUSPEND_USER);
                pa_module_unload(u->core, u->module_combine_sink_for_ex, TRUE);
                u->module_combine_sink_for_ex = NULL;
            } else
                pa_log_error("[CONN] could not get combine_sink_ex");
        }
    }

    return PA_HOOK_OK;
}

#ifdef HAVE_DBUS
static void _do_something1(char* arg1, int arg2, void *data)
{
    pa_assert(data);
    pa_assert(arg1);

    pa_log_debug("Do Something 1 , arg1 (%s) arg2 (%d)", arg1, arg2);
}

static void _do_something2(char* arg1, void *data)
{
    pa_assert(data);
    pa_assert(arg1);

    pa_log_debug("Do Something 2 , arg1 (%s) ", arg1);
}

static void handle_get_property_test1(DBusConnection *conn, DBusMessage *msg, void *userdata)
{
    struct userdata *u = userdata;
    dbus_int32_t value_i = 0;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(u);

    value_i = u->test_property1;

    pa_dbus_send_basic_value_reply(conn, msg, DBUS_TYPE_INT32, &value_i);
}

static void handle_set_property_test1(DBusConnection *conn, DBusMessage *msg, DBusMessageIter *iter, void *userdata)
{
    struct userdata *u = userdata;
    dbus_int32_t value_i = 0;
    pa_assert(conn);
    pa_assert(msg);
    pa_assert(u);

    dbus_message_iter_get_basic(iter, &value_i);

    u->test_property1 = value_i;
    pa_dbus_send_empty_reply(conn, msg);

    /* send signal to notify change of property1*/
    send_prop1_changed_signal(u);
}

/* test property handler : return module name */
static void handle_get_property_test2(DBusConnection *conn, DBusMessage *msg, void *userdata)
{
    struct userdata *u = userdata;
    pa_assert(conn);
    pa_assert(msg);
    pa_assert(u);

    if (!u->module->name) {
        pa_dbus_send_error(conn, msg, DBUS_ERROR_INVALID_ARGS, "property(module name) null");
        return;
    }

    pa_dbus_send_basic_value_reply(conn, msg, DBUS_TYPE_STRING, &u->module->name);
}

static void handle_get_all(DBusConnection *conn, DBusMessage *msg, void *userdata)
{
    struct userdata *u = userdata;
    dbus_int32_t value_i = 0;

    DBusMessage *reply = NULL;
    DBusMessageIter msg_iter;
    DBusMessageIter dict_iter;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(u);

    pa_assert_se((reply = dbus_message_new_method_return(msg)));

    value_i = u->test_property1;

    dbus_message_iter_init_append(reply, &msg_iter);
    pa_assert_se(dbus_message_iter_open_container(&msg_iter, DBUS_TYPE_ARRAY, "{sv}", &dict_iter));

    pa_dbus_append_basic_variant_dict_entry(&dict_iter, property_handlers[PROPERTY_TEST1].property_name, DBUS_TYPE_INT32, &value_i);
    pa_dbus_append_basic_variant_dict_entry(&dict_iter, property_handlers[PROPERTY_TEST2].property_name, DBUS_TYPE_STRING, &u->module->name);
    pa_assert_se(dbus_message_iter_close_container(&msg_iter, &dict_iter));
    pa_assert_se(dbus_connection_send(conn, reply, NULL));
    dbus_message_unref(reply);
}

/* test method : return length of argument string */
static void handle_method_test1(DBusConnection *conn, DBusMessage *msg, void *userdata)
{
    const char* arg1_s = NULL;
    dbus_uint32_t value_u = 0;
    pa_assert(conn);
    pa_assert(msg);

    pa_assert_se(dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &arg1_s, DBUS_TYPE_INVALID));
    value_u = strlen(arg1_s);
    pa_dbus_send_basic_value_reply(conn, msg, DBUS_TYPE_UINT32, &value_u);
}

static void handle_method_test2(DBusConnection *conn, DBusMessage *msg, void *userdata)
{
    dbus_int32_t value1, value2, result;
    pa_assert(conn);
    pa_assert(msg);

    pa_assert_se(dbus_message_get_args(msg, NULL,
                                       DBUS_TYPE_INT32, &value1,
                                       DBUS_TYPE_INT32, &value2,
                                       DBUS_TYPE_INVALID));

    result = value1 * value2;

    pa_dbus_send_basic_value_reply(conn, msg, DBUS_TYPE_INT32, &result);
}

static DBusMessage* _generate_basic_property_change_signal_msg(int property_index, int property_type, void *data) {
    DBusMessage *signal_msg;
    DBusMessageIter signal_iter, dict_iter;
    const char *interface = INTERFACE_POLICY;

    /* org.freedesktop.DBus.Properties.PropertiesChanged (
           STRING interface_name,
           DICT<STRING,VARIANT> changed_properties,
           ARRAY<STRING> invalidated_properties); */

    pa_assert_se(signal_msg = dbus_message_new_signal(OBJECT_PATH, DBUS_INTERFACE_PROPERTIES, SIGNAL_PROP_CHANGED));
    dbus_message_iter_init_append(signal_msg, &signal_iter);

    /* STRING interface_name */
    dbus_message_iter_append_basic(&signal_iter, DBUS_TYPE_STRING, &interface);

    /* DICT<STRING,VARIANT> changed_properties */
    pa_assert_se(dbus_message_iter_open_container(&signal_iter, DBUS_TYPE_ARRAY, "{sv}", &dict_iter));
    pa_dbus_append_basic_variant_dict_entry(&dict_iter, property_handlers[property_index].property_name,
                                            property_type, data);
    dbus_message_iter_close_container(&signal_iter, &dict_iter);

    /* ARRAY<STRING> invalidated_properties (empty) */
    dbus_message_iter_open_container(&signal_iter, DBUS_TYPE_ARRAY, "s", &dict_iter);
    dbus_message_iter_close_container(&signal_iter, &dict_iter);

    return signal_msg;
}

static void send_prop1_changed_signal(struct userdata* u) {
    DBusMessage *signal_msg = _generate_basic_property_change_signal_msg(PROPERTY_TEST1, DBUS_TYPE_INT32, &u->test_property1);

#ifdef USE_DBUS_PROTOCOL
    pa_dbus_protocol_send_signal(u->dbus_protocol, signal_msg);
#else
    dbus_connection_send(pa_dbus_connection_get(u->dbus_conn), signal_msg, NULL);
#endif

    dbus_message_unref(signal_msg);
}

static DBusHandlerResult dbus_filter_audio_handler(DBusConnection *c, DBusMessage *s, void *userdata)
{
    DBusError error;
    char* arg_s = NULL;
    int arg_i = 0;

    pa_assert(userdata);

    if (dbus_message_get_type(s) != DBUS_MESSAGE_TYPE_SIGNAL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    pa_log_info("Audio handler received msg");
    dbus_error_init(&error);

    if (dbus_message_is_signal(s, SOUND_SERVER_INTERFACE_NAME, "TestSignalFromSS1")) {
        if (!dbus_message_get_args(s, NULL,
            DBUS_TYPE_STRING, &arg_s,
            DBUS_TYPE_INT32, &arg_i ,
            DBUS_TYPE_INVALID)) {
            goto fail;
        } else {
            _do_something1(arg_s, arg_i, userdata);
        }
    } else if (dbus_message_is_signal(s, SOUND_SERVER_INTERFACE_NAME, "TestSignalFromSS2")) {
        if (!dbus_message_get_args(s, NULL,
            DBUS_TYPE_STRING, &arg_s,
            DBUS_TYPE_INVALID)) {
            goto fail;
        } else {
            _do_something2(arg_s, userdata);
        }
    } else if (dbus_message_is_signal(s, AUDIO_CLIENT_INTERFACE_NAME, "TestSignalFromClient1")) {
        if (!dbus_message_get_args(s, NULL,
            DBUS_TYPE_STRING, &arg_s,
            DBUS_TYPE_INVALID)) {
            goto fail;
        } else {
            _do_something2(arg_s, userdata);
        }
    } else {
        pa_log_info("Unknown message, not handle it");
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

static DBusHandlerResult handle_get_property(DBusConnection *conn, DBusMessage *msg, void *userdata)
{
    int prop_idx = 0;
    const char *interface_name, *property_name;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(userdata);

    if (pa_streq(dbus_message_get_signature(msg), "ss")) {
        pa_assert_se(dbus_message_get_args(msg, NULL,
                                           DBUS_TYPE_STRING, &interface_name,
                                           DBUS_TYPE_STRING, &property_name,
                                           DBUS_TYPE_INVALID));
        if (pa_streq(interface_name, INTERFACE_POLICY)) {
            for (prop_idx = 0; prop_idx < PROPERTY_MAX; prop_idx++) {
                if (pa_streq(property_name, property_handlers[prop_idx].property_name)) {
                    property_handlers[prop_idx].get_cb(conn, msg, userdata);
                    return DBUS_HANDLER_RESULT_HANDLED;
                }
            }
        }
       else {
            pa_log_warn("Not our interface, not handle it");
        }
    } else {
        pa_log_warn("Wrong Signature");
        pa_dbus_send_error(conn, msg, DBUS_ERROR_INVALID_SIGNATURE,  "Wrong Signature, Expected (ss)");
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusHandlerResult handle_get_all_property(DBusConnection *conn, DBusMessage *msg, void *userdata)
{
    const char *interface_name;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(userdata);

    if (pa_streq(dbus_message_get_signature(msg), "s")) {
        pa_assert_se(dbus_message_get_args(msg, NULL,
                                           DBUS_TYPE_STRING, &interface_name,
                                           DBUS_TYPE_INVALID));
        if (pa_streq(interface_name, INTERFACE_POLICY)) {
            handle_get_all(conn, msg, userdata);
            return DBUS_HANDLER_RESULT_HANDLED;
        }
       else {
            pa_log_warn("Not our interface, not handle it");
        }
    } else {
        pa_log_warn("Wrong Signature");
        pa_dbus_send_error(conn, msg, DBUS_ERROR_INVALID_SIGNATURE,  "Wrong Signature, Expected (ss)");
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusHandlerResult handle_set_property(DBusConnection *conn, DBusMessage *msg, void *userdata)
{
    int prop_idx = 0;
    const char *interface_name, *property_name, *property_sig;
    DBusMessageIter msg_iter;
    DBusMessageIter variant_iter;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(userdata);

    if (pa_streq(dbus_message_get_signature(msg), "ssv")) {
        pa_assert_se(dbus_message_iter_init(msg, &msg_iter));
        dbus_message_iter_get_basic(&msg_iter, &interface_name);
        pa_assert_se(dbus_message_iter_next(&msg_iter));
        dbus_message_iter_get_basic(&msg_iter, &property_name);
        pa_assert_se(dbus_message_iter_next(&msg_iter));

        dbus_message_iter_recurse(&msg_iter, &variant_iter);

        property_sig = dbus_message_iter_get_signature(&variant_iter);

        if (pa_streq(interface_name, INTERFACE_POLICY)) {
            for (prop_idx = 0; prop_idx < PROPERTY_MAX; prop_idx++) {
                if (pa_streq(property_name, property_handlers[prop_idx].property_name)) {
                    if (pa_streq(property_handlers[prop_idx].type, property_sig)) {
                        property_handlers[prop_idx].set_cb(conn, msg, &variant_iter, userdata);
                        return DBUS_HANDLER_RESULT_HANDLED;
                    }
                   else {
                        pa_log_warn("Wrong Property Signature");
                        pa_dbus_send_error(conn, msg, DBUS_ERROR_INVALID_SIGNATURE,  "Wrong Signature, Expected (ssv)");
                    }
                    break;
                }
            }
        }
       else {
            pa_log_warn("Not our interface, not handle it");
        }
    } else {
        pa_log_warn("Wrong Signature");
        pa_dbus_send_error(conn, msg, DBUS_ERROR_INVALID_SIGNATURE,  "Wrong Signature, Expected (ssv)");
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusHandlerResult handle_policy_methods(DBusConnection *conn, DBusMessage *msg, void *userdata)
{
    int method_idx = 0;

    pa_assert(conn);
    pa_assert(msg);
    pa_assert(userdata);

    for (method_idx = 0; method_idx < METHOD_HANDLER_MAX; method_idx++) {
        if (dbus_message_is_method_call(msg, INTERFACE_POLICY, method_handlers[method_idx].method_name)) {
            if (pa_streq(dbus_message_get_signature(msg), method_arg_signatures[method_idx])) {
                method_handlers[method_idx].receive_cb(conn, msg, userdata);
                return DBUS_HANDLER_RESULT_HANDLED;
            }
           else {
                pa_log_warn("Wrong Argument Signature");
                pa_dbus_send_error(conn, msg, DBUS_ERROR_INVALID_SIGNATURE,  "Wrong Signature, Expected %s", method_arg_signatures[method_idx]);
                return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
            }
        }
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusHandlerResult handle_introspect(DBusConnection *conn, DBusMessage *msg, void *userdata)
{
    const char *xml = POLICY_INTROSPECT_XML;
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

static DBusHandlerResult method_call_handler(DBusConnection *c, DBusMessage *m, void *userdata)
{
    struct userdata *u = userdata;
    const char *path, *interface, *member;

    pa_assert(c);
    pa_assert(m);
    pa_assert(u);

    path = dbus_message_get_path(m);
    interface = dbus_message_get_interface(m);
    member = dbus_message_get_member(m);

    pa_log_debug("dbus: path=%s, interface=%s, member=%s", path, interface, member);

    if (!pa_streq(path, OBJECT_PATH))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (dbus_message_is_method_call(m, "org.freedesktop.DBus.Introspectable", "Introspect")) {
        return handle_introspect(c, m, u);
    } else if (dbus_message_is_method_call(m, "org.freedesktop.DBus.Properties", "Get")) {
        return handle_get_property(c, m, u);
    } else if (dbus_message_is_method_call(m, "org.freedesktop.DBus.Properties", "Set")) {
        return  handle_set_property(c, m, u);
    } else if (dbus_message_is_method_call(m, "org.freedesktop.DBus.Properties", "GetAll")) {
        return handle_get_all_property(c, m, u);
    } else {
        return handle_policy_methods(c, m, u);
    }

    return DBUS_HANDLER_RESULT_HANDLED;
}

static void endpoint_init(struct userdata *u)
{
    static const DBusObjectPathVTable vtable_endpoint = {
        .message_function = method_call_handler,
    };

    pa_log_debug("Dbus endpoint init");

    if (u && u->dbus_conn) {
        if (!dbus_connection_register_object_path(pa_dbus_connection_get(u->dbus_conn), OBJECT_PATH, &vtable_endpoint, u))
            pa_log_error("Failed to register object path");
    } else {
        pa_log_error("Cannot get dbus connection to register object path");
    }
}

static void endpoint_done(struct userdata* u)
{
    pa_log_debug("Dbus endpoint done");
    if (u && u->dbus_conn) {
        if (!dbus_connection_unregister_object_path(pa_dbus_connection_get(u->dbus_conn), OBJECT_PATH))
            pa_log_error("Failed to unregister object path");
    } else {
        pa_log_error("Cannot get dbus connection to unregister object path");
    }
}

static int watch_signals(struct userdata* u)
{
    DBusError error;

    dbus_error_init(&error);

    pa_log_debug("Watch Dbus signals");

    if (u && u->dbus_conn) {

        if (!dbus_connection_add_filter(pa_dbus_connection_get(u->dbus_conn), dbus_filter_audio_handler, u, NULL)) {
            pa_log_error("Unable to add D-Bus filter : %s: %s", error.name, error.message);
            goto fail;
        }

        if (pa_dbus_add_matches(pa_dbus_connection_get(u->dbus_conn), &error, SOUND_SERVER_FILTER, AUDIO_CLIENT_FILTER, NULL) < 0) {
            pa_log_error("Unable to subscribe to signals: %s: %s", error.name, error.message);
            goto fail;
        }
        return 0;
    }

fail:
    dbus_error_free(&error);
    return -1;
}

static void unwatch_signals(struct userdata* u)
{
    pa_log_debug("Unwatch Dbus signals");

    if (u && u->dbus_conn) {
        pa_dbus_remove_matches(pa_dbus_connection_get(u->dbus_conn), SOUND_SERVER_FILTER, AUDIO_CLIENT_FILTER, NULL);
        dbus_connection_remove_filter(pa_dbus_connection_get(u->dbus_conn), dbus_filter_audio_handler, u);
    }
}



static void dbus_init(struct userdata* u)
{
    DBusError error;
    pa_dbus_connection *connection = NULL;

    pa_log_debug("Dbus init");
    dbus_error_init(&error);

    if (!(connection = pa_dbus_bus_get(u->core, DBUS_BUS_SYSTEM, &error)) || dbus_error_is_set(&error)) {
        if (connection) {
            pa_dbus_connection_unref(connection);
        }
        pa_log_error("Unable to contact D-Bus system bus: %s: %s", error.name, error.message);
        goto fail;
    } else {
        pa_log_debug("Got dbus connection");
    }

    u->dbus_conn = connection;

    if (watch_signals(u) < 0)
        pa_log_error("dbus watch signals failed");
    else
        pa_log_debug("dbus ready to get signals");

    endpoint_init(u);

fail:
    dbus_error_free(&error);

}

static void dbus_deinit(struct userdata* u)
{
    pa_log_debug("Dbus deinit");
    if (u) {

        endpoint_done(u);
        unwatch_signals(u);

        if (u->dbus_conn) {
            pa_dbus_connection_unref(u->dbus_conn);
            u->dbus_conn = NULL;
        }
    }
}
#endif

int pa__init(pa_module *m)
{
    pa_modargs *ma = NULL;
    struct userdata *u;
    char *args = NULL;

    pa_assert(m);

    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log("Failed to parse module arguments");
        goto fail;
    }

    m->userdata = u = pa_xnew0(struct userdata, 1);
    u->core = m->core;
    u->module = m;

#ifdef HAVE_DBUS
    u->dbus_conn = NULL;
    u->test_property1 = 123;
#endif

    u->hal_manager = pa_hal_manager_get(u->core);

    u->communicator.comm = pa_communicator_get(u->core);
    if (u->communicator.comm) {
        u->communicator.comm_hook_select_proper_sink_or_source_slot = pa_hook_connect(
                pa_communicator_hook(u->communicator.comm, PA_COMMUNICATOR_HOOK_SELECT_INIT_SINK_OR_SOURCE),
                PA_HOOK_EARLY, (pa_hook_cb_t)select_proper_sink_or_source_hook_cb, u);
        u->communicator.comm_hook_change_route_slot = pa_hook_connect(
                pa_communicator_hook(u->communicator.comm, PA_COMMUNICATOR_HOOK_CHANGE_ROUTE),
                PA_HOOK_EARLY, (pa_hook_cb_t)route_change_hook_cb, u);
        u->communicator.comm_hook_update_route_option_slot = pa_hook_connect(
                pa_communicator_hook(u->communicator.comm, PA_COMMUNICATOR_HOOK_UPDATE_ROUTE_OPTION),
                PA_HOOK_EARLY, (pa_hook_cb_t)route_option_update_hook_cb, u);
        u->communicator.comm_hook_device_connection_changed_slot = pa_hook_connect(
                pa_communicator_hook(u->communicator.comm, PA_COMMUNICATOR_HOOK_DEVICE_CONNECTION_CHANGED),
                PA_HOOK_EARLY, (pa_hook_cb_t)device_connection_changed_hook_cb, u);
    }
    u->device_manager = pa_device_manager_get(u->core);

    u->stream_manager = pa_stream_manager_init(u->core);

    /* load null sink/source */
    args = pa_sprintf_malloc("sink_name=%s", SINK_NAME_NULL);
    u->module_null_sink = pa_module_load(u->core, MODULE_NULL_SINK, args);
    pa_xfree(args);
    args = pa_sprintf_malloc("source_name=%s", SOURCE_NAME_NULL);
    u->module_null_source = pa_module_load(u->core, MODULE_NULL_SOURCE, args);
    pa_xfree(args);

    __load_dump_config(u);

#ifdef HAVE_DBUS
    dbus_init(u);
#endif

    pa_log_info("policy module is loaded\n");

    if (ma)
        pa_modargs_free(ma);

    return 0;

fail:
    pa__done(m);

    return -1;
}

void pa__done(pa_module *m)
{
    struct userdata* u;

    pa_assert(m);

    if (!(u = m->userdata))
        return;

    pa_module_unload(u->core, u->module_null_sink, TRUE);
    u->module_null_sink = NULL;
    pa_module_unload(u->core, u->module_null_source, TRUE);
    u->module_null_source = NULL;

#ifdef HAVE_DBUS
    dbus_deinit(u);
#endif
    if (u->device_manager)
        pa_device_manager_unref(u->device_manager);

    if (u->stream_manager)
        pa_stream_manager_done(u->stream_manager);

    if (u->communicator.comm) {
        if (u->communicator.comm_hook_select_proper_sink_or_source_slot)
            pa_hook_slot_free(u->communicator.comm_hook_select_proper_sink_or_source_slot);
        if (u->communicator.comm_hook_change_route_slot)
            pa_hook_slot_free(u->communicator.comm_hook_change_route_slot);
        if (u->communicator.comm_hook_update_route_option_slot)
            pa_hook_slot_free(u->communicator.comm_hook_update_route_option_slot);
        if (u->communicator.comm_hook_device_connection_changed_slot)
            pa_hook_slot_free(u->communicator.comm_hook_device_connection_changed_slot);
        pa_communicator_unref(u->communicator.comm);
    }

    if (u->hal_manager)
        pa_hal_manager_unref(u->hal_manager);

    pa_xfree(u);


    pa_log_info("policy module is unloaded\n");
}
