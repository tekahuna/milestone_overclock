/*
	Motorola Milestone overclock module
	version 1.1-mapphone - 2011-03-30
	by Tiago Sousa <mirage@kaotik.org>, modified by nadlabak, Skrilax_CZ, tekahuna
	License: GNU GPLv2
	<http://www.gnu.org/licenses/old-licenses/gpl-2.0.html>

	Changelog:
 
	version 1.5-mapphone - 2011-03-30
	- port to Yokohama devices

	version 1.1-mapphone - 2011-03-30
	- simplified
	- added missing item to frequency table

	version 1.0-mapphone - 2010-11-19
	- automatic symbol detection
	- automatic values detection
	
	Description:

	The MPU (Microprocessor Unit) clock has 5 discrete pairs of possible
	rate frequencies and respective voltages, of which only 4 are passed
	down to cpufreq as you can see with a tool such as SetCPU.  The
	default frequencies are 125, 250, 500 and 550 MHz (and a hidden
	600).  By using this module, you are changing the highest pair in
	the tables of both cpufreq and MPU frequencies, so it becomes 125,
	250, 500 and, say, 800.  It's quite stable up to 1200; beyond
	that it quickly becomes unusable, specially over 1300, with lockups
	or spontaneous reboots.
*/

#define DRIVER_AUTHOR "Tiago Sousa <mirage@kaotik.org>, nadlabak, Skrilax_CZ, tekahuna"
#define DRIVER_DESCRIPTION "Motorola Milestone CPU overclocking"
#define DRIVER_VERSION "1.5-mapphone-yokohama"

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>

#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <asm/uaccess.h>

#include <linux/kallsyms.h>

#include <linux/notifier.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/completion.h>
#include <linux/mutex.h>

#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/system.h> 
  
#include <plat/omap-pm.h>
#include <plat/opp-max.h>
#include "opp_info.h"
#include "../symsearch/symsearch.h"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");

//extern int cpufreq_stats_freq_update(unsigned int cpu, int index, unsigned int freq);

// opp.c
SYMSEARCH_DECLARE_FUNCTION_STATIC(int, 
								  opp_get_opp_count_fp, struct device *dev);
SYMSEARCH_DECLARE_FUNCTION_STATIC(struct omap_opp *, 
								  opp_find_freq_floor_fp, struct device *dev, unsigned long *freq);
SYMSEARCH_DECLARE_FUNCTION_STATIC(struct omap_opp * __deprecated,
								  opp_find_by_opp_id_fp, struct device *dev, u8 opp_id);
// opp-max.c
SYMSEARCH_DECLARE_FUNCTION_STATIC(unsigned long, omap_max8952_vsel_to_uv_fp, unsigned char vsel);
SYMSEARCH_DECLARE_FUNCTION_STATIC(unsigned char, omap_max8952_uv_to_vsel_fp, unsigned long uv);

// voltage.c
SYMSEARCH_DECLARE_FUNCTION_STATIC(struct voltagedomain *, 
								  omap_voltage_domain_get_fp, char *name);
SYMSEARCH_DECLARE_FUNCTION_STATIC(void, 
								  omap_voltage_reset_fp, struct voltagedomain *voltdm);

#define MPU_CLK         "dpll_mpu_ck"

static int opp_count, enabled_opp_count, main_index, cpufreq_index;

static char bad_governor[16] = "performance";
static char good_governor[16] = "hotplug";

unsigned long default_max_rate;
unsigned long default_max_voltage;

static struct device *dev;
static struct voltagedomain *voltdm;
static struct omap_vdd_info *vdd;
static struct cpufreq_frequency_table *freq_table;
static struct omap_opp *my_mpu_opps;
static struct cpufreq_policy *policy;
static struct clk *mpu_clk;

#define BUF_SIZE PAGE_SIZE
static char *buf;



