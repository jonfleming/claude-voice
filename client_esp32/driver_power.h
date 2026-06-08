#ifndef __DRIVER_POWER_H
#define __DRIVER_POWER_H

#include <Arduino.h>
#include <Wire.h>

// AXP2101 I2C register addresses
#define AXP2101_REG_STATUS1       0x00
#define AXP2101_REG_STATUS2       0x01
#define AXP2101_REG_CHIP_ID       0x03
#define AXP2101_REG_COMMON_CFG    0x10
#define AXP2101_REG_BATFET_CTL    0x12
#define AXP2101_REG_MIN_SYS_VOL   0x14
#define AXP2101_REG_IN_VOL_LIMIT  0x15
#define AXP2101_REG_IN_CUR_LIMIT  0x16
#define AXP2101_REG_RESET_FG      0x17
#define AXP2101_REG_CHG_CTRL      0x18
#define AXP2101_REG_WD_CTRL       0x19
#define AXP2101_REG_LOW_BAT_TH    0x1A
#define AXP2101_REG_PWRON_STS     0x20
#define AXP2101_REG_PWROFF_STS    0x21
#define AXP2101_REG_PWROFF_EN     0x22
#define AXP2101_REG_DCDC_OVP_UVP  0x23
#define AXP2101_REG_DCDC1_V       0x24
#define AXP2101_REG_DCDC2_V       0x25
#define AXP2101_REG_SLEEP_CTL     0x26
#define AXP2101_REG_DCDC3_V       0x29
#define AXP2101_REG_DCDC4_V       0x2A
#define AXP2101_REG_DCDC5_V       0x2B
#define AXP2101_REG_DCDC1_EN      0x37
#define AXP2101_REG_DCDC2_EN      0x38
#define AXP2101_REG_DCDC3_EN      0x39
#define AXP2101_REG_DCDC4_EN      0x3A
#define AXP2101_REG_DCDC5_EN      0x3B
#define AXP2101_REG_LDO1_EN       0x96
#define AXP2101_REG_LDO2_EN       0x97
#define AXP2101_REG_LDO3_EN       0x98
#define AXP2101_REG_LDO4_EN       0x99
#define AXP2101_REG_LDO5_EN       0x9A
#define AXP2101_REG_CHG_LED1      0x9E
#define AXP2101_REG_CHG_LED2      0x9F
#define AXP2101_REG_BAT_VOL_H     0x78
#define AXP2101_REG_BAT_VOL_L     0x79
#define AXP2101_REG_BAT_PCT       0x7A
#define AXP2101_REG_TEMP_H        0x7C
#define AXP2101_REG_TEMP_L        0x7D
#define AXP2101_REG_APS_VOL_H     0x7E
#define AXP2101_REG_APS_VOL_L     0x7F
#define AXP2101_REG_ADC_EN        0x6D
#define AXP2101_REG_ADC_SPEED     0x6E
#define AXP2101_REG_ADC_IRQ_EN    0x6F
#define AXP2101_REG_DEBOUNCE      0x90

// AXP2101 chip ID
#define AXP2101_CHIP_ID_VAL       0x03

// Bit masks for DCDC enable registers
#define AXP2101_BIT_DCDC1_EN      (1 << 0)
#define AXP2101_BIT_DCDC2_EN      (1 << 1)
#define AXP2101_BIT_DCDC3_EN      (1 << 2)
#define AXP2101_BIT_DCDC4_EN      (1 << 3)
#define AXP2101_BIT_DCDC5_EN      (1 << 4)

#define AXP2101_BIT_LDO1_EN       (1 << 0)
#define AXP2101_BIT_LDO2_EN       (1 << 1)
#define AXP2101_BIT_LDO3_EN       (1 << 2)
#define AXP2101_BIT_LDO4_EN       (1 << 3)
#define AXP2101_BIT_LDO5_EN       (1 << 4)

// Bit masks for ADC enable register (0x6D)
#define AXP2101_ADC_BAT_VOL_EN    (1 << 4)
#define AXP2101_ADC_APS_VOL_EN    (1 << 5)
#define AXP2101_ADC_TEMP_EN       (1 << 6)

// Charging current options (register 0x33 bits [4:3])
typedef enum {
  CHG_CUR_100MA = 0,
  CHG_CUR_150MA,
  CHG_CUR_200MA,
  CHG_CUR_250MA,
  CHG_CUR_300MA,
  CHG_CUR_350MA,
  CHG_CUR_400MA,
  CHG_CUR_450MA
} AXP2101_ChargeCurrent;

// Charge termination voltage options (register 0x33 bits [2:0])
typedef enum {
  CHG_TERM_4100MV = 0,
  CHG_TERM_4150MV,
  CHG_TERM_4200MV,
  CHG_TERM_4250MV
} AXP2101_ChgTermVoltage;

// Low battery warning threshold options (register 0x1A)
typedef enum {
  LOW_BAT_3300MV = 0,
  LOW_BAT_3200MV,
  LOW_BAT_3100MV,
  LOW_BAT_3000MV,
  LOW_BAT_2900MV,
  LOW_BAT_2800MV,
  LOW_BAT_2700MV,
  LOW_BAT_2600MV
} AXP2101_LowBatThreshold;

// Shutdown voltage (register 0x14 bits [2:0])
typedef enum {
  SHUTDOWN_2600MV = 0,
  SHUTDOWN_2700MV,
  SHUTDOWN_2800MV,
  SHUTDOWN_2900MV,
  SHUTDOWN_3000MV,
  SHUTDOWN_3100MV,
  SHUTDOWN_3200MV,
  SHUTDOWN_3300MV
} AXP2101_ShutdownVoltage;

// Power management state
typedef enum {
  POWER_STATE_NORMAL,
  POWER_STATE_LOW_BAT,
  POWER_STATE_CRITICAL,
  POWER_STATE_SHUTDOWN
} AXP2101_PowerState;

class DriverPower {
public:
  DriverPower(TwoWire &wire, uint8_t addr);

  bool init();
  bool detect();
  int read_battery_voltage();
  int read_battery_percent();
  int read_temperature();
  int read_input_voltage();
  bool is_battery_present();
  bool is_charging();
  bool is_vbus_present();
  AXP2101_PowerState get_power_state();
  bool is_low_battery();
  void config_deep_sleep_wake();
  void enter_deep_sleep();
  uint8_t get_pwr_on_reason();
  uint8_t get_pwr_off_reason();
  void set_shutdown_voltage(AXP2101_ShutdownVoltage voltage);
  void set_low_battery_threshold(AXP2101_LowBatThreshold threshold);
  void set_charge_current(AXP2101_ChargeCurrent current);
  void set_charge_term_voltage(AXP2101_ChgTermVoltage voltage);
  void enable_adc_battery_voltage(bool enable);
  void enable_adc_input_voltage(bool enable);
  void enable_adc_temperature(bool enable);

private:
  TwoWire *_wire;
  uint8_t _addr;

  bool write_reg(uint8_t reg, uint8_t data);
  bool write_reg_bits(uint8_t reg, uint8_t mask, uint8_t val);
  bool read_reg(uint8_t reg, uint8_t &data);
  bool read_regs(uint8_t reg, uint8_t *buf, uint8_t len);

  void config_dc_dc();
  void config_ldo();
  void config_charging();
  void config_adc();
  void config_shutdown();
};

extern DriverPower power;

#endif
