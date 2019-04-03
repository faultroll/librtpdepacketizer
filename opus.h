/*
 * Author: Pu-Chen Mao
 * Date:   2016/05/17
 * File:   opus.h
 * Desc:   Opus repackaging module
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct opus_toc_header_t
    {
        #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        uint8_t count:  2;
        uint8_t stereo: 1;
        uint8_t config: 5;
        #else
            #error "little-endian only"
        #endif

    } __attribute__ ((__packed__)) opus_toc_header_t;

    typedef struct opus_context_t
    {
        uint8_t unused[4];

    } opus_context_t;

    typedef enum prefix_t prefix_t;

    bool opus_reassemble_frame(uint8_t **index, size_t *length,
            const uint8_t *limit, prefix_t prefix, const uint8_t *frameptr,
            size_t framelen, bool completed, void *data);
    bool opus_is_fragmented(const uint8_t *frameptr, size_t framelen);
    uint8_t opus_get_frame_type(const uint8_t *frameptr, size_t framelen);
    bool opus_is_first_frame(const uint8_t *frameptr, size_t framelen);
    bool opus_is_last_frame(const uint8_t *frameptr, size_t framelen);

#ifdef __cplusplus
}
#endif

