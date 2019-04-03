/*
 * Author: Pu-Chen Mao
 * Date:   2016/05/12
 * File:   h264.h
 * Desc:   H.264 Annex B bitstream reassembly
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct h264_nalu_header_t
    {
        #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        uint8_t nal_unit_type: 5;
        uint8_t nal_ref_idc:   2;
        uint8_t forbidden:     1;
        #else
            #error "little-endian only"
        #endif

    } __attribute__ ((__packed__)) h264_nalu_header_t;

    typedef struct h264_fu_header_t
    {
        #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        uint8_t type:     5;
        uint8_t reserved: 1;
        uint8_t end:      1;
        uint8_t start:    1;
        #else
            #error "little-endian only"
        #endif

    } __attribute__ ((__packed__)) h264_fu_header_t;

    typedef struct h264_context_t
    {
        /* NALU Header */
        uint8_t forbidden_zero_bit;
        uint8_t nal_ref_idc;
        uint8_t nal_unit_type;

        /* H.264 Slice Header */
        uint8_t first_mb_in_slice;
        uint8_t slice_type;
        uint8_t pic_parameter_set_id;
        uint8_t colour_plane_id; // not present in Baseline Profile
        uint8_t frame_num;

        /* H.264 Sequence Parameter Set */
        uint8_t profile_idc;
        bool    constraint_set0_flag;
        bool    constraint_set1_flag;
        bool    constraint_set2_flag;
        bool    constraint_set3_flag;
        bool    reserved_zero_4bits;
        uint8_t level_idc;
        uint8_t seq_parameter_set_id;
        uint8_t chroma_format_idc;  // not present in Baseline Profile
        bool    separate_colour_plane_flag; // not present in Baseline Profile
        uint8_t log2_max_frame_num_minus4;
        uint8_t pic_order_cnt_type;
        uint8_t log2_max_pic_order_cnt_lsb_minus4;
        bool    delta_pic_order_always_zero_flag;
        int8_t  offset_for_non_ref_pic;
        int8_t  offset_for_top_to_bottom_field;
        int8_t  num_ref_frames_in_pic_order_cnt_cycle;
        uint8_t num_ref_frames;
        bool    gaps_in_frame_num_value_allowed_flag;
        uint8_t pic_width_in_mbs_minus_1;
        uint8_t pic_height_in_map_units_minus_1;
        bool    frame_mbs_only_flag;
        bool    direct_8x8_inference_flag;
        bool    frame_cropping_flag;
        bool    vui_prameters_present_flag;
        bool    rbsp_stop_one_bit;

    } h264_context_t;

    typedef enum prefix_t prefix_t;

    bool h264_reassemble_frame(uint8_t **index, size_t *length, const uint8_t *limit,
            prefix_t prefix, const uint8_t *naluptr, size_t nalulen, bool completed,
            void *data);
    bool h264_is_fragmented(const uint8_t *naluptr, size_t nalulen);
    uint8_t h264_get_frame_type(const uint8_t *naluptr, size_t nalulen);
    bool h264_is_first_nalu(const uint8_t *naluptr, size_t nalulen);
    bool h264_is_last_nalu(const uint8_t *naluptr, size_t nalulen);

#ifdef __cplusplus
}
#endif

