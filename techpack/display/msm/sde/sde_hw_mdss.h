/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015-2019, 2021, The Linux Foundation. All rights reserved.
 */

#ifndef _SDE_HW_MDSS_H
#define _SDE_HW_MDSS_H

#include <linux/kernel.h>
#include <linux/err.h>

#include "msm_drv.h"

#define SDE_DBG_NAME			"sde"

#define SDE_NONE                        0

#ifndef SDE_CSC_MATRIX_COEFF_SIZE
#define SDE_CSC_MATRIX_COEFF_SIZE	9
#endif

#ifndef SDE_CSC_CLAMP_SIZE
#define SDE_CSC_CLAMP_SIZE		6
#endif

#ifndef SDE_CSC_BIAS_SIZE
#define SDE_CSC_BIAS_SIZE		3
#endif

#ifndef SDE_MAX_PLANES
#define SDE_MAX_PLANES			4
#endif

#define PIPES_PER_STAGE			2
#ifndef SDE_MAX_DE_CURVES
#define SDE_MAX_DE_CURVES		3
#endif

#define MAX_DSI_DISPLAYS		2
#define MAX_DATA_PATH_PER_DSIPLAY	2

enum sde_format_flags {
	SDE_FORMAT_FLAG_YUV_BIT,
	SDE_FORMAT_FLAG_DX_BIT,
	SDE_FORMAT_FLAG_COMPRESSED_BIT,
	SDE_FORMAT_FLAG_BIT_MAX,
};

#define SDE_FORMAT_FLAG_YUV		BIT(SDE_FORMAT_FLAG_YUV_BIT)
#define SDE_FORMAT_FLAG_DX		BIT(SDE_FORMAT_FLAG_DX_BIT)
#define SDE_FORMAT_FLAG_COMPRESSED	BIT(SDE_FORMAT_FLAG_COMPRESSED_BIT)
#define SDE_FORMAT_IS_YUV(X)		\
	(test_bit(SDE_FORMAT_FLAG_YUV_BIT, (X)->flag))
#define SDE_FORMAT_IS_DX(X)		\
	(test_bit(SDE_FORMAT_FLAG_DX_BIT, (X)->flag))
#define SDE_FORMAT_IS_LINEAR(X)		((X)->fetch_mode == SDE_FETCH_LINEAR)
#define SDE_FORMAT_IS_TILE(X) \
	(((X)->fetch_mode == SDE_FETCH_UBWC) && \
			!test_bit(SDE_FORMAT_FLAG_COMPRESSED_BIT, (X)->flag))
#define SDE_FORMAT_IS_UBWC(X) \
	(((X)->fetch_mode == SDE_FETCH_UBWC) && \
			test_bit(SDE_FORMAT_FLAG_COMPRESSED_BIT, (X)->flag))

#define SDE_BLEND_FG_ALPHA_FG_CONST	(0 << 0)
#define SDE_BLEND_FG_ALPHA_BG_CONST	(1 << 0)
#define SDE_BLEND_FG_ALPHA_FG_PIXEL	(2 << 0)
#define SDE_BLEND_FG_ALPHA_BG_PIXEL	(3 << 0)
#define SDE_BLEND_FG_INV_ALPHA		(1 << 2)
#define SDE_BLEND_FG_MOD_ALPHA		(1 << 3)
#define SDE_BLEND_FG_INV_MOD_ALPHA	(1 << 4)
#define SDE_BLEND_FG_TRANSP_EN		(1 << 5)
#define SDE_BLEND_BG_ALPHA_FG_CONST	(0 << 8)
#define SDE_BLEND_BG_ALPHA_BG_CONST	(1 << 8)
#define SDE_BLEND_BG_ALPHA_FG_PIXEL	(2 << 8)
#define SDE_BLEND_BG_ALPHA_BG_PIXEL	(3 << 8)
#define SDE_BLEND_BG_INV_ALPHA		(1 << 10)
#define SDE_BLEND_BG_MOD_ALPHA		(1 << 11)
#define SDE_BLEND_BG_INV_MOD_ALPHA	(1 << 12)
#define SDE_BLEND_BG_TRANSP_EN		(1 << 13)

