/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2011-2014, 2016, 2018, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_ARCH_MSM_RESTART_H_
#define _ASM_ARCH_MSM_RESTART_H_

#define RESTART_NORMAL 0x0
#define RESTART_DLOAD  0x1

void msm_set_restart_mode(int mode);
extern int pmic_reset_irq;

#ifdef CONFIG_QCOM_WARM_REBOOT_EDL_900E_ENTRY
void qcom_warm_reboot_900e_maybe_intercept(const char *cmd);
#else
static inline void qcom_warm_reboot_900e_maybe_intercept(const char *cmd) { }
#endif

#endif