static int proc_info_read(char *buffer, char **buffer_location,
		off_t offset, int count, int *eof, void *data)
{
	int ret;

	if (offset > 0)
		ret = 0;
	else
		ret = scnprintf(buffer, count, "cpumin=%u cpumax=%u min=%u max=%u usermin=%u usermax=%u\nclk_get_rate=%lu\n",
				policy->cpuinfo.min_freq, policy->cpuinfo.max_freq, policy->min, policy->max, policy->user_policy.min, policy->user_policy.max, clk_get_rate(mpu_clk) / 1000);

	return ret;
}

static int proc_freq_table_read(char *buffer, char **buffer_location,
		off_t offset, int count, int *eof, void *data)
{
	int i, ret = 0;

	if (offset > 0)
		ret = 0;
	else
		for(i = 0; freq_table[i].frequency != CPUFREQ_TABLE_END; i++) 
		{
			if(ret >= count)
				break;

			ret += scnprintf(buffer+ret, count-ret, "freq_table[%d] index=%u frequency=%u\n", i, freq_table[i].index, freq_table[i].frequency);
		}

	return ret;
}
                       
                        
static int proc_mpu_opps_read(char *buffer, char **buffer_location,
		off_t offset, int count, int *eof, void *data)
{
	int i, ret = 0;

	if (offset > 0)
		ret = 0;
	else
		for(i = main_index;i >= main_index-cpufreq_index; i--) 
		{
			my_mpu_opps = opp_find_by_opp_id_fp(dev, i);
			ret += scnprintf(buffer+ret, count-ret, "mpu_opps[%d] rate=%lu opp_id=%u vsel=%u u_volt=%lu\n", i, 
			my_mpu_opps->rate, my_mpu_opps->opp_id, omap_max8952_uv_to_vsel_fp(my_mpu_opps->u_volt), my_mpu_opps->u_volt); 		
		}

	return ret;
}

static int proc_mpu_opps_write(struct file *filp, const char __user *buffer,
		unsigned long len, void *data)
{
	uint index, rate, vsel, volt, temp_rate;
	
	bool bad_gov_check = 0;

	if(!len || len >= BUF_SIZE)
		return -ENOSPC;

	if(copy_from_user(buf, buffer, len))
		return -EFAULT;

	buf[len] = 0;
	
	if(sscanf(buf, "%d %d %d", &index, &rate, &vsel) == 3) 
	{
		if (policy->governor->name == bad_governor) {
			policy->governor->governor = (void *)good_governor;
			bad_gov_check = 1;
		}
		temp_rate = policy->user_policy.max;
		
		freq_table[cpufreq_index].frequency = 
			policy->max = policy->cpuinfo.max_freq =
			policy->user_policy.max = policy->user_policy.min;		

		volt = omap_max8952_vsel_to_uv_fp(vsel);
		mutex_lock(&vdd->scaling_mutex);
		vdd->volt_data[index].volt_nominal = volt;
		vdd->dep_vdd_info[0].dep_table[index].main_vdd_volt = volt;
		mutex_unlock(&vdd->scaling_mutex);
		//update mpu_opps
		my_mpu_opps = opp_find_by_opp_id_fp(dev, index);
		my_mpu_opps->u_volt = volt;
		
		//update frequency table (MAX_VDD1_OPP - index)
		freq_table[index-(main_index-cpufreq_index)].frequency = rate / 1000;
		
		//in case of MAX_VDD1_OPP update max policy
		//and in case of one update min policy
		if (index == main_index)
		{
			freq_table[cpufreq_index].frequency = 
			policy->max = policy->cpuinfo.max_freq =
			policy->user_policy.max = rate / 1000;
		}
		else if (index == main_index-cpufreq_index)
		{
			policy->min = policy->cpuinfo.min_freq =
			policy->user_policy.min = rate / 1000;
			freq_table[cpufreq_index].frequency = 
			policy->max = policy->cpuinfo.max_freq =
			policy->user_policy.max = temp_rate;
		} else {
			freq_table[cpufreq_index].frequency = 
			policy->max = policy->cpuinfo.max_freq =
			policy->user_policy.max = temp_rate;
		}
		my_mpu_opps->rate = rate;
		if (bad_gov_check == 1) {
			policy->governor->governor = (void *)bad_governor;
		}
		omap_voltage_reset_fp(voltdm);
//		cpufreq_stats_freq_update(0, cpufreq_index - index, rate / 1000);
	} 
	else
		printk(KERN_INFO "overclock: insufficient parameters for mpu_opps\n");

	return len;
}                        
                            
