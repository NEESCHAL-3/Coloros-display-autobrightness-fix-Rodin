# ColorOS Display & Auto-Brightness Architecture

This document details the engineering architecture, hardware interactions, and framework mechanisms for auto-brightness and brightness mapping on ported ColorOS (Android 15 / ColorOS 15/17) systems.

> [!IMPORTANT]
> **Companion Sensor Architecture**: This repository delivers the display subsystem components: DisplayDeviceConfig (`display_id_<ID>.xml`), ColorOS brightness splines (`display_brightness_config_*.xml`), and display properties (`display_brightness.prop`).
> 
> The low-level sensor HAL (`sensors.mt6899.so`), which bridges Xiaomi raw light sensor events (`als_raw` type 33171111) to standard Android light events (`android.sensor.light` handle 5) and applies real-time OLED optical leakage subtraction, is maintained in the companion repository:
> - **[NEESCHAL-3/NEESS-COS-AOD-FOD-FIX](https://github.com/NEESCHAL-3/NEESS-COS-AOD-FOD-FIX)**
>
> This separation prevents binary collisions on `/vendor/lib64/hw/sensors.mt6899.so`.

---

## 1. Problem Analysis & Root Causes

Porting ColorOS from native OPPO/OnePlus hardware to third-party devices (e.g. POCO, Xiaomi, Realme, Vivo) typically results in non-functional, sluggish, or erratic auto-brightness. Investigation identified three independent root causes:

### 1.1 Physical Display ID Mismatch & 8-Second Debounce Fallback
* In Android 15, `DisplayDeviceConfig` maps configuration files using the 64-bit physical display ID (`DisplayDeviceInfo.address` -> `StablePhysical{id=..., port=0}`).
* Stock ColorOS firmware only packages display configs matching genuine OPPO panels.
* When a ported device boots without a matching `/vendor/etc/displayconfig/display_id_<ID>.xml`, the framework falls back to internal default constants:
  * `brighteningLightDebounceMillis` = `4000 ms`
  * `darkeningLightDebounceMillis` = `8000 ms` (8 seconds)
  * `screenBrightnessRampSlowDecrease` = `0.23`
* Result: The display takes 8 seconds before beginning to dim in dark environments, and drops brightness at an imperceptibly slow rate.

### 1.2 Under-Display ALS Optical Feedback Loop (Display Leakage)
* On modern OLED devices, the Ambient Light Sensor (AMS `tsl2522_als`) is situated beneath active pixels of the OLED matrix.
* Photons emitted by the OLED panel penetrate the substrate and strike the sensor photodiode.
* At high display brightness, raw counts reach over 50,000 clear counts even in a pitch-black room.
* Stock ColorOS relies on proprietary dual-sensor fusion (front ALS + rear "ARC" sensor) which does not exist on single-sensor devices.
* The companion repository [`NEESS-COS-AOD-FOD-FIX`](https://github.com/NEESCHAL-3/NEESS-COS-AOD-FOD-FIX) addresses this by intercepting raw photodiode events in `sensors.mt6899.so` and subtracting active display leakage.

### 1.3 Minimum Brightness Spline Clamping Floor (222 vs 1)
* Stock ColorOS `/system_ext/etc/display_brightness_config_default.xml` contains:
  ```xml
  <brightness_table max="8191" min="222">
  ```
* The UI slider and `OplusDisplaySplineManager` enforce `min="222"` as the hard floor, corresponding to approximately 100 nits.
* Result: The screen cannot dim to true OLED minimum (2.0 nits), causing severe eye strain in dark environments.

---

## 2. System Architecture

```
+-------------------------------------------------------------------------+
|                              Android System                             |
|  +-----------------------------+     +-------------------------------+  |
|  |    DisplayPowerController   |     |   DisplayDeviceConfig (DDC)   |  |
|  | - AutomaticBrightnessStrat  |     | - display_id_<PHYSICAL_ID>.xml|  |
|  | - Spline & Debounce (400ms) |     | - 400ms brightening / 300ms   |  |
|  +--------------^--------------+     +---------------+---------------+  |
|                 |                                    |                  |
+-----------------|------------------------------------|------------------+
                  | SENSOR_TYPE_LIGHT (handle=5)       | Loads config
+-----------------|------------------------------------|------------------+
|                 v                                    v                  |
|  +-------------------------------------------------------------------+  |
|  |   Sensor HAL (NEESS-COS-AOD-FOD-FIX: sensors.mt6899.so)          |  |
|  |   - Bridges als_raw (0x5e) -> android.sensor.light (5)            |  |
|  |   - Subtracts OLED display optical leakage                        |  |
|  |   - Handles AOD / FOD / Rise-to-Wake                              |  |
|  +-------------------------------------------------------------------+  |
+-------------------------------------------------------------------------+
```

---

## 3. Debounce & Ramp Calibration Parameters

In `display_id_<ID>.xml`, responsive timing values replace the 8,000 ms fallback:

| Parameter | Stock Fallback | Optimized Value | Impact |
|:---|:---|:---|:---|
| `brighteningLightDebounceMillis` | 4,000 ms | **400 ms** | Instant adaptation when entering bright areas |
| `darkeningLightDebounceMillis` | 8,000 ms | **300 ms** | Rapid dimming when entering dark environments |
| `screenBrightnessRampSlowDecrease` | 0.23 | **1.00** | Smooth, perceptible rate of dimming |
| `screenBrightnessRampFastDecrease` | 0.23 | **1.00** | Immediate transition on extreme lux drops |
| `screenBrightnessRampDecreaseMaxMillis` | N/A | **1,200 ms** | Bounded transition duration |

---

## 4. Brightness Spline Tuning (`display_brightness_config_default.xml`)

Lowering the minimum level table bound restores the full hardware dynamic range:

```xml
<!-- Before (Stock ColorOS): Screen clamped at ~100 nits -->
<brightness_table max="8191" min="222">

<!-- After (Patched): Unlocks true 2.0-nit OLED night brightness -->
<brightness_table max="8191" min="1">
```

Along with `ro.vendor.display.brightness.remap=true`, the system remaps slider steps smoothly across all 8,191 backlight quantization levels without stepping artifacts.
