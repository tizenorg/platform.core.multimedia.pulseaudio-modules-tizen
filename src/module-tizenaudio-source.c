/***
  This file is part of PulseAudio.

  Copyright (c) 2015 Samsung Electronics Co., Ltd. All rights reserved.

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

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#include <pulse/rtclock.h>
#include <pulse/timeval.h>
#include <pulse/xmalloc.h>

#include <pulsecore/i18n.h>
#include <pulsecore/macro.h>
#include <pulsecore/source.h>
#include <pulsecore/module.h>
#include <pulsecore/core-util.h>
#include <pulsecore/modargs.h>
#include <pulsecore/log.h>
#include <pulsecore/thread.h>
#include <pulsecore/thread-mq.h>
#include <pulsecore/rtpoll.h>
#include <pulsecore/poll.h>

#include "hal-manager.h"
#include "module-tizenaudio-source-symdef.h"


PA_MODULE_AUTHOR("Tizen");
PA_MODULE_DESCRIPTION("Tizen Audio Source");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(false);
PA_MODULE_USAGE(
        "name=<name of the source, to be prefixed> "
        "source_name=<name of source> "
        "source_properties=<properties for the source> "
        "namereg_fail=<when false attempt to synthesise new source_name if it is already taken> "
        "device=<ALSA device> "
        "device_id=<ALSA card index> "
        "format=<sample format> "
        "rate=<sample rate> "
        "channels=<number of channels> "
        "channel_map=<channel map>"
        "fragments=<number of fragments> "
        "fragment_size=<fragment size> ");


#define DEFAULT_SOURCE_NAME "tizenaudio-source"

/* BLOCK_USEC should be less than amount of fragment_size * fragments */
#define BLOCK_USEC (PA_USEC_PER_SEC * 0.032)

struct userdata {
    pa_core *core;
    pa_module *module;
    pa_source *source;

    pa_thread *thread;
    pa_thread_mq thread_mq;
    pa_rtpoll *rtpoll;

    void *pcm_handle;
    uint32_t nfrags;
    uint32_t frag_size;

    pa_usec_t block_usec;
    pa_usec_t timestamp;

    char* device_name;
    bool first;

    pa_rtpoll_item *rtpoll_item;

    uint64_t read_count;
    pa_usec_t latency_time;
    pa_hal_manager *hal_manager;
};

static const char* const valid_modargs[] = {
    "name",
    "source_name",
    "source_properties",
    "namereg_fail",
    "device",
    "device_id",
    "format",
    "rate",
    "channels",
    "channel_map",
    "fragments",
    "fragment_size",
    NULL
};

static int build_pollfd(struct userdata *u) {
    int32_t ret;
    struct pollfd *pollfd;
    int fd = -1;

    pa_assert(u);
    pa_assert(u->pcm_handle);
    pa_assert(u->rtpoll);

    if (u->rtpoll_item)
        pa_rtpoll_item_free(u->rtpoll_item);

    u->rtpoll_item = pa_rtpoll_item_new(u->rtpoll, PA_RTPOLL_NEVER, 1);
    pollfd = pa_rtpoll_item_get_pollfd(u->rtpoll_item, NULL);
    ret = pa_hal_manager_pcm_get_fd(u->hal_manager, u->pcm_handle, &fd);
    if (ret < 0 || fd < 0) {
        pa_log_error("Failed to get fd(%d) of PCM device %d", fd, ret);
        return -1;
    }
    pollfd->fd = fd;
    pollfd->events = /* POLLIN | */ POLLERR | POLLNVAL;

    return 0;
}

/* Called from IO context */
static int suspend(struct userdata *u) {
    int32_t ret;
    pa_assert(u);
    pa_assert(u->pcm_handle);

    ret = pa_hal_manager_pcm_close(u->hal_manager, u->pcm_handle);
    if (ret) {
        pa_log_error("Error closing PCM device %x", ret);
    }
    u->pcm_handle = NULL;

    if (u->rtpoll_item) {
        pa_rtpoll_item_free(u->rtpoll_item);
        u->rtpoll_item = NULL;
    }

    pa_log_info("Device suspended...[%s]", u->device_name);

    return 0;
}