static int proc_version_read(char *buffer, char **buffer_location,
		off_t offset, int count, int *eof, void *data)
{
	int ret;

	if (offset > 0)
		ret = 0;
	else
		ret = scnprintf(buffer, count, "%s\n", DRIVER_VERSION);

	return ret;
}

static int __init overclock_init(void)
{
	struct proc_dir_entry *proc_entry;
	printk(KERN_INFO "overclock: %s version %s\n", DRIVER_DESCRIPTION, DRIVER_VERSION);
	printk(KERN_INFO "overclock: by %s\n", DRIVER_AUTHOR);

	// opp.c
	SYMSEARCH_BIND_FUNCTION_TO(overclock, 
							   opp_get_opp_count, opp_get_opp_count_fp);
	SYMSEARCH_BIND_FUNCTION_TO(overclock, 
							   opp_find_freq_floor, opp_find_freq_floor_fp);
	SYMSEARCH_BIND_FUNCTION_TO(overclock,
							   opp_find_by_opp_id, opp_find_by_opp_id_fp);
	// opp-max.c
	SYMSEARCH_BIND_FUNCTION_TO(overclock,
							   omap_max8952_vsel_to_uv,  omap_max8952_vsel_to_uv_fp);
	SYMSEARCH_BIND_FUNCTION_TO(overclock,
							   omap_max8952_uv_to_vsel,  omap_max8952_uv_to_vsel_fp);
	// voltage.c
	SYMSEARCH_BIND_FUNCTION_TO(overclock, 
							   omap_voltage_domain_get, omap_voltage_domain_get_fp);
	SYMSEARCH_BIND_FUNCTION_TO(overclock,
							   omap_voltage_reset, omap_voltage_reset_fp);

	freq_table = cpufreq_frequency_get_table(0);
	
	policy = cpufreq_cpu_get(0);
	mpu_clk = clk_get(NULL, MPU_CLK);
	
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
	
	my_mpu_opps = opp_find_by_opp_id_fp(dev, main_index);
	
	default_max_rate = my_mpu_opps->rate;
	default_max_voltage = my_mpu_opps->u_volt;
	
	buf = (char *)vmalloc(BUF_SIZE);

	proc_mkdir("overclock", NULL);
	proc_entry = create_proc_read_entry("overclock/info", 0444, NULL, proc_info_read, NULL);
	proc_entry = create_proc_read_entry("overclock/freq_table", 0444, NULL, proc_freq_table_read, NULL);
	proc_entry = create_proc_read_entry("overclock/mpu_opps", 0644, NULL, proc_mpu_opps_read, NULL);
	proc_entry->write_proc = proc_mpu_opps_write;
	proc_entry = create_proc_read_entry("overclock/version", 0444, NULL, proc_version_read, NULL);

	return 0;
}

static void __exit overclock_exit(void)
{
	remove_proc_entry("overclock/version", NULL);
	remove_proc_entry("overclock/mpu_opps", NULL);
	remove_proc_entry("overclock/freq_table", NULL);
	remove_proc_entry("overclock/info", NULL);
	remove_proc_entry("overclock", NULL);

	vfree(buf);
	printk(KERN_INFO "overclock: removed overclocking and unloaded\n");
}

module_init(overclock_init);
module_exit(overclock_exit);
