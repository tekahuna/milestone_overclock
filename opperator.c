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
#include <plat/voltage.h>

#include "../symsearch/symsearch.h"

#define DRIVER_AUTHOR "Jeffrey Kawika Patricio <jkp@tekahuna.net>\n"
#define DRIVER_DESCRIPTION "opperator.ko - The OPP Management API\n Note: This module makes use of SYMSEARCH by Skrilax_CZ & is inspired\n by Milestone Overclock by Tiago Sousa\n"
#define DRIVER_VERSION "0.1-beta1"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL");

// opp.c
SYMSEARCH_DECLARE_FUNCTION_STATIC(int, opp_get_opp_count_fp, struct device *dev);
SYMSEARCH_DECLARE_FUNCTION_STATIC(struct omap_opp *, opp_find_freq_floor_fp, struct device *dev, unsigned long *freq);
SYMSEARCH_DECLARE_FUNCTION_STATIC(struct device_opp *, find_device_opp_fp, struct device *dev);
// voltage.c
SYMSEARCH_DECLARE_FUNCTION_STATIC(unsigned long, omap_vp_get_curr_volt_fp, struct voltagedomain *voltdm);
SYMSEARCH_DECLARE_FUNCTION_STATIC(unsigned long, omap_voltage_get_nom_volt_fp, struct voltagedomain *voltdm);
SYMSEARCH_DECLARE_FUNCTION_STATIC(struct voltagedomain *, omap_voltage_domain_get_fp, char *name);


static int maxdex;
static unsigned long default_max_rate;
static unsigned long default_max_voltage;

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

/* Voltage processor register offsets */
struct vp_reg_offs {
	u8 vpconfig;
	u8 vstepmin;
	u8 vstepmax;
	u8 vlimitto;
	u8 vstatus;
	u8 voltage;
};

/* Voltage Processor bit field values, shifts and masks */
struct vp_reg_val {
	/* VPx_VPCONFIG */
	u32 vpconfig_erroroffset;
	u16 vpconfig_errorgain;
	u32 vpconfig_errorgain_mask;
	u8 vpconfig_errorgain_shift;
	u32 vpconfig_initvoltage_mask;
	u8 vpconfig_initvoltage_shift;
	u32 vpconfig_timeouten;
	u32 vpconfig_initvdd;
	u32 vpconfig_forceupdate;
	u32 vpconfig_vpenable;
	/* VPx_VSTEPMIN */
	u8 vstepmin_stepmin;
	u16 vstepmin_smpswaittimemin;
	u8 vstepmin_stepmin_shift;
	u8 vstepmin_smpswaittimemin_shift;
	/* VPx_VSTEPMAX */
	u8 vstepmax_stepmax;
	u16 vstepmax_smpswaittimemax;
	u8 vstepmax_stepmax_shift;
	u8 vstepmax_smpswaittimemax_shift;
	/* VPx_VLIMITTO */
	u16 vlimitto_vddmin;
	u16 vlimitto_vddmax;
	u16 vlimitto_timeout;
	u16 vlimitto_vddmin_shift;
	u16 vlimitto_vddmax_shift;
	u16 vlimitto_timeout_shift;
	/* PRM_IRQSTATUS*/
	u32 tranxdone_status;
};

/**
 * omap_vdd_dep_volt - Table containing the parent vdd voltage and the
 *			dependent vdd voltage corresponding to it.
 *
 * @main_vdd_volt	: The main vdd voltage
 * @dep_vdd_volt	: The voltage at which the dependent vdd should be
 *			  when the main vdd is at <main_vdd_volt> voltage
 */
struct omap_vdd_dep_volt {
	u32 main_vdd_volt;
	u32 dep_vdd_volt;
};

/**
 *  ABB Register offsets and masks
 *
 * @prm_abb_ldo_setup_idx : PRM_LDO_ABB_SETUP Register specific to MPU/IVA
 * @prm_abb_ldo_ctrl_idx  : PRM_LDO_ABB_CTRL Register specific to MPU/IVA
 * @prm_irqstatus_mpu	  : PRM_IRQSTATUS_MPU_A9/PRM_IRQSTATUS_MPU_A9_2
 * @abb_done_st_shift	  : ABB_DONE_ST shift
 * @abb_done_st_mask	  : ABB_DONE_ST_MASK bit mask
 *
 */