/* Called from IO context */
static int unsuspend(struct userdata *u) {
    pa_sample_spec sample_spec;
    int32_t ret;
    pa_assert(u);
    pa_assert(!u->pcm_handle);

    pa_log_info("Trying resume...");
    sample_spec = u->source->sample_spec;
    ret = pa_hal_manager_pcm_open(u->hal_manager,
              (void **)&u->pcm_handle,
              DIRECTION_IN,
              &sample_spec,
              u->frag_size / pa_frame_size(&sample_spec),
              u->nfrags);
    if (ret) {
        pa_log_error("Error opening PCM device %x", ret);
        goto fail;
    }

    if (build_pollfd(u) < 0)
        goto fail;

    u->read_count = 0;
    u->first = true;

    pa_log_info("Resumed successfully...");

    return 0;

fail:
    if (u->pcm_handle) {
        pa_hal_manager_pcm_close(u->hal_manager, u->pcm_handle);
        u->pcm_handle = NULL;
    }
    return -PA_ERR_IO;
}

static int source_process_msg(
        pa_msgobject *o,
        int code,
        void *data,
        int64_t offset,
        pa_memchunk *chunk) {

    struct userdata *u = PA_SOURCE(o)->userdata;
    int r;

    switch (code) {
        case PA_SOURCE_MESSAGE_SET_STATE:
            switch ((pa_source_state_t)PA_PTR_TO_UINT(data)) {
                case PA_SOURCE_SUSPENDED: {
                    pa_assert(PA_SOURCE_IS_OPENED(u->source->thread_info.state));
                    if ((r = suspend(u)) < 0)
                        return r;

                    break;
                }

                case PA_SOURCE_IDLE:
                case PA_SOURCE_RUNNING: {
                    if (u->source->thread_info.state == PA_SOURCE_INIT) {
                        if (build_pollfd(u) < 0)
                            return -PA_ERR_IO;
                    }

                    if (u->source->thread_info.state == PA_SOURCE_SUSPENDED) {
                        if ((r = unsuspend(u)) < 0)
                            return r;
                    }
                    break;
                }

                case PA_SOURCE_UNLINKED:
                case PA_SOURCE_INIT:
                case PA_SOURCE_INVALID_STATE:
                    break;
            }
            break;

        case PA_SOURCE_MESSAGE_GET_LATENCY: {
            pa_usec_t now = pa_rtclock_now();
            *((pa_usec_t*)data) = u->timestamp > now ? 0ULL : now - u->timestamp;
            return 0;
        }
    }

    return pa_source_process_msg(o, code, data, offset, chunk);
}

static void source_update_requested_latency_cb(pa_source *s) {
    struct userdata *u;
    size_t nbytes;

    pa_source_assert_ref(s);
    pa_assert_se(u = s->userdata);

    u->block_usec = pa_source_get_requested_latency_within_thread(s);

    if (u->block_usec == (pa_usec_t)-1)
        u->block_usec = s->thread_info.max_latency;

    nbytes = pa_usec_to_bytes(u->block_usec, &s->sample_spec);
    pa_source_set_max_rewind_within_thread(s, nbytes);
}

static int process_render(struct userdata *u, pa_usec_t now) {
    int work_done = 0;
    size_t ate = 0;
    void *p;
    size_t frames_to_read, frame_size;
    uint32_t avail = 0;

    pa_assert(u);

    /* Fill the buffer up the latency size */
    while (u->timestamp < now + u->block_usec) {
        pa_memchunk chunk;
        frame_size = pa_frame_size(&u->source->sample_spec);

        pa_hal_manager_pcm_available(u->hal_manager, u->pcm_handle, &avail);
        if (avail == 0) {
            break;
        }

        frames_to_read = pa_usec_to_bytes(u->block_usec, &u->source->sample_spec) / frame_size;

        chunk.length = pa_usec_to_bytes(u->block_usec, &u->source->sample_spec);
        chunk.memblock = pa_memblock_new(u->core->mempool, chunk.length);

        if (frames_to_read > (size_t)avail)
            frames_to_read = (size_t)avail;

        p = pa_memblock_acquire(chunk.memblock);
        pa_hal_manager_pcm_read(u->hal_manager, u->pcm_handle, p, (uint32_t)frames_to_read);
        pa_memblock_release(chunk.memblock);

        chunk.index = 0;
        chunk.length = (size_t)frames_to_read * frame_size;
        pa_source_post(u->source, &chunk);
        pa_memblock_unref(chunk.memblock);

        u->timestamp += pa_bytes_to_usec(chunk.length, &u->source->sample_spec);

        work_done = 1;

        ate += chunk.length;
        if (ate >= pa_usec_to_bytes(u->block_usec, &u->source->sample_spec)) {
            break;
        }
    }

    return work_done;
}

