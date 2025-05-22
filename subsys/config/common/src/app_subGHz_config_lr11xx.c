
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

#include <sid_pal_serial_bus_ifc.h>
#include <sid_pal_serial_bus_spi_config.h>
#include <sid_gpio_utils.h>

#include <lr11xx_config.h>
#include <app_subGHz_config.h>

#define REGION_US915
#define RADIO_REGION                                               RADIO_REGION_NA

#define RADIO_LR11XX_MAX_TX_POWER                                  22
#define RADIO_LR11XX_MIN_TX_POWER                                  -9

#define RADIO_MAX_TX_POWER_NA                                      20
#define RADIO_MAX_TX_POWER_EU                                      14
#define RADIO_ANT_GAIN(X)                                          ((X) * 100)

#define RADIO_LR11XX_SPI_BUFFER_SIZE                               255
#define RADIO_RX_LNA_GAIN                                          0

#define NULL_STRUCT_INITIALIZER                                    { 0 }
#define INVALID_DT_GPIO                                            NULL_STRUCT_INITIALIZER

extern uint32_t CLI_sleep_to_full_power_us;

void on_gnss_scan_done(lr11xx_gnss_result_t *);	// implement in application
void on_wifi_scan_done(void *);	// implement in application

__weak void on_gnss_scan_done(lr11xx_gnss_result_t *result) {}
__weak void on_wifi_scan_done(void *) {}

__weak void *gnss_scan_done_context = NULL;
__weak void *wifi_scan_done_context = NULL;

static uint8_t radio_lr1110_buffer[RADIO_LR11XX_SPI_BUFFER_SIZE] = { 0 };

const uint8_t pa_duty_cycles[] = {
  //  -9,   -8,   -7,   -6,   -5,   -4,   -3,   -2,   -1,    0,    1,    2,    3,    4,    5,    6, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  //   7,    8,   9,    10,   11,   12,   13,   14,   15,   16,   17,   18,   19,   20,   21,   22
	0x00, 0x00, 0x00, 0x01, 0x02, 0x02, 0x04, 0x05, 0x07, 0x03, 0x04, 0x02, 0x05, 0x03, 0x04, 0x04
};
const uint8_t pa_hp_sels[] = {
  //  -9,   -8,   -7,   -6,   -5,   -4,   -3,   -2,   -1,    0,    1,    2,    3,    4,    5,    6, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  //   7,    8,   9,    10,   11,   12,   13,   14,   15,   16,   17,   18,   19,   20,   21,   22
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x03, 0x05, 0x04, 0x07, 0x07, 0x07
};
const uint8_t powers[] = {
  //  9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5,  6, 
	 -6, -5, -4, -3, -2, -1,  0,  1,  2, 3, 3, 4, 7, 8, 9, 10,
  // 7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22
	12, 13, 14, 14, 13, 14, 14, 14, 14, 22, 22, 22, 22, 22, 21, 22
};

// <pre>src/smtc_shield_lr1110mb1dxs_common.c</pre>
static int32_t radio_lr11xx_pa_cfg(int8_t tx_power, radio_lr11xx_pa_cfg_t *pa_cfg)
{
	int8_t pwr = tx_power;

	if (tx_power > RADIO_LR11XX_MAX_TX_POWER) {
		pwr = RADIO_LR11XX_MAX_TX_POWER;
	}

	if (tx_power < RADIO_LR11XX_MIN_TX_POWER) {
		pwr = RADIO_LR11XX_MIN_TX_POWER;
	}

	if (pwr > 15) {
		pa_cfg->pa_cfg.pa_reg_supply = LR11XX_RADIO_PA_REG_SUPPLY_VBAT;
		pa_cfg->pa_cfg.pa_sel = LR11XX_RADIO_PA_SEL_HP;
	} else {
		pa_cfg->pa_cfg.pa_reg_supply = LR11XX_RADIO_PA_REG_SUPPLY_VREG;
		pa_cfg->pa_cfg.pa_sel = LR11XX_RADIO_PA_SEL_LP;
	}
	pa_cfg->pa_cfg.pa_duty_cycle = pa_duty_cycles[pwr+9];
	pa_cfg->pa_cfg.pa_hp_sel = pa_hp_sels[pwr+9];

	pa_cfg->ramp_time = LR11XX_RADIO_RAMP_48_US;
	pa_cfg->tx_power_in_dbm = powers[pwr+9];
	pa_cfg->enable_ext_pa = false;

	return 0;
}

