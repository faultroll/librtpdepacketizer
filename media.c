/*
 * Author: Pu-Chen Mao
 * Date:   2016/10/11
 * File:   media.c
 * Desc:   RTP depacketizer interface parameter implementation
 */

#include <arpa/inet.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>

#include "media.h"

media_t *
media_create(prefix_t prefix)
{
    media_t *media  = NULL;
    bool     result = false;

    media = g_try_new0(media_t, 1);
    if (!media)
        goto RETURN;

    media->buffer = g_try_malloc(MAX_FRAME_BUFFER_SIZE);
    if (!media->buffer)
        goto RETURN;

    media->prefix = prefix;
    media->length = MAX_FRAME_BUFFER_SIZE;
    result = true;

RETURN:

    if (!result)
        g_clear_pointer(&media, media_destroy);

    return media;
}

gint
media_compare_timestamp(gconstpointer lval,
                        gconstpointer rval,
                        gpointer      data)
{
    media_t *lmedia = NULL;
    media_t *rmedia = NULL;
    gint64   ltime  = 0;
    gint64   rtime  = 0;

    g_return_val_if_fail(NULL != lval, 0);
    g_return_val_if_fail(NULL != rval, 0);

    lmedia = (media_t *)(lval);
    rmedia = (media_t *)(rval);
    ltime = lmedia->rtptime;
    rtime = rmedia->rtptime;

    return (lmedia->is_audio == rmedia->is_audio) ?
        ltime - rtime : lmedia->created_us - rmedia->created_us;
}

void
media_destroy(gpointer data)
{
    media_t *media = NULL;

    g_return_if_fail(NULL != data);

    media = (media_t *)(data);
    g_clear_pointer(&(media->buffer), g_free);
    g_clear_pointer(&media, g_free);
}

