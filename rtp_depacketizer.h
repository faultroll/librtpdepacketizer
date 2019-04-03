/*
 * Author: Pu-Chen Mao
 * Date:   2016/05/12
 * File:   rtp_depacketizer.h
 * Desc:   RTP depacketizer
 */

#pragma once

#include <glib.h>
#include <stddef.h>
#include <stdbool.h>

#include "frame.h"
#include "media.h"
#include "packet.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct rtp_depacketizer_t
    {
        GHashTable *frames;
        GQueue     *completed;
        codec_t     codec;
        gint64      enqueue_us;
        gint64      refresh_us;
        gint64      timeout_us;
        gint64      reap_us;
        context_t   context;

    } rtp_depacketizer_t;

    typedef enum prefix_t prefix_t;

    rtp_depacketizer_t *rtp_depacketizer_create(codec_t codec,
            gint64 timeout_us, gint64 reap_us);
    bool rtp_depacketizer_add_buffer(rtp_depacketizer_t *depacketizer,
            bool is_audio, uint8_t *buffer, size_t length, bool *frame_ready);
    bool rtp_depacketizer_add_packet(rtp_depacketizer_t *depacketizer,
            packet_t *packet, bool *frame_ready);
    bool rtp_depacketizer_get_frame(rtp_depacketizer_t *depacketizer,
            media_t *media);
    void rtp_depacketizer_destroy(gpointer data);

#ifdef __cplusplus
}
#endif

