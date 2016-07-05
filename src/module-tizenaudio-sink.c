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
#include <pulsecore/sink.h>
#include <pulsecore/module.h>
#include <pulsecore/core-util.h>
#include <pulsecore/modargs.h>
#include <pulsecore/log.h>
#include <pulsecore/thread.h>
#include <pulsecore/thread-mq.h>
#include <pulsecore/rtpoll.h>
#include <pulsecore/poll.h>

#include "hal-interface.h"
#include "module-tizenaudio-sink-symdef.h"


PA_MODULE_AUTHOR("Tizen");
PA_MODULE_DESCRIPTION("Tizen Audio Sink");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(false);
PA_MODULE_USAGE(
        "name=<name of the sink, to be prefixed> "
        "sink_name=<name of sink> "
        "sink_properties=<properties for the sink> "
        "namereg_fail=<when false attempt to synthesise new sink_name if it is already taken> "
        "device=<ALSA device> "
        "device_id=<ALSA card index> "
        "format=<sample format> "
        "rate=<sample rate> "
        "channels=<number of channels> "
        "channel_map=<channel map>"
        "fragments=<number of fragments> "
        "fragment_size=<fragment size> ");

#define DEFAULT_SINK_NAME "tizenaudio-sink"

/* BLOCK_USEC should be less than amount of fragment_size * fragments */
#define BLOCK_USEC (PA_USEC_PER_SEC * 0.032)

struct userdata {
    pa_core *core;
    pa_module *module;
    pa_sink *sink;

    pa_thread *thread;
    pa_thread_mq thread_mq;
    pa_rtpoll *rtpoll;

    void *pcm_handle;
    uint32_t nfrags;
    uint32_t frag_size;

    pa_usec_t block_usec;
    pa_usec_t timestamp;
    pa_usec_t timestamp_written;

    char* device_name;
    bool first;

    pa_rtpoll_item *rtpoll_item;

    uint64_t write_count;
    pa_hal_interface *hal_interface;
};

static const char* const valid_modargs[] = {
    "name",
    "sink_name",
    "sink_properties",
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
    ret = pa_hal_interface_pcm_get_fd(u->hal_interface, u->pcm_handle, &fd);
    if (ret < 0 || fd < 0) {
        pa_log_error("Failed to get fd(%d) of PCM device %d", fd, ret);
        return -1;
    }
    pollfd->fd = fd;
    pollfd->events = /* POLLOUT | */ POLLERR | POLLNVAL;

    return 0;
}

/* Called from IO context */
static int suspend(struct userdata *u) {
    int32_t ret;
    pa_assert(u);
    pa_assert(u->pcm_handle);

    ret = pa_hal_interface_pcm_close(u->hal_interface, u->pcm_handle);
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
    sample_spec = u->sink->sample_spec;
    ret = pa_hal_interface_pcm_open(u->hal_interface,
              (void **)&u->pcm_handle,
              DIRECTION_OUT,
              &sample_spec,
              u->frag_size / pa_frame_size(&sample_spec),
              u->nfrags);
    if (ret) {
        pa_log_error("Error opening PCM device %x", ret);
        goto fail;
    }

    if (build_pollfd(u) < 0)
        goto fail;

    u->write_count = 0;
    u->first = true;

    pa_log_info("Resumed successfully...");

    return 0;

fail:
    if (u->pcm_handle) {
        pa_hal_interface_pcm_close(u->hal_interface, u->pcm_handle);
        u->pcm_handle = NULL;
    }
    return -PA_ERR_IO;
}

static int sink_process_msg(
        pa_msgobject *o,
        int code,
        void *data,
        int64_t offset,
        pa_memchunk *chunk) {

    struct userdata *u = PA_SINK(o)->userdata;
    int r;

    switch (code) {
        case PA_SINK_MESSAGE_SET_STATE:
            switch ((pa_sink_state_t)PA_PTR_TO_UINT(data)) {
                case PA_SINK_SUSPENDED: {
                    pa_assert(PA_SINK_IS_OPENED(u->sink->thread_info.state));
                    if ((r = suspend(u)) < 0)
                        return r;

                    break;
                }

                case PA_SINK_IDLE:
                case PA_SINK_RUNNING: {
                    if (u->sink->thread_info.state == PA_SINK_INIT) {
                        if (build_pollfd(u) < 0)
                            return -PA_ERR_IO;
                    }

                    if (u->sink->thread_info.state == PA_SINK_SUSPENDED) {
                        if ((r = unsuspend(u)) < 0)
                            return r;
                    }
                    break;
                }

                case PA_SINK_UNLINKED:
                case PA_SINK_INIT:
                case PA_SINK_INVALID_STATE:
                    break;
            }
            break;

        case PA_SINK_MESSAGE_GET_LATENCY: {
            pa_usec_t now = pa_rtclock_now();
            pa_usec_t latency = 0ULL;
            if (u->timestamp > now) {
                if ((u->timestamp - now) > (now - u->timestamp_written)) {
                    latency = (u->timestamp - now) + (u->timestamp_written - now);
                }
            }
            *((pa_usec_t*)data) = latency;
            return 0;
        }
    }

    return pa_sink_process_msg(o, code, data, offset, chunk);
}