#define SDE_VSYNC0_SOURCE_GPIO		0
#define SDE_VSYNC1_SOURCE_GPIO		1
#define SDE_VSYNC2_SOURCE_GPIO		2
#define SDE_VSYNC_SOURCE_INTF_0		3
#define SDE_VSYNC_SOURCE_INTF_1		4
#define SDE_VSYNC_SOURCE_INTF_2		5
#define SDE_VSYNC_SOURCE_INTF_3		6
#define SDE_VSYNC_SOURCE_WD_TIMER_4	11
#define SDE_VSYNC_SOURCE_WD_TIMER_3	12
#define SDE_VSYNC_SOURCE_WD_TIMER_2	13
#define SDE_VSYNC_SOURCE_WD_TIMER_1	14
#define SDE_VSYNC_SOURCE_WD_TIMER_0	15

enum sde_hw_blk_type {
	SDE_HW_BLK_TOP = 0,
	SDE_HW_BLK_SSPP,
	SDE_HW_BLK_LM,
	SDE_HW_BLK_DSPP,
	SDE_HW_BLK_DS,
	SDE_HW_BLK_CTL,
	SDE_HW_BLK_CDM,
	SDE_HW_BLK_PINGPONG,
	SDE_HW_BLK_INTF,
	SDE_HW_BLK_WB,
	SDE_HW_BLK_DSC,
	SDE_HW_BLK_MERGE_3D,
	SDE_HW_BLK_QDSS,
	SDE_HW_BLK_MAX,
};

enum sde_uidle {
	UIDLE = 0x1,
	UIDLE_MAX,
};

enum sde_mdp {
	MDP_TOP = 0x1,
	MDP_MAX,
};

enum sde_sspp {
	SSPP_NONE,
	SSPP_VIG0,
	SSPP_VIG1,
	SSPP_VIG2,
	SSPP_VIG3,
	SSPP_RGB0,
	SSPP_RGB1,
	SSPP_RGB2,
	SSPP_RGB3,
	SSPP_DMA0,
	SSPP_DMA1,
	SSPP_DMA2,
	SSPP_DMA3,
	SSPP_CURSOR0,
	SSPP_CURSOR1,
	SSPP_MAX
};

enum sde_sspp_type {
	SSPP_TYPE_VIG,
	SSPP_TYPE_RGB,
	SSPP_TYPE_DMA,
	SSPP_TYPE_CURSOR,
	SSPP_TYPE_MAX
};

enum sde_lm {
	LM_0 = 1,
	LM_1,
	LM_2,
	LM_3,
	LM_4,
	LM_5,
	LM_6,
	LM_MAX
};

enum sde_stage {
	SDE_STAGE_BASE = 0,
	SDE_STAGE_0,
	SDE_STAGE_1,
	SDE_STAGE_2,
	SDE_STAGE_3,
	SDE_STAGE_4,
	SDE_STAGE_5,
	SDE_STAGE_6,
	SDE_STAGE_7,
	SDE_STAGE_8,
	SDE_STAGE_9,
	SDE_STAGE_10,
	SDE_STAGE_11,
	SDE_STAGE_MAX
};

enum sde_dspp {
	DSPP_0 = 1,
	DSPP_1,
	DSPP_2,
	DSPP_3,
	DSPP_MAX
};

enum sde_ltm {
	LTM_0 = DSPP_0,
	LTM_1,
	LTM_MAX
};

enum sde_rc {
	RC_0 = DSPP_0,
	RC_1,
	RC_MAX
};

enum sde_ds {
	DS_TOP,
	DS_0,
	DS_1,
	DS_MAX
};

enum sde_ctl {
	CTL_0 = 1,
	CTL_1,
	CTL_2,
	CTL_3,
	CTL_4,
	CTL_5,
	CTL_MAX
};

enum sde_cdm {
	CDM_0 = 1,
	CDM_1,
	CDM_MAX
};

enum sde_pingpong {
	PINGPONG_0 = 1,
	PINGPONG_1,
	PINGPONG_2,
	PINGPONG_3,
	PINGPONG_4,
	PINGPONG_5,
	PINGPONG_S0,
	PINGPONG_MAX
};