static const struct sid_pal_serial_bus_factory radio_spi_factory = {
	.create = sid_pal_serial_bus_nordic_spi_create,
	.config = NULL,
};


const radio_lr11xx_regional_param_t radio_lr11xx_regional_param[] =
{
    #if defined (REGION_ALL) || defined (REGION_US915)
	{
		.param_region = RADIO_REGION_NA,
		.max_tx_power = { RADIO_MAX_TX_POWER_NA, RADIO_MAX_TX_POWER_NA, RADIO_MAX_TX_POWER_NA,
				  RADIO_MAX_TX_POWER_NA, RADIO_MAX_TX_POWER_NA, RADIO_MAX_TX_POWER_NA },
		.cca_level_adjust = { 0, 0, 0, 0, 0, 0 },
		.ant_dbi = RADIO_ANT_GAIN(2.15)
	},
    #endif
    #if defined (REGION_ALL) || defined (REGION_EU868)
	{
		.param_region = RADIO_REGION_EU,
		.max_tx_power = { RADIO_MAX_TX_POWER_EU, RADIO_MAX_TX_POWER_EU, RADIO_MAX_TX_POWER_EU,
				  RADIO_MAX_TX_POWER_EU, RADIO_MAX_TX_POWER_EU, RADIO_MAX_TX_POWER_EU },
		.cca_level_adjust = { 0, 0, 0, 0, 0, 0 },
		.ant_dbi = RADIO_ANT_GAIN(2.15)
	},
    #endif
};

static radio_lr11xx_device_config_t radio_lr11xx_cfg =
{
	.regulator_mode = LR11XX_SYSTEM_REG_MODE_DCDC,
	.rx_boost = false,
	.lna_gain = RADIO_RX_LNA_GAIN,
	.bus_factory = &radio_spi_factory,

	.pa_cfg_callback = radio_lr11xx_pa_cfg,

	.wakeup_delay_us = 0,
	.lfclock_cfg = LR11XX_SYSTEM_LFCLK_RC,
#if defined(CONFIG_SIDEWALK_RADIO_LR11XX)
	.tcxo_config = {
		.ctrl = LR11XX_TCXO_CTRL_DIO3,
		.tune = LR11XX_SYSTEM_TCXO_CTRL_3_3V,
		.timeout = 10 // 20 //200
	},
/* TODO: Add LR1121 config 
 * #elif defined(CONFIG_SIDEWALK_RADIO_LR1121) 
 * 	.tcxo_config = {
 * 		.ctrl                   = LR11XX_TCXO_CTRL_NONE,
 * 	},
 * END TODO */
#else
    #error not_LR1110_or_LR1121
#endif

	.rfswitch = {
    	.enable = LR11XX_SYSTEM_RFSW0_HIGH | LR11XX_SYSTEM_RFSW1_HIGH,
    	.standby = 0,
    	.rx = LR11XX_SYSTEM_RFSW0_HIGH,
    	.tx = LR11XX_SYSTEM_RFSW0_HIGH | LR11XX_SYSTEM_RFSW1_HIGH,
    	.tx_hp = LR11XX_SYSTEM_RFSW1_HIGH,
    	.tx_hf = 0,
    	.gnss = 0,
    	.wifi = 0,
	},

	.rssi_no_signal_offset = 0,

	// .gnss_scan.pre_hook
	// .gnss_scan.post_hook
	// .gnss_scan.arg

	// .wifis_scan.pre_hook
	// .wifis_scan.post_hook
	// .wifis_scan.arg

	.mitigations = { /* prevents servicing of GNSS interrupt */
		.irq_noise_during_sleep = false,
		.lbd_clear_on_wakeup    = false,
	},

	.internal_buffer = {
		.p = radio_lr1110_buffer,
		.size = sizeof(radio_lr1110_buffer),
	},

	.state_timings = {	 // sid_pal_radio_state_transition_timings_t
		.sleep_to_full_power_us = 500,
		.full_power_to_sleep_us = 0,
		.rx_to_tx_us = 0,
		.tx_to_rx_us = 0,
		.tcxo_delay_us = 0,
	},

	.regional_config = {
		.radio_region = RADIO_REGION,
		.reg_param_table_size = sizeof(radio_lr11xx_regional_param) / sizeof(radio_lr11xx_regional_param[0]),
		.reg_param_table = radio_lr11xx_regional_param,
	},
};