static void sink_update_requested_latency_cb(pa_sink *s) {
    struct userdata *u;
    size_t nbytes;

    pa_sink_assert_ref(s);
    pa_assert_se(u = s->userdata);

    u->block_usec = pa_sink_get_requested_latency_within_thread(s);

    if (u->block_usec == (pa_usec_t)-1)
        u->block_usec = s->thread_info.max_latency;

    nbytes = pa_usec_to_bytes(u->block_usec, &s->sample_spec);
    pa_sink_set_max_rewind_within_thread(s, nbytes);
    pa_sink_set_max_request_within_thread(s, nbytes);
}

static void process_rewind(struct userdata *u, pa_usec_t now) {
#if 1
    /* Rewind not supported */
    pa_sink_process_rewind(u->sink, 0);
#else
    size_t rewind_nbytes, in_buffer;
    pa_usec_t delay;

    pa_assert(u);

    rewind_nbytes = u->sink->thread_info.rewind_nbytes;

    if (!PA_SINK_IS_OPENED(u->sink->thread_info.state) || rewind_nbytes <= 0)
        goto do_nothing;

    pa_log_debug("Requested to rewind %lu bytes.", (unsigned long)rewind_nbytes);

    if (u->timestamp <= now)
        goto do_nothing;

    delay = u->timestamp - now;
    in_buffer = pa_usec_to_bytes(delay, &u->sink->sample_spec);

    if (in_buffer <= 0)
        goto do_nothing;

    if (rewind_nbytes > in_buffer)
        rewind_nbytes = in_buffer;

    pa_sink_process_rewind(u->sink, rewind_nbytes);
    u->timestamp -= pa_bytes_to_usec(rewind_nbytes, &u->sink->sample_spec);

    pa_log_debug("Rewound %lu bytes.", (unsigned long)rewind_nbytes);
    return;

do_nothing:
    pa_sink_process_rewind(u->sink, 0);
#endif
}

