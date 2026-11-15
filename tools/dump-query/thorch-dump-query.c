// SPDX-License-Identifier: GPL-2.0-only
/* Read-only dump-register and downstream virtual-watchdog status query. */
#include <linux/arm-smccc.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/module.h>
#include <linux/of_address.h>

static unsigned int dump_register;
static int dump_read_result;
static long virtual_wdt_result;
static unsigned long virtual_wdt_status, virtual_wdt_elapsed;
module_param(dump_register, uint, 0444);
module_param(dump_read_result, int, 0444);
module_param(virtual_wdt_result, long, 0444);
module_param(virtual_wdt_status, ulong, 0444);
module_param(virtual_wdt_elapsed, ulong, 0444);

static int __init dump_query_init(void)
{
	struct device_node *scm, *tcsr;
	struct resource resource;
	struct arm_smccc_res result;
	u32 offset;
	int ret;

	scm = of_find_compatible_node(NULL, NULL, "qcom,scm");
	if (!scm)
		return -ENODEV;
	tcsr = of_parse_phandle(scm, "qcom,dload-mode", 0);
	ret = of_property_read_u32_index(scm, "qcom,dload-mode", 1, &offset);
	of_node_put(scm);
	if (!tcsr)
		return -ENODEV;
	if (!ret)
		ret = of_address_to_resource(tcsr, 0, &resource);
	of_node_put(tcsr);
	if (ret)
		return ret;
	if (resource_size(&resource) < sizeof(u32) ||
	    offset > resource_size(&resource) - sizeof(u32))
		return -EINVAL;
	dump_read_result = qcom_scm_io_readl(resource.start + offset,
					  &dump_register);
	/* ayn_8550: gh_virt_wdt.c VIRT_WDT_STATUS, no configuration calls. */
	arm_smccc_smc(ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL, ARM_SMCCC_SMC_32,
				       ARM_SMCCC_OWNER_VENDOR_HYP, 0x0006),
		      0, 0, 0, 0, 0, 0, 0, &result);
	virtual_wdt_result = (long)result.a0;
	virtual_wdt_status = result.a1;
	virtual_wdt_elapsed = result.a2;
	pr_info("thorch-dump-query: read=%d register=%#x virtual-wdt=%ld status=%#lx elapsed=%lu\n",
		dump_read_result, dump_register, virtual_wdt_result,
		virtual_wdt_status, virtual_wdt_elapsed);
	return 0;
}

static void __exit dump_query_exit(void) {}
module_init(dump_query_init);
module_exit(dump_query_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read-only Thor dump and virtual watchdog status");
