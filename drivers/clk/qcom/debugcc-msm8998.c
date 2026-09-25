// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016, 2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2026, Edrick Sinsuan <evcsinsuan@gmail.com>
 */

#define pr_fmt(fmt) "clk: %s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#include "clk-debug.h"
#include "common.h"

static struct measure_clk_data debug_mux_priv = {
	.ctl_reg = 0x62004,
	.status_reg = 0x62008,
	.xo_div4_cbcr = 0x43008,
};

static const char * const gcc_debug_mux_parent_names[] = {
	"gpu_gcc_debug_clk",
	"gfx_gcc_debug_clk",
	"debug_mmss_clk",
	"debug_cpu_clk",
	"snoc_clk",
	"cnoc_clk",
	"bimc_clk",
	"gcc_mmss_sys_noc_axi_clk",
	"gcc_mmss_noc_cfg_ahb_clk",
	"gcc_usb30_master_clk",
	"gcc_usb30_sleep_clk",
	"gcc_usb30_mock_utmi_clk",
	"gcc_usb3_phy_aux_clk",
	"gcc_usb3_phy_pipe_clk",
	"gcc_sdcc2_apps_clk",
	"gcc_sdcc2_ahb_clk",
	"gcc_sdcc4_apps_clk",
	"gcc_sdcc4_ahb_clk",
	"gcc_blsp1_ahb_clk",
	"gcc_blsp1_qup1_spi_apps_clk",
	"gcc_blsp1_qup1_i2c_apps_clk",
	"gcc_blsp1_uart1_apps_clk",
	"gcc_blsp1_qup2_spi_apps_clk",
	"gcc_blsp1_qup2_i2c_apps_clk",
	"gcc_blsp1_uart2_apps_clk",
	"gcc_blsp1_qup3_spi_apps_clk",
	"gcc_blsp1_qup3_i2c_apps_clk",
	"gcc_blsp1_uart3_apps_clk",
	"gcc_blsp1_qup4_spi_apps_clk",
	"gcc_blsp1_qup4_i2c_apps_clk",
	"gcc_blsp1_qup5_spi_apps_clk",
	"gcc_blsp1_qup5_i2c_apps_clk",
	"gcc_blsp1_qup6_spi_apps_clk",
	"gcc_blsp1_qup6_i2c_apps_clk",
	"gcc_blsp2_ahb_clk",
	"gcc_blsp2_qup1_spi_apps_clk",
	"gcc_blsp2_qup1_i2c_apps_clk",
	"gcc_blsp2_uart1_apps_clk",
	"gcc_blsp2_qup2_spi_apps_clk",
	"gcc_blsp2_qup2_i2c_apps_clk",
	"gcc_blsp2_uart2_apps_clk",
	"gcc_blsp2_qup3_spi_apps_clk",
	"gcc_blsp2_qup3_i2c_apps_clk",
	"gcc_blsp2_uart3_apps_clk",
	"gcc_blsp2_qup4_spi_apps_clk",
	"gcc_blsp2_qup4_i2c_apps_clk",
	"gcc_blsp2_qup5_spi_apps_clk",
	"gcc_blsp2_qup5_i2c_apps_clk",
	"gcc_blsp2_qup6_spi_apps_clk",
	"gcc_blsp2_qup6_i2c_apps_clk",
	"gcc_pdm_ahb_clk",
	"gcc_pdm2_clk",
	"gcc_prng_ahb_clk",
	"gcc_tsif_ahb_clk",
	"gcc_tsif_ref_clk",
	"gcc_boot_rom_ahb_clk",
	"ce1_clk",
	"gcc_ce1_axi_m_clk",
	"gcc_ce1_ahb_m_clk",
	"measure_only_bimc_hmss_axi_clk",
	"gcc_bimc_gfx_clk",
	"gcc_hmss_rbcpr_clk",
	"gcc_gp1_clk",
	"gcc_gp2_clk",
	"gcc_gp3_clk",
	"gcc_pcie_0_slv_axi_clk",
	"gcc_pcie_0_mstr_axi_clk",
	"gcc_pcie_0_cfg_ahb_clk",
	"gcc_pcie_0_aux_clk",
	"gcc_pcie_0_pipe_clk",
	"gcc_pcie_phy_aux_clk",
	"gcc_ufs_axi_clk",
	"gcc_ufs_ahb_clk",
	"gcc_ufs_tx_symbol_0_clk",
	"gcc_ufs_rx_symbol_0_clk",
	"gcc_ufs_rx_symbol_1_clk",
	"gcc_ufs_unipro_core_clk",
	"gcc_ufs_ice_core_clk",
	"gcc_dcc_ahb_clk",
	"ipa_clk",
	"gcc_mss_cfg_ahb_clk",
	"gcc_mss_mnoc_bimc_axi_clk",
	"gcc_mss_snoc_axi_clk",
	"gcc_gpu_cfg_ahb_clk",
	"gcc_gpu_bimc_gfx_clk",
};

