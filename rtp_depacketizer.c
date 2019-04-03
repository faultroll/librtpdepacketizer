/*
 * Author: Pu-Chen Mao
 * Date:   2016/05/12
 * File:   rtp_depacketizer.c
 * Desc:   RTP depacketizer
 */

#include <arpa/inet.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "rtp_depacketizer.h"

static bool rtp_depacketizer_enqueue_packet(rtp_depacketizer_t *depacketizer,
        packet_t *packet, bool *frame_ready);
static gboolean rtp_depacketizer_reap_frame(gpointer key, gpointer val,
        gpointer userdata);
static gboolean rtp_depacketizer_remove_frame(gpointer key, gpointer val,
        gpointer userdata);
static gint rtp_depacketizer_compare_timestamps(gconstpointer lval,
        gconstpointer rval, gpointer data);

#ifdef DEBUG
static void rtp_depacketizer_print_frames(rtp_depacketizer_t *depacketizer);
static void rtp_depacketizer_print_completed(rtp_depacketizer_t *depacketizer);
static void rtp_depacketizer_foreach_frame(gpointer data, gpointer userdata);
#endif

rtp_depacketizer_t *
rtp_depacketizer_create(codec_t codec,
                        gint64  timeout_us,
                        gint64  reap_us)
{
    rtp_depacketizer_t *depacketizer = NULL;
    bool                result       = false;

    depacketizer = g_try_new0(rtp_depacketizer_t, 1);
    if (!depacketizer)
        goto RETURN;

    depacketizer->frames = g_hash_table_new_full(g_direct_hash,
            g_direct_equal, NULL, frame_destroy);
    if (!depacketizer->frames)
        goto RETURN;

    depacketizer->completed = g_queue_new();
    if (!depacketizer->completed)
        goto RETURN;

    depacketizer->codec = codec;
    depacketizer->refresh_us = g_get_monotonic_time();
    depacketizer->timeout_us = timeout_us;
    depacketizer->reap_us = reap_us;

    result = true;

RETURN:

    if (!result)
        g_clear_pointer(&depacketizer, rtp_depacketizer_destroy);

    return depacketizer;
}

/* NOTE: packet ownership is transferred to depacketizer
 * On error, the packet is freed by the depacketizer */
bool
rtp_depacketizer_add_packet(rtp_depacketizer_t *depacketizer,
                            packet_t           *packet,
                            bool               *frame_ready)
{
    g_return_val_if_fail(NULL != depacketizer, false);
    g_return_val_if_fail(NULL != packet, false);
    g_return_val_if_fail(NULL != frame_ready, false);

    return rtp_depacketizer_enqueue_packet(depacketizer, packet, frame_ready);
}

bool
rtp_depacketizer_add_buffer(rtp_depacketizer_t *depacketizer,
                            bool                is_audio,
                            uint8_t            *buffer,
                            size_t              length,
                            bool               *frame_ready)
{
    packet_t *packet = NULL;

    g_return_val_if_fail(NULL != depacketizer, false);
    g_return_val_if_fail(NULL != buffer, false);
    g_return_val_if_fail(0 < length, false);
    g_return_val_if_fail(NULL != frame_ready, false);

    packet = packet_create(buffer, length, is_audio, true);
    if (!packet)
        return false;

    return rtp_depacketizer_enqueue_packet(depacketizer, packet, frame_ready);
}

bool
rtp_depacketizer_get_frame(rtp_depacketizer_t *depacketizer,
                           media_t            *media)
{
    frame_t *frame  = NULL;
    bool     result = false;

    g_return_val_if_fail(NULL != depacketizer, false);
    g_return_val_if_fail(NULL != media, false);
    g_return_val_if_fail(NULL != media->buffer, false);
    g_return_val_if_fail(0 < media->length, false);

    if (g_queue_get_length(depacketizer->completed) <= 0)
        goto RETURN;

    frame = (frame_t *)(g_queue_pop_head(depacketizer->completed));
    if (!frame)
        goto RETURN;

    if (!frame_reassemble(frame, media, frame->completed,
                &(depacketizer->context)))
        goto RETURN;

    if (frame->codec == CODEC_H264)
        media->context = depacketizer->context;

    result = true;

RETURN:

    g_clear_pointer(&frame, frame_destroy);

    return result;
}

void
rtp_depacketizer_destroy(gpointer data)
{
    rtp_depacketizer_t *depacketizer = NULL;

    g_return_if_fail(NULL != data);

    depacketizer = (rtp_depacketizer_t *)(data);
    g_clear_pointer(&(depacketizer->frames), g_hash_table_destroy);
    if (depacketizer->completed)
        g_queue_free_full(depacketizer->completed, frame_destroy);
    g_clear_pointer(&depacketizer, g_free);
}

