
#define pr_fmt(fmt)

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/power_supply.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/usb/usbpd.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/types.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/pmic-voter.h>

#include "pd_policy_manager_munch.h"

#define PD_SRC_PDO_TYPE_FIXED		0
#define PD_SRC_PDO_TYPE_BATTERY		1
#define PD_SRC_PDO_TYPE_VARIABLE	2
#define PD_SRC_PDO_TYPE_AUGMENTED	3

#define BATT_MAX_CHG_VOLT		4450
#define BATT_FAST_CHG_CURR		6000
#define	BUS_OVP_THRESHOLD		12000
#define	BUS_OVP_ALARM_THRESHOLD		9500

#define BUS_VOLT_INIT_UP		700
#define MIN_ADATPER_VOLTAGE_11V 11000
#define CAPACITY_HIGH_THR	90

#define BAT_VOLT_LOOP_LMT		BATT_MAX_CHG_VOLT
#define BAT_CURR_LOOP_LMT		BATT_FAST_CHG_CURR
#define BUS_VOLT_LOOP_LMT		BUS_OVP_THRESHOLD

#define PM_WORK_RUN_NORMAL_INTERVAL		500
#define PM_WORK_RUN_QUICK_INTERVAL		300
#define PM_WORK_RUN_CRITICAL_INTERVAL		300
int pd_log_count_poussin = 0;

enum {
	PM_ALGO_RET_OK,
	PM_ALGO_RET_THERM_FAULT,
	PM_ALGO_RET_OTHER_FAULT,
	PM_ALGO_RET_CHG_DISABLED,
	PM_ALGO_RET_TAPER_DONE,
	PM_ALGO_RET_UNSUPPORT_PPSTA,
	PM_ALGO_RET_NIGHT_CHARGING,
};
enum {
	CP_VBUS_ERROR_NONE,
	CP_VBUS_ERROR_LOW,
	CP_VBUS_ERROR_HIGH,
};
static struct pdpm_config pm_config = {
	.bat_volt_lp_lmt		= BAT_VOLT_LOOP_LMT,
	.bat_curr_lp_lmt		= BAT_CURR_LOOP_LMT + 1000,
	.bus_volt_lp_lmt		= BUS_VOLT_LOOP_LMT,
	.bus_curr_lp_lmt		= BAT_CURR_LOOP_LMT >> 1,
	.bus_curr_compensate	= 0,

	.fc2_taper_current		= TAPER_DONE_NORMAL_MA,
	.fc2_steps			= 1,

	.min_adapter_volt_required	= 10000,
	.min_adapter_curr_required	= 2000,

	.min_vbat_for_cp		= 3500,

	.cp_sec_enable			= true,
	.fc2_disable_sw			= true,
};
enum {
	SC8551_CHARGE_MODE_DIV2 = 0,
	SC8551_CHARGE_MODE_BYPASS,
	SC8551_CHARGE_MODE_STANDBY = -1,
};
static struct usbpd_pm *__pdpm;

static int fc2_taper_timer;
static int ibus_lmt_change_timer;

static int usbpd_pm_enable_cp_sec(struct usbpd_pm *pdpm, bool enable);
static int usbpd_pm_check_cp_sec_enabled(struct usbpd_pm *pdpm);
static int usbpd_pm_enable_cp(struct usbpd_pm *pdpm, bool enable);
static int usbpd_pm_check_cp_enabled(struct usbpd_pm *pdpm);

static void usbpd_check_usb_psy(struct usbpd_pm *pdpm)
{
	if (!pdpm->usb_psy) {
		pdpm->usb_psy = power_supply_get_by_name("usb");
		if (!pdpm->usb_psy)
			pr_debug("usb psy not found!\n");
	}
}
static void usbpd_check_batt_psy(struct usbpd_pm *pdpm)
{
	if (!pdpm->sw_psy) {
		pdpm->sw_psy = power_supply_get_by_name("battery");
		if (!pdpm->sw_psy)
			pr_debug("batt psy not found!\n");
	}
}
static void usbpd_check_bms_psy(struct usbpd_pm *pdpm)
{
	if (!pdpm->bms_psy) {
		pdpm->bms_psy = power_supply_get_by_name("bms");
		if (!pdpm->bms_psy)
			pr_debug("bms psy not found!\n");
	}
}

static void usbpd_check_main_psy(struct usbpd_pm *pdpm)
{
	if (!pdpm->main_psy) {
		pdpm->main_psy = power_supply_get_by_name("main");
		if (!pdpm->main_psy)
			pr_debug("main psy not found!\n");
	}
}

static int usbpd_get_effective_fcc_val(struct usbpd_pm *pdpm)
{
	int effective_fcc_val = 0;

	if (!pdpm->fcc_votable)
		pdpm->fcc_votable = find_votable("FCC");

	if (!pdpm->fcc_votable)
		return -EINVAL;

	effective_fcc_val = get_effective_result(pdpm->fcc_votable);
	effective_fcc_val = effective_fcc_val / 1000;
	/*pr_debug("effective_fcc_val: %d\n", effective_fcc_val);*/
	return effective_fcc_val;
}

/* get main switch mode charger charge type from battery power supply property */
static int pd_get_batt_charge_type(struct usbpd_pm *pdpm, int *charge_type)
{
	union power_supply_propval pval = {0,};
	int rc = 0;

	usbpd_check_batt_psy(pdpm);

	if (!pdpm->sw_psy)
		return -ENODEV;

	rc = power_supply_get_property(pdpm->sw_psy,
				POWER_SUPPLY_PROP_CHARGE_TYPE, &pval);
	if (rc < 0) {
		pr_debug("Couldn't get fastcharge mode:%d\n", rc);
		return rc;
	}

	pr_debug("charge_type: %d\n", pval.intval);

	*charge_type = pval.intval;
	return rc;
}

/* get step charge vfloat index from battery power supply property */
static int pd_get_batt_step_vfloat_index(struct usbpd_pm *pdpm, int *step_index)
{
	union power_supply_propval pval = {0,};
	int rc = 0;

	usbpd_check_batt_psy(pdpm);

	if (!pdpm->sw_psy)
		return -ENODEV;

	rc = power_supply_get_property(pdpm->sw_psy,
				POWER_SUPPLY_PROP_STEP_VFLOAT_INDEX, &pval);
	if (rc < 0) {
		pr_debug("Couldn't get fastcharge mode:%d\n", rc);
		return rc;
	}

	pr_debug("pval.intval: %d\n", pval.intval);

	*step_index = pval.intval;
	return rc;
}

static int pd_bq_soft_taper_by_main_charger_charge_type(struct usbpd_pm *pdpm)
{
	int rc = 0;
	int step_index = 0, curr_charge_type = 0;
	int effective_fcc_bq_taper = 0;

	rc = pd_get_batt_step_vfloat_index(pdpm, &step_index);
	if (rc >=0 && step_index == STEP_VFLOAT_INDEX_MAX) {
		rc = pd_get_batt_charge_type(pdpm, &curr_charge_type);
		if (rc >=0
				&& curr_charge_type == POWER_SUPPLY_CHARGE_TYPE_TAPER) {
			effective_fcc_bq_taper = usbpd_get_effective_fcc_val(pdpm);
			effective_fcc_bq_taper -= BQ_SOFT_TAPER_DECREASE_STEP_MA;
			pr_debug("BS voltage is reached to maxium vfloat, decrease fcc: %d mA\n",
						effective_fcc_bq_taper);
			if (pdpm->fcc_votable)
				vote(pdpm->fcc_votable, BQ_TAPER_FCC_VOTER,
					true, effective_fcc_bq_taper * 1000);
		}
	}

	return rc;
}

/* get thermal level from battery power supply property */
static int pd_get_batt_current_thermal_level(struct usbpd_pm *pdpm, int *level)
{
	union power_supply_propval pval = {0,};
	int rc = 0;

	usbpd_check_batt_psy(pdpm);

	if (!pdpm->sw_psy)
		return -ENODEV;

	rc = power_supply_get_property(pdpm->sw_psy,
				POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT, &pval);
	if (rc < 0) {
		pr_debug("Couldn't get fastcharge mode:%d\n", rc);
		return rc;
	}

	/*pr_debug("pval.intval: %d\n", pval.intval);*/

	*level = pval.intval;
	return rc;
}

static int pd_get_batt_capacity(struct usbpd_pm *pdpm, int *capacity)
{
	union power_supply_propval pval = {0,};
	int rc = 0;

	usbpd_check_batt_psy(pdpm);

	if (!pdpm->sw_psy)
		return -ENODEV;

	rc = power_supply_get_property(pdpm->sw_psy,
				POWER_SUPPLY_PROP_CAPACITY, &pval);
	if (rc < 0) {
		pr_debug("Couldn't get fastcharge mode:%d\n", rc);
		return rc;
	}

	/*pr_debug("pval.intval: %d\n", pval.intval);*/
	*capacity = pval.intval;
	return rc;
}

static void pd_bq_check_ibus_to_enable_dual_bq(struct usbpd_pm *pdpm, int ibus_ma)
{

	int capacity = 0;
	if(pdpm->enable_bypass > 0){
		//pr_debug("%s bypassmode no need to diaable slave and return\n",__func__);
		return;
	}

	pd_get_batt_capacity(pdpm, &capacity);
	if (ibus_ma <= IBUS_THRESHOLD_MA_FOR_DUAL_BQ_LN8000
			&& !pdpm->no_need_en_slave_bq
			&& (pdpm->slave_bq_disabled_check_count < IBUS_THR_TO_CLOSE_SLAVE_COUNT_MAX)) {
		pdpm->slave_bq_disabled_check_count++;
		if (pdpm->slave_bq_disabled_check_count >= IBUS_THR_TO_CLOSE_SLAVE_COUNT_MAX) {
			pdpm->no_need_en_slave_bq = true;
			/* disable slave bq due to total low ibus to avoid bq ucp */
			pr_debug("ibus decrease to threshold, disable slave bq now\n");
			usbpd_pm_enable_cp_sec(pdpm, false);
			usbpd_pm_check_cp_sec_enabled(pdpm);
			usbpd_pm_check_cp_enabled(pdpm);
			if (!pdpm->cp.charge_enabled) {
				pr_debug("%s enable_cp\n", __func__);
				usbpd_pm_enable_cp(pdpm, true);
				msleep(50);
				usbpd_pm_check_cp_enabled(pdpm);
			}
		}
	} else if (pdpm->no_need_en_slave_bq && (capacity < CAPACITY_HIGH_THR)
			&& (ibus_ma > (IBUS_THRESHOLD_MA_FOR_DUAL_BQ_LN8000 + IBUS_THR_MA_HYS_FOR_DUAL_BQ))) {
		if (!pdpm->cp_sec.charge_enabled) {
			pdpm->no_need_en_slave_bq = false;
			/* re-enable slave bq due to master ibus increase above threshold + hys */
			pr_debug("ibus increase above threshold, re-enable slave bq now\n");
			usbpd_pm_enable_cp_sec(pdpm, true);
			msleep(50);
			usbpd_pm_check_cp_sec_enabled(pdpm);
		}
	} else {
		pdpm->slave_bq_disabled_check_count = 0;
	}
}

/* determine whether to disable cp according to jeita status */
static bool pd_disable_cp_by_jeita_status(struct usbpd_pm *pdpm)
{
	union power_supply_propval pval = {0,};
	int batt_temp = 0, bq_input_suspend = 0;
	int rc;

	if (!pdpm->sw_psy)
		return false;

	rc = power_supply_get_property(pdpm->sw_psy,
			POWER_SUPPLY_PROP_INPUT_SUSPEND, &pval);
	if (!rc)
		bq_input_suspend = !!pval.intval;

	pr_debug("bq_input_suspend: %d\n", bq_input_suspend);

	/* is input suspend is set true, do not allow bq quick charging */
	if (bq_input_suspend)
		return true;

	if (!pdpm->bms_psy)
		return false;

	rc = power_supply_get_property(pdpm->bms_psy,
				POWER_SUPPLY_PROP_TEMP, &pval);
	if (rc < 0) {
		pr_debug("Couldn't get batt temp prop:%d\n", rc);
		return false;
	}
	pdpm->cp.bms_batt_temp = pval.intval;
	batt_temp = pval.intval;
	/*pr_debug("batt_temp: %d\n", batt_temp);*/

	if (batt_temp >= pdpm->battery_warm_th && !pdpm->jeita_triggered) {
		pdpm->jeita_triggered = true;
		return true;
	} else if (batt_temp <= JEITA_COOL_NOT_ALLOW_CP_THR 
			&& !pdpm->jeita_triggered) {
		pdpm->jeita_triggered = true;
		return true;
	} else if (batt_temp <= (pdpm->battery_warm_th - JEITA_HYSTERESIS)
			&& (batt_temp >= (JEITA_COOL_NOT_ALLOW_CP_THR + JEITA_HYSTERESIS))
			&& pdpm->jeita_triggered) {
		pdpm->jeita_triggered = false;
		return false;
	} else {
		return pdpm->jeita_triggered;
	}
}

/* get bq27z561 fastcharge mode to enable or disabled */
static bool pd_get_bms_digest_verified(struct usbpd_pm *pdpm)
{
	return true;

}

