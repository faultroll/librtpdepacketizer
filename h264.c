/*
 * Author: Pu-Chen Mao
 * Date:   2016/05/12
 * File:   h264.c
 * Desc:   H.264 Annex B bitstream reassembly
 */

#include <arpa/inet.h>
#include <glib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "format.h"
#include "h264.h"

// #define DEBUG
#define INLINE inline
#define ADD_TIMESTAMP_USERDATA_SEI 1

static INLINE bool h264_compose_single_nalu(uint8_t **index, size_t *length,
        const uint8_t *limit, prefix_t prefix, const uint8_t *naluptr,
        size_t nalulen);
static INLINE bool h264_compose_aggregation_unit(uint8_t **index,
        size_t *length, const uint8_t *limit, prefix_t prefix,
        const uint8_t *naluptr, size_t nalulen);
static INLINE bool h264_compose_fragmentation_unit(uint8_t **index,
        size_t *length, const uint8_t *limit, prefix_t prefix,
        const uint8_t *naluptr, size_t nalulen, bool completed);
static INLINE bool h264_compose_timestamp_sei_nalu(uint8_t **index,
        size_t *length, const uint8_t *limit, prefix_t prefix);
static INLINE bool h264_compose_prefix(uint8_t **index, size_t *length,
        const uint8_t *limit, prefix_t prefix, size_t nalulen);
static INLINE bool h264_compose_start_code(uint8_t **index,
        size_t *length, const uint8_t *limit);
static INLINE bool h264_compose_avcc_prefix(uint8_t **index,
        size_t *length, const uint8_t *limit, size_t nalulen);
static INLINE uint8_t h264_get_nal_ref_idc(uint8_t nal_unit_type);
static INLINE bool h264_decode_slice_header(const uint8_t *nalu,
        size_t length, h264_context_t *context);
static INLINE void h264_print_slice_header(const h264_context_t *context);
static INLINE bool h264_decode_sps(uint8_t *nalu, size_t length,
        h264_context_t *context);
static INLINE void h264_print_sps(const h264_context_t *context);
static INLINE uint32_t h264_decode_uexpgolomb(const uint8_t *bitstream,
        size_t *offset);
static INLINE uint32_t h264_decode_sexpgolomb(const uint8_t *bitstream,
        size_t *offset);
static INLINE uint32_t h264_get_bits(const uint8_t *bitstream, size_t *offset,
        size_t count);
static INLINE uint32_t h264_get_bit(const uint8_t *bitstream, size_t offset);
static INLINE void h264_set_bit(uint8_t *bitstream, size_t offset);
static INLINE void h264_print_octets(const uint8_t *base, size_t length);

 /* 7627DFE0-4924-4084-B98D-F2C9444B8E98 */
static const uint8_t time_sync_uuid[] = {0x76, 0x27, 0xDF, 0xE0,
                                         0x49, 0x24, 0x40, 0x84,
                                         0xB9, 0x8D, 0xF2, 0xC9,
                                         0x44, 0x4B, 0x8E, 0x98};

