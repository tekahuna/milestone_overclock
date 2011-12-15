/*
 opperator.ko - The OPP Mannagement API
 version 0.1-beta1 - 12-14-11
 by Jeffrey Kawika Patricio <jkp@tekahuna.net>
 License: GNU GPLv3
 <http://www.gnu.org/licenses/gpl-3.0.html>
 
 Project site:
 http://code.google.com/p/opperator/
 
 Changelog:
 
 version 0.2-beta1 - 12-14-11
 - Voltage manipulation
 
 version 0.1-beta1 - 12-14-11
 - Cleaned up code to work on OMAP2+ w/kernel 2.6.35-7 and greater
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
#include <plat/voltage.h>


#include "opp_info.h"
#include "../symsearch/symsearch.h"

#define DRIVER_AUTHOR "Jeffrey Kawika Patricio <jkp@tekahuna.net>\n"
#define DRIVER_DESCRIPTION "opperator.ko - The OPP Management API\n\
code.google.com/p/opperator for more info\n\
This modules uses SYMSEARCH by Skrilax_CZ\n\
Inspire by Milestone Overclock by Tiago Sousa\n"
#define DRIVER_VERSION "0.1-beta1"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");

// opp.c
SYMSEARCH_DECLARE_FUNCTION_STATIC(int, 
			opp_get_opp_count_fp, struct device *dev);
SYMSEARCH_DECLARE_FUNCTION_STATIC(struct omap_opp *, 
			opp_find_freq_floor_fp, struct device *dev, unsigned long *freq);
// voltage.c
SYMSEARCH_DECLARE_FUNCTION_STATIC(struct voltagedomain *, 
			omap_voltage_domain_get_fp, char *name);
SYMSEARCH_DECLARE_FUNCTION_STATIC(void, 
			omap_voltage_reset_fp, struct voltagedomain *voltdm);



static int opp_count, enabled_opp_count, main_index, cpufreq_index;

static char bad_governor[16] = "performance";
static char good_governor[16] = "hotplug";

unsigned long default_max_rate;
unsigned long default_max_voltage;

static struct cpufreq_frequency_table *freq_table;
static struct cpufreq_policy *policy;

#define BUF_SIZE PAGE_SIZE
static char *buf;

static int proc_opperator_read(char *buffer, char **buffer_location,
							   off_t offset, int count, int *eof, void *data)
{
	int ret = 0;
	unsigned long freq = ULONG_MAX;
	struct device *dev = NULL;
	struct omap_opp *opp = ERR_PTR(-ENODEV);
	
	dev = omap2_get_mpuss_device();
	if (!dev || IS_ERR(dev)) {
		return -ENODEV;
	}
	opp = opp_find_freq_floor_fp(dev, &freq);
	
	ret += scnprintf(buffer+ret, count-ret, "%lu %lu\n", opp->rate, opp->u_volt);

	return ret;
};

static int proc_opperator_write(struct file *filp, const char __user *buffer,
								unsigned long len, void *data)
{
	int bad_gov_check = 0;
	unsigned long volt, temp_rate, rate, freq = ULONG_MAX;
	struct device *dev = NULL;
	struct voltagedomain *voltdm = NULL;
	struct omap_vdd_info *vdd;
	struct omap_opp *opp = ERR_PTR(-ENODEV);
	
	if(!len || len >= BUF_SIZE)
		return -ENOSPC;
	if(copy_from_user(buf, buffer, len))
		return -EFAULT;
	buf[len] = 0;
	if(sscanf(buf, "%lu %lu", &rate, &volt) == 2) {
		voltdm = omap_voltage_domain_get_fp("mpu");
		if (!voltdm || IS_ERR(voltdm)) {
			return -ENODEV;
		}

		vdd = container_of(voltdm, struct omap_vdd_info, voltdm);
		if (!vdd || IS_ERR(vdd)) {
			return -ENODEV;
		}
		mutex_lock(&vdd->scaling_mutex);

		dev = omap2_get_mpuss_device();
		if (!dev || IS_ERR(dev)) {
			return -ENODEV;
		}
		
		opp = opp_find_freq_floor_fp(dev, &freq);
		if (!opp || IS_ERR(opp)) {
			return -ENODEV;
		}

		if (policy->governor->name == bad_governor) {
			policy->governor->governor = (void *)good_governor;
			bad_gov_check = 1;
		}
		temp_rate = policy->user_policy.min;
		freq_table[cpufreq_index].frequency = 
			policy->max = policy->cpuinfo.max_freq =
			policy->user_policy.max = rate / 1000;		
		freq_table[0].frequency = policy->min = 
			policy->cpuinfo.min_freq =
			policy->user_policy.min = rate / 1000;

		vdd->volt_data[main_index].volt_nominal = volt;
		vdd->dep_vdd_info[0].dep_table[main_index].main_vdd_volt = volt;
		opp->u_volt = volt;

		opp->rate = rate;	

		if (bad_gov_check == 1) {
			policy->governor->governor = (void *)bad_governor;
		}
		freq_table[0].frequency = policy->min = policy->cpuinfo.min_freq =
			policy->user_policy.min = temp_rate;

		omap_voltage_reset_fp(voltdm);
		mutex_unlock(&vdd->scaling_mutex);
	} else
		printk(KERN_INFO "OPPerator: incorrect parameters\n");
	return len;
};
							 
static int __init opperator_init(void)
{
	unsigned long freq = ULONG_MAX;
	struct device *dev = ERR_PTR(-ENODEV);
	struct omap_opp *opp = ERR_PTR(-ENODEV);
	struct voltagedomain *voltdm = ERR_PTR(-ENODEV);
	struct omap_vdd_info *vdd = ERR_PTR(-ENODEV);
	struct proc_dir_entry *proc_entry;
	
	printk(KERN_INFO " %s Version: %s\n", 
		   DRIVER_DESCRIPTION, DRIVER_VERSION);
	printk(KERN_INFO " Created by: %s\n", 
		   DRIVER_AUTHOR);

	// opp.c
	SYMSEARCH_BIND_FUNCTION_TO(opperator, 
				opp_get_opp_count, opp_get_opp_count_fp);
	SYMSEARCH_BIND_FUNCTION_TO(opperator, 
				opp_find_freq_floor, opp_find_freq_floor_fp);
	// voltage.c
	SYMSEARCH_BIND_FUNCTION_TO(opperator, 
				omap_voltage_domain_get, omap_voltage_domain_get_fp);
	SYMSEARCH_BIND_FUNCTION_TO(opperator,
							   omap_voltage_reset, omap_voltage_reset_fp);
	 
	freq_table = cpufreq_frequency_get_table(0);
	policy = cpufreq_cpu_get(0);
	
	voltdm = omap_voltage_domain_get_fp("mpu");
	if (!voltdm || IS_ERR(voltdm)) {
		return -ENODEV;
	}
	vdd = container_of(voltdm, struct omap_vdd_info, voltdm);
	if (!vdd || IS_ERR(vdd)) {
		return -ENODEV;
	}
	opp_count = vdd->volt_data_count;
	
	dev = omap2_get_mpuss_device();
	if (!dev || IS_ERR(dev)) {
		return -ENODEV;
	}
	enabled_opp_count = opp_get_opp_count_fp(dev);

	if (enabled_opp_count == opp_count) {
		main_index = cpufreq_index = (enabled_opp_count-1);
	} else {
		main_index = enabled_opp_count;
		cpufreq_index = (enabled_opp_count-1);
	}
	
	opp = opp_find_freq_floor_fp(dev, &freq);
	if (!opp || IS_ERR(opp)) {
		return -ENODEV;
	}
	
	default_max_rate = opp->rate;
	default_max_voltage = opp->u_volt;
	
	buf = (char *)vmalloc(BUF_SIZE);
	
	proc_entry = create_proc_read_entry("opperator", 0644, NULL, 
										proc_opperator_read, NULL);
	proc_entry->write_proc = proc_opperator_write;
	
	return 0;
};

static void __exit opperator_exit(void)
{
	int bad_gov_check = 0;
	unsigned long temp_rate, freq = ULONG_MAX;
	struct device *dev = NULL;
	struct voltagedomain *voltdm = NULL;
	struct omap_vdd_info *vdd;
	struct omap_opp *opp = ERR_PTR(-ENODEV);
	
	remove_proc_entry("opperator", NULL);
	
	vfree(buf);
	
	voltdm = omap_voltage_domain_get_fp("mpu");
	if (!voltdm || IS_ERR(voltdm)) {
		return;
	}
	
	vdd = container_of(voltdm, struct omap_vdd_info, voltdm);
	if (!vdd || IS_ERR(vdd)) {
		return;
	}
	mutex_lock(&vdd->scaling_mutex);
	
	dev = omap2_get_mpuss_device();
	if (!dev || IS_ERR(dev)) {
		return;
	}
	
	opp = opp_find_freq_floor_fp(dev, &freq);
	if (!opp || IS_ERR(opp)) {
		return;
	}
	opp->rate = default_max_rate;
	
	if (policy->governor->name == bad_governor) {
		policy->governor->governor = (void *)good_governor;
		bad_gov_check = 1;
	}
	temp_rate = policy->user_policy.min;
	freq_table[cpufreq_index].frequency = 
		policy->max = policy->cpuinfo.max_freq =
		policy->user_policy.max = default_max_rate / 1000;		
	freq_table[0].frequency = policy->min = 
		policy->cpuinfo.min_freq =
		policy->user_policy.min = default_max_rate / 1000;
	
	vdd->volt_data[main_index].volt_nominal = default_max_voltage;
	vdd->dep_vdd_info[0].dep_table[main_index].main_vdd_volt = default_max_voltage;
	opp->u_volt = default_max_voltage;
	
	if (bad_gov_check == 1) {
		policy->governor->governor = (void *)bad_governor;
	}
	freq_table[0].frequency = policy->min = policy->cpuinfo.min_freq =
	policy->user_policy.min = temp_rate;
	
	omap_voltage_reset_fp(voltdm);
	mutex_unlock(&vdd->scaling_mutex);
	
	printk(KERN_INFO " OPPerator: Resetting values to default... Goodbye!\n");
};
							 
module_init(opperator_init);
module_exit(opperator_exit);