/* get bq27z561 chip ok*/
static bool pd_get_bms_chip_ok(struct usbpd_pm *pdpm)
{
	union power_supply_propval pval = {0,};
	int rc;

	if (!pdpm->bms_psy)
		return false;

	rc = power_supply_get_property(pdpm->bms_psy,
				POWER_SUPPLY_PROP_CHIP_OK, &pval);
	if (rc < 0) {
		pr_debug("Couldn't get chip ok:%d\n", rc);
		return false;
	}

	/*pr_debug("pval.intval: %d\n", pval.intval);*/
	pdpm->cp.bms_chip_ok = pval.intval;
	if (pval.intval == 1)
		return true;
	else
		return false;

}

/* get fastcharge mode */
static bool pd_get_fastcharge_mode_enabled(struct usbpd_pm *pdpm)
{
	union power_supply_propval pval = {0,};
	int rc;

	if (!pdpm->bms_psy)
		return false;

	rc = power_supply_get_property(pdpm->bms_psy,
				POWER_SUPPLY_PROP_FASTCHARGE_MODE, &pval);
	if (rc < 0) {
		pr_debug("Couldn't get fastcharge mode:%d\n", rc);
		return false;
	}

	/*pr_debug("fastcharge mode: %d\n", pval.intval);*/

	if (pval.intval == 1)
		return true;
	else
		return false;
}
/* get bq27z561 fastcharge mode to enable or disabled */

/* get pd pps charger verified result  */
#if 0
static bool pd_get_pps_charger_verified(struct usbpd_pm *pdpm)
{
	union power_supply_propval pval = {0,};
	int rc;

	if (!pdpm->usb_psy)
		return false;

	rc = power_supply_get_property(pdpm->usb_psy,
				POWER_SUPPLY_PROP_PD_AUTHENTICATION, &pval);
	if (rc < 0) {
		pr_debug("Couldn't get pd_authentication result:%d\n", rc);
		return false;
	}

	pr_debug("pval.intval: %d\n", pval.intval);

	if (pval.intval == 1)
		return true;
	else
		return false;
}
#endif

/* No need to use bms current max so far */
#if 0

static int pd_get_bms_charge_current_max(struct usbpd_pm *pdpm, int *fcc_ua)
{
	union power_supply_propval pval = {0,};
	int rc = 0;

	if (!pdpm->bms_psy || !pdpm->main_psy)
		return rc;

	/*J1 use ti gauge get fcc current, J2 use qcom gauge, need get main constant fcc current */
#ifdef CONFIG_FUEL_GAUGE_BQ27Z561_MUNCH
	if (pdpm->bms_psy) {
		rc = power_supply_get_property(pdpm->bms_psy,
				POWER_SUPPLY_PROP_CURRENT_MAX, &pval);
		if (rc < 0) {
			pr_debug("Couldn't get current max:%d\n", rc);
			return rc;
		}
	}
#else
	if (pdpm->main_psy) {
		rc = power_supply_get_property(pdpm->main_psy,
				POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX, &pval);
		if (rc < 0) {
			pr_debug("Couldn't get current max:%d\n", rc);
			return rc;
		}
	}
#endif

	*fcc_ua = pval.intval;
	return rc;

}
#endif

static int usbpd_set_new_fcc_voter(struct usbpd_pm *pdpm)
{
	int rc = 0;
	/* No need to use bms current max so far */
#if 0
	int fcc_ua = 0;
	rc = pd_get_bms_charge_current_max(pdpm, &fcc_ua);

	if (rc < 0)
		return rc;

	if (!pdpm->fcc_votable)
		pdpm->fcc_votable = find_votable("FCC");

	if (!pdpm->fcc_votable)
		return -EINVAL;

	if (pdpm->fcc_votable)
		vote(pdpm->fcc_votable, STEP_BMS_CHG_VOTER, true, fcc_ua);
#endif
	return rc;
}

static void usbpd_check_cp_psy(struct usbpd_pm *pdpm)
{
	if (!pdpm->cp_psy) {
		if (pm_config.cp_sec_enable)
			pdpm->cp_psy = power_supply_get_by_name("bq2597x-master");
		else
			pdpm->cp_psy = power_supply_get_by_name("bq2597x-standalone");
		if (!pdpm->cp_psy)
			pr_debug("cp_psy not found\n");
	}
}

static void usbpd_check_cp_sec_psy(struct usbpd_pm *pdpm)
{
	if (!pdpm->cp_sec_psy) {
		pdpm->cp_sec_psy = power_supply_get_by_name("bq2597x-slave");
		if (!pdpm->cp_sec_psy)
			pr_debug("cp_sec_psy not found\n");
	}
}

static void usbpd_pm_update_cp_status(struct usbpd_pm *pdpm)
{
	int ret;
	union power_supply_propval val = {0,};

	usbpd_check_cp_psy(pdpm);

	if (!pdpm->cp_psy)
		return;

	ret = power_supply_get_property(pdpm->cp_psy,
			POWER_SUPPLY_PROP_TI_BATTERY_VOLTAGE, &val);
	if (!ret)
		pdpm->cp.vbat_volt = val.intval;
	if (!pdpm->cp.vbat_volt)
		pdpm->cp.vbat_volt = pdpm->cp.bms_vbat_mv;
	ret = power_supply_get_property(pdpm->cp_psy,
			POWER_SUPPLY_PROP_TI_BUS_VOLTAGE, &val);
	if (!ret)
		pdpm->cp.vbus_volt = val.intval;

	ret = power_supply_get_property(pdpm->cp_psy,
			POWER_SUPPLY_PROP_TI_BUS_CURRENT, &val);
	if (!ret)
		pdpm->cp.ibus_curr = val.intval;

	ret = power_supply_get_property(pdpm->cp_psy,
			POWER_SUPPLY_PROP_TI_BUS_TEMPERATURE, &val);
	if (!ret)
		pdpm->cp.bus_temp = val.intval;

	ret = power_supply_get_property(pdpm->cp_psy,
			POWER_SUPPLY_PROP_TI_BATTERY_TEMPERATURE, &val);
	if (!ret)
		pdpm->cp.bat_temp = val.intval;

	ret = power_supply_get_property(pdpm->cp_psy,
			POWER_SUPPLY_PROP_TI_DIE_TEMPERATURE, &val);
	if (!ret)
		pdpm->cp.die_temp = val.intval;

	ret = power_supply_get_property(pdpm->cp_psy,
			POWER_SUPPLY_PROP_TI_BATTERY_PRESENT, &val);
	if (!ret)
		pdpm->cp.batt_pres = val.intval;

	ret = power_supply_get_property(pdpm->cp_psy,
			POWER_SUPPLY_PROP_TI_VBUS_PRESENT, &val);
	if (!ret)
		pdpm->cp.vbus_pres = val.intval;

	usbpd_check_bms_psy(pdpm);
	if (pdpm->bms_psy) {
		ret = power_supply_get_property(pdpm->bms_psy,
				POWER_SUPPLY_PROP_CURRENT_NOW, &val);
		if (!ret) {
			if (pdpm->cp.vbus_pres)
				pdpm->cp.ibat_curr = -(val.intval / 1000);
		}

		ret = power_supply_get_property(pdpm->bms_psy,
			POWER_SUPPLY_PROP_I2C_ERROR_COUNT, &val);
		if (!ret) {
			pdpm->bms_i2c_error_count = val.intval;
		}

		if (!pdpm->use_qcom_gauge) {
			ret = power_supply_get_property(pdpm->bms_psy,
						POWER_SUPPLY_PROP_VOLTAGE_NOW, &val);
			if (!ret)
				pdpm->cp.bms_vbat_mv = val.intval / 1000;
			else
				pr_debug("Failed to read bms voltage now\n");
		}
	}
	ret = power_supply_get_property(pdpm->cp_psy,
			POWER_SUPPLY_PROP_CHARGING_ENABLED, &val);
	if (!ret)
		pdpm->cp.charge_enabled = val.intval;

	if ((pm_config.cp_sec_enable) && (pdpm->cp_sec.charge_enabled))
		ret = power_supply_get_property(pdpm->cp_sec_psy,
				POWER_SUPPLY_PROP_TI_ALARM_STATUS, &val);

	ret = power_supply_get_property(pdpm->cp_psy,
			POWER_SUPPLY_PROP_TI_ALARM_STATUS, &val);
	if (!ret) {
		pdpm->cp.bat_ovp_alarm = !!(val.intval & BAT_OVP_ALARM_MASK);
		pdpm->cp.bat_ocp_alarm = !!(val.intval & BAT_OCP_ALARM_MASK);
		pdpm->cp.bus_ovp_alarm = !!(val.intval & BUS_OVP_ALARM_MASK);
		pdpm->cp.bus_ocp_alarm = !!(val.intval & BUS_OCP_ALARM_MASK);
		pdpm->cp.bat_ucp_alarm = !!(val.intval & BAT_UCP_ALARM_MASK);
		pdpm->cp.bat_therm_alarm = !!(val.intval & BAT_THERM_ALARM_MASK);
		pdpm->cp.bus_therm_alarm = !!(val.intval & BUS_THERM_ALARM_MASK);
		pdpm->cp.die_therm_alarm = !!(val.intval & DIE_THERM_ALARM_MASK);
	}

	ret = power_supply_get_property(pdpm->cp_psy,
			POWER_SUPPLY_PROP_TI_FAULT_STATUS, &val);
	if (!ret) {
		pdpm->cp.bat_ovp_fault = !!(val.intval & BAT_OVP_FAULT_MASK);
		pdpm->cp.bat_ocp_fault = !!(val.intval & BAT_OCP_FAULT_MASK);
		pdpm->cp.bus_ovp_fault = !!(val.intval & BUS_OVP_FAULT_MASK);
		pdpm->cp.bus_ocp_fault = !!(val.intval & BUS_OCP_FAULT_MASK);
		pdpm->cp.bat_therm_fault = !!(val.intval & BAT_THERM_FAULT_MASK);
		pdpm->cp.bus_therm_fault = !!(val.intval & BUS_THERM_FAULT_MASK);
		pdpm->cp.die_therm_fault = !!(val.intval & DIE_THERM_FAULT_MASK);
	}

	ret = power_supply_get_property(pdpm->cp_psy,
			POWER_SUPPLY_PROP_TI_REG_STATUS, &val);
	if (!ret) {
		pdpm->cp.vbat_reg = !!(val.intval & VBAT_REG_STATUS_MASK);
		pdpm->cp.ibat_reg = !!(val.intval & IBAT_REG_STATUS_MASK);
	}
}

static void usbpd_pm_update_cp_sec_status(struct usbpd_pm *pdpm)
{
	int ret;
	union power_supply_propval val = {0,};

	if (!pm_config.cp_sec_enable)
		return;

	usbpd_check_cp_sec_psy(pdpm);

	if (!pdpm->cp_sec_psy)
		return;

	ret = power_supply_get_property(pdpm->cp_sec_psy,
			POWER_SUPPLY_PROP_TI_BUS_CURRENT, &val);
	if (!ret)
		pdpm->cp_sec.ibus_curr = val.intval;

	ret = power_supply_get_property(pdpm->cp_sec_psy,
			POWER_SUPPLY_PROP_CHARGING_ENABLED, &val);
	if (!ret)
		pdpm->cp_sec.charge_enabled = val.intval;
}
/* SC8551 bypass mode related */
static int usbpd_pm_get_vbus_error(struct usbpd_pm *pdpm)
{
	int ret;
	union power_supply_propval val = {0,};

	usbpd_check_cp_psy(pdpm);

	if (!pdpm->cp_psy)
		return -ENODEV;

	ret = power_supply_get_property(pdpm->cp_psy,
			POWER_SUPPLY_PROP_TI_BUS_ERROR_STATUS, &val);
	if (!ret)
		pdpm->cp.vbus_error_status = !!val.intval;
	pr_debug("%s %d\n", __func__,pdpm->cp.vbus_error_status);
	return ret;
}

#define MAX_BYPASS_CURRENT_MA			4000
#define BYPASS_VBAT_ENTER_THRES			3600 // avoid triger vin_uvlo
#define BYPASS_FCC_ENTER_MAX_THRES		4000
#define BYPASS_FCC_ENTER_MIN_THRES		2000
#define BYPASS_FCC_EXIT_THRES			6000
#define START_DRIECT_CHARGE_FCC_MIN_THR		2000
static int usbpd_pm_set_cp_mode(struct usbpd_pm *pdpm, int mode)
{
	int ret;
	union power_supply_propval val = {0,};

	usbpd_check_cp_psy(pdpm);

	if (!pdpm->cp_psy)
		return -ENODEV;

	val.intval = mode;
	ret = power_supply_set_property(pdpm->cp_psy,
			POWER_SUPPLY_PROP_CHARGING_MODE, &val);
	pr_debug("%s enable:%d\n", __func__, mode);
	return ret;
}

static int usbpd_pm_set_cp_sec_mode(struct usbpd_pm *pdpm, int mode)
{
	int ret;
	union power_supply_propval val = {0,};

	usbpd_check_cp_sec_psy(pdpm);

	if (!pdpm->cp_sec_psy)
		return -ENODEV;

	val.intval = mode;
	ret = power_supply_set_property(pdpm->cp_sec_psy,
			POWER_SUPPLY_PROP_CHARGING_MODE, &val);
	pr_debug("%s enable:%d\n", __func__, mode);
	return ret;
}