enum sde_dsc {
	DSC_NONE = 0,
	DSC_0,
	DSC_1,
	DSC_2,
	DSC_3,
	DSC_4,
	DSC_5,
	DSC_MAX
};

enum sde_intf {
	INTF_0 = 1,
	INTF_1,
	INTF_2,
	INTF_3,
	INTF_4,
	INTF_5,
	INTF_6,
	INTF_MAX
};

enum sde_intf_type {
	INTF_NONE = 0x0,
	INTF_DSI = 0x1,
	INTF_HDMI = 0x3,
	INTF_LCDC = 0x5,
	INTF_EDP = 0x9,
	INTF_DP = 0xa,
	INTF_TYPE_MAX,

	/* virtual interfaces */
	INTF_WB = 0x100,
};

enum sde_intf_mode {
	INTF_MODE_NONE = 0,
	INTF_MODE_CMD,
	INTF_MODE_VIDEO,
	INTF_MODE_WB_BLOCK,
	INTF_MODE_WB_LINE,
	INTF_MODE_MAX
};

enum sde_wb {
	WB_0 = 1,
	WB_1,
	WB_2,
	WB_3,
	WB_MAX
};

enum sde_ad {
	AD_0 = 0x1,
	AD_1,
	AD_MAX
};

enum sde_cwb {
	CWB_0 = 0x1,
	CWB_1,
	CWB_2,
	CWB_3,
	CWB_4,
	CWB_5,
	CWB_MAX
};

enum sde_wd_timer {
	WD_TIMER_0 = 0x1,
	WD_TIMER_1,
	WD_TIMER_2,
	WD_TIMER_3,
	WD_TIMER_4,
	WD_TIMER_5,
	WD_TIMER_MAX
};

enum sde_vbif {
	VBIF_0,
	VBIF_1,
	VBIF_MAX,
	VBIF_RT = VBIF_0,
	VBIF_NRT = VBIF_1
};

enum sde_iommu_domain {
	SDE_IOMMU_DOMAIN_UNSECURE,
	SDE_IOMMU_DOMAIN_SECURE,
	SDE_IOMMU_DOMAIN_MAX
};

enum sde_rot {
	ROT_0 = 1,
	ROT_MAX
};

enum sde_merge_3d {
	MERGE_3D_0 = 1,
	MERGE_3D_1,
	MERGE_3D_2,
	MERGE_3D_MAX
};

enum sde_qdss {
	QDSS_0,
	QDSS_MAX
};

/**
 * SDE HW,Component order color map
 */
enum {
	C0_G_Y = 0,
	C1_B_Cb = 1,
	C2_R_Cr = 2,
	C3_ALPHA = 3
};

/**
 * enum sde_plane_type - defines how the color component pixel packing
 * @SDE_PLANE_INTERLEAVED   : Color components in single plane
 * @SDE_PLANE_PLANAR        : Color component in separate planes
 * @SDE_PLANE_PSEUDO_PLANAR : Chroma components interleaved in separate plane
 */
enum sde_plane_type {
	SDE_PLANE_INTERLEAVED,
	SDE_PLANE_PLANAR,
	SDE_PLANE_PSEUDO_PLANAR,
};

/**
 * enum sde_chroma_samp_type - chroma sub-samplng type
 * @SDE_CHROMA_RGB   : No chroma subsampling
 * @SDE_CHROMA_H2V1  : Chroma pixels are horizontally subsampled
 * @SDE_CHROMA_H1V2  : Chroma pixels are vertically subsampled
 * @SDE_CHROMA_420   : 420 subsampling
 */
enum sde_chroma_samp_type {
	SDE_CHROMA_RGB,
	SDE_CHROMA_H2V1,
	SDE_CHROMA_H1V2,
	SDE_CHROMA_420
};

/**
 * sde_fetch_type - Defines How SDE HW fetches data
 * @SDE_FETCH_LINEAR   : fetch is line by line
 * @SDE_FETCH_TILE     : fetches data in Z order from a tile
 * @SDE_FETCH_UBWC     : fetch and decompress data
 */
enum sde_fetch_type {
	SDE_FETCH_LINEAR,
	SDE_FETCH_TILE,
	SDE_FETCH_UBWC
};