bool
h264_reassemble_frame(uint8_t       **index,
                      size_t         *length,
                      const uint8_t  *limit,
                      prefix_t        prefix,
                      const uint8_t  *payload,
                      size_t          size,
                      bool            completed,
                      void           *data)
{
    h264_nalu_header_t *naluptr = NULL;
    h264_context_t     *context = NULL;
    size_t              nalulen = size;
    uint8_t            *start   = NULL;
    bool                result  = false;

    g_return_val_if_fail(NULL != index, NULL);
    g_return_val_if_fail(NULL != *index, NULL);
    g_return_val_if_fail(NULL != length, NULL);
    g_return_val_if_fail(NULL != limit, NULL);
    g_return_val_if_fail(NULL != payload, NULL);
    g_return_val_if_fail(NULL != data, NULL);
    g_return_val_if_fail(1 < size,  NULL);

    start = *index + sizeof(uint32_t);
    context = (h264_context_t *)(data);

    /* Reassemble frame here */
    naluptr = (h264_nalu_header_t *)(payload);
    switch (naluptr->nal_unit_type)
    {
        case 1:  /* Single unit inter-frame (P-frame) */
        case 5:  /* Single unit intra-frame (I-frame) */
        case 6:  /* Supplemental enhancement information */
        case 7:  /* Single unit SPS */
        case 8:  /* Single unit PPS */
            result = h264_compose_single_nalu(index, length, limit, prefix,
                    (const uint8_t *)(naluptr), nalulen); break;
        case 24: /* Single time aggregation packet A (SPS + PPS) */
        case 25: /* Single time aggregation packet B (DON + SPS + PPS) */
        case 26: /* Multi-time aggregation packet A */
        case 27: /* Multi-time aggregation packet B */
            result = h264_compose_aggregation_unit(index, length, limit,
                    prefix, (const uint8_t *)(naluptr), nalulen); break;
        case 28: /* Fragmentation unit A */
        case 29: /* Fragmentation unit B */
            result = h264_compose_fragmentation_unit(index, length, limit,
                    prefix, (const uint8_t *)(naluptr), nalulen,
                    completed); break;
        default:
            fprintf(stderr, "Unsupported NAL type [%u]\n",
                    naluptr->nal_unit_type);
            result = false;
    }

    g_return_val_if_fail(result, false);

    /* Parse H.264 context here */
    naluptr = (h264_nalu_header_t *)(start);
    if (h264_is_first_nalu(payload, *length))
    {
        switch (naluptr->nal_unit_type)
        {
            /* Get slice header information */
            case 1:  /* Single unit inter-frame (P-frame) */
            case 5:  /* Single unit intra-frame (I-frame) */
                result = h264_decode_slice_header(start, *length,
                        context); break;
            /* Get SPS information */
            case 7:  /* Single unit SPS */
                result = h264_decode_sps(start, *length, context); break;
            default: break;
        }
    }

    return result;
}

bool
h264_is_fragmented(const uint8_t *naluptr,
                   size_t         nalulen)
{
    h264_nalu_header_t *naluhdr    = NULL;
    bool                fragmented = false;

    g_return_val_if_fail(NULL != naluptr, false);
    g_return_val_if_fail(1 <= nalulen, false);

    naluhdr = (h264_nalu_header_t *)(naluptr);
    switch (naluhdr->nal_unit_type)
    {
        case 1:  /* Single unit inter-frame (P-frame) */
            fragmented = false; break;
        case 2:  /* Data Partition A */
        case 3:  /* Data Partition B */
        case 4:  /* Data Partition C */
            fragmented = true; break;
        case 5:  /* Single unit intra-frame (I-frame) */
        case 6:  /* Supplemental enhancement information */
        case 7:  /* Single unit SPS */
        case 8:  /* Single unit PPS */
        case 9:  /* Access unit delimiter */
        case 10: /* End of sequence */
        case 11: /* End of stream */
        case 12: /* Filter data */
        case 24: /* Single-time aggregation packet A (SPS + PPS) */
        case 25: /* Single-time aggregation packet B (DON + SPS + PPS) */
        case 26: /* Multi-time aggregation packet A */
        case 27: /* Multi-time aggregation packet B */
            fragmented = false; break;
        case 28: /* Fragmentation unit A */
        case 29: /* Fragmentation unit B */
            fragmented = true; break;
        default:
            fragmented = false;
    }

    return fragmented;
}

uint8_t
h264_get_frame_type(const uint8_t *naluptr,
                    size_t         nalulen)
{
    h264_nalu_header_t *naluhdr = NULL;

    g_return_val_if_fail(NULL != naluptr, false);

    naluhdr = (h264_nalu_header_t *)(naluptr + sizeof(uint32_t));

    return naluhdr->nal_unit_type;
}

