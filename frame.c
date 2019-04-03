/*
 * Author: Pu-Chen Mao
 * Date:   2016/05/12
 * File:   frame.c
 * Desc:   Media frame composed of RTP packets
 */

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

#include "frame.h"

static void frame_order_packets(GQueue *packets);
static bool frame_check_completeness(frame_t *frame);
static void frame_foreach_packet(gpointer data, gpointer userdata);

frame_t *
frame_create(uint32_t timestamp,
             codec_t  codec)
{
    frame_t *frame  = NULL;
    bool     result = false;

    frame = g_try_new0(frame_t, 1);
    if (!frame)
        goto RETURN;

    frame->packets = g_queue_new();
    if (!frame->packets)
        goto RETURN;

    frame->created_us = g_get_monotonic_time();
    frame->codec = codec;
    frame->timestamp = timestamp;
    frame->marker = false;
    frame->completed = false;

    result = true;

RETURN:

    if (!result)
        g_clear_pointer(&frame, frame_destroy);

    return frame;
}

bool
frame_add_packet(frame_t  *frame,
                 packet_t *packet,
                 bool     *completed)
{
    const format_t *format    = NULL;
    uint32_t        timestamp = 0;
    bool            result    = false;

    g_return_val_if_fail(frame != NULL, false);
    g_return_val_if_fail(frame->packets != NULL, false);
    g_return_val_if_fail(packet != NULL, false);
    g_return_val_if_fail(packet->rtp != NULL, false);
    g_return_val_if_fail(completed != NULL, false);

    timestamp = ntohl((packet->rtp->header).timestamp);
    if (timestamp != frame->timestamp)
        goto RETURN;

    format = format_get_reassembly_context(frame->codec);
    if (!format)
        goto RETURN;

    g_queue_push_tail(frame->packets, packet);
    if ((packet->rtp->header).marker ||
        format->last_unit(packet->rtp->payload, packet->length))
    {
        frame->marker = true;
        if (g_queue_get_length(frame->packets) > 1)
            frame_order_packets(frame->packets);
        frame->completed = frame_check_completeness(frame);
    }

    *completed = frame->completed;
    result = true;

RETURN:

    return result;
}

bool
frame_reassemble(frame_t *frame,
                 media_t *media,
                 bool     completed,
                 void    *data)
{
    packet_t       *packet  = NULL;
    const format_t *format  = NULL;
    const uint8_t  *payload = NULL;
    const uint8_t  *limit   = NULL;
    uint8_t        *index   = NULL;
    size_t          size    = 0;
    bool            result  = false;

    g_return_val_if_fail(NULL != frame, false);
    g_return_val_if_fail(NULL != frame->packets, false);
    g_return_val_if_fail(NULL != media, false);
    g_return_val_if_fail(NULL != media->buffer, false);
    g_return_val_if_fail(NULL != data, false);
    g_return_val_if_fail(0 < media->length, false);

    index = media->buffer;
    limit = media->buffer + media->length;
    media->length = 0;

    format = format_get_reassembly_context(frame->codec);
    if (!format)
        goto RETURN;

    while (!g_queue_is_empty(frame->packets))
    {
        packet = (packet_t *)(g_queue_pop_head(frame->packets));
        if (!packet)
            goto RETURN;

        result = packet_get_payload(packet, &payload, &size);
        if (!result || !payload || size <= 0)
            goto RETURN;

        if (frame->unitcount == 0)
            media->head_seq = ntohs((packet->rtp->header).sequence);

        /* NOTE: we MUST use payload returned from packet_get_payload() here,
         * since packet->rtp->payload does not skip RTP padding at the end */
        result = format->reassemble(&index, &(media->length), limit,
                media->prefix, payload, size, completed, data);
        if (!result)
            goto RETURN;

        ++(frame->unitcount);
        media->tail_seq = ntohs((packet->rtp->header).sequence);
        g_clear_pointer(&packet, packet_destroy);
    }

    media->is_audio = (frame->codec == CODEC_OPUS);
    media->type = format->frame_type(media->buffer, media->length);
    media->created_us = frame->created_us;
    media->rtptime = frame->timestamp;

    result = true;

RETURN:

    g_clear_pointer(&packet, packet_destroy);

    return result;
}

#ifdef DEBUG
void
frame_print_packets(frame_t *frame)
{
    g_return_if_fail(NULL != frame);

    printf("[ ");
    g_queue_foreach(frame->packets, packet_print_info, NULL);
    printf("]\n");
}
#endif

void
frame_destroy(gpointer data)
{
    frame_t *frame = NULL;

    g_return_if_fail(NULL != data);

    frame = (frame_t *)(data);
    if (frame->packets)
        g_queue_free_full(frame->packets, packet_destroy);

    g_clear_pointer(&frame, g_free);
}

static void
frame_order_packets(GQueue *packets)
{
    g_return_if_fail(NULL != packets);

    g_queue_sort(packets, packet_compare_sequence, NULL);
}

static bool
frame_check_completeness(frame_t *frame)
{
    packet_t       *head     = NULL;
    packet_t       *tail     = NULL;
    const format_t *format   = NULL;
    uint32_t        sequence = 0;
    bool            result   = false;

    g_return_val_if_fail(NULL != frame, false);
    g_return_val_if_fail(NULL != frame->packets, false);

    format = format_get_reassembly_context(frame->codec);
    g_return_val_if_fail(NULL != format, false);

    head = (packet_t *)(g_queue_peek_head(frame->packets));
    g_return_val_if_fail(NULL != head, false);
    g_return_val_if_fail(NULL != head->rtp, false);
    tail = (packet_t *)(g_queue_peek_tail(frame->packets));
    g_return_val_if_fail(NULL != tail, false);
    g_return_val_if_fail(NULL != tail->rtp, false);

    if (!format->first_unit(head->rtp->payload, head->length))
        return false;
    if (!format->last_unit(tail->rtp->payload, tail->length))
        return false;
    if (head == tail)
        return !format->fragmented(head->rtp->payload, head->length);
    g_queue_foreach(frame->packets, frame_foreach_packet, &sequence);
    result = ntohs((tail->rtp->header).sequence) == sequence;

    return result;
}

static void
frame_foreach_packet(gpointer data,
                     gpointer userdata)
{
    packet_t *packet   = NULL;
    uint16_t *prevseq  = 0;
    uint16_t  sequence = 0;

    g_return_if_fail(NULL != data);
    g_return_if_fail(NULL != userdata);

    packet = (packet_t *)(data);
    prevseq = (uint16_t *)(userdata);
    sequence = ntohs((packet->rtp->header).sequence);
    if (*prevseq == sequence - 1 || *prevseq == 0)
        *prevseq = sequence;
}