/**
 * Value of enum chosen to fit the number of bits
 * expected by the HW programming.
 */
enum {
	COLOR_ALPHA_1BIT = 0,
	COLOR_ALPHA_4BIT = 1,
	COLOR_4BIT = 0,
	COLOR_5BIT = 1, /* No 5-bit Alpha */
	COLOR_6BIT = 2, /* 6-Bit Alpha also = 2 */
	COLOR_8BIT = 3, /* 8-Bit Alpha also = 3 */
};

/**
 * enum sde_3d_blend_mode
 * Desribes how the 3d data is blended
 * @BLEND_3D_NONE      : 3d blending not enabled
 * @BLEND_3D_FRAME_INT : Frame interleaving
 * @BLEND_3D_H_ROW_INT : Horizontal row interleaving
 * @BLEND_3D_V_ROW_INT : vertical row interleaving
 * @BLEND_3D_COL_INT   : column interleaving
 * @BLEND_3D_MAX       :
 */
enum sde_3d_blend_mode {
	BLEND_3D_NONE = 0,
	BLEND_3D_FRAME_INT,
	BLEND_3D_H_ROW_INT,
	BLEND_3D_V_ROW_INT,
	BLEND_3D_COL_INT,
	BLEND_3D_MAX
};

/** struct sde_format - defines the format configuration which
 * allows SDE HW to correctly fetch and decode the format
 * @base: base msm_format struture containing fourcc code
 * @fetch_planes: how the color components are packed in pixel format
 * @element: element color ordering
 * @bits: element bit widths
 * @chroma_sample: chroma sub-samplng type
 * @unpack_align_msb: unpack aligned, 0 to LSB, 1 to MSB
 * @unpack_tight: 0 for loose, 1 for tight
 * @unpack_count: 0 = 1 component, 1 = 2 component
 * @bpp: bytes per pixel
 * @alpha_enable: whether the format has an alpha channel
 * @num_planes: number of planes (including meta data planes)
 * @fetch_mode: linear, tiled, or ubwc hw fetch behavior
 * @is_yuv: is format a yuv variant
 * @flag: usage bit flags
 * @tile_width: format tile width
 * @tile_height: format tile height
 */
struct sde_format {
	struct msm_format base;
	enum sde_plane_type fetch_planes;
	u8 element[SDE_MAX_PLANES];
	u8 bits[SDE_MAX_PLANES];
	enum sde_chroma_samp_type chroma_sample;
	u8 unpack_align_msb;
	u8 unpack_tight;
	u8 unpack_count;
	u8 bpp;
	u8 alpha_enable;
	u8 num_planes;
	enum sde_fetch_type fetch_mode;
	DECLARE_BITMAP(flag, SDE_FORMAT_FLAG_BIT_MAX);
	u16 tile_width;
	u16 tile_height;
};
#define to_sde_format(x) container_of(x, struct sde_format, base)

/**
 * struct sde_hw_fmt_layout - format information of the source pixel data
 * @format: pixel format parameters
 * @num_planes: number of planes (including meta data planes)
 * @width: image width
 * @height: image height
 * @total_size: total size in bytes
 * @plane_addr: address of each plane
 * @plane_size: length of each plane
 * @plane_pitch: pitch of each plane
 */
struct sde_hw_fmt_layout {
	const struct sde_format *format;
	uint32_t num_planes;
	uint32_t width;
	uint32_t height;
	uint32_t total_size;
	uint32_t plane_addr[SDE_MAX_PLANES];
	uint32_t plane_size[SDE_MAX_PLANES];
	uint32_t plane_pitch[SDE_MAX_PLANES];
};

struct sde_rect {
	u16 x;
	u16 y;
	u16 w;
	u16 h;
};

struct sde_csc_cfg {
	/* matrix coefficients in S15.16 format */
	uint32_t csc_mv[SDE_CSC_MATRIX_COEFF_SIZE];
	uint32_t csc_pre_bv[SDE_CSC_BIAS_SIZE];
	uint32_t csc_post_bv[SDE_CSC_BIAS_SIZE];
	uint32_t csc_pre_lv[SDE_CSC_CLAMP_SIZE];
	uint32_t csc_post_lv[SDE_CSC_CLAMP_SIZE];
};

