// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <asm/cacheflush.h>
#include <soc/qcom/scm.h>
#include <soc/qcom/qtee_shmbridge.h>
#include <linux/crypto-qti-common.h>
#include "crypto-qti-platform.h"
#include "crypto-qti-tz.h"

unsigned int storage_type = SDCC_CE;
bool qti_ice_legacy_v2;

/* legacy (ICE v2 era) TZ ES commands, as used by the 4.4 pfk_ice driver */
#define TZ_ES_SET_ICE_KEY		0x2
#define TZ_ES_INVALIDATE_ICE_KEY	0x3
#define TZ_ES_SET_ICE_KEY_ID \
	TZ_SYSCALL_CREATE_SMC_ID(TZ_OWNER_SIP, TZ_SVC_ES, TZ_ES_SET_ICE_KEY)
#define TZ_ES_INVALIDATE_ICE_KEY_ID \
	TZ_SYSCALL_CREATE_SMC_ID(TZ_OWNER_SIP, TZ_SVC_ES, \
				 TZ_ES_INVALIDATE_ICE_KEY)
#define TZ_ES_SET_ICE_KEY_PARAM_ID \
	TZ_SYSCALL_CREATE_PARAM_ID_5(TZ_SYSCALL_PARAM_TYPE_VAL, \
		TZ_SYSCALL_PARAM_TYPE_BUF_RW, TZ_SYSCALL_PARAM_TYPE_VAL, \
		TZ_SYSCALL_PARAM_TYPE_BUF_RW, TZ_SYSCALL_PARAM_TYPE_VAL)
#define TZ_ES_INVALIDATE_ICE_KEY_PARAM_ID \
	TZ_SYSCALL_CREATE_PARAM_ID_1(TZ_SYSCALL_PARAM_TYPE_VAL)

#define LEGACY_ICE_KEY_SIZE	32
#define LEGACY_ICE_SALT_SIZE	32
/* key index 0 and 1 are reserved for FDE */
#define LEGACY_ICE_MIN_INDEX	2
#define LEGACY_ICE_MAX_INDEX	31

static int crypto_qti_legacy_program_key(const struct blk_crypto_key *key,
					 unsigned int slot)
{
	struct scm_desc desc = {0};
	unsigned int index = slot + LEGACY_ICE_MIN_INDEX;
	u32 words[BLK_CRYPTO_MAX_WRAPPED_KEY_SIZE / sizeof(u32)];
	u8 *tzkey, *tzsalt;
	int err, i;

	if (index > LEGACY_ICE_MAX_INDEX ||
	    key->size < LEGACY_ICE_KEY_SIZE + LEGACY_ICE_SALT_SIZE)
		return -EINVAL;

	tzkey = kzalloc(LEGACY_ICE_KEY_SIZE, GFP_KERNEL);
	tzsalt = kzalloc(LEGACY_ICE_SALT_SIZE, GFP_KERNEL);
	if (!tzkey || !tzsalt) {
		err = -ENOMEM;
		goto out;
	}

	/* undo the be32 swap done by fscrypt, giving the derived 4.4 key */
	memcpy(words, key->raw, key->size);
	for (i = 0; i < key->size / sizeof(u32); i++)
		__cpu_to_be32s(&words[i]);
	memcpy(tzkey, words, LEGACY_ICE_KEY_SIZE);
	memcpy(tzsalt, (u8 *)words + LEGACY_ICE_KEY_SIZE, LEGACY_ICE_SALT_SIZE);
	memzero_explicit(words, sizeof(words));

	dmac_flush_range(tzkey, tzkey + LEGACY_ICE_KEY_SIZE);
	dmac_flush_range(tzsalt, tzsalt + LEGACY_ICE_SALT_SIZE);

	desc.arginfo = TZ_ES_SET_ICE_KEY_PARAM_ID;
	desc.args[0] = index;
	desc.args[1] = virt_to_phys(tzkey);
	desc.args[2] = LEGACY_ICE_KEY_SIZE;
	desc.args[3] = virt_to_phys(tzsalt);
	desc.args[4] = LEGACY_ICE_SALT_SIZE;

	err = scm_call2(TZ_ES_SET_ICE_KEY_ID, &desc);
	if (err)
		pr_err("%s: legacy set key error %d index %u\n",
		       __func__, err, index);
out:
	if (tzkey)
		memzero_explicit(tzkey, LEGACY_ICE_KEY_SIZE);
	if (tzsalt)
		memzero_explicit(tzsalt, LEGACY_ICE_SALT_SIZE);
	kfree(tzkey);
	kfree(tzsalt);
	return err;
}

static int crypto_qti_legacy_invalidate_key(unsigned int slot)
{
	struct scm_desc desc = {0};
	unsigned int index = slot + LEGACY_ICE_MIN_INDEX;
	int err;

	if (index > LEGACY_ICE_MAX_INDEX)
		return -EINVAL;

	desc.arginfo = TZ_ES_INVALIDATE_ICE_KEY_PARAM_ID;
	desc.args[0] = index;
	err = scm_call2(TZ_ES_INVALIDATE_ICE_KEY_ID, &desc);
	if (err)
		pr_err("%s: legacy invalidate error %d index %u\n",
		       __func__, err, index);
	return err;
}