static int gcc_debug_mux_sels[] = {
	0x013d,
	0x013d,
	0x0022,
	0x00c0,
	0x0000,
	0x000e,
	0x014e,
	0x001f,
	0x0020,
	0x003e,
	0x003f,
	0x0040,
	0x0041,
	0x0042,
	0x0046,
	0x0047,
	0x0048,
	0x0049,
	0x004a,
	0x004c,
	0x004d,
	0x004e,
	0x0050,
	0x0051,
	0x0052,
	0x0054,
	0x0055,
	0x0056,
	0x0058,
	0x0059,
	0x005a,
	0x005b,
	0x005c,
	0x005d,
	0x005e,
	0x0060,
	0x0061,
	0x0062,
	0x0064,
	0x0065,
	0x0066,
	0x0068,
	0x0069,
	0x006a,
	0x006c,
	0x006d,
	0x006e,
	0x006f,
	0x0070,
	0x0071,
	0x0072,
	0x0074,
	0x0075,
	0x0076,
	0x0077,
	0x007a,
	0x0097,
	0x0098,
	0x0099,
	0x00bb,
	0x00ac,
	0x00bc,
	0x00df,
	0x00e0,
	0x00e1,
	0x00e2,
	0x00e3,
	0x00e4,
	0x00e5,
	0x00e6,
	0x00e8,
	0x00ea,
	0x00eb,
	0x00ec,
	0x00ed,
	0x0162,
	0x00f0,
	0x00f1,
	0x0119,
	0x011b,
	0x011f,
	0x0120,
	0x0123,
	0x013b,
	0x013f,
};

static struct clk_debug_mux gcc_debug_mux = {
	.priv = &debug_mux_priv,
	.en_mask = BIT(16),
	.debug_offset = 0x62000,
	.post_div_offset = 0x62000,
	.cbcr_offset = 0x62000,
	.src_sel_mask = 0x3ff,
	.src_sel_shift = 0,
	.post_div_mask = 0xf000,
	.post_div_shift = 12,
	.post_div_val = 1,
	.mux_sels = gcc_debug_mux_sels,
	.hw.init = &(struct clk_init_data){
		.name = "gcc_debug_mux",
		.ops = &clk_debug_mux_ops,
		.parent_names = gcc_debug_mux_parent_names,
		.num_parents = ARRAY_SIZE(gcc_debug_mux_parent_names),
		.flags = CLK_IS_MEASURE,
	},
};

static const char * const gpu_cc_debug_mux_parent_names[] = {
	"gpucc_rbcpr_clk",
	"gpucc_rbbmtimer_clk",
	"gpucc_gfx3d_isense_clk",
};

static int gpu_cc_debug_mux_sels[] = {
	0x3,
	0x5,
	0xa,
};

static struct clk_debug_mux gpu_cc_debug_mux = {
	.priv = &debug_mux_priv,
	.en_mask = BIT(16),
	.debug_offset = 0x120,
	.post_div_offset = 0x120,
	.cbcr_offset = 0x120,
	.src_sel_mask = 0x3ff,
	.src_sel_shift = 0,
	.post_div_mask = 0,
	.post_div_shift = 0,
	.post_div_val = 1,
	.mux_sels = gpu_cc_debug_mux_sels,
	.hw.init = &(struct clk_init_data){
		.name = "gpu_gcc_debug_clk",
		.ops = &clk_debug_mux_ops,
		.parent_names = gpu_cc_debug_mux_parent_names,
		.num_parents = ARRAY_SIZE(gpu_cc_debug_mux_parent_names),
		.flags = CLK_IS_MEASURE,
	},
};