/**
 * struct sde_mdss_color - mdss color description
 * color 0 : green
 * color 1 : blue
 * color 2 : red
 * color 3 : alpha
 */
struct sde_mdss_color {
	u32 color_0;
	u32 color_1;
	u32 color_2;
	u32 color_3;
};

/*
 * Define bit masks for h/w logging.
 */
#define SDE_DBG_MASK_NONE     (1 << 0)
#define SDE_DBG_MASK_CDM      (1 << 1)
#define SDE_DBG_MASK_DSPP     (1 << 2)
#define SDE_DBG_MASK_INTF     (1 << 3)
#define SDE_DBG_MASK_LM       (1 << 4)
#define SDE_DBG_MASK_CTL      (1 << 5)
#define SDE_DBG_MASK_PINGPONG (1 << 6)
#define SDE_DBG_MASK_SSPP     (1 << 7)
#define SDE_DBG_MASK_WB       (1 << 8)
#define SDE_DBG_MASK_TOP      (1 << 9)
#define SDE_DBG_MASK_VBIF     (1 << 10)
#define SDE_DBG_MASK_DSC      (1 << 11)
#define SDE_DBG_MASK_ROT      (1 << 12)
#define SDE_DBG_MASK_DS       (1 << 13)
#define SDE_DBG_MASK_REGDMA   (1 << 14)
#define SDE_DBG_MASK_UIDLE    (1 << 15)
#define SDE_DBG_MASK_SID      (1 << 15)
#define SDE_DBG_MASK_QDSS     (1 << 16)

/**
 * struct sde_hw_cp_cfg: hardware dspp/lm feature payload.
 * @payload: Feature specific payload.
 * @len: Length of the payload.
 * @ctl: control pointer associated with dspp/lm.
 * @last_feature: last feature that will be set.
 * @num_of_mixers: number of layer mixers for the display.
 * @mixer_info: mixer info pointer associated with lm.
 * @displayv: height of the display.
 * @displayh: width of the display.
 * @dspp[DSPP_MAX]: array of hw_dspp pointers associated with crtc.
 * @broadcast_disabled: flag indicating if broadcast should be avoided when
 *			using LUTDMA
 */
struct sde_hw_cp_cfg {
	void *payload;
	u32 len;
	void *ctl;
	u32 last_feature;
	u32 num_of_mixers;
	void *mixer_info;
	u32 displayv;
	u32 displayh;
	struct sde_hw_dspp *dspp[DSPP_MAX];
	bool broadcast_disabled;
	u32 mi_dimlayer_type;
	void *payload_clear;
};

/**
 * struct sde_hw_dim_layer: dim layer configs
 * @flags: Flag to represent INCLUSIVE/EXCLUSIVE
 * @stage: Blending stage of dim layer
 * @color_fill: Color fill to be used for the layer
 * @rect: Dim layer coordinates
 */
struct sde_hw_dim_layer {
	uint32_t flags;
	uint32_t stage;
	struct sde_mdss_color color_fill;
	struct sde_rect rect;
};

/**
 * struct sde_splash_mem - Struct contains splah memory info
 * @splash_buf_size: Indicates the size of the memory region
 * @splash_buf_base: Address of specific splash memory region
 * @ramdump_size: Size of ramdump buffer region
 * @ramdump_base: Address of ramdump region reserved by bootloader
 * @ref_cnt:	Tracks the map count to help in sharing splash memory
 */
struct sde_splash_mem {
	u32 splash_buf_size;
	unsigned long splash_buf_base;
	u32 ramdump_size;
	unsigned long ramdump_base;
	u32 ref_cnt;
};

/**
 * struct sde_sspp_index_info - Struct containing sspp identifier info
 * @sspp:	Enum value indicates sspp id
 * @is_virtual: Boolean to identify if virtual or base
 */
struct sde_sspp_index_info {
	enum sde_sspp sspp;
	bool is_virtual;
};