struct abb_reg_val {
	u16 prm_abb_ldo_setup_idx;
	u16 prm_abb_ldo_ctrl_idx;
	u16 prm_irqstatus_mpu;
	u32 abb_done_st_shift;
	u32 abb_done_st_mask;
};

/**
 * omap_vdd_dep_info - Dependent vdd info
 *
 * @name		: Dependent vdd name
 * @voltdm		: Dependent vdd pointer
 * @dep_table		: Table containing the dependent vdd voltage
 *			  corresponding to every main vdd voltage.
 * @cur_dep_volt	: The voltage to which dependent vdd should be put
 *			  to for the current main vdd voltage.
 */
struct omap_vdd_dep_info{
	char *name;
	struct voltagedomain *voltdm;
	struct omap_vdd_dep_volt *dep_table;
	unsigned long cur_dep_volt;
};

/**
 * omap_vdd_user_list	- The per vdd user list
 *
 * @dev		: The device asking for the vdd to be set at a particular
 *		  voltage
 * @node	: The list head entry
 * @volt	: The voltage requested by the device <dev>
 */
struct omap_vdd_user_list {
	struct device *dev;
	struct plist_node node;
	u32 volt;
};

/**
 * omap_vdd_info - Per Voltage Domain info
 *
 * @volt_data		: voltage table having the distinct voltages supported
 *			  by the domain and other associated per voltage data.
 * @vp_offs		: structure containing the offsets for various
 *			  vp registers
 * @vp_reg		: the register values, shifts, masks for various
 *			  vp registers
 * @volt_clk		: the clock associated with the vdd.
 * @opp_dev		: the 'struct device' associated with this vdd.
 * @user_lock		: the lock to be used by the plist user_list
 * @user_list		: the list head maintaining the various users
 *			  of this vdd with the voltage requested by each user.
 * @volt_data_count	: Number of distinct voltages supported by this vdd.
 * @nominal_volt	: Nominal voltaged for this vdd.
 * cmdval_reg		: Voltage controller cmdval register.
 * @vdd_sr_reg		: The smartreflex register associated with this VDD.
 */
struct omap_vdd_info{
	struct omap_volt_data *volt_data;
	struct vp_reg_offs vp_offs;
	struct vp_reg_val vp_reg;
	struct clk *volt_clk;
	struct device *opp_dev;
	struct voltagedomain voltdm;
	struct abb_reg_val omap_abb_reg_val;
	struct omap_vdd_dep_info *dep_vdd_info;
	spinlock_t user_lock;
	struct plist_head user_list;
	struct mutex scaling_mutex;
	struct srcu_notifier_head volt_change_notify_list;
	int volt_data_count;
	int nr_dep_vdd;
	struct device **dev_list;
	int dev_count;
	unsigned long nominal_volt;
	unsigned long curr_volt;
	u8 cmdval_reg;
	u8 vdd_sr_reg;
	struct omap_volt_pmic_info *pmic;
	struct device vdd_device;
};