int crypto_qti_program_key(struct crypto_vops_qti_entry *ice_entry,
			   const struct blk_crypto_key *key,
			   unsigned int slot, unsigned int data_unit_mask,
			   int capid)
{
	int err = 0;
	uint32_t smc_id = 0;
	char *tzbuf = NULL;
	struct qtee_shm shm;
	struct scm_desc desc = {0};
	int i;
	union {
		u8 bytes[BLK_CRYPTO_MAX_WRAPPED_KEY_SIZE];
		u32 words[BLK_CRYPTO_MAX_WRAPPED_KEY_SIZE / sizeof(u32)];
	} key_new;

	if (qti_ice_legacy_v2)
		return crypto_qti_legacy_program_key(key, slot);

	err = qtee_shmbridge_allocate_shm(key->size, &shm);
	if (err)
		return -ENOMEM;

	tzbuf = shm.vaddr;

	memcpy(key_new.bytes, key->raw, key->size);
	if (!key->is_hw_wrapped) {
		for (i = 0; i < ARRAY_SIZE(key_new.words); i++)
			__cpu_to_be32s(&key_new.words[i]);
	}

	memcpy(tzbuf, key_new.bytes, key->size);
	dmac_flush_range(tzbuf, tzbuf + key->size);

	smc_id = TZ_ES_CONFIG_SET_ICE_KEY_CE_TYPE_ID;
	desc.arginfo = TZ_ES_CONFIG_SET_ICE_KEY_CE_TYPE_PARAM_ID;
	desc.args[0] = slot;
	desc.args[1] = shm.paddr;
	desc.args[2] = key->size;
	desc.args[3] = ICE_CIPHER_MODE_XTS_256;
	desc.args[4] = data_unit_mask;
	desc.args[5] = storage_type;


	err = scm_call2_noretry(smc_id, &desc);
	if (err)
		pr_err("%s:SCM call Error: 0x%x slot %d\n",
				__func__, err, slot);

	qtee_shmbridge_free_shm(&shm);

	return err;
}

int crypto_qti_invalidate_key(
		struct crypto_vops_qti_entry *ice_entry, unsigned int slot)
{
	int err = 0;
	uint32_t smc_id = 0;
	struct scm_desc desc = {0};

	if (qti_ice_legacy_v2)
		return crypto_qti_legacy_invalidate_key(slot);

	smc_id = TZ_ES_INVALIDATE_ICE_KEY_CE_TYPE_ID;

	desc.arginfo = TZ_ES_INVALIDATE_ICE_KEY_CE_TYPE_PARAM_ID;
	desc.args[0] = slot;
	desc.args[1] = storage_type;

	err = scm_call2_noretry(smc_id, &desc);
	if (err)
		pr_err("%s:SCM call Error: 0x%x\n", __func__, err);
	return err;
}

int crypto_qti_tz_raw_secret(const u8 *wrapped_key,
			     unsigned int wrapped_key_size, u8 *secret,
			     unsigned int secret_size)
{
	int err = 0;
	struct qtee_shm shm_key, shm_secret;
	uint32_t smc_id = 0;

	struct scm_desc desc = {0};
	char *tzbuf_key;

	err = qtee_shmbridge_allocate_shm(wrapped_key_size, &shm_key);
	if (err)
		return -ENOMEM;

	err = qtee_shmbridge_allocate_shm(secret_size, &shm_secret);
	if (err)
		return -ENOMEM;

	tzbuf_key = shm_key.vaddr;
	memcpy(tzbuf_key, wrapped_key, wrapped_key_size);
	dmac_flush_range(tzbuf_key, tzbuf_key + wrapped_key_size);

	smc_id = TZ_ES_RETRIEVE_RAW_SECRET_CE_TYPE_ID;
	desc.arginfo = TZ_ES_RETRIEVE_RAW_SECRET_CE_TYPE_PARAM_ID;
	desc.args[0] = shm_key.paddr;
	desc.args[1] = wrapped_key_size;
	desc.args[2] = shm_secret.paddr;
	desc.args[3] = secret_size;

	memset(shm_secret.vaddr, 0, secret_size);
	dmac_flush_range(shm_secret.vaddr, shm_secret.vaddr + secret_size);

	err = scm_call2(smc_id, &desc);
	if (err) {
		pr_err("%s failed to retrieve raw secret\n", __func__, err);
		return err;
	}

	dmac_inv_range(shm_secret.vaddr, shm_secret.vaddr + secret_size);
	memcpy(secret, shm_secret.vaddr, secret_size);

	qtee_shmbridge_free_shm(&shm_key);
	qtee_shmbridge_free_shm(&shm_secret);

	return err;
}

static int crypto_qti_storage_type(unsigned int *s_type)
{
	char boot[20] = {'\0'};
	char *match = (char *)strnstr(saved_command_line,
				"androidboot.bootdevice=",
				strlen(saved_command_line));
	if (match) {
		memcpy(boot, (match + strlen("androidboot.bootdevice=")),
			sizeof(boot) - 1);
		if (strnstr(boot, "ufs", strlen(boot)))
			*s_type = UFS_CE;

		return 0;
	}
	return -EINVAL;
}

static int __init crypto_qti_init(void)
{
	return crypto_qti_storage_type(&storage_type);
}

module_init(crypto_qti_init);
