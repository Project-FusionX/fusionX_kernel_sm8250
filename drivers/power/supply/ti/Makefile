fpu_cflag := -mgeneral-regs-only

obj-$(CONFIG_BQ2597X_CHARGE_PUMP)	+= bq2597x_charger.o pd_policy_manager.o cp_qc30.o
obj-$(CONFIG_BQ_PUMP_WIRELESS_CHARGE)	+= wireless_bq_dc_charge.o
obj-$(CONFIG_CHARGER_LN8000)	+= ln8000_charger.o pd_policy_manager.o cp_qc30.o
obj-$(CONFIG_CHARGER_LN8000_PSYCHE)    += ln8000_charger_psyche.o pd_policy_manager_psyche.o cp_qc30_psyche.o
obj-$(CONFIG_BQ2597X_CHARGE_PUMP_STANDALONE)	+= bq2597x_charger_poussin.o pd_policy_manager_poussin.o cp_qc30.o
obj-$(CONFIG_BQ2597X_CHARGE_PUMP_STANDALONE_MUNCH)	+= bq2597x_charger_munch.o pd_policy_manager_munch.o cp_qc30_munch.o ln8000_charger_munch.o

CFLAGS_REMOVE_bq2597x_charger.o		:= $(fpu_cflag)
CFLAGS_REMOVE_bq2597x_charger_dagu.o	:= $(fpu_cflag)
CFLAGS_REMOVE_bq2597x_charger_munch.o	:= $(fpu_cflag)
CFLAGS_REMOVE_bq2597x_charger_pipa.o	:= $(fpu_cflag)
