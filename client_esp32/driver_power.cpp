// driver_power.cpp — AXP2101 power management driver for Waveshare ESP32-S3-Touch-AMOLED-1.8
//
// Hardware:
//   AXP2101 PMU at I2C address 0x34
//   SDA=GPIO15, SCL=GPIO14 (shared with FT3168 touch controller)
//   No I2C conflicts: both devices on same Wire bus, different addresses
//
// Functions:
//   - I2C read/write helpers
//   - Chip detection (read ID register)
//   - DC/DC converter enablement (DCDC1-DCDC5, LDO1-LDO5)
//   - Battery charging configuration
//   - Battery voltage / percentage / temperature ADC reading
//   - Low battery detection
//   - Deep sleep wake source configuration
//   - Shutdown voltage threshold

#include "driver_power.h"
#include "board_pins.h"

#ifdef BOARD_WAVESHARE_AMOLED

#include <Arduino.h>
#include <Wire.h>

// =============================================================================
// I2C helper functions (generic, board-independent)
// =============================================================================

bool DriverPower::write_reg(uint8_t reg, uint8_t data) {
    Wire.beginTransmission(_addr);
    Wire.write(reg);
    Wire.write(data);
    bool ok = (Wire.endTransmission() == 0);
    return ok;
}

bool DriverPower::write_reg_bits(uint8_t reg, uint8_t mask, uint8_t val) {
    uint8_t old;
    if (!read_reg(reg, old)) return false;
    uint8_t new_val = (old & ~mask) | (val & mask);
    return write_reg(reg, new_val);
}