static bool
rtp_depacketizer_enqueue_packet(rtp_depacketizer_t *depacketizer,
                                packet_t           *packet,
                                bool               *frame_ready)
{
    frame_t  *frame     = NULL;
    gint64    now_us    = 0;
    uint32_t  timestamp = 0;
    bool      new_frame = false;
    bool      completed = false;
    bool      result    = false;

    g_return_val_if_fail(NULL != depacketizer, false);
    g_return_val_if_fail(NULL != depacketizer->frames, false);
    g_return_val_if_fail(NULL != depacketizer->completed, false);
    g_return_val_if_fail(NULL != packet, false);
    g_return_val_if_fail(NULL != packet->rtp, false);
    g_return_val_if_fail(NULL != frame_ready, false);

    depacketizer->enqueue_us = g_get_monotonic_time();
    timestamp = ntohl((packet->rtp->header).timestamp);
    frame = (frame_t *)(g_hash_table_lookup(depacketizer->frames,
                GUINT_TO_POINTER(timestamp)));
    if (!frame)
    {
        frame = frame_create(timestamp, depacketizer->codec);
        if (!frame)
            goto RETURN;
        new_frame = true;
    }

    if (!frame_add_packet(frame, packet, &completed))
        goto RETURN;
    if (new_frame)
        g_hash_table_insert(depacketizer->frames,
                GUINT_TO_POINTER(timestamp), frame);
    g_hash_table_foreach_steal(depacketizer->frames,
            rtp_depacketizer_reap_frame, depacketizer);

    *frame_ready = !g_queue_is_empty(depacketizer->completed);
    result = true;

RETURN:

    if (!result && new_frame)
        g_clear_pointer(&frame, frame_destroy);
    if (now_us - depacketizer->refresh_us > depacketizer->timeout_us)
    {
        g_hash_table_foreach_remove(depacketizer->frames,
                rtp_depacketizer_remove_frame, depacketizer);
        depacketizer->refresh_us = now_us;
#ifdef DEBUG
        rtp_depacketizer_print_frames(depacketizer);
        rtp_depacketizer_print_completed(depacketizer);
#endif
    }

    return result;
}

static gboolean
rtp_depacketizer_reap_frame(gpointer key,
                        gpointer val,
                        gpointer userdata)
{
    rtp_depacketizer_t *depacketizer = NULL;
    frame_t            *frame        = NULL;
    gint64              age_us       = 0;

    g_return_val_if_fail(NULL != key, TRUE);
    g_return_val_if_fail(NULL != val, TRUE);
    g_return_val_if_fail(NULL != userdata, TRUE);

    depacketizer = (rtp_depacketizer_t *)(userdata);
    frame = (frame_t *)(val);
    age_us = depacketizer->enqueue_us - frame->created_us;
    if (frame->completed || age_us > depacketizer->reap_us)
    {
        g_queue_insert_sorted(depacketizer->completed, frame,
                rtp_depacketizer_compare_timestamps, NULL);
        return TRUE;
    }

    return FALSE;
}

static gboolean
rtp_depacketizer_remove_frame(gpointer key,
                          gpointer val,
                          gpointer userdata)
{
    rtp_depacketizer_t *depacketizer = NULL;
    frame_t            *frame        = NULL;
    gint64              age_us       = 0;

    g_return_val_if_fail(NULL != key, TRUE);
    g_return_val_if_fail(NULL != val, TRUE);
    g_return_val_if_fail(NULL != userdata, TRUE);

    depacketizer = (rtp_depacketizer_t *)(userdata);
    frame = (frame_t *)(val);
    age_us = depacketizer->enqueue_us - frame->created_us;
    if (age_us > depacketizer->timeout_us)
        return TRUE;

    return FALSE;
}

static gint
rtp_depacketizer_compare_timestamps(gconstpointer lval,
                                gconstpointer rval,
                                gpointer      data)
{
    frame_t *lframe = NULL;
    frame_t *rframe = NULL;

    g_return_val_if_fail(NULL != lval, 0);
    g_return_val_if_fail(NULL != rval, 0);

    lframe = (frame_t *)(lval);
    rframe = (frame_t *)(rval);

    return (gint64)(lframe->timestamp) - (gint64)(rframe->timestamp);
}

#ifdef DEBUG
static void rtp_depacketizer_print_frames(rtp_depacketizer_t *depacketizer)
{
    GHashTableIter  frame_it  = {};
    uint32_t        timestamp = 0;
    gint64          now_us    = 0;
    float           age       = 0.0;
    frame_t        *frame     = NULL;

    g_return_if_fail(NULL != depacketizer);
    g_return_if_fail(NULL != depacketizer->frames);

    printf("\nIncomplete frames:\n");

    now_us = g_get_monotonic_time();
    g_hash_table_iter_init(&frame_it, depacketizer->frames);
    while (g_hash_table_iter_next(&frame_it, (gpointer *)(&timestamp),
                (gpointer *)(&frame)))
    {
        age = ((float)(now_us) - (float)(frame->created_us)) / 1000000;
        printf("Frame timestamp: [%u], marker: [%u], completed: [%u], "
               "age: [%03.3f] packets: ", frame->timestamp, frame->marker,
               frame->completed, age);
        frame_print_packets(frame);
    }
}

static void rtp_depacketizer_print_completed(rtp_depacketizer_t *depacketizer)
{
    g_return_if_fail(NULL != depacketizer);

    printf("\nCompleted frames:\n");
    g_queue_foreach(depacketizer->completed,
            rtp_depacketizer_foreach_frame, NULL);
}

static void rtp_depacketizer_foreach_frame(gpointer data, gpointer userdata)
{
    frame_t *frame  = NULL;
    gint64   now_us = 0;
    float    age    = 0.0;

    g_return_if_fail(NULL != data);

    frame = (frame_t *)(data);
    now_us = g_get_monotonic_time();
    age = ((float)(now_us) - (float)(frame->created_us)) / 1000000;
    printf("Frame timestamp: [%u], marker: [%u], completed: [%u], "
            "age: [%03.3f], packets: ", frame->timestamp, frame->marker,
            frame->completed, age);
    frame_print_packets(frame);
}
#endif