static const char * const gfx_cc_debug_mux_parent_names[] = {
	"gpucc_gfx3d_clk",
};

static int gfx_cc_debug_mux_sels[] = {
	0x8,
};

static struct clk_debug_mux gfx_cc_debug_mux = {
	.priv = &debug_mux_priv,
	.en_mask = BIT(16),
	.debug_offset = 0x120,
	.post_div_offset = 0x120,
	.cbcr_offset = 0x120,
	.src_sel_mask = 0x3ff,
	.src_sel_shift = 0,
	.post_div_mask = 0,
	.post_div_shift = 0,
	.post_div_val = 1,
	.mux_sels = gfx_cc_debug_mux_sels,
	.hw.init = &(struct clk_init_data){
		.name = "gfx_gcc_debug_clk",
		.ops = &clk_debug_mux_ops,
		.parent_names = gfx_cc_debug_mux_parent_names,
		.num_parents = ARRAY_SIZE(gfx_cc_debug_mux_parent_names),
		.flags = CLK_IS_MEASURE,
	},
};

static const char * const mmss_cc_debug_mux_parent_names[] = {
	"mmss_mnoc_ahb_clk",
	"mmss_misc_ahb_clk",
	"mmss_vmem_maxi_clk",
	"mmss_vmem_ahb_clk",
	"mmss_bimc_smmu_ahb_clk",
	"mmss_bimc_smmu_axi_clk",
	"mmss_video_core_clk",
	"mmss_video_axi_clk",
	"mmss_video_maxi_clk",
	"mmss_video_ahb_clk",
	"mmss_mdss_rot_clk",
	"mmss_snoc_dvm_axi_clk",
	"mmss_mdss_mdp_clk",
	"mmss_mdss_mdp_lut_clk",
	"mmss_mdss_pclk0_clk",
	"mmss_mdss_pclk1_clk",
	"mmss_mdss_extpclk_clk",
	"mmss_video_subcore0_clk",
	"mmss_video_subcore1_clk",
	"mmss_mdss_vsync_clk",
	"mmss_mdss_hdmi_clk",
	"mmss_mdss_byte0_clk",
	"mmss_mdss_byte1_clk",
	"mmss_mdss_esc0_clk",
	"mmss_mdss_esc1_clk",
	"mmss_mdss_ahb_clk",
	"mmss_mdss_hdmi_dp_ahb_clk",
	"mmss_mdss_axi_clk",
	"mmss_camss_top_ahb_clk",
	"mmss_camss_micro_ahb_clk",
	"mmss_camss_gp0_clk",
	"mmss_camss_gp1_clk",
	"mmss_camss_mclk0_clk",
	"mmss_camss_mclk1_clk",
	"mmss_camss_mclk2_clk",
	"mmss_camss_mclk3_clk",
	"mmss_camss_cci_clk",
	"mmss_camss_cci_ahb_clk",
	"mmss_camss_csi0phytimer_clk",
	"mmss_camss_csi1phytimer_clk",
	"mmss_camss_csi2phytimer_clk",
	"mmss_camss_jpeg0_clk",
	"mmss_camss_ispif_ahb_clk",
	"mmss_camss_jpeg_ahb_clk",
	"mmss_camss_jpeg_axi_clk",
	"mmss_camss_ahb_clk",
	"mmss_camss_vfe0_clk",
	"mmss_camss_vfe1_clk",
	"mmss_camss_cpp_clk",
	"mmss_camss_cpp_ahb_clk",
	"mmss_camss_csi_vfe0_clk",
	"mmss_camss_csi_vfe1_clk",
	"mmss_camss_csi0_clk",
	"mmss_camss_csi0_ahb_clk",
	"mmss_camss_csiphy0_clk",
	"mmss_camss_csi0rdi_clk",
	"mmss_camss_csi0pix_clk",
	"mmss_camss_csi1_clk",
	"mmss_camss_csi1_ahb_clk",
	"mmss_camss_csi1rdi_clk",
	"mmss_camss_csi1pix_clk",
	"mmss_camss_csi2_clk",
	"mmss_camss_csi2_ahb_clk",
	"mmss_camss_csi2rdi_clk",
	"mmss_camss_csi2pix_clk",
	"mmss_camss_csi3_clk",
	"mmss_camss_csi3_ahb_clk",
	"mmss_camss_csi3rdi_clk",
	"mmss_camss_csi3pix_clk",
	"mmss_mnoc_maxi_clk",
	"mmss_camss_vfe0_stream_clk",
	"mmss_camss_vfe1_stream_clk",
	"mmss_camss_cpp_vbif_ahb_clk",
	"mmss_misc_cxo_clk",
	"mmss_camss_cpp_axi_clk",
	"mmss_camss_csiphy1_clk",
	"mmss_camss_vfe0_ahb_clk",
	"mmss_camss_vfe1_ahb_clk",
	"mmss_camss_csiphy2_clk",
	"mmss_fd_core_clk",
	"mmss_fd_core_uar_clk",
	"mmss_fd_ahb_clk",
	"mmss_camss_cphy_csid0_clk",
	"mmss_camss_cphy_csid1_clk",
	"mmss_camss_cphy_csid2_clk",
	"mmss_camss_cphy_csid3_clk",
	"mmss_mdss_dp_link_clk",
	"mmss_mdss_dp_link_intf_clk",
	"mmss_mdss_dp_crypto_clk",
	"mmss_mdss_dp_pixel_clk",
	"mmss_mdss_dp_aux_clk",
	"mmss_mdss_dp_gtc_clk",
	"mmss_mdss_byte0_intf_clk",
	"mmss_mdss_byte1_intf_clk",
};

