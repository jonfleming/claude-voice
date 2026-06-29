**Here's your text cleanly formatted in Markdown:**

---

# Plan to Make AIPI Lite Selectable with `#ifdef`

## Overview
Add one shared board pin profile header to support both Freenove and AIPI Lite hardware through compile-time selection.

### 1. Create Shared Board Pins Header
- Create `client_esp32/board_pins.h`
- Hold all pin constants behind one compile-time switch
- Use a single selector macro: `BOARD_AIPI_LITE`

**Structure:**
```cpp
#ifdef BOARD_AIPI_LITE
    // AIPI Lite pins (from AIPI-Lite-GPIO-Pins.md)
#else
    // Current Freenove defaults
#endif
```

**Pins to include at minimum:**
- Button pin
- TFT backlight pin
- Audio input pins (`SCK`/`WS`/`DIN`)
- Audio output pins (`BCLK`/`LRCLK`/`DOUT`)
- AIPI power keep-alive pin (`GPIO10`)
- AIPI speaker amp enable pin (`GPIO9`)

### 2. Update Display/Button Headers
- Update `display.h`:
  - Include `client_esp32/board_pins.h`
  - Replace hard-coded `BUTTON_PIN` and `TFT_BL` defines with profile macros
- Keep `driver_button.h` unchanged (for now)

### 3. Update All Five Test Sketches

#### `client_esp32_test_01_display.ino`
- Include `board_pins.h`
- Initialize AIPI power keep-alive in `setup()` under `#ifdef BOARD_AIPI_LITE`

#### `client_esp32_test_02_button.ino`
- Include `board_pins.h`
- Replace `BUTTON_ADC_PIN` (19) with board-profile button pin macro
- Add AIPI keep-alive in `setup()`

#### `client_esp32_test_03_speaker_wav.ino`
- Replace local `AUDIO_OUTPUT_BCLK`/`LRC`/`DOUT` defines with board-profile macros
- Under `#ifdef BOARD_AIPI_LITE`, set speaker amp enable pin **HIGH** before playback
- Add AIPI keep-alive in `setup()`

#### `client_esp32_test_04_microphone_record.ino`
- Replace local audio input and output pin defines with board-profile macros
- Under `#ifdef BOARD_AIPI_LITE`, set speaker amp enable **HIGH** during playback phase
- Add AIPI keep-alive in `setup()`

#### `client_esp32_test_05_websocket_ping.ino`
- Include `board_pins.h`
- Add AIPI keep-alive in `setup()`

### 4. Add Compile-Time Board Selection Instructions
Add to the top comment of each test sketch:

> Default build targets **Freenove**.  
> To build for **AIPI Lite**: define `BOARD_AIPI_LITE` (add `-DBOARD_AIPI_LITE` in build flags, or uncomment the define in `board_pins.h`).

**Optional:** Place a single commented `#define BOARD_AIPI_LITE` in `board_pins.h` for quick toggling in the IDE.

### Validation Checklist

- **Test 01**: Display boots, banner visible, line updates work
- **Test 02**: Button transitions register correctly on AIPI pin mapping
- **Test 03**: Tone plays only after amp enable is asserted on AIPI
- **Test 04**: Mic capture works and playback returns audio
- **Test 05**: Button-triggered ping works and websocket reconnect loop is unchanged

### Important Risk

> Current button driver in `driver_button.h` is **analog-threshold** based (resistor ladder).  
> AIPI buttons may be **plain digital GPIOs**.  
> 
> **Impact**: The pin `#ifdef` is still the correct first step. If button behavior is unstable on AIPI, the next step is adding a second `#ifdef` in the button driver logic to use `digitalRead()` mode for AIPI Lite.

---

Let me know if you'd like any adjustments (e.g., more code blocks, different heading levels, or task checkboxes).