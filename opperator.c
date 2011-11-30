/*
 opperator.ko - The OPP Mannagement API
 version 0.1-beta1 - 11-30-11
 by Jeffrey Kawika Patricio <jkp@tekahuna.net>
 License: GNU GPLv3
 <http://www.gnu.org/licenses/gpl-3.0.html>
 
 Project site:
 http://code.google.com/p/opperator/
 
 Changelog:
 
 version 0.1-beta1 - 11-11-11
 - Initilize git repository.
 - Cleaned up code to work on OMAP2+ w/kernel 2.6.35-7 and greater, not just OMAP4
 - Misc Cleanup
 
 version 0.1-alpha - 11-11-11
 - Initial working build.
*/
 
#include <linux/module.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <asm/uaccess.h>
#include <linux/cpufreq.h>
#include <plat/common.h>

#include "../symsearch/symsearch.h"

#define DRIVER_AUTHOR "Jeffrey Kawika Patricio <jkp@tekahuna.net>\n"
#define DRIVER_DESCRIPTION "OPPerator - The OPP Management API\n This module makes use of symsearch.ko by Skrilax_CZ\n"
#define DRIVER_VERSION "0.1-beta1"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");

// opp.c
SYMSEARCH_DECLARE_FUNCTION_STATIC(int, opp_get_opp_count_fp, struct device *dev);
SYMSEARCH_DECLARE_FUNCTION_STATIC(struct omap_opp *, opp_find_freq_floor_fp, struct device *dev, unsigned long *freq);

static int maxdex;
static unsigned long def_max_rate;
static unsigned long def_max_volt;

static struct cpufreq_frequency_table *freq_table;
static struct cpufreq_policy *policy;

#define BUF_SIZE PAGE_SIZE
static char *buf;

/**
 * struct omap_opp - OMAP OPP description structure
 * @enabled:	true/false - marking this OPP as enabled/disabled
 * @rate:	Frequency in hertz
 * @u_volt:	Nominal voltage in microvolts corresponding to this OPP
 * @opp_id:	opp identifier (deprecated)
 *
 * This structure stores the OPP information for a given domain.
 */
struct omap_opp {
	struct list_head node;
	
	bool enabled;
	unsigned long rate;
	unsigned long u_volt;
	u8 opp_id;
	
	struct device_opp *dev_opp;  /* containing device_opp struct */
};

struct device_opp {
	struct list_head node;
	
	struct omap_hwmod *oh;
	struct device *dev;
	
	struct list_head opp_list;
	u32 opp_count;
	u32 enabled_opp_count;
	
	int (*set_rate)(struct device *dev, unsigned long rate);
	unsigned long (*get_rate) (struct device *dev);
};

static int proc_opperator_read(char *buffer, char **buffer_location,
							   off_t offset, int count, int *eof, void *data) {
	int ret = 0;
	unsigned long freq = ULONG_MAX;
	struct device *dev = NULL;
	struct omap_opp *opp = ERR_PTR(-ENODEV);
	
	dev = omap2_get_mpuss_device();
	if (IS_ERR(dev)) {
		return -ENODEV;
	}
	while (!IS_ERR(opp = opp_find_freq_floor_fp(dev, &freq))) {
		ret += scnprintf(buffer+ret, count-ret, "mpu: enabled=%u frequency=%lu voltage=%lu\n", 
											 opp->enabled, opp->rate, opp->u_volt);
		freq--;
	}
	return ret;
};

static int proc_opperator_write(struct file *filp, const char __user *buffer,
								unsigned long len, void *data) {
	unsigned long volt, rate, freq = ULONG_MAX;
	struct device *dev = NULL;
	struct omap_opp *opp = ERR_PTR(-ENODEV);
	
	
	if(!len || len >= BUF_SIZE)
		return -ENOSPC;
	if(copy_from_user(buf, buffer, len))
		return -EFAULT;
	buf[len] = 0;
	if(sscanf(buf, "%lu %lu", &rate, &volt) == 2) {
		dev = omap2_get_mpuss_device();
		opp = opp_find_freq_floor_fp(dev, &freq);
		if (IS_ERR(opp)) {
			return -ENODEV;
		}
		opp->u_volt = volt;
		opp->rate = rate;
		freq_table[maxdex].frequency = policy->max = policy->cpuinfo.max_freq =
			policy->user_policy.max = rate / 1000;
	} else
		printk(KERN_INFO "OPPerator: incorrect parameters\n");
	return len;
};
							 
static int __init opperator_init(void)
{
	unsigned long freq = ULONG_MAX;
	struct device *dev = NULL;
	struct omap_opp *opp = ERR_PTR(-ENODEV);
	struct proc_dir_entry *proc_entry;
	
	printk(KERN_INFO " %s %s\n", DRIVER_DESCRIPTION, DRIVER_VERSION);
	printk(KERN_INFO " Created by %s\n", DRIVER_AUTHOR);

	SYMSEARCH_BIND_FUNCTION_TO(opperator, opp_get_opp_count, opp_get_opp_count_fp);
	SYMSEARCH_BIND_FUNCTION_TO(opperator, opp_find_freq_floor, opp_find_freq_floor_fp);
	
	freq_table = cpufreq_frequency_get_table(0);
	policy = cpufreq_cpu_get(0);
	
	dev = omap2_get_mpuss_device();
	maxdex = (opp_get_opp_count_fp(dev)-1);
	opp = opp_find_freq_floor_fp(dev, &freq);
	if (IS_ERR(opp)) {
		return -ENODEV;
	}
	def_max_rate = opp->rate;
	def_max_volt = opp->u_volt;
	
	buf = (char *)vmalloc(BUF_SIZE);
	
	proc_entry = create_proc_read_entry("opperator", 0644, NULL, proc_opperator_read, NULL);
	proc_entry->write_proc = proc_opperator_write;
	
	return 0;
};

static void __exit opperator_exit(void)
{
	unsigned long freq = ULONG_MAX;
	struct device *dev = NULL;
	struct omap_opp *opp = ERR_PTR(-ENODEV);
	
	remove_proc_entry("opperator", NULL);
	
	vfree(buf);
	
	dev = omap2_get_mpuss_device();
	opp = opp_find_freq_floor_fp(dev, &freq);
	opp->rate = def_max_rate;
	opp->u_volt = def_max_volt;
	freq_table[maxdex].frequency = policy->max = policy->cpuinfo.max_freq =
	policy->user_policy.max = def_max_rate / 1000;
	
	printk(KERN_INFO " OPPerator: Reseting values to default... Goodbye!\n");
};
							 
module_init(opperator_init);
module_exit(opperator_exit);