static int usbpd_pm_get_cp_mode(struct usbpd_pm *pdpm)
{
	int ret;
	union power_supply_propval val = {0,};

	usbpd_check_cp_psy(pdpm);

	if (!pdpm->cp_psy)
		return -ENODEV;

	ret = power_supply_get_property(pdpm->cp_psy,
			POWER_SUPPLY_PROP_CHARGING_MODE, &val);
	return val.intval;
}

static int usbpd_pm_get_cp_sec_mode(struct usbpd_pm *pdpm)
{
	int ret;
	union power_supply_propval val = {0,};

	usbpd_check_cp_sec_psy(pdpm);

	if (!pdpm->cp_sec_psy)
		return -ENODEV;

	ret = power_supply_get_property(pdpm->cp_sec_psy,
			POWER_SUPPLY_PROP_CHARGING_MODE, &val);
	return val.intval;;
}

static int usbpd_pm_enable_cp(struct usbpd_pm *pdpm, bool enable)
{
	int ret;
	union power_supply_propval val = {0,};

	usbpd_check_cp_psy(pdpm);

	if (!pdpm->cp_psy)
		return -ENODEV;

	val.intval = enable;
	ret = power_supply_set_property(pdpm->cp_psy,
			POWER_SUPPLY_PROP_CHARGING_ENABLED, &val);
	pr_debug("%s enable:%d\n", __func__, enable);
	return ret;
}

static int usbpd_pm_check_cp_bus_ovp(struct usbpd_pm *pdpm, bool enable)
{
	int ret;
	union power_supply_propval val = {0,};

	usbpd_check_cp_sec_psy(pdpm);
	usbpd_check_cp_psy(pdpm);

	if (!pdpm->cp_sec_psy && !pdpm->cp_sec_psy)
		return -ENODEV;

	val.intval = enable;
	ret = power_supply_set_property(pdpm->cp_sec_psy,
			POWER_SUPPLY_PROP_TI_SET_BUS_PROTECTION_FOR_PD, &val);

	msleep(10);

	ret = power_supply_set_property(pdpm->cp_psy,
			POWER_SUPPLY_PROP_TI_SET_BUS_PROTECTION_FOR_PD, &val);

	msleep(10);

	return ret;
}

static int usbpd_pm_enable_cp_sec(struct usbpd_pm *pdpm, bool enable)
{
	int ret;
	union power_supply_propval val = {0,};

	usbpd_check_cp_sec_psy(pdpm);

	if (!pdpm->cp_sec_psy)
		return -ENODEV;

	val.intval = enable;
	ret = power_supply_set_property(pdpm->cp_sec_psy,
			POWER_SUPPLY_PROP_CHARGING_ENABLED, &val);
	pr_debug("%s enable:%d\n", __func__, enable);
	return ret;
}

static int usbpd_pm_check_cp_enabled(struct usbpd_pm *pdpm)
{
	int ret;
	union power_supply_propval val = {0,};

	usbpd_check_cp_psy(pdpm);

	if (!pdpm->cp_psy)
		return -ENODEV;

	ret = power_supply_get_property(pdpm->cp_psy,
			POWER_SUPPLY_PROP_CHARGING_ENABLED, &val);
	//pr_debug("%s enable:%d,ret:%d\n", __func__,val.intval,ret);

	pdpm->cp.charge_enabled = !!val.intval;

	return ret;
}

static int usbpd_pm_check_cp_sec_enabled(struct usbpd_pm *pdpm)
{
	int ret;
	union power_supply_propval val = {0,};

	usbpd_check_cp_sec_psy(pdpm);

	if (!pdpm->cp_sec_psy)
		return -ENODEV;

	ret = power_supply_get_property(pdpm->cp_sec_psy,
			POWER_SUPPLY_PROP_CHARGING_ENABLED, &val);
	pr_debug("%s enable:%d,ret:%d\n", __func__,val.intval,ret);

	pdpm->cp_sec.charge_enabled = !!val.intval;

	return ret;
}

static int usbpd_pm_check_sec_batt_present(struct usbpd_pm *pdpm)
{
	int ret;
	union power_supply_propval val = {0,};

	usbpd_check_cp_sec_psy(pdpm);

	if (!pdpm->cp_sec_psy)
		return -ENODEV;

	ret = power_supply_get_property(pdpm->cp_sec_psy,
			POWER_SUPPLY_PROP_TI_BATTERY_PRESENT, &val);
	if (!ret)
		pdpm->cp_sec.batt_connecter_present = !!val.intval;
	pr_debug("pdpm->cp_sec.batt_connecter_present:%d\n", pdpm->cp_sec.batt_connecter_present);
	return ret;
}

static int usbpd_pm_check_cp_charge_mode(struct usbpd_pm *pdpm)
{
	int ret1 = 0, ret2 = 0;

	usbpd_check_cp_psy(pdpm);
	if (!pdpm->cp_psy)
		return -ENODEV;
	if (pm_config.cp_sec_enable) {
		usbpd_check_cp_sec_psy(pdpm);
		if (!pdpm->cp_sec_psy)
			return -ENODEV;
	}
	ret1 = usbpd_pm_get_cp_mode(pdpm);
	ret2 = usbpd_pm_get_cp_sec_mode(pdpm);
	return ret1;
}
static int usbpd_pm_set_cp_charge_mode(struct usbpd_pm *pdpm, int mode)
{
	int ret;

	pr_debug("cp charge mode switch from %d to %d.\n",
			pdpm->cp.sc8551_charge_mode, mode);
	usbpd_check_cp_psy(pdpm);
	if (!pdpm->cp_psy)
		return -ENODEV;
	if (pm_config.cp_sec_enable) {
		usbpd_check_cp_sec_psy(pdpm);
		if (!pdpm->cp_sec_psy)
			return -ENODEV;
	}
	pr_debug("mode:%d \n",mode);
	ret = usbpd_pm_set_cp_mode(pdpm, mode);

	if (pm_config.cp_sec_enable) {
		ret = usbpd_pm_set_cp_sec_mode(pdpm, mode);
	}
	pdpm->cp.sc8551_charge_mode = mode;
	return ret;
}

static int usbpd_pm_update_cp_charge_mode(struct usbpd_pm *pdpm)
{
	int fcc = 0,chg_mode = 0;

	usbpd_check_bms_psy(pdpm);
	if (!pdpm->bms_psy)
		return -ENODEV;
	chg_mode = usbpd_pm_get_cp_mode(pdpm);
	fcc = usbpd_get_effective_fcc_val(pdpm);
	/*if (!strcmp(get_effective_client(pdpm->fcc_votable),"BQ_TAPER_FCC_VOTER")) {
		return chg_mode;
	}*/
	pr_debug("fcc:%d,chg_mode:%d,sc8551_mode:%d\n",fcc,chg_mode,pdpm->cp.sc8551_charge_mode);

	if (fcc <= BYPASS_FCC_ENTER_MAX_THRES && fcc >= BYPASS_FCC_ENTER_MIN_THRES && pdpm->apdo_max_curr >= BYPASS_FCC_ENTER_MAX_THRES &&
				 (chg_mode == SC8551_CHARGE_MODE_DIV2 || chg_mode == SC8551_CHARGE_MODE_STANDBY)){
		chg_mode = SC8551_CHARGE_MODE_BYPASS;

	} else if (fcc >= BYPASS_FCC_EXIT_THRES && (chg_mode == SC8551_CHARGE_MODE_BYPASS || chg_mode == SC8551_CHARGE_MODE_STANDBY)) {
		chg_mode = SC8551_CHARGE_MODE_DIV2;
	} else if (fcc < BYPASS_FCC_EXIT_THRES && fcc > BYPASS_FCC_ENTER_MAX_THRES && chg_mode == SC8551_CHARGE_MODE_STANDBY) {

			chg_mode = SC8551_CHARGE_MODE_DIV2;
	}
	pr_debug("chg_mode:%d\n",chg_mode);
	return chg_mode;
}

int usbpd_pm_set_batt_input_suspend(struct usbpd_pm *pdpm, bool val)
{
	int rc;

	if (!pdpm->usb_icl_votable)
		pdpm->usb_icl_votable = find_votable("USB_ICL");

	if (!pdpm->usb_icl_votable){
		pr_debug("Couldn't find the usb icl votable \n");
	}

	if (!pdpm->dc_suspend_votable)
		pdpm->dc_suspend_votable = find_votable("DC_SUSPEND");

	if (!pdpm->dc_suspend_votable){
		pr_debug("Couldn't find the dc suspend votable \n");
	}

	/* vote 0mA when suspended */
	rc = vote(pdpm->usb_icl_votable, BYPASS_SWITCH_VOTER, val, 100000);
	if (rc < 0) {
		pr_debug("Couldn't vote to %s USB rc=%d\n",
			val ? "suspend" : "resume", rc);
		return rc;
	}

	rc = vote(pdpm->dc_suspend_votable, BYPASS_SWITCH_VOTER, val, 100000);
	if (rc < 0) {
		pr_debug("Couldn't vote to %s DC rc=%d\n",
			val ? "suspend" : "resume", rc);
		return rc;
	}

	/*if(pdpm->sw_psy)
		power_supply_changed(pdpm->sw_psy);*/
	return rc;
}


static int usbpd_pm_switch_cp_charge_mode(struct usbpd_pm *pdpm,int mode)
{
	int ret = 0;
	int chg_mode = 0;

	if (!pdpm->sw_psy)
		return -ENODEV;
	usbpd_check_bms_psy(pdpm);
	if (!pdpm->bms_psy)
		return -ENODEV;

	chg_mode = mode;

	ret = usbpd_pm_set_batt_input_suspend(pdpm,true);

	msleep(100);

	if (pm_config.cp_sec_enable && pdpm->cp_sec.charge_enabled) {
		pr_debug("close two cp to change mode\n");
		usbpd_pm_enable_cp_sec(pdpm, false);
		msleep(30);
		usbpd_pm_check_cp_sec_enabled(pdpm);
	}

	if (pdpm->cp.charge_enabled) {
		usbpd_pm_enable_cp(pdpm, false);
		msleep(30);
		usbpd_pm_check_cp_enabled(pdpm);
	}

	usbpd_pm_set_cp_charge_mode(pdpm, chg_mode);

	ret = usbpd_pm_set_batt_input_suspend(pdpm,false);

	ret = usbpd_pm_check_cp_charge_mode(pdpm);

	pr_debug("check cp charge mode, ret:%d.\n", ret);

	ret = 1;

	return ret;
}
/*
static int usbpd_pm_enable_sw(struct usbpd_pm *pdpm, bool enable)
{
	int ret;

	if (!pdpm->usb_icl_votable) {
		pdpm->usb_icl_votable = find_votable("USB_ICL");
	}

	if (!pdpm->usb_icl_votable){
		pr_debug("Couldn't find the usb icl votable \n");
		ret = -1;
		return ret;
	}

	if (enable == false) {
		vote(pdpm->usb_icl_votable, CHARGEPUMP_SWITCH_VOTER,
						true, 100000);
	} else {
		vote(pdpm->usb_icl_votable, CHARGEPUMP_SWITCH_VOTER,
						false, 0);
	}

	return ret;
}*/

static int usbpd_pm_enable_sw(struct usbpd_pm *pdpm, bool enable)
{
	int ret;
	union power_supply_propval val = {0,};

	if (!pdpm->sw_psy) {
		pdpm->sw_psy = power_supply_get_by_name("battery");
		if (!pdpm->sw_psy) {
			return -ENODEV;
		}
	}

	val.intval = enable;
	ret = power_supply_set_property(pdpm->sw_psy,
			POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED, &val);

	return ret;
}

static int usbpd_pm_check_night_charging_enabled(struct usbpd_pm *pdpm)
{
	int ret;
	union power_supply_propval val = {0,};
	if (!pdpm->sw_psy) {
		pdpm->sw_psy = power_supply_get_by_name("battery");
		if (!pdpm->sw_psy) {
			return -ENODEV;
		}
	}
	ret = power_supply_get_property(pdpm->sw_psy,
			POWER_SUPPLY_PROP_BATTERY_INPUT_SUSPEND, &val);
	if (!ret)
		pdpm->sw.night_charging = !!val.intval;
	return ret;
}
/*
static int usbpd_pm_check_sw_enabled(struct usbpd_pm *pdpm)
{
	int ret,usb_icl;

	if (!pdpm->usb_icl_votable) {
		pdpm->usb_icl_votable = find_votable("USB_ICL");
	}

	if (!pdpm->usb_icl_votable){
		pr_debug("Couldn't find the usb icl votable \n");
		ret = -1;
		return ret;
	}

	usb_icl = get_client_vote(pdpm->usb_icl_votable,CHARGEPUMP_SWITCH_VOTER);

	if(usb_icl >= 100000)
		pdpm->sw.charge_enabled = false;
	else
		pdpm->sw.charge_enabled = true;

	return ret;
}*/

static int usbpd_pm_check_sw_enabled(struct usbpd_pm *pdpm)
{
	int ret;
	union power_supply_propval val = {0,};

	if (!pdpm->sw_psy) {
		pdpm->sw_psy = power_supply_get_by_name("battery");
		if (!pdpm->sw_psy) {
			return -ENODEV;
		}
	}

	ret = power_supply_get_property(pdpm->sw_psy,
			POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED, &val);
	if (!ret)
		pdpm->sw.charge_enabled = !!val.intval;

	return ret;
}