bool
h264_is_first_nalu(const uint8_t *naluptr,
                   size_t         nalulen)
{
    h264_nalu_header_t *naluhdr = NULL;
    h264_fu_header_t   *fuhdr   = NULL;
    bool                first   = false;

    g_return_val_if_fail(NULL != naluptr, false);
    g_return_val_if_fail(1 < nalulen, false);

    naluhdr = (h264_nalu_header_t *)(naluptr);
    fuhdr = (h264_fu_header_t *)(naluptr + sizeof(*naluhdr));
    switch (naluhdr->nal_unit_type)
    {
        case 1:  /* Single unit inter-frame (P-frame) */
        case 5:  /* Single unit intra-frame (I-frame) */
        case 6:  /* Supplemental enhancement information */
        case 7:  /* Single unit SPS */
        case 8:  /* Single unit PPS */
        case 9:  /* Access unit delimiter */
            first = true; break;
        case 10: /* End of sequence */
        case 11: /* End of stream */
            first = false; break;
        case 12: /* Filter data */
        case 24: /* Single time aggregation packet A (SPS + PPS) */
        case 25: /* Single time aggregation packet B (DON + SPS + PPS) */
        case 26: /* Multi-time aggregation packet A */
        case 27: /* Multi-time aggregation packet B */
            first = true; break;
        case 28: /* Fragmentation unit A */
        case 29: /* Fragmentation unit B */
            first = fuhdr->start; break;
        default:
            first = false;
    }

    return first;
}

bool
h264_is_last_nalu(const uint8_t *naluptr,
                  size_t         nalulen)
{
    h264_nalu_header_t *naluhdr = NULL;
    h264_fu_header_t   *fuhdr   = NULL;
    bool                last    = false;

    g_return_val_if_fail(NULL != naluptr, false);
    g_return_val_if_fail(1 < nalulen, false);

    naluhdr = (h264_nalu_header_t *)(naluptr);
    fuhdr = (h264_fu_header_t *)(naluptr + sizeof(*naluhdr));
    switch (naluhdr->nal_unit_type)
    {
        case 1:  /* Single unit inter-frame (P-frame) */
        case 5:  /* Single unit intra-frame (I-frame) */
        case 6:  /* Supplemental enhancement information */
        case 7:  /* Single unit SPS */
        case 8:  /* Single unit PPS */
        case 9:  /* Access unit delimiter */
        case 10: /* End of sequence */
        case 11: /* End of stream */
            last = true; break;
        case 12: /* Filter data */
        case 24: /* Single time aggregation packet A (SPS + PPS) */
        case 25: /* Single time aggregation packet B (DON + SPS + PPS) */
        case 26: /* Multi-time aggregation packet A */
        case 27: /* Multi-time aggregation packet B */
            last = false; break;
        case 28: /* Fragmentation unit A */
        case 29: /* Fragmentation unit B */
            last = fuhdr->end; break;
        default:
            last = false; break;
    }

    return last;
}

static INLINE bool
h264_compose_single_nalu(uint8_t       **index,
                         size_t         *length,
                         const uint8_t  *limit,
                         prefix_t        prefix,
                         const uint8_t  *naluptr,
                         size_t          nalulen)
{
    bool result = false;

    g_return_val_if_fail(NULL != index, NULL);
    g_return_val_if_fail(NULL != *index, NULL);
    g_return_val_if_fail(NULL != length, NULL);
    g_return_val_if_fail(NULL != limit, NULL);
    g_return_val_if_fail(NULL != naluptr, NULL);

    result = h264_compose_prefix(index, length, limit, prefix, nalulen);
    g_return_val_if_fail(result, false);
    g_return_val_if_fail(*index + nalulen < limit, false);
    memcpy(*index, naluptr, nalulen);
    *index += nalulen;
    *length += nalulen;

    return true;
}

