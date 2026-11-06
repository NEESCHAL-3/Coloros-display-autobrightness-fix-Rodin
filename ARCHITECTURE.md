# ColorOS Display & Auto-Brightness Architecture

This document details the engineering architecture, hardware interactions, and framework mechanisms for auto-brightness and brightness mapping on ported ColorOS (Android 15 / ColorOS 15/17) systems.

> [!NOTE]
> Architectural Boundary: This repository delivers the display subsystem components: DisplayDeviceConfig (`display_id_<ID>.xml`), ColorOS brightness splines (`display_brightness_config_*.xml`), and display properties (`display_brightness.prop`). Sensor wrappers (such as `sensors.mt6899.so` for AOD, FOD, and Rise-to-Wake) belong to the dedicated AOD-FOD compatibility repository (`NEESS-COS-AOD-FOD-FIX`), preventing binary collisions.

---

## 1. Problem Analysis & Root Causes

Porting ColorOS from native OPPO/OnePlus hardware to third-party devices (e.g. POCO, Xiaomi, Realme, Vivo) typically results in non-functional, sluggish, or erratic auto-brightness. Investigation identified two core display framework root causes:

### 1.1 Physical Display ID Mismatch & 8-Second Debounce Fallback
* In Android 15, `DisplayDeviceConfig` maps configuration files using the 64-bit physical display ID (`DisplayDeviceInfo.address` -> `StablePhysical{id=..., port=0}`).
* Stock ColorOS firmware only packages display configs matching genuine OPPO panels.
* When a ported device boots without a matching `/vendor/etc/displayconfig/display_id_<ID>.xml`, the framework falls back to internal default constants:
  * `brighteningLightDebounceMillis` = `4000 ms`
  * `darkeningLightDebounceMillis` = `8000 ms` (8 seconds)
  * `screenBrightnessRampSlowDecrease` = `0.23`
* Result: The display takes 8 seconds before beginning to dim in dark environments, and drops brightness at an imperceptibly slow rate.

### 1.2 Minimum Brightness Spline Clamping Floor (222 vs 1)
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
|  |          Ambient Light Sensor (AMS tsl2522 / Hardware ALS)         |  |
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