static void usbpd_pm_update_sw_status(struct usbpd_pm *pdpm)
{
	usbpd_pm_check_sw_enabled(pdpm);
}

static void usbpd_pm_evaluate_src_caps(struct usbpd_pm *pdpm)
{
	int ret;
	int i;
	union power_supply_propval pval = {0, };

	if (!pdpm->pd) {
		pdpm->pd = smb_get_usbpd();
		if (!pdpm->pd) {
			pr_debug("couldn't get usbpd device\n");
			return;
		}
	}

	ret = usbpd_fetch_pdo(pdpm->pd, pdpm->pdo);
	if (ret) {
		pr_debug("Failed to fetch pdo info\n");
		return;
	}

	pdpm->apdo_max_volt = pm_config.min_adapter_volt_required;
	pdpm->apdo_max_curr = pm_config.min_adapter_curr_required;

	for (i = 0; i < PDO_MAX_NUM; i++) {
		if (pdpm->pdo[i].type == PD_SRC_PDO_TYPE_AUGMENTED
			&& pdpm->pdo[i].pps && pdpm->pdo[i].pos) {
			if (pdpm->pdo[i].max_volt_mv >= pdpm->apdo_max_volt
					&& pdpm->pdo[i].curr_ma >= pdpm->apdo_max_curr
					&& pdpm->pdo[i].max_volt_mv <= APDO_MAX_VOLT) {
				pdpm->apdo_max_volt = pdpm->pdo[i].max_volt_mv;
				pdpm->apdo_min_volt = pdpm->pdo[i].min_volt_mv;
				pdpm->apdo_max_curr = pdpm->pdo[i].curr_ma;
				pdpm->apdo_selected_pdo = pdpm->pdo[i].pos;
				pdpm->pps_supported = true;
			}
		}
	}

	if (pdpm->pps_supported) {
		pr_debug("PPS supported, preferred APDO pos:%d, max volt:%d, current:%d\n",
				pdpm->apdo_selected_pdo,
				pdpm->apdo_max_volt,
				pdpm->apdo_max_curr);
		if (pdpm->apdo_max_curr <= LOW_POWER_PPS_CURR_THR)
			pdpm->apdo_max_curr = XIAOMI_LOW_POWER_PPS_CURR_MAX;
		if(pdpm->apdo_min_volt >= 5000 && pdpm->usb_psy){
			//pdpm->enable_bypass = false;
			pval.intval = 0;
			ret = power_supply_set_property(pdpm->usb_psy,POWER_SUPPLY_PROP_ENABLE_BYPASS_MODE,&pval);
			if(ret < 0)
				pr_debug("can not get enable_bypass with rc:%d\n", ret);
			pr_debug("weak vbus detect and disable bypass mode\n");
		}
		pval.intval = (pdpm->apdo_max_volt / 1000) * (pdpm->apdo_max_curr / 1000);
		power_supply_set_property(pdpm->usb_psy,
				POWER_SUPPLY_PROP_APDO_MAX, &pval);
	} else {
		pr_debug("Not qualified PPS adapter\n");
	}
}

static void usbpd_update_pps_status(struct usbpd_pm *pdpm)
{
	int ret;
	u32 status;

	/* we will use it later, to do */
	return;

	ret = usbpd_get_pps_status(pdpm->pd, &status);

	if (!ret) {
		pr_debug("get_pps_status: status_db :0x%x\n", status);
		/*TODO: check byte order to insure data integrity*/
		pdpm->adapter_voltage = (status & 0xFFFF) * 20;
		pdpm->adapter_current = ((status >> 16) & 0xFF) * 50;
		pdpm->adapter_ptf = ((status >> 24) & 0x06) >> 1;
		pdpm->adapter_omf = !!((status >> 24) & 0x08);
		pr_debug("adapter_volt:%d, adapter_current:%d\n",
				pdpm->adapter_voltage, pdpm->adapter_current);
		pr_debug("pdpm->adapter_ptf:%d, pdpm->adapter_omf:%d\n",
				pdpm->adapter_ptf, pdpm->adapter_omf);
	}
}
#define TAPER_TIMEOUT	(10000 / PM_WORK_RUN_QUICK_INTERVAL)
#define IBUS_CHANGE_TIMEOUT  (1000 / PM_WORK_RUN_QUICK_INTERVAL)

enum{
	CHARGEPUMP_UNKNOWN,
	CHARGEPUMP_LN8000,
	CHARGEPUMP_SC8551,
};


static void usbpd_fc2_exit_work(struct work_struct *work)
{
	struct usbpd_pm *pdpm = container_of(work, struct usbpd_pm,
					fc2_exit_work.work);

	while (pdpm->cp.vbus_volt > 6000) {
		if (!pdpm->fc2_exit_flag) {
			pr_debug("fc2_exit_flag:false, break.\n");
			return;
		}

		pdpm->request_voltage -= 500;
		pr_debug("request_voltage:%d.\n", pdpm->request_voltage);
		if (pdpm->request_voltage < 5500) {
			pr_debug("request_voltage < 5.5V, break.\n");
			break;
		}
		usbpd_select_pdo(pdpm->pd, pdpm->apdo_selected_pdo,
				pdpm->request_voltage * 1000, pdpm->request_current * 1000);
		msleep(500);
		usbpd_pm_update_cp_status(pdpm);
		pr_debug("vbus_mv:%d.\n", pdpm->cp.vbus_volt);
	}

	pr_debug("%s:select default 5V.\n", __func__);
	usbpd_select_pdo(pdpm->pd, 1, 0, 0);
	pdpm->fc2_exit_flag = false;
}


