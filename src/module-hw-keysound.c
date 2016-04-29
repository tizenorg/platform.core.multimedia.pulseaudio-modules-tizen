/***
  This file is part of PulseAudio.

  Copyright 2016 Seungbae Shin <seungbae.shin@samsung.com>

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
#include <pulsecore/namereg.h>
#include <pulsecore/sink.h>
#include <pulsecore/modargs.h>
#include <pulsecore/macro.h>
#include <pulsecore/core-util.h>
#include <pulsecore/core-error.h>
#include <pulsecore/dbus-shared.h>
#include <pulsecore/protocol-dbus.h>
#include <pulsecore/dbus-util.h>
#include <pulsecore/core-scache.h>

#include <vconf.h>

#include "module-hw-keysound-symdef.h"

PA_MODULE_AUTHOR("Seungbae Shin");
PA_MODULE_DESCRIPTION("H/W Keysound play module");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(true);

#define INTERFACE_NAME "org.tizen.system.deviced.Key"
#define SIGNAL_NAME    "ChangeHardkey"
#define DBUS_HW_KEYTONE "/usr/share/sounds/sound-server/Tizen_HW_Touch.ogg"
#define FILTER_HARDKEY  "type='signal', interface='org.tizen.system.deviced.Key', member='ChangeHardkey'"
#define MAX_NAME_LEN 256
#define DEFAULT_ROLE "system"
#define DEFAULT_VOLUME_GAIN "touch"

struct userdata {
    pa_module *module;
    pa_dbus_connection *dbus_conn;
};

static void _hw_keysound_play(struct userdata *u, const char *file_path, const char *role, const char *vol_gain_type) {
    int ret = 0;
    pa_sink *sink = NULL;
    pa_proplist *p = NULL;
    const char *name_prefix = "HWKEY";
    char name[MAX_NAME_LEN] = {0};
    uint32_t stream_idx = 0;
    uint32_t scache_idx = 0;

    /* Set role type / volume gain of stream */
    p = pa_proplist_new();
    if (!p) {
        pa_log_error("failed to create proplist...");
        return;
    }
    if (role)
        pa_proplist_sets(p, PA_PROP_MEDIA_ROLE, role);
    if (vol_gain_type)
        pa_proplist_sets(p, PA_PROP_MEDIA_TIZEN_VOLUME_GAIN_TYPE, vol_gain_type);

    pa_log_debug("role[%s], volume_gain_type[%s]", role, vol_gain_type);

    /* Load if not cached */
    snprintf(name, sizeof(name)-1, "%s_%s", name_prefix, file_path);

    scache_idx = pa_scache_get_id_by_name(u->module->core, name);
    if (scache_idx != PA_IDXSET_INVALID) {
        pa_log_debug("found cached index [%u] for name [%s]", scache_idx, file_path);
    } else {
        /* for more precision, need to update volume value here */
        if ((ret = pa_scache_add_file_lazy(u->module->core, name, file_path, &scache_idx)) != 0) {
            pa_log_error("failed to add file [%s]", file_path);
            goto exit;
        }
        pa_log_debug("success to add file [%s], index [%u]", file_path, scache_idx);
    }

    /* Play sample */
    pa_log_debug("pa_scache_play_item() start");
    sink = pa_namereg_get_default_sink(u->module->core);
    ret = pa_scache_play_item(u->module->core, name, sink, PA_VOLUME_NORM, p, &stream_idx);
    if (ret < 0) {
        pa_log_error("pa_scache_play_item fail, ret[%d]", ret);
        goto exit;
    }
    pa_log_debug("pa_scache_play_item() end, stream_idx(%u)", stream_idx);

exit:
    pa_proplist_free(p);
}

static bool _is_mute_sound () {
    int setting_sound_status = true;
    int setting_touch_sound = true;

    vconf_get_bool(VCONFKEY_SETAPPL_SOUND_STATUS_BOOL, &setting_sound_status);
    vconf_get_bool(VCONFKEY_SETAPPL_TOUCH_SOUNDS_BOOL, &setting_touch_sound);

    return !(setting_sound_status & setting_touch_sound);
}