static int proc_opperator_read(char *buffer, char **buffer_location,
							   off_t offset, int count, int *eof, void *data)
{
	int i, ret = 0;
	unsigned long freq = ULONG_MAX;
	struct device *dev = NULL;
	struct voltagedomain *voltdm = NULL;
	struct omap_vdd_info *vdd;
	struct device_opp *dev_opp = ERR_PTR(-ENODEV);
	struct omap_opp *opp = ERR_PTR(-ENODEV);
	
	voltdm = omap_voltage_domain_get_fp("mpu");
	if (!voltdm || IS_ERR(voltdm)) {
		pr_warning("%s: VDD specified does not exist!\n", __func__);
		return -EINVAL;
	}
	vdd = container_of(voltdm, struct omap_vdd_info, voltdm);
	dev = omap2_get_mpuss_device();
	if (IS_ERR(dev)) {
		return -ENODEV;
	}
	dev_opp = find_device_opp_fp(dev);
	if (IS_ERR(dev_opp)) {
		return -ENODEV;
	}
	ret += scnprintf(buffer+ret, count-ret, "mpu: volt_data_count=%u\n", vdd->volt_data_count);
	for (i = 0; i < vdd->volt_data_count; i++) {
		ret += scnprintf(buffer+ret, count-ret, "mpu: volt_data=%u\n", vdd->volt_data[i].volt_nominal);
	}
	for (i = 0;vdd->dep_vdd_info[0].dep_table[i].main_vdd_volt != 0; i++) {
		ret += scnprintf(buffer+ret, count-ret, "mpu: main_vdd_volt=%u dep_vdd_volt=%u\n", 
						 vdd->dep_vdd_info[0].dep_table[i].main_vdd_volt,
						 vdd->dep_vdd_info[0].dep_table[i].dep_vdd_volt);
	}
	ret += scnprintf(buffer+ret, count-ret, "mpu: opp_count=%u\n", dev_opp->opp_count);
	ret += scnprintf(buffer+ret, count-ret, "mpu: opp_count_enabled=%u\n", dev_opp->enabled_opp_count);
	ret += scnprintf(buffer+ret, count-ret, "mpu: default_max_rate=%lu\n", default_max_rate);
	ret += scnprintf(buffer+ret, count-ret, "mpu: default_max_voltage=%lu\n", default_max_voltage);
	ret += scnprintf(buffer+ret, count-ret, "mpu: current_voltdm_voltage=%lu\n", omap_vp_get_curr_volt_fp(voltdm));
	ret += scnprintf(buffer+ret, count-ret, "mpu: nominal_voltdm_voltage=%lu\n", omap_voltage_get_nom_volt_fp(voltdm));
	while (!IS_ERR(opp = opp_find_freq_floor_fp(dev, &freq))) {
		ret += scnprintf(buffer+ret, count-ret, "mpu: enabled=%u rate=%lu voltage=%lu\n", 
											 opp->enabled, opp->rate, opp->u_volt);
		freq--;
	}
	return ret;
};

static int proc_opperator_write(struct file *filp, const char __user *buffer,
								unsigned long len, void *data)
{
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
	
	printk(KERN_INFO " %s Version: %s\n", DRIVER_DESCRIPTION, DRIVER_VERSION);
	printk(KERN_INFO " Created by: %s\n", DRIVER_AUTHOR);

	// opp.c
	SYMSEARCH_BIND_FUNCTION_TO(opperator, opp_get_opp_count, opp_get_opp_count_fp);
	SYMSEARCH_BIND_FUNCTION_TO(opperator, opp_find_freq_floor, opp_find_freq_floor_fp);
	SYMSEARCH_BIND_FUNCTION_TO(opperator, find_device_opp, find_device_opp_fp);
	//voltage.c
	SYMSEARCH_BIND_FUNCTION_TO(opperator, omap_vp_get_curr_volt, omap_vp_get_curr_volt_fp);
	SYMSEARCH_BIND_FUNCTION_TO(opperator, omap_voltage_get_nom_volt, omap_voltage_get_nom_volt_fp);
	SYMSEARCH_BIND_FUNCTION_TO(opperator, omap_voltage_domain_get, omap_voltage_domain_get_fp);
	
	freq_table = cpufreq_frequency_get_table(0);
	policy = cpufreq_cpu_get(0);
	
	dev = omap2_get_mpuss_device();
	maxdex = (opp_get_opp_count_fp(dev)-1);
	opp = opp_find_freq_floor_fp(dev, &freq);
	if (IS_ERR(opp)) {
		return -ENODEV;
	}
	default_max_rate = opp->rate;
	default_max_voltage = opp->u_volt;
	
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
	opp->rate = default_max_rate;
	opp->u_volt = default_max_voltage;
	freq_table[maxdex].frequency = policy->max = policy->cpuinfo.max_freq =
	policy->user_policy.max = default_max_rate / 1000;
	
	printk(KERN_INFO " OPPerator: Reseting values to default... Goodbye!\n");
};
							 
module_init(opperator_init);
module_exit(opperator_exit);