static int chg_mode,ibus_upgrade,ibus_upgrade_bypass,last_curr_ibus_limit,last_curr_ibus_limit_bypass;
static int usbpd_pm_fc2_charge_algo(struct usbpd_pm *pdpm)
{
	int steps;
	int sw_ctrl_steps = 0;
	int hw_ctrl_steps = 0;
	int step_vbat = 0;
	int step_ibus = 0;
	int step_ibat = 0;
	int step_bat_reg = 0;
	int ibus_total = 0;
	int effective_fcc_val = 0;
	int effective_fcc_taper = 0;
	int thermal_level = 0;
	int bq_taper_hys_mv = BQ_TAPER_HYS_MV;
	bool is_fastcharge_mode = false;
	bool unsupport_pps_status = false;
	static int curr_fcc_limit, curr_ibus_limit;
	int capacity = 0;
	static int ibus_limit;
	union power_supply_propval pval = {0, };
	int rc = -1;
	//int chg_mode;
	pd_get_batt_capacity(pdpm, &capacity);

	rc = power_supply_get_property(pdpm->sw_psy,
				POWER_SUPPLY_PROP_SMART_BATT, &pval);
	if (rc < 0) {
		pr_debug("Get samrt batt failed, rc = %d\n", rc);
	}

	is_fastcharge_mode = pd_get_fastcharge_mode_enabled(pdpm);
	if (is_fastcharge_mode) {
		pm_config.bat_volt_lp_lmt = pdpm->bat_volt_max - pval.intval;
		bq_taper_hys_mv = BQ_TAPER_HYS_MV;
		pm_config.fc2_taper_current = TAPER_DONE_FFC_MA_LN8000;
	} else {
		pm_config.bat_volt_lp_lmt = pdpm->non_ffc_bat_volt_max - pval.intval;
		bq_taper_hys_mv = NON_FFC_BQ_TAPER_HYS_MV;
		pm_config.fc2_taper_current = TAPER_DONE_NORMAL_MA;
	}
	usbpd_set_new_fcc_voter(pdpm);
	pr_info("pdpm->cp.bms_vbat_mv: %d,pm_config.bat_volt_lp_lmt:%d\n", pdpm->cp.bms_vbat_mv,pm_config.bat_volt_lp_lmt);
	/* if cell vol read from fuel gauge is higher than threshold, vote saft fcc to protect battery */
	if (!pdpm->use_qcom_gauge && is_fastcharge_mode) {
		if (pdpm->cp.bms_vbat_mv > pdpm->cell_vol_max_threshold_mv) {
			if (pdpm->over_cell_vol_max_count++ > CELL_VOLTAGE_MAX_COUNT_MAX) {
				pdpm->over_cell_vol_max_count = 0;
				effective_fcc_taper = usbpd_get_effective_fcc_val(pdpm);
				effective_fcc_taper -= BQ_TAPER_DECREASE_STEP_MA;
				pr_debug("vcell is reached to max threshold, decrease fcc: %d mA\n",
							effective_fcc_taper);
				if (pdpm->fcc_votable) {
					if (effective_fcc_taper >= 2000)
						vote(pdpm->fcc_votable, BQ_TAPER_FCC_VOTER,
							true, effective_fcc_taper * 1000);
				}
			}
		} else {
			pdpm->over_cell_vol_max_count = 0;
		}
	}

	if (pdpm->use_qcom_gauge && (!pdpm->chg_enable_k11a))
		pd_bq_soft_taper_by_main_charger_charge_type(pdpm);

	effective_fcc_val = usbpd_get_effective_fcc_val(pdpm);

	if (effective_fcc_val > 0) {
		curr_fcc_limit = min(pm_config.bat_curr_lp_lmt, effective_fcc_val);
		if (pm_config.cp_sec_enable) {
			/* only master bq works, maxium target fcc should limit to 6A */
			if (pdpm->no_need_en_slave_bq)
				curr_fcc_limit = min(curr_fcc_limit, FCC_MAX_MA_FOR_MASTER_BQ);
		}
		if (pdpm->cp.sc8551_charge_mode != SC8551_CHARGE_MODE_BYPASS)
			curr_ibus_limit = curr_fcc_limit >> 1;
		else {
			curr_ibus_limit = curr_fcc_limit;
			//pdpm->apdo_max_curr = 6000;
		}
		/*
		 * bq25970 alone compensate 100mA,  bq25970 master ans slave  compensate 300mA,
		 * for target curr_ibus_limit for bq adc accurancy is below standard and power suuply system current
		 */
		curr_ibus_limit += pm_config.bus_curr_compensate;

		/* curr_ibus_limit should compare with apdo_max_curr here*/
		curr_ibus_limit = min(curr_ibus_limit, pdpm->apdo_max_curr);

		if (pdpm->use_qcom_gauge && curr_ibus_limit == IBUS_MAX_CURRENT_MA) {
			if (!pdpm->chg_enable_k11a) {
				curr_ibus_limit += pm_config.bus_curr_compensate;
			} else {
				curr_ibus_limit += 30;
			}
		}
	}
	if (pdpm->cp.sc8551_charge_mode == SC8551_CHARGE_MODE_BYPASS) {
		curr_fcc_limit = min(curr_fcc_limit, MAX_BYPASS_CURRENT_MA);
	}
	/* if cell vol read from fuel gauge is higher than threshold, vote saft fcc to protect battery */
	if (!pdpm->use_qcom_gauge && is_fastcharge_mode) {

		if (pdpm->cp.bms_vbat_mv > pdpm->cell_vol_high_threshold_mv - pval.intval) {
			if (pdpm->over_cell_vol_high_count++ > CELL_VOLTAGE_HIGH_COUNT_MAX) {
				pdpm->over_cell_vol_high_count = 0;
				if (pdpm->fcc_votable)
					vote(pdpm->fcc_votable, BQ_TAPER_CELL_HGIH_FCC_VOTER,
							true, pdpm->step_charge_high_vol_curr_max * 1000);
				pr_info("step_charge_high_vol_curr_max: %d\n", pdpm->step_charge_high_vol_curr_max);
			}
		} else {
			pdpm->over_cell_vol_high_count = 0;
		}
	}

	if (!pdpm->use_qcom_gauge) {
		if (curr_ibus_limit >= HIGH_IBUS_LIMI_THR_MA && !pdpm->chg_enable_k81 && pdpm->cp.sc8551_charge_mode != SC8551_CHARGE_MODE_BYPASS)
			ibus_limit = curr_ibus_limit + IBUS_TARGET_COMP_MA;
		else if (curr_ibus_limit >= HIGH_IBUS_LIMI_THR_MA && pdpm->chg_enable_k81)
				ibus_limit = curr_ibus_limit + IBUS_TARGET_COMP_30MA;
		else
			ibus_limit = curr_ibus_limit;
	} else {
		ibus_limit = curr_ibus_limit;
	}

	/* reduce bus current in cv loop */
	if (pdpm->cp.bms_vbat_mv > (pm_config.bat_volt_lp_lmt - bq_taper_hys_mv)) {

		if (ibus_lmt_change_timer++ > IBUS_CHANGE_TIMEOUT
				&& !pdpm->use_qcom_gauge) {
			ibus_lmt_change_timer = 0;
			ibus_limit = curr_ibus_limit - 100;
			effective_fcc_taper = usbpd_get_effective_fcc_val(pdpm);
			effective_fcc_taper -= BQ_TAPER_DECREASE_STEP_MA;
			pr_debug("bq set taper fcc to : %d mA\n", effective_fcc_taper);
			if (pdpm->fcc_votable) {
				if (effective_fcc_taper >= 2000) {
					vote(pdpm->fcc_votable, BQ_TAPER_FCC_VOTER,
							true, effective_fcc_taper * 1000);
					pr_debug("BQ_TAPER_FCC_VOTER \n");
				}
			}
		}
	} else if (pdpm->cp.bms_vbat_mv < pm_config.bat_volt_lp_lmt - 250) {
		if (!pdpm->use_qcom_gauge) {
			if (curr_ibus_limit >= HIGH_IBUS_LIMI_THR_MA && pdpm->cp.sc8551_charge_mode != SC8551_CHARGE_MODE_BYPASS)
				ibus_limit = curr_ibus_limit + IBUS_TARGET_COMP_MA;
			else
				ibus_limit = curr_ibus_limit;
		} else {
			ibus_limit = curr_ibus_limit;
		}
		ibus_lmt_change_timer = 0;
	} else {
		ibus_lmt_change_timer = 0;
	}


	/* battery voltage loop*/

	if (pdpm->cp.bms_vbat_mv > pm_config.bat_volt_lp_lmt-5)
		step_vbat = -pm_config.fc2_steps;
	else if (pdpm->cp.bms_vbat_mv < pm_config.bat_volt_lp_lmt - 15)
		step_vbat = pm_config.fc2_steps;
	/* battery charge current loop*/
	if (!pdpm->use_qcom_gauge) {
		if (pdpm->cp.ibat_curr < curr_fcc_limit-100)
			step_ibat = pm_config.fc2_steps;
		else if (pdpm->cp.ibat_curr > curr_fcc_limit + 50)
			step_ibat = -pm_config.fc2_steps;
	}

	/* bus current loop*/
	ibus_total = pdpm->cp.ibus_curr;
	if (pm_config.cp_sec_enable) {
		ibus_total += pdpm->cp_sec.ibus_curr;
		pd_bq_check_ibus_to_enable_dual_bq(pdpm, ibus_total);
	}

	if (pdpm->cp.sc8551_charge_mode) {
		ibus_limit += 300;
		ibus_limit = min(ibus_limit, 4500);
		//ibus_limit = min(ibus_limit, curr_fcc_limit);
	}

	if (pdpm->cp.sc8551_charge_mode != SC8551_CHARGE_MODE_BYPASS){
		if (ibus_total < ibus_limit - 50)
			step_ibus = pm_config.fc2_steps;
		else if (ibus_total > ibus_limit)
			step_ibus = -pm_config.fc2_steps;
	}else {
		if (ibus_total < ibus_limit - 100)
			step_ibus = pm_config.fc2_steps;
		else if (ibus_total > ibus_limit)
			step_ibus = -pm_config.fc2_steps;	
	}

	/* hardware regulation loop*/
	if (pdpm->cp.vbat_reg) /*|| pdpm->cp.ibat_reg*/
		step_bat_reg = 3 * (-pm_config.fc2_steps);
	else
		step_bat_reg = pm_config.fc2_steps;

	/*
	 * As qcom gauge ibat changes every 1 second,
	 * so do not use step_ibat for qcom gauge project
	 * such as J2 & J11
	 */

	if (!pdpm->use_qcom_gauge && !pdpm->cp.sc8551_charge_mode)
		sw_ctrl_steps = min(min(step_vbat, step_ibus), step_ibat);
	else
		sw_ctrl_steps = min(min(step_vbat, step_ibus), step_ibat);

	sw_ctrl_steps = min(sw_ctrl_steps, step_bat_reg);

	/* hardware alarm loop */
	if (pdpm->cp.bus_ocp_alarm || pdpm->cp.bus_ovp_alarm)
		hw_ctrl_steps = -pm_config.fc2_steps;
	else
		hw_ctrl_steps = pm_config.fc2_steps;

	/* check if cp disabled due to other reason*/
	usbpd_pm_check_cp_enabled(pdpm);

	if (pm_config.cp_sec_enable)
		usbpd_pm_check_cp_sec_enabled(pdpm);

	pd_get_batt_current_thermal_level(pdpm, &thermal_level);
	pdpm->is_temp_out_fc2_range = pd_disable_cp_by_jeita_status(pdpm);

	if (pdpm->pd_active == POWER_SUPPLY_PPS_NON_VERIFIED &&
		pdpm->cp.ibat_curr > MAX_UNSUPPORT_PPS_CURRENT_MA) {
		pdpm->unsupport_pps_ta_check_count++;
		if (pdpm->unsupport_pps_ta_check_count > 3) {
			unsupport_pps_status = true;
			pr_info("unsupport_pps_status:%d\n", unsupport_pps_status);
		}
	} else {
		pdpm->unsupport_pps_ta_check_count = 0;
	}
	pd_log_count_poussin++;
	if (pd_log_count_poussin >= 15) {
		pd_log_count_poussin = 0;
		pr_debug("michglog:batt[%d-%d-%d-%d-%d-%d],bus[%d-%d-%d-%d-%d-%d-%d],chgmode[%d-%d-%d-%d-%d-%d],therm[%d-%d-%d-%d-%d], \
reg[%d-%d-%d-%d-%d],step[%d-%d-%d-%d-%d-%d-%d-%d],pmconfig[%d-%d-%d,%d-%d-%d-%d][%d-%d-%d-%d-%d]\n",
		pdpm->cp.batt_pres, pdpm->cp.bms_batt_temp, capacity, pdpm->cp.bms_vbat_mv, pdpm->cp.vbat_volt, pdpm->cp.ibat_curr,
		pdpm->cp.vbus_pres, pdpm->request_voltage, pdpm->cp.vbus_volt, ibus_total, pdpm->cp.ibus_curr, pdpm->cp_sec.ibus_curr, ibus_limit, \
		is_fastcharge_mode, pdpm->cp.charge_enabled, pdpm->cp_sec.charge_enabled, effective_fcc_taper,pdpm->cp.sc8551_charge_mode,pdpm->enable_bypass,  \
		thermal_level, pdpm->cp.bus_temp, pdpm->cp.die_temp, pdpm->is_temp_out_fc2_range, pdpm->cp.bat_therm_fault, \
		pdpm->cp.vbat_reg, step_bat_reg, step_ibus, pdpm->cp.ibat_reg, step_ibat, \
		sw_ctrl_steps, hw_ctrl_steps, pdpm->use_qcom_gauge, pdpm->cell_vol_max_threshold_mv, \
		pdpm->over_cell_vol_max_count, pdpm->sw.night_charging, pdpm->cp.bms_chip_ok, pdpm->cp.bq_input_suspend,\
		pm_config.bus_curr_lp_lmt, pm_config.bat_curr_lp_lmt, pm_config.bus_curr_compensate, \
		pm_config.min_adapter_curr_required, pm_config.fc2_taper_current, pm_config.fc2_steps, pm_config.fc2_disable_sw, \
		pm_config.bus_volt_lp_lmt, pm_config.bat_volt_lp_lmt, pm_config.min_adapter_volt_required, \
		pm_config.min_vbat_for_cp, pm_config.cp_sec_enable);
	}

	usbpd_pm_check_night_charging_enabled(pdpm);

	if (pdpm->cp.bat_therm_fault) { /* battery overheat, stop charge*/
		pr_debug("bat_therm_fault:%d\n", pdpm->cp.bat_therm_fault);
		return PM_ALGO_RET_THERM_FAULT;
	} else if (!pdpm->cp.charge_enabled ||
		(pm_config.cp_sec_enable && !pdpm->cp_sec.charge_enabled && !pdpm->no_need_en_slave_bq)) {
		pr_debug("cp.charge_enabled:%d, cp_sec.charge_enabled:%d\n",
			pdpm->cp.charge_enabled, pdpm->cp_sec.charge_enabled);
		return PM_ALGO_RET_CHG_DISABLED;
	} else if (thermal_level >= pdpm->therm_level_threshold || pdpm->is_temp_out_fc2_range) {
		pr_debug("thermal level too high:%d\n", thermal_level);
		return PM_ALGO_RET_CHG_DISABLED;
	} else if (pdpm->cp.bat_ocp_fault || pdpm->cp.bus_ocp_fault
			|| pdpm->cp.bat_ovp_fault || pdpm->cp.bus_ovp_fault || pdpm->bms_i2c_error_count) {
		pr_debug("bat_ocp_fault:%d, bus_ocp_fault:%d, bat_ovp_fault:%d, bus_ovp_fault:%d,bms_i2c_error_count:%d\n",
				pdpm->cp.bat_ocp_fault, pdpm->cp.bus_ocp_fault,
				pdpm->cp.bat_ovp_fault, pdpm->cp.bus_ovp_fault,pdpm->bms_i2c_error_count);
		return PM_ALGO_RET_OTHER_FAULT; /* go to switch, and try to ramp up*/
	} else if (pm_config.cp_sec_enable && pdpm->no_need_en_slave_bq
			&& pdpm->cp.charge_enabled && pdpm->cp.ibus_curr < CRITICAL_LOW_IBUS_THR) {
		if (pdpm->master_ibus_below_critical_low_count++ >= 5) {
			pr_debug("master ibus below critical low but still enabled\n");
			pdpm->master_ibus_below_critical_low_count = 0;
			return PM_ALGO_RET_CHG_DISABLED;
		}
	} else if (unsupport_pps_status) {
		pr_debug("unsupport pps charger.\n");
		return PM_ALGO_RET_UNSUPPORT_PPSTA;
	} else if (pdpm->sw.night_charging) {
		pr_debug("night charging enabled[%d]\n", pdpm->sw.night_charging);
		return PM_ALGO_RET_NIGHT_CHARGING;
	} else if (pm_config.cp_sec_enable) {
		pdpm->master_ibus_below_critical_low_count = 0;
	}

	if (pdpm->cp.bms_vbat_mv > pm_config.bat_volt_lp_lmt - TAPER_VOL_HYS
			&& pdpm->cp.ibat_curr < pm_config.fc2_taper_current) {
			if (fc2_taper_timer++ > TAPER_TIMEOUT) {
				pr_debug("charge pump taper charging done\n");
				fc2_taper_timer = 0;
				return PM_ALGO_RET_TAPER_DONE;
			}
	} else {
		fc2_taper_timer = 0;
	}

	/* sc8551 mode switch*/
	if(pdpm->enable_bypass == 1){
		chg_mode = usbpd_pm_update_cp_charge_mode(pdpm);
		if(pdpm->cp.sc8551_charge_mode != chg_mode && chg_mode != SC8551_CHARGE_MODE_STANDBY) {
			if(chg_mode == SC8551_CHARGE_MODE_DIV2 || pdpm->cp_model == CHARGEPUMP_SC8551)
				usbpd_pm_switch_cp_charge_mode(pdpm,chg_mode);
			pr_info("charge mode changed, re-run statemachine\n");
			return PM_ALGO_RET_CHG_DISABLED;
		}
	}

	/*if (usbpd_pm_switch_cp_charge_mode(pdpm) > 0) {
		pr_debug("charge mode changed, re-run statemachine\n");
		return PM_ALGO_RET_CHG_DISABLED;
	}*/
	/*TODO: customer can add hook here to check system level
	 * thermal mitigation*/

	steps = min(sw_ctrl_steps, hw_ctrl_steps);

	if ((curr_ibus_limit < 3100) && (steps > 0)) {
		pdpm->request_voltage += steps * 5;
	} else {
		pdpm->request_voltage += steps * STEP_MV;
	}

	
	if (pdpm->cp.sc8551_charge_mode != SC8551_CHARGE_MODE_BYPASS) {
		if(step_ibat > 0){
			ibus_upgrade += 50;
			curr_ibus_limit += ibus_upgrade;
		}else if(step_ibat < 0){
			ibus_upgrade -= 50;
			curr_ibus_limit += ibus_upgrade;
		}else
			curr_ibus_limit = last_curr_ibus_limit;


		if(curr_ibus_limit > pdpm->apdo_max_curr) {
			curr_ibus_limit = pdpm->apdo_max_curr;
		}
		if(ibus_upgrade > 10000)
			ibus_upgrade = 10000;

		last_curr_ibus_limit = curr_ibus_limit;
		pdpm->request_current = min(pdpm->apdo_max_curr, curr_ibus_limit);
	}else if (pdpm->cp.sc8551_charge_mode == SC8551_CHARGE_MODE_BYPASS){
		if (step_ibat > 0){
			ibus_upgrade_bypass += 50;
			curr_ibus_limit += ibus_upgrade_bypass;
		} else if (step_ibat < 0){
			ibus_upgrade_bypass -= 50;
			curr_ibus_limit += ibus_upgrade_bypass;
		} else
			curr_ibus_limit = last_curr_ibus_limit_bypass;

		if (curr_ibus_limit > pdpm->apdo_max_curr) {
			curr_ibus_limit = pdpm->apdo_max_curr;
		}
		if (ibus_upgrade_bypass > 10000)
			ibus_upgrade_bypass = 10000;

		last_curr_ibus_limit_bypass = curr_ibus_limit;

		pdpm->request_current = min(pdpm->apdo_max_curr, curr_ibus_limit);
		pdpm->request_voltage = min(6000,pdpm->request_voltage);
	}

	/*if (pdpm->apdo_max_volt == PPS_VOL_MAX)
		pdpm->apdo_max_volt = pdpm->apdo_max_volt - PPS_VOL_HYS;*/

	if (pdpm->request_voltage > pdpm->apdo_max_volt)
		pdpm->request_voltage = pdpm->apdo_max_volt;

	if(steps < 0) {
		pr_debug("steps[%d-%d-%d-%d-%d-%d-%d],request[%d-%d-%d-%d]ibus[%d-%d-%d-%d]cp[%d-%d-%d-%d-%d]\n",
		steps, sw_ctrl_steps, hw_ctrl_steps, step_ibus, step_ibat, step_vbat, step_bat_reg, \
		pdpm->request_voltage, pdpm->cp.vbus_volt, pdpm->request_current, ibus_total, \
		ibus_limit, curr_ibus_limit, pdpm->cp.ibus_curr, pdpm->cp_sec.ibus_curr, \
		pdpm->cp.vbat_volt, pdpm->cp.bms_vbat_mv, pdpm->cp.ibat_curr, pdpm->cp.vbat_reg, pdpm->cp.ibat_reg);
	}
	/*if (pdpm->adapter_voltage > 0
			&& pdpm->request_voltage > pdpm->adapter_voltage + 500)
		pdpm->request_voltage = pdpm->adapter_voltage + 500; */

	return PM_ALGO_RET_OK;
}