static DBusHandlerResult _dbus_filter_device_detect_handler(DBusConnection *c, DBusMessage *s, void *userdata) {
    DBusError error;
    struct userdata *u = (struct userdata *)userdata;

    pa_assert(u);

    if (dbus_message_get_type(s) != DBUS_MESSAGE_TYPE_SIGNAL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    pa_log_info("DBus device detect handler received msg");

    pa_log_debug("path       : %s", dbus_message_get_path(s));
    pa_log_debug("interface  : %s", dbus_message_get_interface(s));
    pa_log_debug("member     : %s", dbus_message_get_member(s));
    pa_log_debug("signature : %s", dbus_message_get_signature(s));

    dbus_error_init(&error);

    if (dbus_message_is_signal(s, INTERFACE_NAME, SIGNAL_NAME)) {
        if (_is_mute_sound()) {
            pa_log_debug("Skip playing keytone due to mute sound mode");
        } else {
            _hw_keysound_play(u, DBUS_HW_KEYTONE, DEFAULT_ROLE, DEFAULT_VOLUME_GAIN);
        }
    } else {
        pa_log_info("Unknown message, do not handle this");
        dbus_error_free(&error);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    pa_log_debug("Dbus Message handled");

    dbus_error_free(&error);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static int _watch_signals(struct userdata *u) {
    DBusError error;

    pa_assert(u);
    pa_assert(u->dbus_conn);

    dbus_error_init(&error);

    pa_log_debug("Watch Dbus signals");

    if (!dbus_connection_add_filter(pa_dbus_connection_get(u->dbus_conn), _dbus_filter_device_detect_handler, u, NULL)) {
        pa_log_error("Unable to add D-Bus filter : %s: %s", error.name, error.message);
        goto fail;
    }

    if (pa_dbus_add_matches(pa_dbus_connection_get(u->dbus_conn), &error, FILTER_HARDKEY, NULL) < 0) {
        pa_log_error("Unable to subscribe to signals: %s: %s", error.name, error.message);
        goto fail;
    }
    return 0;

fail:
    dbus_error_free(&error);
    return -1;
}

static void _unwatch_signals(struct userdata *u) {
    pa_log_debug("Unwatch Dbus signals");

    pa_assert(u);
    pa_assert(u->dbus_conn);

    pa_dbus_remove_matches(pa_dbus_connection_get(u->dbus_conn), FILTER_HARDKEY, NULL);
    dbus_connection_remove_filter(pa_dbus_connection_get(u->dbus_conn), _dbus_filter_device_detect_handler, u);
}

static int _dbus_init(struct userdata *u) {
    DBusError error;
    pa_dbus_connection *connection = NULL;

    pa_assert(u);

    pa_log_debug("Dbus init");
    dbus_error_init(&error);

    if (!(connection = pa_dbus_bus_get(u->module->core, DBUS_BUS_SYSTEM, &error)) || dbus_error_is_set(&error)) {
        if (connection) {
            pa_dbus_connection_unref(connection);
        }
        pa_log_error("Unable to contact D-Bus system bus: %s: %s", error.name, error.message);
        goto fail;
    }
    pa_log_debug("Got dbus connection %p", connection);
    u->dbus_conn = connection;

    if (_watch_signals(u) < 0)
        pa_log_error("dbus watch signals failed");
    else
        pa_log_debug("dbus ready to get signals");

    return 0;

fail:
    dbus_error_free(&error);
    return -1;
}

static void _dbus_deinit(struct userdata *u) {
    pa_assert(u);

    pa_log_debug("Dbus deinit");

    _unwatch_signals(u);

    if (u->dbus_conn) {
        pa_dbus_connection_unref(u->dbus_conn);
        u->dbus_conn = NULL;
    }
}

int pa__init(pa_module *m) {
    struct userdata *u;

    pa_assert(m);

    m->userdata = u = pa_xnew0(struct userdata, 1);
    u->module = m;
    u->dbus_conn = NULL;

    if (_dbus_init(u) == -1) {
        pa__done(m);
        return -1;
    }
    return 0;
}

void pa__done(pa_module *m) {
    struct userdata *u;

    pa_assert(m);

    if (!(u = m->userdata))
        return;

    _dbus_deinit(u);

    pa_xfree(u);
}
