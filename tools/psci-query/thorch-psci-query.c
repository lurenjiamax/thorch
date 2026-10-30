// SPDX-License-Identifier: GPL-2.0-only
/* Read-only PSCI capability query. Never invokes a reset function. */
#include <linux/arm-smccc.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <uapi/linux/psci.h>

static unsigned long version;
static int reset2_64_features;
static int reset2_32_features;
module_param(version, ulong, 0444);
module_param(reset2_64_features, int, 0444);
module_param(reset2_32_features, int, 0444);

static int query_features(unsigned long function)
{
	struct arm_smccc_res result;

	arm_smccc_smc(PSCI_1_0_FN_PSCI_FEATURES, function, 0, 0, 0, 0, 0, 0,
		      &result);
	return (s32)result.a0;
}

static int __init thorch_psci_query_init(void)
{
	struct arm_smccc_res result;

	if (arm_smccc_1_1_get_conduit() != SMCCC_CONDUIT_SMC)
		return -ENODEV;

	arm_smccc_smc(PSCI_0_2_FN_PSCI_VERSION, 0, 0, 0, 0, 0, 0, 0, &result);
	version = result.a0;
	reset2_64_features = query_features(PSCI_1_1_FN64_SYSTEM_RESET2);
	reset2_32_features = query_features(PSCI_1_1_FN_SYSTEM_RESET2);
	pr_info("thorch-psci-query: version=%#lx reset2_64=%d reset2_32=%d (queries only)\n",
		version, reset2_64_features, reset2_32_features);
	return 0;
}

static void __exit thorch_psci_query_exit(void)
{
}

module_init(thorch_psci_query_init);
module_exit(thorch_psci_query_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read-only PSCI SYSTEM_RESET2 capability query");