static INLINE bool
h264_compose_aggregation_unit(uint8_t       **index,
                              size_t         *length,
                              const uint8_t  *limit,
                              prefix_t        prefix,
                              const uint8_t  *naluptr,
                              size_t          nalulen)
{
    h264_nalu_header_t *naluhdr  = NULL;
    const uint8_t      *auptr    = NULL;
    uint16_t            aulen    = 0;
    const uint8_t      *aulenptr = NULL;
    bool                result   = false;

    g_return_val_if_fail(NULL != index, NULL);
    g_return_val_if_fail(NULL != *index, NULL);
    g_return_val_if_fail(NULL != length, NULL);
    g_return_val_if_fail(NULL != limit, NULL);
    g_return_val_if_fail(NULL != naluptr, NULL);

    for (aulenptr = naluptr + sizeof(uint8_t),
         auptr = aulenptr + sizeof(uint16_t);
         aulenptr < naluptr + nalulen && auptr < naluptr + nalulen;
         aulenptr += sizeof(uint16_t) + aulen,
         auptr = aulenptr + sizeof(uint16_t))
    {
        naluhdr = (h264_nalu_header_t *)(auptr);
        aulen = ntohs(*(uint16_t *)(aulenptr));
        result = h264_compose_prefix(index, length, limit, prefix, aulen);
        g_return_val_if_fail(result, false);
        g_return_val_if_fail(*index + aulen < limit, false);
        memcpy(*index, auptr, aulen);
        *index += aulen;
        *length += aulen;
#ifdef ADD_TIMESTAMP_USERDATA_SEI
        /* Add an user unregistered SEI messsage containing
         * the system timestamp if we encounter end of PPS */
        if (naluhdr->nal_unit_type == 0x08)
        {
            result = h264_compose_timestamp_sei_nalu(index,
                    length, limit, prefix);
            g_return_val_if_fail(result, false);
        }
#endif
    }

    return true;
}

static INLINE bool
h264_compose_fragmentation_unit(uint8_t       **index,
                                size_t         *length,
                                const uint8_t  *limit,
                                prefix_t        prefix,
                                const uint8_t  *naluptr,
                                size_t          nalulen,
                                bool            completed)
{
    h264_fu_header_t   *fuhdr   = NULL;
    h264_nalu_header_t *naluhdr = NULL;
    size_t              hdrlen  = 0;
    bool                result  = false;

    g_return_val_if_fail(NULL != index, NULL);
    g_return_val_if_fail(NULL != *index, NULL);
    g_return_val_if_fail(NULL != length, NULL);
    g_return_val_if_fail(NULL != limit, NULL);
    g_return_val_if_fail(NULL != naluptr, NULL);

    fuhdr = (h264_fu_header_t *)(naluptr + sizeof(h264_nalu_header_t));
    if (fuhdr->start)
    {
        result = h264_compose_prefix(index, length, limit, prefix, nalulen);
        g_return_val_if_fail(result, false);
        g_return_val_if_fail(*index + 1 < limit, false);
        naluhdr = (h264_nalu_header_t *)(*index);
        naluhdr->forbidden = !completed;
        naluhdr->nal_unit_type = fuhdr->type;
        naluhdr->nal_ref_idc = h264_get_nal_ref_idc(fuhdr->type);
        *index += sizeof(h264_nalu_header_t);
        *length += sizeof(h264_nalu_header_t);
    }

    hdrlen = sizeof(h264_nalu_header_t) + sizeof(h264_fu_header_t);
    g_return_val_if_fail(*index + (nalulen - hdrlen) < limit, false);
    memcpy(*index, naluptr + hdrlen, nalulen - hdrlen);
    *index += nalulen - hdrlen;
    *length += nalulen - hdrlen;

    return true;
}

