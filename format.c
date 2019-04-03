/*
 * Author: Pu-Chen Mao
 * Date:   2016/05/12
 * File:   format.c
 * Desc:   Frame reassembly contexts
 */

#include <arpa/inet.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>

#include "format.h"

static format_t h264_format =
{
    .reassemble = h264_reassemble_frame,
    .fragmented = h264_is_fragmented,
    .frame_type = h264_get_frame_type,
    .first_unit = h264_is_first_nalu,
    .last_unit  = h264_is_last_nalu,
};

static format_t opus_format =
{
    .reassemble = opus_reassemble_frame,
    .fragmented = opus_is_fragmented,
    .frame_type = opus_get_frame_type,
    .first_unit = opus_is_first_frame,
    .last_unit  = opus_is_last_frame,
};

const format_t *
format_get_reassembly_context(codec_t codec)
{
    switch (codec)
    {
        case CODEC_H264:
            return &h264_format;
        case CODEC_OPUS:
            return &opus_format;
        default:
            return NULL;
    }

    return NULL;
}