static int mmss_cc_debug_mux_sels[] = {
	0x0001,
	0x0003,
	0x0009,
	0x000a,
	0x000c,
	0x000d,
	0x000e,
	0x000f,
	0x0010,
	0x0011,
	0x0012,
	0x0013,
	0x0014,
	0x0015,
	0x0016,
	0x0017,
	0x0018,
	0x001a,
	0x001b,
	0x001c,
	0x001d,
	0x001e,
	0x001f,
	0x0020,
	0x0021,
	0x0022,
	0x0023,
	0x0024,
	0x0025,
	0x0026,
	0x0027,
	0x0028,
	0x0029,
	0x002a,
	0x002b,
	0x002c,
	0x002d,
	0x002e,
	0x002f,
	0x0030,
	0x0031,
	0x0032,
	0x0033,
	0x0035,
	0x0036,
	0x0037,
	0x0038,
	0x0039,
	0x003a,
	0x003b,
	0x003f,
	0x0040,
	0x0041,
	0x0042,
	0x0043,
	0x0044,
	0x0045,
	0x0046,
	0x0047,
	0x0049,
	0x004a,
	0x004b,
	0x004c,
	0x004e,
	0x004f,
	0x0050,
	0x0051,
	0x0053,
	0x0054,
	0x0070,
	0x0071,
	0x0072,
	0x0073,
	0x0077,
	0x007a,
	0x0085,
	0x0086,
	0x0087,
	0x0088,
	0x0089,
	0x008a,
	0x008c,
	0x008d,
	0x008e,
	0x008f,
	0x0090,
	0x0098,
	0x0099,
	0x009a,
	0x009b,
	0x009c,
	0x009d,
	0x00ad,
	0x00ae,
};

static struct clk_debug_mux mmss_cc_debug_mux = {
	.priv = &debug_mux_priv,
	.en_mask = BIT(16),
	.debug_offset = 0x900,
	.post_div_offset = 0x900,
	.cbcr_offset = 0x900,
	.src_sel_mask = 0x3ff,
	.src_sel_shift = 0,
	.post_div_mask = 0,
	.post_div_shift = 0,
	.post_div_val = 1,
	.mux_sels = mmss_cc_debug_mux_sels,
	.hw.init = &(struct clk_init_data){
		.name = "debug_mmss_clk",
		.ops = &clk_debug_mux_ops,
		.parent_names = mmss_cc_debug_mux_parent_names,
		.num_parents = ARRAY_SIZE(mmss_cc_debug_mux_parent_names),
		.flags = CLK_IS_MEASURE,
	},
};

static const char * const cpu_cc_debug_mux_parent_names[] = {
	"pwrcl_clk",
	"perfcl_clk",
};

static int cpu_cc_debug_mux_sels[] = {
	0x0,
	0x1,
};

static int cpu_cc_debug_mux_pre_divs[] = {
	4,
	8,
};