static INLINE bool
h264_compose_timestamp_sei_nalu(uint8_t       **index,
                                size_t         *length,
                                const uint8_t  *limit,
                                prefix_t        prefix)
{
    uint64_t timestamp = 0;
    size_t   nalulen   = 0;
    bool     result    = false;

    g_return_val_if_fail(NULL != index, false);
    g_return_val_if_fail(NULL != *index, false);
    g_return_val_if_fail(NULL != length, false);
    g_return_val_if_fail(NULL != limit, NULL);

    /* H.264 NALU prefix */
    nalulen = 1 + 1 + 1 + sizeof(time_sync_uuid) + sizeof(timestamp) + 1;
    result = h264_compose_prefix(index, length, limit, prefix, nalulen);
    g_return_val_if_fail(result, false);

    /* SEI NALU header, user unregistered type, and payload size */
    g_return_val_if_fail(*index + 1 < limit, false);
    **index = 0x06;
    ++(*index);
    g_return_val_if_fail(*index + 1 < limit, false);
    **index = 0x05;
    ++(*index);
    g_return_val_if_fail(*index + 1 < limit, false);
    **index = sizeof(time_sync_uuid) + sizeof(timestamp);
    ++(*index);
    *length += 3;

    /* UUID */
    g_return_val_if_fail(*index + sizeof(time_sync_uuid) < limit, false);
    memcpy(*index, time_sync_uuid, sizeof(time_sync_uuid));
    *index += sizeof(time_sync_uuid);
    *length += sizeof(time_sync_uuid);

    /* 64-bit timestamp, microseconds since 01/01/1970 */
    timestamp = GUINT64_TO_BE((uint64_t)(g_get_real_time()));
    g_return_val_if_fail(*index + sizeof(timestamp) < limit, false);
    memcpy(*index, &timestamp, sizeof(timestamp));
    *index += sizeof(timestamp);
    *length += sizeof(timestamp);

    /* Padding */
    g_return_val_if_fail(*index + 1 < limit, false);
    **index = 0xFF;
    ++(*index);
    ++(*length);

    return true;
}

static INLINE bool
h264_compose_prefix(uint8_t       **index,
                    size_t         *length,
                    const uint8_t  *limit,
                    prefix_t        prefix,
                    size_t          nalulen)
{
    bool result = false;

    g_return_val_if_fail(NULL != index, false);
    g_return_val_if_fail(NULL != *index, false);
    g_return_val_if_fail(NULL != length, NULL);
    g_return_val_if_fail(NULL != limit, NULL);
    g_return_val_if_fail(1 < nalulen, false);

    switch (prefix)
    {
        case PREFIX_ANNEXB:
            result = h264_compose_start_code(index, length, limit); break;
        case PREFIX_AVCC:
            result = h264_compose_avcc_prefix(index, length,
                    limit, nalulen); break;
        case PREFIX_NONE:
        default:
            result = true;
    }

    return result;
}

static INLINE bool
h264_compose_start_code(uint8_t       **index,
                        size_t         *length,
                        const uint8_t  *limit)
{
    g_return_val_if_fail(NULL != index, false);
    g_return_val_if_fail(NULL != *index, false);
    g_return_val_if_fail(NULL != length, false);
    g_return_val_if_fail(NULL != limit, NULL);

    g_return_val_if_fail(*index + 4 < limit, false);
    *(uint32_t *)(*index) = htonl(0x00000001);
    *index += sizeof(uint32_t);
    *length += sizeof(uint32_t);

    return true;
}

static INLINE bool
h264_compose_avcc_prefix(uint8_t       **index,
                         size_t         *length,
                         const uint8_t  *limit,
                         size_t          nalulen)
{
    uint32_t *prefix = NULL;

    g_return_val_if_fail(NULL != index, false);
    g_return_val_if_fail(NULL != *index, false);
    g_return_val_if_fail(NULL != length, false);
    g_return_val_if_fail(NULL != limit, NULL);

    g_return_val_if_fail(*index + 4 < limit, false);
    prefix = (uint32_t *)(*index);
    *prefix = htonl((uint32_t)(nalulen));
    *index += sizeof(*prefix);
    *length += sizeof(*prefix);

    return true;
}