static int process_render(struct userdata *u, pa_usec_t now) {
    int work_done = 0;
    size_t ate = 0;
    void *p;
    size_t frames_to_write, frame_size;
    uint32_t avail = 0;

    pa_assert(u);

    /* Fill the buffer up the latency size */
    while (u->timestamp < now + u->block_usec) {
        pa_memchunk chunk;
        frame_size = pa_frame_size(&u->sink->sample_spec);

        pa_hal_interface_pcm_available(u->hal_interface, u->pcm_handle, &avail);
        if ((avail == 0) && !(u->first)) {
            break;
        }

        if (u->first) {
            pa_log_debug("Fill initial buffer");
            frames_to_write = (u->frag_size * u->nfrags) / frame_size;
            u->timestamp = u->timestamp_written = now;
        } else {
            frames_to_write = u->sink->thread_info.max_request / frame_size;
            if (frames_to_write > avail)
                break;
        }

        pa_sink_render_full(u->sink, frames_to_write * frame_size, &chunk);
        p = pa_memblock_acquire(chunk.memblock);

        pa_hal_interface_pcm_write(u->hal_interface, u->pcm_handle, (const char*)p + chunk.index, (uint32_t)frames_to_write);

        pa_memblock_release(chunk.memblock);
        pa_memblock_unref(chunk.memblock);
        u->timestamp += pa_bytes_to_usec(chunk.length, &u->sink->sample_spec);
        u->timestamp_written = pa_rtclock_now();

        work_done = 1;

        ate += chunk.length;
        if (ate >= u->sink->thread_info.max_request) {
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

        if (PA_SINK_IS_OPENED(u->sink->thread_info.state))
            now = pa_rtclock_now();

        if (PA_UNLIKELY(u->sink->thread_info.rewind_requested))
            process_rewind(u, now);

        /* Render some data and drop it immediately */
        if (PA_SINK_IS_OPENED(u->sink->thread_info.state)) {
            int work_done = process_render(u, now);

            if (work_done < 0)
                goto fail;

            if (work_done == 0) {
                pa_rtpoll_set_timer_relative(u->rtpoll, (10 * PA_USEC_PER_MSEC));
            } else {
                if (u->first) {
                    pa_log_info("Starting playback.");
                    pa_hal_interface_pcm_start(u->hal_interface, u->pcm_handle);
                    u->first = false;
                }
                pa_rtpoll_set_timer_relative(u->rtpoll, (10 * PA_USEC_PER_MSEC));
            }
        } else {
            pa_rtpoll_set_timer_disabled(u->rtpoll);
        }

        /* Hmm, nothing to do. Let's sleep */
        if ((ret = pa_rtpoll_run(u->rtpoll, true)) < 0)
            goto fail;

        if (ret == 0)
            goto finish;

        if (PA_SINK_IS_OPENED(u->sink->thread_info.state)) {
            struct pollfd *pollfd;
            if (u->rtpoll_item) {
                pollfd = pa_rtpoll_item_get_pollfd(u->rtpoll_item, NULL);
                revents = pollfd->revents;
                if (revents & ~POLLOUT) {
                    pa_log_debug("Poll error 0x%x occured, try recover.", revents);
                    pa_hal_interface_pcm_recover(u->hal_interface, u->pcm_handle, revents);
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
    pa_sink_new_data data;
    size_t nbytes;
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
    u->hal_interface = pa_hal_interface_get(u->core);
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
    pa_log_info("fragment_size(%u), fragments(%u)", u->frag_size, u->nfrags);

    pa_sink_new_data_init(&data);
    data.driver = __FILE__;
    data.module = m;
    pa_sink_new_data_set_name(&data, pa_modargs_get_value(ma, "sink_name", DEFAULT_SINK_NAME));
    pa_sink_new_data_set_sample_spec(&data, &ss);
    pa_sink_new_data_set_channel_map(&data, &map);
    pa_proplist_sets(data.proplist, PA_PROP_DEVICE_DESCRIPTION, _("Tizen audio sink"));
    pa_proplist_sets(data.proplist, PA_PROP_DEVICE_CLASS, "abstract");
    pa_proplist_sets(data.proplist, PA_PROP_DEVICE_STRING, "tizen");
    pa_proplist_sets(data.proplist, PA_PROP_DEVICE_API, "tizen");

    if (pa_modargs_get_proplist(ma, "sink_properties", data.proplist, PA_UPDATE_REPLACE) < 0) {
        pa_log_error("Invalid properties.");
        pa_sink_new_data_done(&data);
        goto fail;
    }

    u->sink = pa_sink_new(m->core, &data, PA_SINK_LATENCY);
    pa_sink_new_data_done(&data);

    if (!u->sink) {
        pa_log_error("Failed to create sink object.");
        goto fail;
    }

    u->sink->parent.process_msg = sink_process_msg;
    u->sink->update_requested_latency = sink_update_requested_latency_cb;
    u->sink->userdata = u;

    if (pa_hal_interface_pcm_open(u->hal_interface,
              (void **)&u->pcm_handle,
              DIRECTION_OUT,
              &u->sink->sample_spec,
              u->frag_size / pa_frame_size(&u->sink->sample_spec),
              u->nfrags)) {
        pa_log_error("Error opening PCM device");
        goto fail;
    }

    pa_sink_set_asyncmsgq(u->sink, u->thread_mq.inq);
    pa_sink_set_rtpoll(u->sink, u->rtpoll);

    u->block_usec = BLOCK_USEC;
    u->timestamp = 0ULL;
    u->timestamp_written = 0ULL;

    nbytes = pa_usec_to_bytes(u->block_usec, &u->sink->sample_spec);
    pa_sink_set_max_rewind(u->sink, 0);
    pa_sink_set_max_request(u->sink, nbytes);

    if (!(u->thread = pa_thread_new("tizenaudio-sink", thread_func, u))) {
        pa_log_error("Failed to create thread.");
        goto fail;
    }
    pa_sink_set_fixed_latency(u->sink, u->block_usec);
    pa_sink_put(u->sink);
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

    return pa_sink_linked_by(u->sink);
}

void pa__done(pa_module*m) {
    struct userdata *u;

    pa_assert(m);

    if (!(u = m->userdata))
        return;

    if (u->sink)
        pa_sink_unlink(u->sink);

    if (u->thread) {
        pa_asyncmsgq_send(u->thread_mq.inq, NULL, PA_MESSAGE_SHUTDOWN, NULL, 0, NULL);
        pa_thread_free(u->thread);
    }

    pa_thread_mq_done(&u->thread_mq);

    if (u->sink)
        pa_sink_unref(u->sink);

    if (u->rtpoll)
        pa_rtpoll_free(u->rtpoll);

    if (u->pcm_handle) {
        pa_hal_interface_pcm_stop(u->hal_interface, u->pcm_handle);
        pa_hal_interface_pcm_close(u->hal_interface, u->pcm_handle);
    }

    pa_xfree(u);
}