bool DriverPower::read_reg(uint8_t reg, uint8_t &data) {
    Wire.beginTransmission(_addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    Wire.requestFrom(_addr, (uint8_t)1);
    if (Wire.available() < 1) return false;
    data = Wire.read();
    return true;
}

bool DriverPower::read_regs(uint8_t reg, uint8_t *buf, uint8_t len) {
    Wire.beginTransmission(_addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    Wire.requestFrom(_addr, len);
    if (Wire.available() < len) return false;
    for (uint8_t i = 0; i < len; i++) {
        buf[i] = Wire.read();
    }
    return true;
}

// =============================================================================
// Public constructor
// =============================================================================

DriverPower::DriverPower(TwoWire &wire, uint8_t addr)
    : _wire(&wire), _addr(addr) {}

// =============================================================================
// Detection
// =============================================================================

bool DriverPower::detect() {
    Serial.printf("[power] Scanning I2C bus for AXP2101 at 0x%02X...\r\n", _addr);
    uint8_t chip_id = 0;
    if (!read_reg(AXP2101_REG_CHIP_ID, chip_id)) {
        Serial.println("[power] [FAIL] Cannot read chip ID — no ACK on I2C");
        return false;
    }
    Serial.printf("[power] Chip ID: 0x%02X (expected 0x%02X)\r\n", chip_id, AXP2101_CHIP_ID_VAL);
    if (chip_id != AXP2101_CHIP_ID_VAL) {
        Serial.printf("[power] [FAIL] Chip ID mismatch! Expected 0x%02X, got 0x%02X\r\n",
            AXP2101_CHIP_ID_VAL, chip_id);
        return false;
    }
    Serial.println("[power] [OK] AXP2101 detected.");
    return true;
}

bool DriverPower::init() {
    Serial.println("[power] Initializing AXP2101...");

    // Ensure I2C bus is initialized (should already be done by touch_init())
    Wire.begin(TOUCH_I2C_SDA, TOUCH_I2C_SCL);

    // First verify chip is present
    if (!detect()) {
        Serial.println("[power] [FAIL] AXP2101 not found — power driver disabled.");
        return false;
    }

    // Configure hardware
    config_charging();
    config_dc_dc();
    config_ldo();
    config_adc();
    config_shutdown();

    Serial.println("[power] [OK] AXP2101 initialized.");
    return true;
}

// =============================================================================
// Charging configuration
// =============================================================================

void DriverPower::config_charging() {
    // Register 0x33 (CHG_CTRL): enable charging, set termination current
    // Bit [7]  = 1: enable charge
    // Bit [6]  = 1: enable pre-charge
    // Bits [5:4] = pre-charge current (00=100mA, 01=150mA, 10=200mA, 11=250mA)
    // Bits [3:2] = charge current (00=100mA..11=450mA per enum)
    // Bits [1:0] = termination voltage (00=4100mV, 01=4150mV, 10=4200mV, 11=4250mV)
    //
    // For Li-ion single cell: 4.2V terminate, 300mA charge current
    uint8_t val = 0xC0;  // enable charge + pre-charge
    val |= ((uint8_t)CHG_CUR_300MA << 2);  // charge current 300mA
    val |= (uint8_t)CHG_TERM_4200MV;  // terminate at 4.2V
    write_reg(AXP2101_REG_CHG_CTRL, val);

    // Register 0x32 (PRE_CHG): pre-charge timer and current
    // Set pre-charge to 100mA (bits [3:0])
    write_reg(0x32, 0x00);  // 100mA pre-charge

    Serial.println("[power] Charging: 300mA, 4.2V terminate, 100mA pre-charge");
}

void DriverPower::set_charge_current(AXP2101_ChargeCurrent current) {
    uint8_t reg;
    read_reg(AXP2101_REG_CHG_CTRL, reg);
    reg = (reg & ~0x0C) | ((uint8_t)current << 2);
    write_reg(AXP2101_REG_CHG_CTRL, reg);
    Serial.printf("[power] Charge current set to %d\r\n",
        (int)(current * 50 + 100));
}

void DriverPower::set_charge_term_voltage(AXP2101_ChgTermVoltage voltage) {
    uint8_t reg;
    read_reg(AXP2101_REG_CHG_CTRL, reg);
    reg = (reg & ~0x03) | ((uint8_t)voltage & 0x03);
    write_reg(AXP2101_REG_CHG_CTRL, reg);
    Serial.printf("[power] Charge terminate voltage set to %d\r\n",
        (int)(4100 + (uint32_t)voltage * 50));
}

// =============================================================================
// DC/DC converter configuration
// =============================================================================

void DriverPower::config_dc_dc() {
    // DCDC1 (0x24): VOUT = 0.7V + 0.025V * REG_VAL, range 0.7-3.5V, step 25mV
    // ESP32-S3 core VDD = 3.3V => REG_VAL = (3.3 - 0.7) / 0.025 = 104 (0x68)
    write_reg(AXP2101_REG_DCDC1_V, 0x68);  // 3.3V

    // DCDC2 (0x25): VOUT = 0.7V + 0.025V * REG_VAL
    // Used for peripherals => 3.3V
    write_reg(AXP2101_REG_DCDC2_V, 0x68);  // 3.3V

    // DCDC3 (0x26): VOUT = 0.61V + 0.01V * REG_VAL, range 0.61-3.47V
    // Used for display backlight => 3.3V => (3.3-0.61)/0.01 = 269 (0x10D, clamped to 0x69)
    write_reg(AXP2101_REG_DCDC3_V, 0x69);  // 3.3V

    // DCDC4 (0x27): VOUT = 0.61V + 0.01V * REG_VAL
    // Used for SD card / external => 3.3V
    write_reg(AXP2101_REG_DCDC4_V, 0x69);  // 3.3V

    // DCDC5 (0x28): VOUT = 0.5V + 0.025V * REG_VAL, range 0.5-2.75V
    // Used for flash memory => 1.8V => (1.8-0.5)/0.025 = 52 (0x34)
    write_reg(AXP2101_REG_DCDC5_V, 0x34);  // 1.8V

    // Enable DCDC1 (ESP32-S3 core)
    write_reg(AXP2101_REG_DCDC1_EN, AXP2101_BIT_DCDC1_EN);
    // Enable DCDC2 (peripherals)
    write_reg(AXP2101_REG_DCDC2_EN, AXP2101_BIT_DCDC2_EN);
    // Enable DCDC3 (display)
    write_reg(AXP2101_REG_DCDC3_EN, AXP2101_BIT_DCDC3_EN);
    // Enable DCDC4 (SD card)
    write_reg(AXP2101_REG_DCDC4_EN, AXP2101_BIT_DCDC4_EN);
    // Enable DCDC5 (flash)
    write_reg(AXP2101_REG_DCDC5_EN, AXP2101_BIT_DCDC5_EN);

    Serial.println("[power] DCDC1=3.3V ESP32, DCDC2=3.3V, DCDC3=3.3V display, DCDC4=3.3V SD, DCDC5=1.8V flash");
}

// =============================================================================
// LDO configuration
// =============================================================================

void DriverPower::config_ldo() {
    // LDO1 (0x96): VDD12 — 3.3V (display VDD)
    write_reg(AXP2101_REG_LDO1_EN, AXP2101_BIT_LDO1_EN);
    // LDO2 (0x97): VDDIO — 3.3V (I/O voltage)
    write_reg(AXP2101_REG_LDO2_EN, AXP2101_BIT_LDO2_EN);
    // LDO3 (0x98): VDDRTC — 3.3V (RTC)
    write_reg(AXP2101_REG_LDO3_EN, AXP2101_BIT_LDO3_EN);
    // LDO4 (0x99): VDDIO2 — 1.8V (external IO)
    write_reg(AXP2101_REG_LDO4_EN, AXP2101_BIT_LDO4_EN);
    // LDO5 (0x9A): VDDIO3 — 3.3V
    write_reg(AXP2101_REG_LDO5_EN, AXP2101_BIT_LDO5_EN);

    Serial.println("[power] LDO1-5 enabled");
}

// =============================================================================
// ADC configuration
// =============================================================================

void DriverPower::config_adc() {
    // Register 0x6D (ADC_EN): enable ADC channels
    // Bit 4 = BAT_VOL (battery voltage)
    // Bit 5 = APS_VOL (system voltage)
    // Bit 6 = TEMP (temperature)
    // Bit 7 = VBUS_I (VBUS current)
    write_reg(AXP2101_REG_ADC_EN, AXP2101_ADC_BAT_VOL_EN | AXP2101_ADC_APS_VOL_EN | AXP2101_ADC_TEMP_EN);

    // Register 0x6E (ADC_SPEED): set ADC sample rate
    // Bits [5:4] = 00 = 12.5ms (fastest)
    write_reg(AXP2101_REG_ADC_SPEED, 0x00);

    Serial.println("[power] ADC: BAT_VOL, APS_VOL, TEMP enabled");
}

void DriverPower::enable_adc_battery_voltage(bool enable) {
    uint8_t reg;
    read_reg(AXP2101_REG_ADC_EN, reg);
    if (enable) {
        reg |= AXP2101_ADC_BAT_VOL_EN;
    } else {
        reg &= ~AXP2101_ADC_BAT_VOL_EN;
    }
    write_reg(AXP2101_REG_ADC_EN, reg);
}

void DriverPower::enable_adc_input_voltage(bool enable) {
    uint8_t reg;
    read_reg(AXP2101_REG_ADC_EN, reg);
    if (enable) {
        reg |= AXP2101_ADC_APS_VOL_EN;
    } else {
        reg &= ~AXP2101_ADC_APS_VOL_EN;
    }
    write_reg(AXP2101_REG_ADC_EN, reg);
}

void DriverPower::enable_adc_temperature(bool enable) {
    uint8_t reg;
    read_reg(AXP2101_REG_ADC_EN, reg);
    if (enable) {
        reg |= AXP2101_ADC_TEMP_EN;
    } else {
        reg &= ~AXP2101_ADC_TEMP_EN;
    }
    write_reg(AXP2101_REG_ADC_EN, reg);
}

// =============================================================================
// Shutdown configuration
// =============================================================================

void DriverPower::config_shutdown() {
    // Register 0x14 (MIN_SYS_VOL): shutdown voltage threshold
    // Bits [2:0] = 010 = 2600mV (default safe shutdown)
    write_reg(AXP2101_REG_MIN_SYS_VOL, 0x02);  // 2.6V shutdown

    // Register 0x1A (LOW_BAT_TH): low battery threshold
    // Bits [2:0] = 001 = 3200mV (warning at 3.2V)
    write_reg(AXP2101_REG_LOW_BAT_TH, 0x01);

    // Register 0x22 (PWROFF_EN): enable power-off by long press
    // Bit 7 = 1: enable long-press power off (4s)
    write_reg(AXP2101_REG_PWROFF_EN, 0x80);

    Serial.println("[power] Shutdown: 2.6V threshold, low-bat at 3.2V");
}

void DriverPower::set_shutdown_voltage(AXP2101_ShutdownVoltage voltage) {
    write_reg(AXP2101_REG_MIN_SYS_VOL, (uint8_t)voltage);
}

void DriverPower::set_low_battery_threshold(AXP2101_LowBatThreshold threshold) {
    write_reg(AXP2101_REG_LOW_BAT_TH, (uint8_t)threshold);
}

// =============================================================================
// Battery voltage reading
// =============================================================================

int DriverPower::read_battery_voltage() {
    uint8_t hi, lo;
    if (!read_reg(AXP2101_REG_BAT_VOL_H, hi)) return -1;
    if (!read_reg(AXP2101_REG_BAT_VOL_L, lo)) return -1;

    // Bits [7:0] of hi + bits [7:4] of lo = 13-bit value
    uint16_t raw = ((hi & 0x1F) << 4) | ((lo >> 4) & 0x0F);
    // Each LSB = 1.1mV, offset = 0mV
    int voltage_mv = raw * 11;
    return voltage_mv;
}

// =============================================================================
// Battery percentage (fuel gauge)
// =============================================================================

int DriverPower::read_battery_percent() {
    uint8_t pct = 0;
    if (!read_reg(AXP2101_REG_BAT_PCT, pct)) return -1;
    return pct;
}

// =============================================================================
// Temperature reading
// =============================================================================

int DriverPower::read_temperature() {
    uint8_t hi, lo;
    if (!read_reg(AXP2101_REG_TEMP_H, hi)) return -1;
    if (!read_reg(AXP2101_REG_TEMP_L, lo)) return -1;

    // 13-bit signed value, 1 LSB = 0.1°C, offset = -144°C
    int16_t raw;
    if (hi & 0x10) {
        // Negative temperature: sign extend
        raw = (int16_t)((hi & 0x0F) << 8) | lo;
        raw |= 0xF000;  // sign extend
    } else {
        raw = (int16_t)((hi & 0x0F) << 8) | lo;
    }
    int temp_c = raw / 10 - 144;
    return temp_c;
}

// =============================================================================
// System (APS) voltage reading
// =============================================================================

int DriverPower::read_input_voltage() {
    uint8_t hi, lo;
    if (!read_reg(AXP2101_REG_APS_VOL_H, hi)) return -1;
    if (!read_reg(AXP2101_REG_APS_VOL_L, lo)) return -1;

    uint16_t raw = ((hi & 0x1F) << 4) | ((lo >> 4) & 0x0F);
    // Each LSB = 1.1mV
    int voltage_mv = raw * 11;
    return voltage_mv;
}

// =============================================================================
// Battery presence
// =============================================================================

bool DriverPower::is_battery_present() {
    uint8_t status = 0;
    if (!read_reg(AXP2101_REG_STATUS1, status)) return false;
    // Bit 7 = 1: battery present
    return (status & 0x80) != 0;
}

// =============================================================================
// Charging status
// =============================================================================

bool DriverPower::is_charging() {
    uint8_t status = 0;
    if (!read_reg(AXP2101_REG_STATUS2, status)) return false;
    // Bit 4 = 1: charging
    return (status & 0x10) != 0;
}

// =============================================================================
// VBUS presence
// =============================================================================

bool DriverPower::is_vbus_present() {
    uint8_t status = 0;
    if (!read_reg(AXP2101_REG_STATUS2, status)) return false;
    // Bit 5 = 1: VBUS present
    return (status & 0x20) != 0;
}

// =============================================================================
// Power state
// =============================================================================

AXP2101_PowerState DriverPower::get_power_state() {
    int bat_mv = read_battery_voltage();
    if (bat_mv < 2600) return POWER_STATE_SHUTDOWN;
    if (bat_mv < 3000) return POWER_STATE_CRITICAL;
    if (bat_mv < 3300) return POWER_STATE_LOW_BAT;
    return POWER_STATE_NORMAL;
}

bool DriverPower::is_low_battery() {
    int bat_mv = read_battery_voltage();
    return (bat_mv > 0 && bat_mv < 3300);
}

// =============================================================================
// Deep sleep wake source
// =============================================================================

void DriverPower::config_deep_sleep_wake() {
    // Register 0x26 (SLEEP_CTL): configure wakeup sources
    // Bit 0 = 1: enable wakeup on GPIO button (KEY pin)
    // Bit 1 = 1: enable wakeup on power-on key
    // Bit 2 = 1: enable wakeup on low-battery
    uint8_t val = 0x07;  // wake on KEY, POWERON, low-battery
    write_reg(AXP2101_REG_SLEEP_CTL, val);

    Serial.println("[power] Deep sleep wake: KEY pin, POWERON, low-battery");
}

void DriverPower::enter_deep_sleep() {
    // Register 0x10 (COMMON_CFG): bit 3 = 1 to enter sleep mode
    uint8_t reg;
    read_reg(AXP2101_REG_COMMON_CFG, reg);
    reg |= 0x08;  // set SLEEP bit
    write_reg(AXP2101_REG_COMMON_CFG, reg);

    // Disable ADC to reduce current
    write_reg(AXP2101_REG_ADC_EN, 0x00);

    // Disable unused DCDCs
    write_reg(AXP2101_REG_DCDC4_EN, 0x00);
    write_reg(AXP2101_REG_DCDC5_EN, 0x00);
    write_reg(AXP2101_REG_DCDC3_EN, 0x00);

    Serial.println("[power] Entering deep sleep...");

    // ESP32 deep sleep (AXP2101 will wake on KEY press)
    esp_deep_sleep_start();
}

// =============================================================================
// Power-on / power-off reason
// =============================================================================

uint8_t DriverPower::get_pwr_on_reason() {
    uint8_t reason = 0;
    if (read_reg(AXP2101_REG_PWRON_STS, reason)) {
        return reason;
    }
    return 0;
}

uint8_t DriverPower::get_pwr_off_reason() {
    uint8_t reason = 0;
    if (read_reg(AXP2101_REG_PWROFF_STS, reason)) {
        return reason;
    }
    return 0;
}

// =============================================================================
// Global instance
// =============================================================================

// Wire bus: GPIO15 (SDA), GPIO14 (SCL)
// AXP2101 I2C address: 0x34
// Wire is initialized by touch_init() before power.init() is called.
// This global instance uses the same Wire bus as the touch controller.
DriverPower power(Wire, AXP2101_I2C_ADDR);

#else  // BOARD_WAVESHARE_AMOLED

// Stub for non-Waveshare boards (no AXP2101 PMU)

// Stub constructor — no real I2C communication
DriverPower::DriverPower(TwoWire &wire, uint8_t addr) : _wire(&wire), _addr(addr) {}

// Global stub instance
DriverPower power(Wire, 0x34);

bool DriverPower::init() { return false; }
bool DriverPower::detect() { return false; }
int DriverPower::read_battery_voltage() { return 0; }
int DriverPower::read_battery_percent() { return 0; }
int DriverPower::read_temperature() { return 0; }
int DriverPower::read_input_voltage() { return 0; }
bool DriverPower::is_battery_present() { return false; }
bool DriverPower::is_charging() { return false; }
bool DriverPower::is_vbus_present() { return false; }
AXP2101_PowerState DriverPower::get_power_state() { return POWER_STATE_NORMAL; }
bool DriverPower::is_low_battery() { return false; }
void DriverPower::config_deep_sleep_wake() {}
void DriverPower::enter_deep_sleep() {}
uint8_t DriverPower::get_pwr_on_reason() { return 0; }
uint8_t DriverPower::get_pwr_off_reason() { return 0; }
void DriverPower::set_shutdown_voltage(AXP2101_ShutdownVoltage) {}
void DriverPower::set_low_battery_threshold(AXP2101_LowBatThreshold) {}
void DriverPower::set_charge_current(AXP2101_ChargeCurrent) {}
void DriverPower::set_charge_term_voltage(AXP2101_ChgTermVoltage) {}
void DriverPower::enable_adc_battery_voltage(bool) {}
void DriverPower::enable_adc_input_voltage(bool) {}
void DriverPower::enable_adc_temperature(bool) {}

#endif  // BOARD_WAVESHARE_AMOLED