static INLINE uint8_t
h264_get_nal_ref_idc(uint8_t nal_unit_type)
{
    switch (nal_unit_type)
    {
        case 5:
        case 7:
        case 8:
            return 0x03;
        case 1:
        case 2:
            return 0x02;
        case 3:
        case 4:
            return 0x01;
        case 6:
        case 9:
        case 10:
        case 11:
        case 12:
        default:
            return 0x00;
    }
}

static INLINE bool
h264_decode_slice_header(const uint8_t  *nalu,
                         size_t          length,
                         h264_context_t *context)
{
    size_t offset = 0; // nth bit, not byte, 0-based

    g_return_val_if_fail(NULL != nalu, false);
    g_return_val_if_fail(NULL != context, false);
    g_return_val_if_fail(0 < length, false);

    context->forbidden_zero_bit = h264_get_bits(nalu, &offset, 1);
    context->nal_ref_idc = h264_get_bits(nalu, &offset, 2);
    context->nal_unit_type = h264_get_bits(nalu, &offset, 5);
    context->first_mb_in_slice = h264_decode_uexpgolomb(nalu, &offset);
    context->slice_type = h264_decode_uexpgolomb(nalu, &offset);
    context->pic_parameter_set_id = h264_decode_uexpgolomb(nalu, &offset);
    context->frame_num = h264_get_bits(nalu, &offset,
            context->log2_max_frame_num_minus4 + 4);
#ifdef DEBUG
    h264_print_slice_header(context);
    h264_print_octets(nalu, 16);
#endif

    return true;
}

static INLINE void
h264_print_slice_header(const h264_context_t *context)
{
    g_return_if_fail(NULL != context);

    printf("H.264 Slice Header:\n");
    printf("   forbidden_zero_bit: %u\n", context->forbidden_zero_bit);
    printf("   nal_ref_idc: %u\n", context->nal_ref_idc);
    printf("   nal_unit_type: %u\n", context->nal_unit_type);
    printf("   first_mb_in_slice: %u\n", context->first_mb_in_slice);
    printf("   slice_type: %u\n", context->slice_type);
    printf("   pic_parameter_set_id: %u\n", context->pic_parameter_set_id);
    printf("   frame_num: %u\n", context->frame_num);
}

/* NOTE: we modify the gaps_in_frame_num_value_allowed_flag
 * in the bitstream within this function */