static struct clk_debug_mux cpu_cc_debug_mux = {
	.priv = &debug_mux_priv,
	.en_mask = 0,
	.debug_offset = 0x0,
	.post_div_offset = 0x0,
	.cbcr_offset = U32_MAX,
	.src_sel_mask = 0x3,
	.src_sel_shift = 8,
	.post_div_mask = 0,
	.post_div_shift = 0,
	.post_div_val = 1,
	.mux_sels = cpu_cc_debug_mux_sels,
	.pre_div_vals = cpu_cc_debug_mux_pre_divs,
	.hw.init = &(struct clk_init_data){
		.name = "debug_cpu_clk",
		.ops = &clk_debug_mux_ops,
		.parent_names = cpu_cc_debug_mux_parent_names,
		.num_parents = ARRAY_SIZE(cpu_cc_debug_mux_parent_names),
		.flags = CLK_IS_MEASURE,
	},
};


static struct clk_dummy bimc_hmss_axi_clk_dummy_clk = {
	.rrate = 1000,
	.hw.init = &(struct clk_init_data){
		.name = "measure_only_bimc_hmss_axi_clk",
		.ops = &clk_dummy_ops,
	},
};

static struct clk_hw *debugcc_msm8998_hws[] = {
	&bimc_hmss_axi_clk_dummy_clk.hw,
};

static struct mux_regmap_names mux_list[] = {
	{ .mux = &gcc_debug_mux, .regmap_name = "qcom,gcc" },
	{ .mux = &gpu_cc_debug_mux, .regmap_name = "qcom,gpu" },
	{ .mux = &gfx_cc_debug_mux, .regmap_name = "qcom,gpu" },
	{ .mux = &mmss_cc_debug_mux, .regmap_name = "qcom,mmss" },
	{ .mux = &cpu_cc_debug_mux, .regmap_name = "qcom,cpu" },
};

static const struct of_device_id clk_debug_match_table[] = {
	{ .compatible = "qcom,msm8998-debugcc" },
	{}
};

static int clk_debug_msm8998_probe(struct platform_device *pdev)
{
	struct clk *clk;
	int ret = 0, i;

	BUILD_BUG_ON(ARRAY_SIZE(gcc_debug_mux_parent_names) !=
		     ARRAY_SIZE(gcc_debug_mux_sels));
	BUILD_BUG_ON(ARRAY_SIZE(mmss_cc_debug_mux_parent_names) !=
		     ARRAY_SIZE(mmss_cc_debug_mux_sels));

	clk = devm_clk_get(&pdev->dev, "xo_clk_src");
	if (IS_ERR(clk)) {
		if (PTR_ERR(clk) != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Unable to get xo clock\n");
		return PTR_ERR(clk);
	}

	debug_mux_priv.cxo = clk;

	for (i = 0; i < ARRAY_SIZE(mux_list); i++) {
		ret = map_debug_bases(pdev, mux_list[i].regmap_name,
				      mux_list[i].mux);
		if (ret == -EBADR)
			continue;
		else if (ret)
			return ret;

		clk = devm_clk_register(&pdev->dev, &mux_list[i].mux->hw);
		if (IS_ERR(clk)) {
			dev_err(&pdev->dev, "Unable to register %s, err:(%ld)\n",
				mux_list[i].mux->hw.init->name, PTR_ERR(clk));
			return PTR_ERR(clk);
		}
	}

	for (i = 0; i < ARRAY_SIZE(debugcc_msm8998_hws); i++) {
		clk = devm_clk_register(&pdev->dev, debugcc_msm8998_hws[i]);
		if (IS_ERR(clk)) {
			dev_err(&pdev->dev, "Unable to register %s, err:(%ld)\n",
				debugcc_msm8998_hws[i]->init->name,
				PTR_ERR(clk));
			return PTR_ERR(clk);
		}
	}

	ret = clk_debug_measure_register(&gcc_debug_mux.hw);
	if (ret)
		dev_err(&pdev->dev, "Could not register Measure clock\n");

	return ret;
}

static struct platform_driver clk_debug_driver = {
	.probe = clk_debug_msm8998_probe,
	.driver = {
		.name = "debugcc-msm8998",
		.of_match_table = clk_debug_match_table,
	},
};

static int __init clk_debug_msm8998_init(void)
{
	return platform_driver_register(&clk_debug_driver);
}
fs_initcall(clk_debug_msm8998_init);
