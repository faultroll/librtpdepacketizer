/*
 * Author: Pu-Chen Mao
 * Date:   2016/05/12
 * File:   packet.h
 * Desc:   RTP packet
 */

#pragma once

#include <glib.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct rtp_header_t
    {
        #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        uint32_t csrc_cnt:  4;
        uint32_t extension: 1;
        uint32_t padding:   1;
        uint32_t version:   2;
        uint32_t profile:   7;
        uint32_t marker:    1;
        uint32_t sequence:  16;
        uint32_t timestamp;
        uint32_t ssrc;
        #else
            #error "little-endian only"
        #endif
    } __attribute__ ((__packed__)) rtp_header_t;

    typedef struct rtp_ext_header_t
    {
        #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        uint32_t extension_id:     16;
        uint32_t extension_length: 16;
        #else
            #error "little-endian only"
        #endif
    } __attribute__ ((__packed__)) rtp_ext_header_t;

    typedef struct rtp_packet_t
    {
        #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        rtp_header_t header;
        uint8_t      payload[];
        #else
            #error "little-endian only"
        #endif
    } __attribute__ ((__packed__)) rtp_packet_t;

    typedef struct packet_t
    {
        rtp_packet_t *rtp;
        size_t        length;
        gint64        created_us;
        bool          is_audio;

    } packet_t;

    packet_t *packet_create(const uint8_t *buffer, size_t length,
            bool is_audio, bool copy);
    bool packet_get_payload(const packet_t *packet, const uint8_t **payload,
            size_t *length);
    gint packet_compare_sequence(gconstpointer lval, gconstpointer rval,
            gpointer data);
    void packet_destroy(gpointer data);

#ifdef DEBUG
    void packet_print_info(gpointer data, gpointer userdata);
#endif

#ifdef __cplusplus
}
#endif

