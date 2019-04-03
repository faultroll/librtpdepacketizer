/*
 * Author: Pu-Chen Mao
 * Date:   2016/05/12
 * File:   packet.c
 * Desc:   RTP packet
 */

#include <arpa/inet.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <glib.h>

#include "packet.h"

static size_t packet_padding_length(const packet_t *packet);

packet_t *
packet_create(const uint8_t *buffer,
              size_t         length,
              bool           is_audio,
              bool           copy)
{
    packet_t *packet = NULL;
    bool      result = false;

    g_return_val_if_fail(NULL != buffer, NULL);
    g_return_val_if_fail(0 < length, NULL);

    packet = g_try_new0(packet_t, 1);
    if (!packet)
        goto RETURN;

    if (!copy)
        packet->rtp = (rtp_packet_t *)(buffer);
    else
    {
        packet->rtp = (rtp_packet_t *)(g_try_malloc(length));
        if (!packet->rtp)
            goto RETURN;
        memcpy(packet->rtp, buffer, length);
    }

    packet->length = length;
    packet->created_us = g_get_monotonic_time();
    packet->is_audio = is_audio;
    result = true;

RETURN:

    if (!result)
        g_clear_pointer(&packet, packet_destroy);

    return packet;
}

bool
packet_get_payload(const packet_t  *packet,
                   const uint8_t  **payload,
                   size_t          *length)
{
    const rtp_header_t     *header    = NULL;
    const rtp_ext_header_t *extension = NULL;
    const uint8_t          *index     = NULL;
    ptrdiff_t               nalulen   = 0;
    size_t                  padlen    = 0;

    g_return_val_if_fail(NULL != packet, NULL);
    g_return_val_if_fail(NULL != packet->rtp, NULL);
    g_return_val_if_fail(NULL != payload, NULL);
    g_return_val_if_fail(NULL != length, NULL);

    header = &(packet->rtp->header);
    index = packet->rtp->payload;
    extension = (rtp_ext_header_t *)(index);
    index += header->csrc_cnt * sizeof(uint32_t);
    index += header->extension * sizeof(rtp_ext_header_t);
    index += header->extension * ntohs(extension->extension_length);
    if (header->padding)
        padlen = packet_padding_length(packet);
    nalulen = (uintptr_t)(index) - (uintptr_t)(header);
    *length = packet->length - nalulen - padlen;
    *payload = index;

    return true;
}

gint
packet_compare_sequence(gconstpointer lval,
                        gconstpointer rval,
                        gpointer      data)
{
    packet_t *lpkt    = NULL;
    packet_t *rpkt    = NULL;
    gint      lseq    = 0;
    gint      rseq    = 0;
    gint      wrapped = 0;

    g_return_val_if_fail(NULL != lval, 0);
    g_return_val_if_fail(NULL != rval, 0);

    lpkt = (packet_t *)(lval);
    rpkt = (packet_t *)(rval);
    lseq = ntohs((lpkt->rtp->header).sequence);
    rseq = ntohs((rpkt->rtp->header).sequence);
    wrapped = 1 - 2 * (ABS(lseq - rseq) > (G_MAXUSHORT >> 1)); // [0, 1] -> [-1, 1]

    return wrapped * (lseq - rseq);
}

void
packet_destroy(gpointer data)
{
    packet_t *packet = NULL;

    g_return_if_fail(NULL != data);
    g_return_if_fail(GUINT_TO_POINTER(G_MAXSIZE) != data);

    packet = (packet_t *)(data);
    g_clear_pointer(&(packet->rtp), g_free);
    g_clear_pointer(&packet, g_free);
}

static size_t
packet_padding_length(const packet_t *packet)
{
    uint8_t *padptr = NULL;
    size_t   length = 0;

    g_return_val_if_fail(NULL != packet, 0);

    padptr = (uint8_t *)(&(packet->rtp->header));
    length = (size_t)(padptr[packet->length - 1]);

    return length;
}

#ifdef DEBUG
void
packet_print_info(gpointer data,
                  gpointer userdata)
{
    packet_t     *packet = NULL;
    rtp_header_t *header = NULL;

    g_return_if_fail(NULL != data);

    packet = (packet_t *)(data);
    g_return_if_fail(NULL != packet->rtp);
    header = &(packet->rtp->header);

    /* timestamp, sequence, size, type */
    printf("(%u, %u, %zu, %u) ", ntohl(header->timestamp),
            ntohs(header->sequence), packet->length,
            (packet->rtp->payload)[0] & 0x1F);
}
#endif

