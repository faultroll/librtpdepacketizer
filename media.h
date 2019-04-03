/*
 * Author: Pu-Chen Mao
 * Date:   2016/09/23
 * File:   media.h
 * Desc:   RTP depacketizer interface parameter definition
 */

#pragma once

#include <glib.h>
#include <stddef.h>
#include <stdbool.h>

#include "format.h"

#ifdef __cplusplus
extern "C"
{
#endif

    #define MAX_FRAME_BUFFER_SIZE (512 * 1024)

    typedef struct media_t
    {
        bool       is_audio;
        prefix_t   prefix;
        uint8_t    type;
        uint32_t   rtptime;
        gint64     created_us;
        uint32_t   timestamp;
        uint8_t   *buffer;
        size_t     length;
        uint16_t   head_seq;
        uint16_t   tail_seq;
        context_t  context;

    } media_t;

    media_t *media_create(prefix_t prefix);
    gint media_compare_timestamp(gconstpointer lval, gconstpointer rval,
            gpointer data);
    void media_destroy(gpointer data);


#ifdef __cplusplus
}
#endif

