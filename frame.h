/*
 * Author: Pu-Chen Mao
 * Date:   2016/05/12
 * File:   frame.h
 * Desc:   Media frame composed of RTP packets
 */

#pragma once

#include <glib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "format.h"
#include "media.h"
#include "packet.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct frame_t
    {
        GQueue   *packets;
        codec_t   codec;
        gint64    created_us;
        uint32_t  timestamp;
        bool      marker;
        bool      completed;
        size_t    unitcount;

    } frame_t;

    frame_t *frame_create(uint32_t timestamp, codec_t codec);
    bool frame_add_packet(frame_t *frame, packet_t *packet, bool *completed);
    bool frame_reassemble(frame_t *frame, media_t *media, bool completed,
            void *data);
    void frame_destroy(gpointer data);

#ifdef DEBUG
    void frame_print_packets(frame_t *frame);
#endif

#ifdef __cplusplus
}
#endif