/**
 * struct sde_splash_data - Struct contains details of resources and hw blocks
 * used in continuous splash on a specific display.
 * @cont_splash_enabled:  Stores the cont_splash status (enabled/disabled)
 * @encoder:	Pointer to the drm encoder object used for this display
 * @splash:     Pointer to struct sde_splash_mem used for this display
 * @ctl_ids:	Stores the valid MDSS ctl block ids for the current mode
 * @lm_ids:	Stores the valid MDSS layer mixer block ids for the current mode
 * @dsc_ids:	Stores the valid MDSS DSC block ids for the current mode
 * @pipes:      Array of sspp info detected on this display
 * @ctl_cnt:    Stores the active number of MDSS "top" blks of the current mode
 * @lm_cnt:	Stores the active number of MDSS "LM" blks for the current mode
 * @dsc_cnt:	Stores the active number of MDSS "dsc" blks for the current mode
 * @pipe_cnt:	Stores the active number of "sspp" blks connected
 */
struct sde_splash_display {
	bool cont_splash_enabled;
	struct drm_encoder *encoder;
	struct sde_splash_mem *splash;
	u8 ctl_ids[MAX_DATA_PATH_PER_DSIPLAY];
	u8 lm_ids[MAX_DATA_PATH_PER_DSIPLAY];
	u8 dsc_ids[MAX_DATA_PATH_PER_DSIPLAY];
	struct sde_sspp_index_info pipes[MAX_DATA_PATH_PER_DSIPLAY];
	u8 ctl_cnt;
	u8 lm_cnt;
	u8 dsc_cnt;
	u8 pipe_cnt;
};

/**
 * struct sde_splash_data - Struct contains details of continuous splash
 *	for all the displays connected by probe time
 * @num_splash_regions:  Indicates number of splash memory regions from dtsi
 * @num_splash_displays: Indicates count of active displays in continuous splash
 * @splash_mem:          Array of all struct sde_splash_mem listed from dtsi
 * @splash_display:      Array of all struct sde_splash_display
 */
struct sde_splash_data {
	u32 num_splash_regions;
	u32 num_splash_displays;
	struct sde_splash_mem splash_mem[MAX_DSI_DISPLAYS];
	struct sde_splash_display splash_display[MAX_DSI_DISPLAYS];
};

/**
 * struct sde_hw_tear_check - Struct contains parameters to configure
 *	tear-effect module. This structure is used to configure tear-check
 *	logic present either in ping-pong or in interface module.
 * @vsync_count:	Ratio of MDP VSYNC clk freq(Hz) to refresh rate divided
 *                      by no of lines
 * @sync_cfg_height:	Total vertical lines (display height - 1)
 * @vsync_init_val:	Init value to which the read pointer gets loaded at
 *                      vsync edge
 * @sync_threshold_start: Read pointer threshold start ROI for write operation
 * @sync_threshold_continue: The minimum number of lines the write pointer
 *                           needs to be above the read pointer
 * @start_pos:	The position from which the start_threshold value is added
 * @rd_ptr_irq:	The read pointer line at which interrupt has to be generated
 * @wr_ptr_irq:	The write pointer line at which interrupt has to be generated
 * @hw_vsync_mode:	Sync with external frame sync input
 */
struct sde_hw_tear_check {
	u32 vsync_count;
	u32 sync_cfg_height;
	u32 vsync_init_val;
	u32 sync_threshold_start;
	u32 sync_threshold_continue;
	u32 start_pos;
	u32 rd_ptr_irq;
	u32 wr_ptr_irq;
	u8 hw_vsync_mode;
};

/**
 * struct sde_hw_autorefresh - Struct contains parameters to configure
 *            auto-refresh mode for command mode panels
 * @enable:	Enalbe or disable the auto-refresh mode
 * @frame_count:	Auto-refresh frame counter at which update occurs
 */
struct sde_hw_autorefresh {
	bool  enable;
	u32 frame_count;
};

/**
 * struct sde_hw_pp_vsync_info - Struct contains parameters to configure
 *        read and write pointers for command mode panels
 * @pp_idx:		Ping-pong block index
 * @intf_idx:		Interface block index
 * @wr_ptr_line_count:	current line within pp fifo (wr ptr)
 */
struct sde_hw_pp_vsync_info {
	u32 pp_idx;
	u32 intf_idx;
	u32 wr_ptr_line_count;
};

#endif  /* _SDE_HW_MDSS_H */