static const unsigned char *pm_str[] = {
	"PD_PM_STATE_ENTRY",
	"PD_PM_STATE_FC2_ENTRY",
	"PD_PM_STATE_FC2_ENTRY_1",
	"PD_PM_STATE_FC2_ENTRY_2",
	"PD_PM_STATE_FC2_ENTRY_3",
	"PD_PM_STATE_FC2_TUNE",
	"PD_PM_STATE_FC2_EXIT",
};

static void usbpd_pm_move_state(struct usbpd_pm *pdpm, enum pm_state state)
{
#if 1
	pr_debug("state change:%s -> %s\n",
		pm_str[pdpm->state], pm_str[state]);
#endif
	pdpm->state = state;
}

static void usbpd_pm_check_charge_pump_model_name(struct usbpd_pm *pdpm)
{
	union power_supply_propval val = {0,};
	int rc;

	usbpd_check_cp_psy(pdpm);
	rc = power_supply_get_property(pdpm->cp_psy,POWER_SUPPLY_PROP_MODEL_NAME,&val);
	if (!rc) {
		if(!strncmp("ln8000-",val.strval,7))
			pdpm->cp_model = CHARGEPUMP_LN8000;
		else if(!strncmp("bq2597x",val.strval,7))
			pdpm->cp_model = CHARGEPUMP_SC8551;
		else
			pdpm->cp_model = CHARGEPUMP_UNKNOWN;
		//pr_debug("chargepump model is  %d\n",pdpm->cp_model);
	}
}

static int usbpd_pm_sm(struct usbpd_pm *pdpm)
{
	int ret;
	union power_supply_propval val = {0,};
	int rc = 0;
	static int tune_vbus_retry;
	static bool stop_sw;
	static bool recover;
	int effective_fcc_val = 0;
	int thermal_level = 0, capacity;
	static int curr_fcc_lmt, curr_ibus_lmt,retry_count;

	if(pdpm->usb_psy){
		rc = power_supply_get_property(pdpm->usb_psy,POWER_SUPPLY_PROP_ENABLE_BYPASS_MODE,&val);
		if(rc < 0)
			pr_debug("can not get enable_bypass with rc:%d\n", rc);
		pdpm->enable_bypass = val.intval;
	}
	usbpd_pm_check_charge_pump_model_name(pdpm);

	switch (pdpm->state) {
	case PD_PM_STATE_ENTRY:
		stop_sw = false;
		recover = false;
		chg_mode = 0;
		pdpm->cp.sc8551_charge_mode = usbpd_pm_check_cp_charge_mode(pdpm);
		usbpd_pm_check_night_charging_enabled(pdpm);
		/* update new fcc from bms charge current */
		usbpd_set_new_fcc_voter(pdpm);
		pd_get_batt_current_thermal_level(pdpm, &thermal_level);
		pdpm->is_temp_out_fc2_range = pd_disable_cp_by_jeita_status(pdpm);
		usbpd_pm_check_sec_batt_present(pdpm);
		pr_debug("is_temp_out_fc2_range:%d\n", pdpm->is_temp_out_fc2_range);
		pd_get_batt_capacity(pdpm, &capacity);
		usbpd_pm_check_cp_bus_ovp(pdpm,1);
		if(pdpm->enable_bypass == 1)
			chg_mode = usbpd_pm_update_cp_charge_mode(pdpm);
		pr_debug("PD_PM_STATE_ENTRY chgmode: %d\n", chg_mode);
		effective_fcc_val = usbpd_get_effective_fcc_val(pdpm);
		if (effective_fcc_val > 0) {
			curr_fcc_lmt = min(pm_config.bat_curr_lp_lmt, effective_fcc_val);
			if (chg_mode == SC8551_CHARGE_MODE_BYPASS)
				curr_ibus_lmt = curr_fcc_lmt + 100;
			else
				curr_ibus_lmt = curr_fcc_lmt >> 1;
		}

		if (pdpm->cp.vbat_volt < pm_config.min_vbat_for_cp) {
			pr_debug("batt_volt %d, waiting...\n", pdpm->cp.vbat_volt);
		} else if ((pdpm->cp.vbat_volt > pm_config.bat_volt_lp_lmt - VBAT_HIGH_FOR_FC_HYS_MV
			&& !pdpm->is_temp_out_fc2_range) || capacity >= CAPACITY_TOO_HIGH_THR) {
			pr_debug("batt_volt %d is too high for cp,\
					charging with switch charger\n",
					pdpm->cp.vbat_volt);
			usbpd_pm_move_state(pdpm, PD_PM_STATE_FC2_EXIT);
		} else if (!pd_get_bms_digest_verified(pdpm)) {
			pr_debug("bms digest is not verified, waiting...\n");
		} else if (thermal_level >= pdpm->therm_level_threshold || pdpm->is_temp_out_fc2_range) {
			pr_debug("thermal level is too high, waiting...\n");
		} else if (pdpm->sw.night_charging) {
			pr_debug("night charging is open, waiting...\n");
		} else if (effective_fcc_val < BYPASS_FCC_ENTER_MIN_THRES && chg_mode != SC8551_CHARGE_MODE_BYPASS) {
			pr_debug("effective fcc is below start dc threshold, waiting...\n");
		} else if (pdpm->cp_sec_enable && !pdpm->cp_sec.batt_connecter_present) {
			pr_debug("sec batt connecter miss! charging with switch charger\n");
			usbpd_pm_move_state(pdpm, PD_PM_STATE_FC2_EXIT);
		} else {
			pdpm->fc2_exit_flag = false;

			pr_debug("batt_volt-%d is ok, start flash charging\n",
					pdpm->cp.vbat_volt);
			usbpd_pm_move_state(pdpm, PD_PM_STATE_FC2_ENTRY);
		}
		break;

	case PD_PM_STATE_FC2_ENTRY:
		if (pm_config.fc2_disable_sw) {
			if (pdpm->sw.charge_enabled) {
				usbpd_pm_enable_sw(pdpm, false);
				usbpd_pm_check_sw_enabled(pdpm);
			}
			if (!pdpm->sw.charge_enabled)
				usbpd_pm_move_state(pdpm, PD_PM_STATE_FC2_ENTRY_1);
		} else {
			usbpd_pm_move_state(pdpm, PD_PM_STATE_FC2_ENTRY_1);
		}
		break;

	case PD_PM_STATE_FC2_ENTRY_1:
		/* sc8551 mode switch*/
		if(pdpm->enable_bypass == 1){
			chg_mode = usbpd_pm_update_cp_charge_mode(pdpm);
			if (pdpm->cp.sc8551_charge_mode != chg_mode) {
				if (pdpm->cp_model == CHARGEPUMP_LN8000)
					pdpm->cp.sc8551_charge_mode = chg_mode;
				else if (pdpm->cp_model == CHARGEPUMP_SC8551)
					usbpd_pm_switch_cp_charge_mode(pdpm,chg_mode);
			pr_info("charge mode changed,begin to switch to %d\n",chg_mode);
			}
		}
		//pr_info("charge mode changed,chg_mode: %d,sc8551_charge_mode:%d\n",chg_mode,pdpm->cp.sc8551_charge_mode);
		if (pdpm->cp.sc8551_charge_mode == 1) {
			pdpm->request_voltage = pdpm->cp.vbat_volt + 300;
			pr_debug("%s ENTRY_1  request_voltage [%d-%d]\n", __func__,pdpm->request_voltage, pdpm->cp.vbat_volt);
			pdpm->request_current = min(pdpm->apdo_max_curr, curr_ibus_lmt);
			pr_debug("%s ENTRY_1 request_current [%d-%d-%d]\n", __func__,pdpm->apdo_max_curr, curr_ibus_lmt, MAX_BYPASS_CURRENT_MA);
		} else {
			if(pdpm->cp.sc8551_charge_mode == -1)
				pdpm->cp.sc8551_charge_mode = 0;

			pdpm->request_voltage = pdpm->cp.vbat_volt * 2 + BUS_VOLT_INIT_UP;
			//pdpm->request_current = min(pdpm->apdo_max_curr, pm_config.bus_curr_lp_lmt);
			pdpm->request_current = min(pdpm->apdo_max_curr, curr_ibus_lmt);
		}

		usbpd_select_pdo(pdpm->pd, pdpm->apdo_selected_pdo,
				pdpm->request_voltage * 1000, pdpm->request_current * 1000);
		pr_debug("request_voltage:%d, request_current:%d\n",
				pdpm->request_voltage, pdpm->request_current);

		usbpd_pm_move_state(pdpm, PD_PM_STATE_FC2_ENTRY_2);

		tune_vbus_retry = 0;

		if(pdpm->sw_psy){
			val.intval = 0;
			rc = power_supply_set_property(pdpm->sw_psy,POWER_SUPPLY_PROP_CP_TO_SW_STATUS,&val);
			if(rc < 0)
				pr_debug("can not set cp to sw status with rc:%d\n", rc);
		}
		break;

	case PD_PM_STATE_FC2_ENTRY_2:
		usbpd_pm_get_vbus_error(pdpm);
		pdpm->request_current = min(pdpm->apdo_max_curr, curr_ibus_lmt);
		if (((pdpm->cp.vbus_volt < (pdpm->cp.vbat_volt * 2 + BUS_VOLT_INIT_UP - 50))
			&& (pdpm->cp.sc8551_charge_mode != SC8551_CHARGE_MODE_BYPASS))
			|| ((pdpm->cp.vbus_volt < pdpm->cp.vbat_volt + 250)
					&& (pdpm->cp.sc8551_charge_mode == SC8551_CHARGE_MODE_BYPASS))) {
			tune_vbus_retry++;
			if (pdpm->cp.vbus_error_status == CP_VBUS_ERROR_HIGH)
				pdpm->request_voltage -= STEP_MV;
			else if (pdpm->cp.vbus_error_status == CP_VBUS_ERROR_LOW || pdpm->cp_model == CHARGEPUMP_LN8000)
				pdpm->request_voltage += STEP_MV_INIT_VBUS;
			pr_debug("%s [ENTRY_2low] %d -%d-%d-%d-%d\n", __func__,tune_vbus_retry,pdpm->request_voltage,pdpm->request_current,pdpm->cp.vbus_volt,pdpm->cp.vbat_volt);
			usbpd_select_pdo(pdpm->pd, pdpm->apdo_selected_pdo,
						pdpm->request_voltage * 1000,
						pdpm->request_current * 1000);
		} else if ((pdpm->cp.vbus_volt > (pdpm->cp.vbat_volt * 2 + BUS_VOLT_INIT_UP + 200))
			&& (pdpm->cp.sc8551_charge_mode != SC8551_CHARGE_MODE_BYPASS)) {
			tune_vbus_retry++;
			pdpm->request_voltage -= STEP_MV_INIT_VBUS;
			pr_debug("%s [ENTRY_2high] %d-%d-%d-%d\n", __func__,tune_vbus_retry,pdpm->request_voltage,pdpm->request_current,pdpm->cp.vbus_volt);
			usbpd_select_pdo(pdpm->pd, pdpm->apdo_selected_pdo,
						pdpm->request_voltage * 1000,
						pdpm->request_current * 1000);
		} else {
			pr_debug("adapter volt tune ok, retry %d times\n", tune_vbus_retry);
			usbpd_pm_move_state(pdpm, PD_PM_STATE_FC2_ENTRY_3);
			break;
		}

		if (tune_vbus_retry > 80) {
			pr_debug("Failed to tune adapter volt into valid range, charge with switching charger\n");
			usbpd_pm_move_state(pdpm, PD_PM_STATE_FC2_EXIT);
		}
		break;

	case PD_PM_STATE_FC2_ENTRY_3:
/*The charging enable sequence for LN8000 must be execute on master first, and then execute on slave.*/
	//if (pdpm->cp_model == CHARGEPUMP_LN8000 || pdpm->cp_model == CHARGEPUMP_SC8551) {
#if defined(CONFIG_BQ2597X_CHARGE_PUMP_STANDALONE_MUNCH)
		if (!pdpm->cp.charge_enabled) {
			pr_debug("%s ENTRY_3 enable_cp\n", __func__);
			usbpd_pm_enable_cp(pdpm, true);
			msleep(30);
			usbpd_pm_check_cp_enabled(pdpm);
		}

		if (pm_config.cp_sec_enable && !pdpm->cp_sec.charge_enabled) {
			pd_get_batt_current_thermal_level(pdpm, &thermal_level);
			pd_get_batt_capacity(pdpm, &capacity);
			if (pdpm->enable_bypass == 1 || (thermal_level < MAX_THERMAL_LEVEL_FOR_DUAL_BQ
					&& capacity < CAPACITY_HIGH_THR)) {
				pr_debug("%s ENTRY_3 enable_cp_sec\n", __func__);
				usbpd_pm_enable_cp_sec(pdpm, true);
				msleep(30);
				usbpd_pm_check_cp_sec_enabled(pdpm);
			} else if(pdpm->enable_bypass == 0)
				pdpm->no_need_en_slave_bq = true;
		}
#else
		if (pm_config.cp_sec_enable && !pdpm->cp_sec.charge_enabled) {
			pd_get_batt_current_thermal_level(pdpm, &thermal_level);
			pd_get_batt_capacity(pdpm, &capacity);
			if (pdpm->enable_bypass == 1 || (thermal_level < MAX_THERMAL_LEVEL_FOR_DUAL_BQ
					&& capacity < CAPACITY_HIGH_THR))
				usbpd_pm_enable_cp_sec(pdpm, true);
			else if(pdpm->enable_bypass == 0)
				pdpm->no_need_en_slave_bq = true;
		}

		if (!pdpm->cp.charge_enabled) {
			usbpd_pm_enable_cp(pdpm, true);
		}
#endif
	//}

		if (pdpm->cp_model == CHARGEPUMP_LN8000 && chg_mode == SC8551_CHARGE_MODE_BYPASS) {
			pr_info("PD_PM_STATE_FC2_ENTRY_3,chg_mode: %d,cp_model:%d\n",chg_mode,pdpm->cp_model);
			usbpd_pm_switch_cp_charge_mode(pdpm,chg_mode);
		}

		usbpd_pm_check_cp_sec_enabled(pdpm);
		usbpd_pm_check_cp_enabled(pdpm);

		pr_debug("%s PD_PM_STATE_FC2_ENTRY_3 :%d-%d-%d-%d\n", __func__, pdpm->cp_sec.charge_enabled,pm_config.cp_sec_enable,pdpm->no_need_en_slave_bq,pdpm->cp.charge_enabled);
		if ((!pdpm->cp_sec.charge_enabled && pm_config.cp_sec_enable
				&& !pdpm->no_need_en_slave_bq) || !pdpm->cp.charge_enabled)
			usbpd_pm_move_state(pdpm, PD_PM_STATE_FC2_ENTRY_3);

		if (pdpm->cp.charge_enabled) {
			if ((pm_config.cp_sec_enable && pdpm->cp_sec.charge_enabled
					&& !pdpm->no_need_en_slave_bq)
					|| pdpm->no_need_en_slave_bq
					|| !pm_config.cp_sec_enable) {
				usbpd_pm_move_state(pdpm, PD_PM_STATE_FC2_TUNE);
				ibus_lmt_change_timer = 0;
				fc2_taper_timer = 0;
			}
		}
		break;

	case PD_PM_STATE_FC2_TUNE:
#if 0
		if (pdpm->cp.vbat_volt < pm_config.min_vbat_for_cp - 400) {
			usbpd_pm_move_state(PD_PM_STATE_SW_ENTRY);
			break;
		}
#endif
		usbpd_update_pps_status(pdpm);

		ret = usbpd_pm_fc2_charge_algo(pdpm);
		if (ret == PM_ALGO_RET_THERM_FAULT) {
			pr_info("Move to stop charging:%d\n", ret);
			stop_sw = true;
			usbpd_pm_move_state(pdpm, PD_PM_STATE_FC2_EXIT);
			break;
		} else if (usbpd_get_current_state(pdpm->pd) == 1) {
				pr_debug("adapter receive softreset\n");
				//ln8000
				pr_debug("close dual ln8000\n");
				usbpd_pm_enable_cp_sec(pdpm, false);
				usbpd_pm_enable_cp(pdpm, false);
				//ln8000
				usbpd_pm_evaluate_src_caps(pdpm);
				usbpd_pm_move_state(pdpm, PD_PM_STATE_FC2_ENTRY_1);
				break;
		} else if (ret == PM_ALGO_RET_OTHER_FAULT
				|| ret == PM_ALGO_RET_TAPER_DONE
				|| ret == PM_ALGO_RET_UNSUPPORT_PPSTA) {
			if(pdpm->cp.sc8551_charge_mode == 1){
				usbpd_pm_switch_cp_charge_mode(pdpm,0);
				msleep(50);
			}
			ibus_upgrade = 0;
			ibus_upgrade_bypass = 0;
			last_curr_ibus_limit = 0;
			last_curr_ibus_limit_bypass = 0;
			pr_debug("Move to 2:1 before switch charging:%d\n", ret);
			usbpd_pm_move_state(pdpm, PD_PM_STATE_FC2_EXIT);
			break;
		} else if (ret == PM_ALGO_RET_CHG_DISABLED) {
			ibus_upgrade = 0;
			ibus_upgrade_bypass = 0;
			last_curr_ibus_limit = 0;
			last_curr_ibus_limit_bypass = 0;
			if (pdpm->cp.sc8551_charge_mode == 1) {
				usbpd_pm_switch_cp_charge_mode(pdpm,0);
				msleep(50);
			}
			pr_debug("Move to switch charging, will try to recover flash charging:%d\n",
					ret);
			recover = true;
			usbpd_pm_move_state(pdpm, PD_PM_STATE_FC2_EXIT);
			break;
		} else if (ret == PM_ALGO_RET_NIGHT_CHARGING) {
			recover = true;
			pr_debug("Night Charging Feature is running %d\n", ret);
			usbpd_pm_move_state(pdpm, PD_PM_STATE_FC2_EXIT);
			break;
		} else if (!pd_get_bms_chip_ok(pdpm) && pdpm->chg_enable_k81) {
			if (pdpm->chip_ok_count++ > 2) {
				pr_debug("bms chip ok is not ready, exit\n");
				pdpm->chip_ok_count = 0;
				usbpd_pm_move_state(pdpm, PD_PM_STATE_FC2_EXIT);
			}
		} else {
			ret = usbpd_select_pdo(pdpm->pd, pdpm->apdo_selected_pdo,
						pdpm->request_voltage * 1000,
						pdpm->request_current * 1000);
			if (ret == -EBUSY && retry_count <= 3){
				retry_count++;
			} else if(retry_count > 3){		
				pr_debug("CC conminucation failed, will try to recover flash charging:%d\n",
						ret);
				retry_count = 0;
				recover = true;
				usbpd_pm_move_state(pdpm, PD_PM_STATE_FC2_EXIT);
			} else {
				retry_count = 0;
			}

			/*pr_debug("request_voltage:%d, request_current:%d\n",
					pdpm->request_voltage, pdpm->request_current);*/
		}
		/*stop second charge pump if either of ibus is lower than 400ma during CV*/
		if (pm_config.cp_sec_enable && pdpm->cp_sec.charge_enabled && !pdpm->no_need_en_slave_bq
				&& pdpm->cp.vbat_volt > pm_config.bat_volt_lp_lmt - TAPER_WITH_IBUS_HYS
				&& !pdpm->cp.sc8551_charge_mode
				&& (pdpm->cp.ibus_curr < TAPER_IBUS_THR || pdpm->cp_sec.ibus_curr < TAPER_IBUS_THR)) {
			pr_debug("second cp is disabled due to ibus < 450mA\n");
			usbpd_pm_enable_cp_sec(pdpm, false);
			usbpd_pm_check_cp_sec_enabled(pdpm);
		}
		break;

	case PD_PM_STATE_FC2_EXIT:
		/* select default 5V*/
		if (!pdpm->fc2_exit_flag) {
			if (pdpm->cp.vbus_volt > 6000) {
				pdpm->fc2_exit_flag = true;
				schedule_delayed_work(&pdpm->fc2_exit_work, 0);
			} else {
				usbpd_select_pdo(pdpm->pd, 1, 0, 0);
			}
		}
		pdpm->no_need_en_slave_bq = false;
		pdpm->master_ibus_below_critical_low_count = 0;
		pdpm->chip_ok_count = 0;
		pr_debug("PD_PM_STATE_FC2_EXIT begin 1\n");

		if (chg_mode != SC8551_CHARGE_MODE_BYPASS)
			if (pdpm->fcc_votable) {
				vote(pdpm->fcc_votable, BQ_TAPER_FCC_VOTER,
						false, 0);
				vote(pdpm->fcc_votable, BQ_TAPER_CELL_HGIH_FCC_VOTER,
						false, 0);
			}

		pr_debug("PD_PM_STATE_FC2_EXIT begin 2 stop_sw:%d,sw.charge_enabled:%d\n",stop_sw,pdpm->sw.charge_enabled);
		if(pdpm->sw_psy){
			val.intval = 1;
			rc = power_supply_set_property(pdpm->sw_psy,POWER_SUPPLY_PROP_CP_TO_SW_STATUS,&val);
			if(rc < 0)
				pr_debug("can not set cp to sw status with rc:%d\n", rc);
		}
		if (stop_sw && pdpm->sw.charge_enabled)
			usbpd_pm_enable_sw(pdpm, false);
		else if (!stop_sw && !pdpm->sw.charge_enabled)
			usbpd_pm_enable_sw(pdpm, true);

		msleep(50);
		usbpd_pm_check_sw_enabled(pdpm);


#if defined(CONFIG_BQ2597X_CHARGE_PUMP_STANDALONE_MUNCH)
		if (pm_config.cp_sec_enable && pdpm->cp_sec.charge_enabled) {
			pr_debug("%s PD_PM_STATE_FC2_EXIT cp_sec close\n", __func__);
			usbpd_pm_enable_cp_sec(pdpm, false);
			usbpd_pm_check_cp_sec_enabled(pdpm);
		}

		if (pdpm->cp.charge_enabled) {
			pr_debug("%s disable_cp\n", __func__);
			usbpd_pm_enable_cp(pdpm, false);
			usbpd_pm_check_cp_enabled(pdpm);
		}
#else
		if (pdpm->cp.charge_enabled) {
			usbpd_pm_enable_cp(pdpm, false);
			usbpd_pm_check_cp_enabled(pdpm);
		}

		if (pm_config.cp_sec_enable && pdpm->cp_sec.charge_enabled) {
			pr_debug("%s PD_PM_STATE_FC2_EXIT cp_secclose\n", __func__);
			usbpd_pm_enable_cp_sec(pdpm, false);
			usbpd_pm_check_cp_sec_enabled(pdpm);
		}
#endif

		if (recover)
			usbpd_pm_move_state(pdpm, PD_PM_STATE_ENTRY);
		else
			rc = 1;

		break;
	default:
		usbpd_pm_move_state(pdpm, PD_PM_STATE_ENTRY);
		break;
	}

	return rc;
}

