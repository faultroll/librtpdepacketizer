/*
 * Author: Pu-Chen Mao
 * Date:   2016/05/12
 * File:   format.h
 * Desc:   Frame reassembly contexts
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "h264.h"
#include "opus.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum codec_t
    {
        CODEC_NONE,
        CODEC_H264,
        CODEC_OPUS

    } codec_t;

    typedef enum prefix_t
    {
        PREFIX_NONE,
        PREFIX_ANNEXB,
        PREFIX_AVCC,

    } prefix_t;

    typedef bool (*reassemble_functor_t)(uint8_t **index, size_t *length,
            const uint8_t *limit, prefix_t prefix, const uint8_t *payload,
            size_t size, bool completed, void *data);
    typedef bool (*fragmented_functor_t)(const uint8_t *payload, size_t length);
    typedef uint8_t (*frame_type_functor_t)(const uint8_t *payload, size_t length);
    typedef bool (*first_unit_functor_t)(const uint8_t *payuload, size_t length);
    typedef bool (*last_unit_functor_t)(const uint8_t *payuload, size_t length);

    typedef struct format_t
    {
        reassemble_functor_t reassemble;
        fragmented_functor_t fragmented;
        frame_type_functor_t frame_type;
        first_unit_functor_t first_unit;
        last_unit_functor_t  last_unit;

    } format_t;

    typedef union context_t
    {
        h264_context_t h264;
        opus_context_t opus;

    } context_t;

    const format_t *format_get_reassembly_context(codec_t codec);

#ifdef __cplusplus
}
#endif