static INLINE bool
h264_decode_sps(uint8_t        *nalu,
                size_t          length,
                h264_context_t *context)
{
    size_t  offset = 0; // nth bit, not byte, 0-based
    int32_t count  = 0;

    g_return_val_if_fail(NULL != nalu, false);
    g_return_val_if_fail(NULL != context, false);
    g_return_val_if_fail(0 < length, false);

    context->forbidden_zero_bit = h264_get_bits(nalu, &offset, 1);
    context->nal_ref_idc = h264_get_bits(nalu, &offset, 2);
    context->nal_unit_type = h264_get_bits(nalu, &offset, 5);
    context->profile_idc = h264_get_bits(nalu, &offset, 8);
    context->constraint_set0_flag = h264_get_bits(nalu, &offset, 1);
    context->constraint_set1_flag = h264_get_bits(nalu, &offset, 1);
    context->constraint_set2_flag = h264_get_bits(nalu, &offset, 1);
    context->constraint_set3_flag = h264_get_bits(nalu, &offset, 1);
    context->reserved_zero_4bits = h264_get_bits(nalu, &offset, 4);
    context->level_idc = h264_get_bits(nalu, &offset, 8);
    if (context->profile_idc != 100 && context->profile_idc != 110 &&
        context->profile_idc != 122 && context->profile_idc != 122 &&
        context->profile_idc != 244 && context->profile_idc != 44  &&
        context->profile_idc != 83  && context->profile_idc != 86  &&
        context->profile_idc != 118 && context->profile_idc != 128 &&
        context->profile_idc != 138 && context->profile_idc != 144)
        context->seq_parameter_set_id = h264_decode_uexpgolomb(nalu, &offset);
    context->log2_max_frame_num_minus4 = h264_decode_uexpgolomb(nalu, &offset);
    context->pic_order_cnt_type = h264_decode_uexpgolomb(nalu, &offset);
    if (context->pic_order_cnt_type == 0)
        context->log2_max_pic_order_cnt_lsb_minus4 =
            h264_decode_uexpgolomb(nalu, &offset);
    else
    {
        context->delta_pic_order_always_zero_flag =
            h264_get_bits(nalu, &offset, 1);
        context->offset_for_non_ref_pic = h264_decode_sexpgolomb(nalu, &offset);
        context->offset_for_top_to_bottom_field =
            h264_decode_sexpgolomb(nalu, &offset);
        context->num_ref_frames_in_pic_order_cnt_cycle =
            h264_decode_uexpgolomb(nalu, &offset);
        for (count = 0; count < context->num_ref_frames_in_pic_order_cnt_cycle;
             count++, (void)(h264_decode_sexpgolomb(nalu, &offset)));
    }
    context->num_ref_frames = h264_decode_uexpgolomb(nalu, &offset);
    /* Set the gaps_in_frame_num_value_allowed_flag bit */
    h264_set_bit(nalu, offset);
    context->gaps_in_frame_num_value_allowed_flag =
        h264_get_bits(nalu, &offset, 1);
    context->pic_width_in_mbs_minus_1 = h264_decode_uexpgolomb(nalu, &offset);
    context->pic_height_in_map_units_minus_1 =
        h264_decode_uexpgolomb(nalu, &offset);
    context->frame_mbs_only_flag = h264_get_bits(nalu, &offset, 1);
    context->direct_8x8_inference_flag = h264_get_bits(nalu, &offset, 1);
    context->frame_cropping_flag = h264_get_bits(nalu, &offset, 1);
    context->vui_prameters_present_flag = h264_get_bits(nalu, &offset, 1);
    context->rbsp_stop_one_bit = h264_get_bits(nalu, &offset, 1);
#ifdef DEBUG
    h264_print_sps(context);
    h264_print_octets(nalu, 16);
#endif

    return true;
}

static INLINE void
h264_print_sps(const h264_context_t *context)
{
    g_return_if_fail(NULL != context);

    printf("H.264 SPS:\n");
    printf("   forbidden_zero_bit: %u\n", context->forbidden_zero_bit);
    printf("   nal_ref_idc: %u\n", context->nal_ref_idc);
    printf("   nal_unit_type: %u\n", context->nal_unit_type);
    printf("   profile_idc: %u\n", context->profile_idc);
    printf("   constraint_set0_flag: %u\n", context->constraint_set0_flag);
    printf("   constraint_set1_flag: %u\n", context->constraint_set1_flag);
    printf("   constraint_set2_flag: %u\n", context->constraint_set2_flag);
    printf("   constraint_set3_flag: %u\n", context->constraint_set3_flag);
    printf("   reserved_zero_4bits: %u\n", context->reserved_zero_4bits);
    printf("   level_idc: %u\n", context->level_idc);
    printf("   seq_parameter_set_id: %u\n", context->seq_parameter_set_id);
    printf("   log2_max_frame_num_minus4: %u\n", context->log2_max_frame_num_minus4);
    printf("   pic_order_cnt_type: %u\n", context->pic_order_cnt_type);
    printf("   log2_max_pic_order_cnt_lsb_minus4: %u\n",
            context->log2_max_pic_order_cnt_lsb_minus4);
    printf("   delta_pic_order_always_zero_flag: %u\n",
            context->delta_pic_order_always_zero_flag);
    printf("   offset_for_non_ref_pic: %d\n", context->offset_for_non_ref_pic);
    printf("   offset_for_top_to_bottom_field: %d\n",
            context->offset_for_top_to_bottom_field);
    printf("   num_ref_frames_in_pic_order_cnt_cycle: %d\n",
            context->num_ref_frames_in_pic_order_cnt_cycle);
    printf("   num_ref_frames: %u\n", context->num_ref_frames);
    printf("   gaps_in_frame_num_value_allowed_flag: %u\n",
            context->gaps_in_frame_num_value_allowed_flag);
    printf("   pic_width_in_mbs_minus_1: %u\n", context->pic_width_in_mbs_minus_1);
    printf("   pic_height_in_map_units_minus_1: %u\n",
            context->pic_height_in_map_units_minus_1);
    printf("   frame_mbs_only_flag: %u\n", context->frame_mbs_only_flag);
    printf("   direct_8x8_inference_flag: %u\n",
            context->direct_8x8_inference_flag);
    printf("   frame_cropping_flag: %u\n", context->frame_cropping_flag);
    printf("   vui_prameters_present_flag: %u\n",
            context->vui_prameters_present_flag);
    printf("   rbsp_stop_one_bit: %u\n", context->rbsp_stop_one_bit);
}

