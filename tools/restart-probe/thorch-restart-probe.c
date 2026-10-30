// SPDX-License-Identifier: GPL-2.0-only
/* Opt-in, one-boot warm restart probe using the existing reboot-mode cell. */
#include <linux/arm-smccc.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/regmap.h>
#include <uapi/linux/psci.h>

static bool armed;
module_param(armed, bool, 0444);
static bool cold_control;
module_param(cold_control, bool, 0444);
static bool early_control;
module_param(early_control, bool, 0444);
static bool halt_before_reset;
module_param(halt_before_reset, bool, 0444);
static unsigned int original_mode;
module_param(original_mode, uint, 0444);
static unsigned int before_mode;
module_param(before_mode, uint, 0444);
static unsigned int returned_mode;
module_param(returned_mode, uint, 0444);

static struct platform_device *provider;
static struct regmap *map;
static unsigned int reg, mask, shift;

static int set_mode(unsigned int mode)
{
	unsigned int value;
	int ret;

	ret = regmap_update_bits(map, reg, mask, mode << shift);
	if (ret)
		return ret;
	ret = regmap_read(map, reg, &value);
	if (ret)
		return ret;
	return (value & mask) == (mode << shift) ? 0 : -EIO;
}

static int restart_probe(struct notifier_block *nb, unsigned long action,
			 void *cmd)
{
	struct arm_smccc_res result;
	int ret;

	if (!armed || (reboot_mode != REBOOT_WARM &&
		       !(cold_control && reboot_mode == REBOOT_COLD)))
		return NOTIFY_DONE;

	/* This notifier executes after machine_restart has disabled interrupts
	 * and stopped secondary CPUs. The PMIC regmap uses fast_io and the
	 * SPMI arbiter polls commands under a raw spinlock; no nvmem/mutex API.
	 */
	ret = set_mode(early_control ? original_mode : before_mode);
	if (ret) {
		pr_emerg("thorch-restart-probe: marker failed: %d; reset not called\n", ret);
		return cold_control ? NOTIFY_DONE : NOTIFY_STOP;
	}
	if (cold_control) {
		pr_emerg("thorch-restart-probe: cold control marker written\n");
		return NOTIFY_DONE;
	}
	if (halt_before_reset) {
		pr_emerg("thorch-restart-probe: deliberate stop before SYSTEM_RESET2\n");
		return NOTIFY_STOP;
	}
	pr_emerg("thorch-restart-probe: entering SYSTEM_RESET2\n");
	arm_smccc_smc(PSCI_1_1_FN64_SYSTEM_RESET2, 0, 0, 0, 0, 0, 0, 0, &result);
	ret = set_mode(returned_mode);
	pr_emerg("thorch-restart-probe: SYSTEM_RESET2 returned %ld; marker=%d\n",
		 (long)result.a0, ret);
	/* Do not invoke the original reset callback a second time. */
	return NOTIFY_STOP;
}

static struct notifier_block restart_nb = {
	.notifier_call = restart_probe,
	.priority = 130, /* immediately ahead of PSCI's priority 129 */
};

static int early_probe(struct notifier_block *nb, unsigned long action, void *cmd)
{
	int ret;

	if (!armed || !early_control || action != SYS_RESTART ||
	    reboot_mode != REBOOT_WARM)
		return NOTIFY_DONE;
	ret = set_mode(before_mode);
	pr_emerg("thorch-restart-probe: early marker result=%d\n", ret);
	return NOTIFY_DONE;
}

static struct notifier_block early_nb = {
	.notifier_call = early_probe,
	.priority = -128, /* after the standard reboot-mode notifier */
};

static int __init restart_probe_init(void)
{
	struct device_node *mode, *cell = NULL, *node = NULL;
	struct arm_smccc_res result;
	u32 base, offset, bits[2];
	unsigned int raw;
	int ret, restore_ret;

	if (arm_smccc_1_1_get_conduit() != SMCCC_CONDUIT_SMC)
		return -ENODEV;
	arm_smccc_smc(PSCI_1_0_FN_PSCI_FEATURES,
		      PSCI_1_1_FN64_SYSTEM_RESET2, 0, 0, 0, 0, 0, 0, &result);
	if ((s32)result.a0 < 0)
		return -EOPNOTSUPP;

	mode = of_find_compatible_node(NULL, NULL, "nvmem-reboot-mode");
	if (!mode)
		return -ENODEV;
	ret = of_property_read_u32(mode, "mode-bootloader", &before_mode);
	if (!ret)
		ret = of_property_read_u32(mode, "mode-recovery", &returned_mode);
	if (ret)
		goto out_nodes;
	cell = of_parse_phandle(mode, "nvmem-cells", 0);
	if (!cell) {
		ret = -ENODEV;
		goto out_nodes;
	}
	node = of_get_parent(cell);
	if (!of_device_is_compatible(node, "qcom,spmi-sdam")) {
		ret = -ENODEV;
		goto out_nodes;
	}
	ret = of_property_read_u32(node, "reg", &base);
	if (!ret)
		ret = of_property_read_u32(cell, "reg", &offset);
	if (!ret)
		ret = of_property_read_u32_array(cell, "bits", bits, 2);
	if (ret)
		goto out_nodes;
	if (!bits[1] || bits[0] + bits[1] > 8 ||
	    before_mode >= BIT(bits[1]) || returned_mode >= BIT(bits[1]) ||
	    before_mode == returned_mode) {
		ret = -EINVAL;
		goto out_nodes;
	}
	provider = of_find_device_by_node(node);
	if (!provider) {
		ret = -ENODEV;
		goto out_nodes;
	}
	map = dev_get_regmap(provider->dev.parent, NULL);
	if (!map || regmap_might_sleep(map)) {
		ret = -EOPNOTSUPP;
		goto out_device;
	}
	reg = base + offset;
	shift = bits[0];
	mask = GENMASK(bits[0] + bits[1] - 1, bits[0]);
	ret = regmap_read(map, reg, &raw);
	if (ret)
		goto out_device;
	original_mode = (raw & mask) >> shift;
	/* Exercise only documented values and always restore the original field. */
	ret = set_mode(before_mode);
	if (!ret)
		ret = set_mode(returned_mode);
	restore_ret = set_mode(original_mode);
	if (!ret)
		ret = restore_ret;
	if (ret)
		goto out_device;
	ret = register_restart_handler(&restart_nb);
	if (ret)
		goto out_device;
	ret = register_reboot_notifier(&early_nb);
	if (ret) {
		unregister_restart_handler(&restart_nb);
		goto out_device;
	}
	pr_info("thorch-restart-probe: selfcheck passed; original=%u before=%u returned=%u armed=%d\n",
		original_mode, before_mode, returned_mode, armed);
	goto out_nodes;

out_device:
	put_device(&provider->dev);
	provider = NULL;
out_nodes:
	of_node_put(node);
	of_node_put(cell);
	of_node_put(mode);
	return ret;
}

static void __exit restart_probe_exit(void)
{
	unregister_reboot_notifier(&early_nb);
	unregister_restart_handler(&restart_nb);
	put_device(&provider->dev);
}

module_init(restart_probe_init);
module_exit(restart_probe_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Opt-in final warm restart stage probe");