static void thread_func(void *userdata) {
    struct userdata *u = userdata;
    unsigned short revents = 0;

    pa_assert(u);
    pa_log_debug("Thread starting up");

    if (u->core->realtime_scheduling)
        pa_make_realtime(u->core->realtime_priority);

    pa_thread_mq_install(&u->thread_mq);

    for (;;) {
        pa_usec_t now = 0;
        int ret;

        if (PA_SOURCE_IS_OPENED(u->source->thread_info.state))
            now = pa_rtclock_now();

        /* Render some data and drop it immediately */
        if (PA_SOURCE_IS_OPENED(u->source->thread_info.state)) {
            int work_done;

            if (u->first) {
                pa_log_info("Starting capture.");
                pa_hal_manager_pcm_start(u->hal_manager, u->pcm_handle);
                u->first = false;
                u->timestamp = now;
            }

            work_done = process_render(u, now);

            if (work_done < 0)
                goto fail;

            if (work_done == 0) {
                pa_rtpoll_set_timer_relative(u->rtpoll, (10 * PA_USEC_PER_MSEC));
            } else {
                pa_rtpoll_set_timer_absolute(u->rtpoll, u->timestamp);
            }
        } else {
            pa_rtpoll_set_timer_disabled(u->rtpoll);
        }

        /* Hmm, nothing to do. Let's sleep */
        if ((ret = pa_rtpoll_run(u->rtpoll)) < 0)
            goto fail;

        if (ret == 0)
            goto finish;

        if (PA_SOURCE_IS_OPENED(u->source->thread_info.state)) {
            struct pollfd *pollfd;
            if (u->rtpoll_item) {
                pollfd = pa_rtpoll_item_get_pollfd(u->rtpoll_item, NULL);
                revents = pollfd->revents;
                if (revents & ~POLLIN) {
                    pa_log_debug("Poll error 0x%x occured, try recover.", revents);
                    pa_hal_manager_pcm_recover(u->hal_manager, u->pcm_handle, revents);
                    u->first = true;
                    revents = 0;
                } else {
                    //pa_log_debug("Poll wakeup.", revents);
                }
            }
        }
    }

fail:
    /* If this was no regular exit from the loop we have to continue
     * processing messages until we received PA_MESSAGE_SHUTDOWN */
    pa_asyncmsgq_post(u->thread_mq.outq, PA_MSGOBJECT(u->core), PA_CORE_MESSAGE_UNLOAD_MODULE, u->module, 0, NULL, NULL);
    pa_asyncmsgq_wait_for(u->thread_mq.inq, PA_MESSAGE_SHUTDOWN);

finish:
    pa_log_debug("Thread shutting down");
}