static INLINE uint32_t
h264_decode_uexpgolomb(const uint8_t *bitstream,
                       size_t        *offset)
{
    uint32_t zeroes = -1;
    uint32_t code   = 0;
    uint8_t  bit    = 0x00;
    uint32_t retval = 0;

    g_return_val_if_fail(NULL != bitstream, 0);
    g_return_val_if_fail(NULL != offset, 0);

    for (bit = 0; !bit; zeroes++)
        bit = h264_get_bit(bitstream, (*offset)++);
    if (zeroes > 0)
    {
        code = h264_get_bits(bitstream, offset, zeroes);
        retval = (1 << zeroes) - 1 + code;
    }

    return retval;
}

static INLINE uint32_t
h264_decode_sexpgolomb(const uint8_t *bitstream,
                       size_t        *offset)
{
    uint32_t retval = 0;

    g_return_val_if_fail(NULL != bitstream, 0);
    g_return_val_if_fail(NULL != offset, 0);

    retval = h264_decode_uexpgolomb(bitstream, offset);
    retval = (retval & 0x01 ? -1 : 1) * ceil(retval / 2.0);

    return retval;
}

static INLINE uint32_t
h264_get_bits(const uint8_t *bitstream,
              size_t        *offset,
              size_t         count)
{
    size_t   limit = 0;
    uint32_t code  = 0;

    g_return_val_if_fail(NULL != bitstream, 0);
    g_return_val_if_fail(NULL != offset, 0);
    g_return_val_if_fail(0 < count, 0);

    for (limit = *offset + count; *offset < limit; (*offset)++)
        code = (code << 1) + h264_get_bit(bitstream, *offset);

    return code;
}

static INLINE uint32_t
h264_get_bit(const uint8_t *bitstream,
             size_t         offset)
{
    g_return_val_if_fail(NULL != bitstream, 0);

    return (bitstream[offset >> 3] >> (7 - (offset & 0x7))) & 0x01;
}

static INLINE void
h264_set_bit(uint8_t *bitstream,
             size_t   offset)
{
    g_return_if_fail(NULL != bitstream);

    bitstream[offset >> 3] |= (0x01 << (7 - (offset & 0x7)));
}

static INLINE void
h264_print_octets(const uint8_t *base,
                  size_t         length)
{
    size_t idx = 0;

    g_return_if_fail(NULL != base);
    g_return_if_fail(0 < length);

    for (idx = 0; idx < length; idx++)
        printf("%02X%s", base[idx] & 0xFF, (idx + 1) % 16 == 0 ? "\n" : " ");
    printf("\n");
}