const radio_lr11xx_device_config_t *get_radio_cfg(void)
{
	radio_lr11xx_cfg.gpios.power =
		sid_gpio_utils_register_gpio(
			(struct gpio_dt_spec)GPIO_DT_SPEC_GET_OR(DT_NODELABEL(semtech_sx1262_reset_gpios), gpios, INVALID_DT_GPIO));
	radio_lr11xx_cfg.gpios.int1 =
		sid_gpio_utils_register_gpio(
			(struct gpio_dt_spec)GPIO_DT_SPEC_GET_OR(DT_NODELABEL(semtech_sx1262_dio1_gpios), gpios, INVALID_DT_GPIO));
	radio_lr11xx_cfg.gpios.radio_busy =
		sid_gpio_utils_register_gpio(
			(struct gpio_dt_spec)GPIO_DT_SPEC_GET_OR(DT_NODELABEL(semtech_sx1262_busy_gpios), gpios, INVALID_DT_GPIO));
	radio_lr11xx_cfg.gpios.rf_sw_ena =
		sid_gpio_utils_register_gpio(
			(struct gpio_dt_spec)GPIO_DT_SPEC_GET_OR(DT_NODELABEL(semtech_sx1262_antenna_enable_gpios),
							      gpios, INVALID_DT_GPIO));
	radio_lr11xx_cfg.bus_selector.client_selector =
		sid_gpio_utils_register_gpio(
			(struct gpio_dt_spec)GPIO_DT_SPEC_GET_OR(DT_NODELABEL(semtech_sx1262_cs), gpios, INVALID_DT_GPIO));
	radio_lr11xx_cfg.bus_selector.speed_hz = DT_PROP_OR(DT_NODELABEL(sid_semtech), clock_frequency, SPI_FREQUENCY_DEFAULT);

	radio_lr11xx_cfg.gpios.tx_bypass = sid_gpio_utils_register_gpio(
			(struct gpio_dt_spec)GPIO_DT_SPEC_GET_OR(DT_NODELABEL(semtech_sx1262_tx_bypass), gpios, INVALID_DT_GPIO));
	radio_lr11xx_cfg.gpios.led_tx = sid_gpio_utils_register_gpio(
			(struct gpio_dt_spec)GPIO_DT_SPEC_GET_OR(DT_NODELABEL(radio_led_tx), gpios, INVALID_DT_GPIO));
	radio_lr11xx_cfg.gpios.led_rx = sid_gpio_utils_register_gpio(
			(struct gpio_dt_spec)GPIO_DT_SPEC_GET_OR(DT_NODELABEL(radio_led_rx), gpios, INVALID_DT_GPIO));
	radio_lr11xx_cfg.gpios.gnss_lna = sid_gpio_utils_register_gpio(
			(struct gpio_dt_spec)GPIO_DT_SPEC_GET_OR(DT_NODELABEL(radio_gnss_lna), gpios, INVALID_DT_GPIO));
	radio_lr11xx_cfg.gpios.led_sniff = sid_gpio_utils_register_gpio(
			(struct gpio_dt_spec)GPIO_DT_SPEC_GET_OR(DT_NODELABEL(radio_led_sniff), gpios, INVALID_DT_GPIO));

	radio_lr11xx_cfg.gnss_scan.post_hook = on_gnss_scan_done;
	radio_lr11xx_cfg.wifi_scan.post_hook = on_wifi_scan_done;
	radio_lr11xx_cfg.gnss_scan.arg = gnss_scan_done_context;
	radio_lr11xx_cfg.wifi_scan.arg = wifi_scan_done_context;

#ifdef CONFIG_LR1110_CLI
    radio_lr11xx_cfg.state_timings.sleep_to_full_power_us = CLI_sleep_to_full_power_us;
#endif /* CONFIG_LR1110_CLI */

	return &radio_lr11xx_cfg;
}

const struct sid_sub_ghz_links_config sub_ghz_link_config = {
	.enable_link_metrics = true,
	.registration_config = {
		.enable = true,
		.periodicity_s = UINT32_MAX,
	},
};

struct sid_sub_ghz_links_config *app_get_sub_ghz_config(void)
{
	return &sub_ghz_link_config;
}