static void usbpd_pm_workfunc(struct work_struct *work)
{
	struct usbpd_pm *pdpm = container_of(work, struct usbpd_pm,
					pm_work.work);
	int internal = PM_WORK_RUN_NORMAL_INTERVAL;

	usbpd_pm_update_sw_status(pdpm);
	usbpd_pm_update_cp_status(pdpm);
	usbpd_pm_update_cp_sec_status(pdpm);

	if (!usbpd_pm_sm(pdpm) && pdpm->pd_active) {
		if (pdpm->cp.vbat_volt >= CRITICAL_HIGH_VOL_THR_MV
				&& pm_config.cp_sec_enable)
			internal = PM_WORK_RUN_CRITICAL_INTERVAL;
		else if (pdpm->cp.vbat_volt >= HIGH_VOL_THR_MV)
			internal = PM_WORK_RUN_QUICK_INTERVAL;
		else
			internal = PM_WORK_RUN_NORMAL_INTERVAL;
		schedule_delayed_work(&pdpm->pm_work,
				msecs_to_jiffies(internal));
	}
}

static void usbpd_pm_disconnect(struct usbpd_pm *pdpm)
{
	union power_supply_propval pval = {0, };

	if (pdpm->cp.sc8551_charge_mode == 1) {
		usbpd_pm_switch_cp_charge_mode(pdpm,0);
		msleep(50);
	}

	cancel_delayed_work_sync(&pdpm->pm_work);


#if defined(CONFIG_BQ2597X_CHARGE_PUMP_STANDALONE_MUNCH)
	pr_debug("%s cp close\n", __func__);
	usbpd_pm_enable_cp_sec(pdpm, false);
	usbpd_pm_enable_cp(pdpm, false);
#else
	usbpd_pm_enable_cp(pdpm, false);
	usbpd_pm_enable_cp_sec(pdpm, false);
#endif

	if (pdpm->fcc_votable) {
		vote(pdpm->fcc_votable, BQ_TAPER_FCC_VOTER,
				false, 0);
		vote(pdpm->fcc_votable, BQ_TAPER_CELL_HGIH_FCC_VOTER,
				false, 0);
	}
	pdpm->pps_supported = false;
	pdpm->jeita_triggered = false;
	pdpm->is_temp_out_fc2_range = false;
	pdpm->no_need_en_slave_bq = false;
	pdpm->fc2_exit_flag = false;
	pdpm->apdo_selected_pdo = 0;
	pdpm->over_cell_vol_high_count = 0;
	pdpm->slave_bq_disabled_check_count = 0;
	pdpm->master_ibus_below_critical_low_count = 0;
	pdpm->chip_ok_count = 0;
	memset(&pdpm->pdo, 0, sizeof(pdpm->pdo));
	pm_config.bat_curr_lp_lmt = pdpm->bat_curr_max;
	pm_config.bat_volt_lp_lmt = pdpm->bat_volt_max;
	pm_config.fc2_taper_current = TAPER_DONE_NORMAL_MA;

	pval.intval = 0;
	power_supply_set_property(pdpm->usb_psy,
			POWER_SUPPLY_PROP_APDO_MAX, &pval);
	usbpd_pm_move_state(pdpm, PD_PM_STATE_ENTRY);
}

