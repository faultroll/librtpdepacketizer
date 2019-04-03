/*
 * Author: Pu-Chen Mao
 * Date:   2016/05/17
 * File:   opus.c
 * Desc:   Opus repackaging module
 */

#include <arpa/inet.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>

#include "format.h"
#include "opus.h"

bool
opus_reassemble_frame(uint8_t       **index,
                      size_t         *length,
                      const uint8_t  *limit,
                      prefix_t        prefix,
                      const uint8_t  *payload,
                      size_t          size,
                      bool            completed,
                      void           *data)
{
    opus_toc_header_t *header  = NULL;
    opus_context_t    *context = NULL;
    bool               result  = false;

    g_return_val_if_fail(NULL != index, false);
    g_return_val_if_fail(NULL != *index, false);
    g_return_val_if_fail(NULL != length, false);
    g_return_val_if_fail(NULL != limit, false);
    g_return_val_if_fail(NULL != payload, false);
    g_return_val_if_fail(NULL != data, false);
    g_return_val_if_fail(1 <= size, false);

    context = (opus_context_t *)(data);
    header = (opus_toc_header_t *)(payload);
    switch (header->count)
    {
        case 0:
        case 1:
        case 2:
            result = true; break;
        case 3:
            fprintf(stderr, "opus header type not implemented\n");
        default:
            result = false;
    }

    g_return_val_if_fail(*index + size < limit, false);
    memcpy(*index, payload, size);
    *length += size;
    (void)(context);

    return result;
}

bool
opus_is_fragmented(const uint8_t *frameptr,
                   size_t         framelen)
{
    g_return_val_if_fail(NULL != frameptr, false);
    g_return_val_if_fail(1 <= framelen, false);

    return false;
}

uint8_t
opus_get_frame_type(const uint8_t *frameptr,
                    size_t         framelen)
{
    g_return_val_if_fail(NULL != frameptr, 0x00);
    g_return_val_if_fail(1 <= framelen, 0x00);

    /* TODO: implement Opus frame type getter */

    return 0x00;
}

bool
opus_is_first_frame(const uint8_t *frameptr,
                    size_t         framelen)
{
    g_return_val_if_fail(NULL != frameptr, false);
    g_return_val_if_fail(1 <= framelen, false);

    return true;
}

bool
opus_is_last_frame(const uint8_t *frameptr,
                   size_t         framelen)
{
    g_return_val_if_fail(NULL != frameptr, false);
    g_return_val_if_fail(1 <= framelen, false);

    return true;
}