int pa__init(pa_module*m) {
    struct userdata *u = NULL;
    pa_sample_spec ss;
    pa_channel_map map;
    pa_modargs *ma = NULL;
    pa_source_new_data data;
    uint32_t alternate_sample_rate;

    pa_assert(m);

    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log_error("Failed to parse module arguments.");
        goto fail;
    }

    ss = m->core->default_sample_spec;
    map = m->core->default_channel_map;
    if (pa_modargs_get_sample_spec_and_channel_map(ma, &ss, &map, PA_CHANNEL_MAP_DEFAULT) < 0) {
        pa_log_error("Invalid sample format specification or channel map");
        goto fail;
    }

    alternate_sample_rate = m->core->alternate_sample_rate;
    if (pa_modargs_get_alternate_sample_rate(ma, &alternate_sample_rate) < 0) {
        pa_log_error("Failed to parse alternate sample rate");
        goto fail;
    }

    m->userdata = u = pa_xnew0(struct userdata, 1);
    u->core = m->core;
    u->module = m;
    u->first = true;
    u->hal_manager = pa_hal_manager_get(u->core);
    u->rtpoll = pa_rtpoll_new();
    pa_thread_mq_init(&u->thread_mq, m->core->mainloop, u->rtpoll);

    u->frag_size = 0;
    u->nfrags = 0;
    pa_modargs_get_value_u32(ma, "fragment_size", &u->frag_size);
    pa_modargs_get_value_u32(ma, "fragments", &u->nfrags);
    if (u->frag_size == 0 || u->nfrags == 0) {
        pa_log_error("fragment_size or fragments are invalid.");
        goto fail;
    }

    pa_source_new_data_init(&data);
    data.driver = __FILE__;
    data.module = m;
    pa_source_new_data_set_name(&data, pa_modargs_get_value(ma, "source_name", DEFAULT_SOURCE_NAME));
    pa_source_new_data_set_sample_spec(&data, &ss);
    pa_source_new_data_set_channel_map(&data, &map);
    pa_proplist_sets(data.proplist, PA_PROP_DEVICE_DESCRIPTION, _("Tizen audio source"));
    pa_proplist_sets(data.proplist, PA_PROP_DEVICE_CLASS, "abstract");
    pa_proplist_sets(data.proplist, PA_PROP_DEVICE_STRING, "tizen");
    pa_proplist_sets(data.proplist, PA_PROP_DEVICE_API, "tizen");

    if (pa_modargs_get_proplist(ma, "source_properties", data.proplist, PA_UPDATE_REPLACE) < 0) {
        pa_log_error("Invalid properties.");
        pa_source_new_data_done(&data);
        goto fail;
    }

    u->source = pa_source_new(m->core, &data, PA_SOURCE_LATENCY);
    pa_source_new_data_done(&data);

    if (!u->source) {
        pa_log_error("Failed to create source object.");
        goto fail;
    }

    u->source->parent.process_msg = source_process_msg;
    u->source->update_requested_latency = source_update_requested_latency_cb;
    u->source->userdata = u;

    pa_source_set_asyncmsgq(u->source, u->thread_mq.inq);
    pa_source_set_rtpoll(u->source, u->rtpoll);

    unsuspend(u);

    u->block_usec = BLOCK_USEC;
    u->latency_time = u->block_usec;
    u->timestamp = 0ULL;

    pa_source_set_max_rewind(u->source, 0);

    if (!(u->thread = pa_thread_new("tizenaudio-source", thread_func, u))) {
        pa_log_error("Failed to create thread.");
        goto fail;
    }
    pa_source_set_fixed_latency(u->source, u->block_usec);
    pa_source_put(u->source);
    pa_modargs_free(ma);

    return 0;

fail:
    if (ma)
        pa_modargs_free(ma);

    pa__done(m);
    return -1;
}

int pa__get_n_used(pa_module *m) {
    struct userdata *u;

    pa_assert(m);
    pa_assert_se(u = m->userdata);

    return pa_source_linked_by(u->source);
}

void pa__done(pa_module*m) {
    struct userdata *u;

    pa_assert(m);

    if (!(u = m->userdata))
        return;

    if (u->source)
        pa_source_unlink(u->source);

    if (u->thread) {
        pa_asyncmsgq_send(u->thread_mq.inq, NULL, PA_MESSAGE_SHUTDOWN, NULL, 0, NULL);
        pa_thread_free(u->thread);
    }

    pa_thread_mq_done(&u->thread_mq);

    if (u->source)
        pa_source_unref(u->source);

    if (u->rtpoll)
        pa_rtpoll_free(u->rtpoll);

    pa_xfree(u);
}