static void usbpd_pd_contact(struct usbpd_pm *pdpm, int status)
{
	pdpm->pd_active = status;

	if (status) {
		usbpd_pm_evaluate_src_caps(pdpm);
		if (pdpm->pps_supported)
			schedule_delayed_work(&pdpm->pm_work, 0);
	} else {
		usbpd_pm_disconnect(pdpm);
	}
}

static void usbpd_pps_non_verified_contact(struct usbpd_pm *pdpm, int status)
{
	pdpm->pd_active = status;

	if (status) {
		usbpd_pm_evaluate_src_caps(pdpm);
		if (pdpm->pps_supported)
			schedule_delayed_work(&pdpm->pm_work, 5*HZ);
	} else {
		usbpd_pm_disconnect(pdpm);
		if (pdpm->fcc_votable)
			vote(pdpm->fcc_votable, NON_PPS_PD_FCC_VOTER, false, 0);
	}
}

static void cp_psy_change_work(struct work_struct *work)
{
	struct usbpd_pm *pdpm = container_of(work, struct usbpd_pm,
					cp_psy_change_work);
#if 0
	union power_supply_propval val = {0,};
	bool ac_pres = pdpm->cp.vbus_pres;
	int ret;

	if (!pdpm->cp_psy)
		return;

	ret = power_supply_get_property(pdpm->cp_psy, POWER_SUPPLY_PROP_TI_VBUS_PRESENT, &val);
	if (!ret)
		pdpm->cp.vbus_pres = val.intval;

	if (!ac_pres && pdpm->cp.vbus_pres)
		schedule_delayed_work(&pdpm->pm_work, 0);
#endif
	pdpm->psy_change_running = false;
}

static void usb_psy_change_work(struct work_struct *work)
{
	struct usbpd_pm *pdpm = container_of(work, struct usbpd_pm,
					usb_psy_change_work);
	union power_supply_propval val = {0,};
	union power_supply_propval pd_auth_val = {0,};
	int ret = 0;

	ret = power_supply_get_property(pdpm->usb_psy,
			POWER_SUPPLY_PROP_TYPEC_POWER_ROLE, &val);
	if (ret) {
		pr_debug("Failed to read typec power role\n");
		goto out;
	}

	if (val.intval != POWER_SUPPLY_TYPEC_PR_SINK &&
			val.intval != POWER_SUPPLY_TYPEC_PR_DUAL)
		goto out;

	ret = power_supply_get_property(pdpm->usb_psy,
			POWER_SUPPLY_PROP_PD_ACTIVE, &val);
	if (ret) {
		pr_debug("Failed to get usb pd active state\n");
		goto out;
	}

	ret = power_supply_get_property(pdpm->usb_psy,
				POWER_SUPPLY_PROP_PD_AUTHENTICATION, &pd_auth_val);
	if (ret) {
		pr_debug("Failed to read typec power role\n");
		goto out;
	}

	if ((pdpm->pd_active < POWER_SUPPLY_PPS_VERIFIED) && (pd_auth_val.intval == 1)
			&& (val.intval == POWER_SUPPLY_PD_PPS_ACTIVE)) {
		msleep(30);
		usbpd_pd_contact(pdpm, POWER_SUPPLY_PPS_VERIFIED);
		if (pdpm->fcc_votable)
			vote(pdpm->fcc_votable, NON_PPS_PD_FCC_VOTER, false, 0);
	} else if (!pdpm->pd_active
			&& (val.intval == POWER_SUPPLY_PD_PPS_ACTIVE)) {
		usbpd_pps_non_verified_contact(pdpm, POWER_SUPPLY_PPS_NON_VERIFIED);
		if (pdpm->fcc_votable)
			vote(pdpm->fcc_votable, NON_PPS_PD_FCC_VOTER, false, 0);
	} else if (pdpm->pd_active && !val.intval) {
		usbpd_pd_contact(pdpm, POWER_SUPPLY_PPS_INACTIVE);
		if (pdpm->fcc_votable)
			vote(pdpm->fcc_votable, NON_PPS_PD_FCC_VOTER, false, 0);
	} else if (!pdpm->pd_active && val.intval == POWER_SUPPLY_PD_ACTIVE) {
		if (pdpm->fcc_votable)
			vote(pdpm->fcc_votable, NON_PPS_PD_FCC_VOTER, true, NON_PPS_PD_FCC_LIMIT);
	}
out:
	pdpm->psy_change_running = false;
}

static int usbpd_psy_notifier_cb(struct notifier_block *nb,
			unsigned long event, void *data)
{
	struct usbpd_pm *pdpm = container_of(nb, struct usbpd_pm, nb);
	struct power_supply *psy = data;
	unsigned long flags;

	if (event != PSY_EVENT_PROP_CHANGED)
		return NOTIFY_OK;

	usbpd_check_cp_psy(pdpm);
	usbpd_check_usb_psy(pdpm);
	usbpd_check_main_psy(pdpm);

	if (!pdpm->cp_psy || !pdpm->usb_psy)
		return NOTIFY_OK;

	if (psy == pdpm->cp_psy || psy == pdpm->usb_psy) {
		spin_lock_irqsave(&pdpm->psy_change_lock, flags);
		if (!pdpm->psy_change_running) {
			pdpm->psy_change_running = true;
			if (psy == pdpm->cp_psy)
				schedule_work(&pdpm->cp_psy_change_work);
			else
				schedule_work(&pdpm->usb_psy_change_work);
		}
		spin_unlock_irqrestore(&pdpm->psy_change_lock, flags);
	}

	return NOTIFY_OK;
}

static int pd_policy_parse_dt(struct usbpd_pm *pdpm)
{
	struct device_node *node = pdpm->dev->of_node;
	int rc = 0;

	if (!node) {
		pr_debug("device tree node missing\n");
		return -EINVAL;
	}

	rc = of_property_read_u32(node,
			"mi,pd-bat-volt-max", &pdpm->bat_volt_max);
	if (rc < 0)
		pr_debug("pd-bat-volt-max property missing, use default val\n");
	else
		pm_config.bat_volt_lp_lmt = pdpm->bat_volt_max;
	pr_info("pm_config.bat_volt_lp_lmt:%d\n", pm_config.bat_volt_lp_lmt);

	rc = of_property_read_u32(node,
			"mi,pd-bat-curr-max", &pdpm->bat_curr_max);
	if (rc < 0)
		pr_debug("pd-bat-curr-max property missing, use default val\n");
	else
		pm_config.bat_curr_lp_lmt = pdpm->bat_curr_max;
	pr_info("pm_config.bat_curr_lp_lmt:%d\n", pm_config.bat_curr_lp_lmt);

	rc = of_property_read_u32(node,
			"mi,pd-bus-volt-max", &pdpm->bus_volt_max);
	if (rc < 0)
		pr_debug("pd-bus-volt-max property missing, use default val\n");
	else
		pm_config.bus_volt_lp_lmt = pdpm->bus_volt_max;
	pr_info("pm_config.bus_volt_lp_lmt:%d\n", pm_config.bus_volt_lp_lmt);

	rc = of_property_read_u32(node,
			"mi,pd-bus-curr-max", &pdpm->bus_curr_max);
	if (rc < 0)
		pr_debug("pd-bus-curr-max property missing, use default val\n");
	else
		pm_config.bus_curr_lp_lmt = pdpm->bus_curr_max;
	pr_info("pm_config.bus_curr_lp_lmt:%d\n", pm_config.bus_curr_lp_lmt);

	rc = of_property_read_u32(node,
			"mi,step-charge-high-vol-curr-max", &pdpm->step_charge_high_vol_curr_max);

	pr_info("pdpm->step_charge_high_vol_curr_max:%d\n",
				pdpm->step_charge_high_vol_curr_max);

	rc = of_property_read_u32(node,
			"mi,cell-vol-high-threshold-mv", &pdpm->cell_vol_high_threshold_mv);

	pr_info("pdpm->cell_vol_high_threshold_mv:%d\n",
				pdpm->cell_vol_high_threshold_mv);

	rc = of_property_read_u32(node,
			"mi,cell-vol-max-threshold-mv", &pdpm->cell_vol_max_threshold_mv);

	pr_info("pdpm->cell_vol_max_threshold_mv:%d\n",
				pdpm->cell_vol_max_threshold_mv);

	rc = of_property_read_u32(node,
			"mi,pd-non-ffc-bat-volt-max", &pdpm->non_ffc_bat_volt_max);

	pr_info("pdpm->non_ffc_bat_volt_max:%d\n",
				pdpm->non_ffc_bat_volt_max);

	rc = of_property_read_u32(node,
			"mi,pd-bus-curr-compensate", &pdpm->bus_curr_compensate);
	if (rc < 0)
		pr_debug("pd-bus-curr-compensate property missing, use default val\n");
	else
		pm_config.bus_curr_compensate = pdpm->bus_curr_compensate;

	pdpm->therm_level_threshold = MAX_THERMAL_LEVEL;
	rc = of_property_read_u32(node,
			"mi,therm-level-threshold", &pdpm->therm_level_threshold);
	if (rc < 0)
		pr_debug("therm-level-threshold missing, use default val\n");
	pr_debug("therm-level-threshold:%d\n", pdpm->therm_level_threshold);

	pdpm->battery_warm_th = JEITA_WARM_THR;
	rc = of_property_read_u32(node,
			"mi,pd-battery-warm-th", &pdpm->battery_warm_th);
	if (rc < 0)
		pr_debug("pd-battery-warm-th missing, use default val\n");
	pr_debug("pd-battery-warm-th:%d\n", pdpm->battery_warm_th);

	pdpm->cp_sec_enable = of_property_read_bool(node,
				"mi,cp-sec-enable");
	pm_config.cp_sec_enable = pdpm->cp_sec_enable;

	pdpm->use_qcom_gauge = of_property_read_bool(node,
				"mi,use-qcom-gauge");

	pdpm->chg_enable_k11a = of_property_read_bool(node,
				"mi,chg-enable-k11a");

	pdpm->chg_enable_k81 = of_property_read_bool(node,
				"mi,chg-enable-k81");
	return rc;
}

static int usbpd_pm_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;
	struct usbpd_pm *pdpm;

	pr_debug("%s enter\n", __func__);

	pdpm = kzalloc(sizeof(struct usbpd_pm), GFP_KERNEL);
	if (!pdpm)
		return -ENOMEM;

	__pdpm = pdpm;

	pdpm->dev = dev;

	ret = pd_policy_parse_dt(pdpm);
	if (ret < 0) {
		pr_debug("Couldn't parse device tree rc=%d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, pdpm);

	spin_lock_init(&pdpm->psy_change_lock);

	usbpd_check_cp_psy(pdpm);
	usbpd_check_cp_sec_psy(pdpm);
	usbpd_check_usb_psy(pdpm);
	usbpd_check_main_psy(pdpm);

	pdpm->over_cell_vol_high_count = 0;
	pdpm->master_ibus_below_critical_low_count = 0;
	pdpm->chip_ok_count = 0;
	pdpm->cp.sc8551_charge_mode = 0;
	pdpm->enable_bypass = 1;
	INIT_WORK(&pdpm->cp_psy_change_work, cp_psy_change_work);
	INIT_WORK(&pdpm->usb_psy_change_work, usb_psy_change_work);
	INIT_DELAYED_WORK(&pdpm->pm_work, usbpd_pm_workfunc);
	INIT_DELAYED_WORK(&pdpm->fc2_exit_work, usbpd_fc2_exit_work);
	pdpm->nb.notifier_call = usbpd_psy_notifier_cb;
	power_supply_reg_notifier(&pdpm->nb);

	return ret;
}

static int usbpd_pm_remove(struct platform_device *pdev)
{
	power_supply_unreg_notifier(&__pdpm->nb);
	cancel_delayed_work_sync(&__pdpm->pm_work);
	cancel_work_sync(&__pdpm->cp_psy_change_work);
	cancel_work_sync(&__pdpm->usb_psy_change_work);
	cancel_delayed_work_sync(&__pdpm->fc2_exit_work);

	return 0;
}

static const struct of_device_id usbpd_pm_of_match[] = {
	{ .compatible = "xiaomi,usbpd-pm", },
	{},
};

static struct platform_driver usbpd_pm_driver = {
	.driver = {
		.name = "usbpd-pm",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(usbpd_pm_of_match),
	},
	.probe = usbpd_pm_probe,
	.remove = usbpd_pm_remove,
};

static int __init usbpd_pm_init(void)
{
	return platform_driver_register(&usbpd_pm_driver);
}

late_initcall(usbpd_pm_init);

static void __exit usbpd_pm_exit(void)
{
	return platform_driver_unregister(&usbpd_pm_driver);
}
module_exit(usbpd_pm_exit);

MODULE_AUTHOR("Fei Jiang<jiangfei1@xiaomi.com>");
MODULE_DESCRIPTION("Xiaomi usb pd statemachine for bq");
MODULE_LICENSE("GPL");


