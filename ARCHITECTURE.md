# ColorOS Display & Auto-Brightness Architecture

This document details the engineering architecture, hardware interactions, and framework mechanisms for auto-brightness and brightness mapping on ported ColorOS (Android 15 / ColorOS 15/17) systems.

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
* On modern OLED devices, the Ambient Light Sensor (e.g. AMS `tsl2522_als`) is situated beneath active pixels of the OLED matrix.
* Photons emitted by the OLED panel penetrate the substrate and strike the sensor photodiode.
* At high display brightness, raw counts reach over 50,000 clear counts even in a pitch-black room.
* Stock ColorOS relies on proprietary dual-sensor fusion (front ALS + rear "ARC" sensor) which does not exist on single-sensor devices.
* Result: A positive feedback runaway loop occurs. High brightness leaks into the sensor -> framework computes high lux -> framework pushes brightness higher.

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
|                 |                                    v                  |
|                 |           Sensor HAL Layer                            |
|  +--------------+----------------------------------------------------+  |
|  |                 sensors.compat.so (Shim)                          |  |
|  |  1. Reads raw clear & ir from Xiaomi ALS (Type 33171111)          |  |
|  |  2. Reads active OLED backlight from sysfs                        |  |
|  |  3. Evaluates optical leakage: leak_c, leak_ir = k_cali(bl)       |  |
|  |  4. Net counts: c_net = clear - leak_c; ir_net = ir - leak_ir     |  |
|  |  5. Computes calibrated lux; drops stale 1.0 dummy readings       |  |
|  |  6. Synthesizes standard SENSOR_TYPE_LIGHT event                  |  |
|  +------------------------------^------------------------------------+  |
|                                 |                                       |
|  +------------------------------+------------------------------------+  |
|  |           Genuine Hardware Vendor HAL (e.g. MediaTek)             |  |
|  +------------------------------^------------------------------------+  |
+---------------------------------|---------------------------------------+
                                  |
                        +---------+---------+
                        |  Physical Sensors |
                        +-------------------+
```

---

## 3. Optical Leakage Compensation Algorithm

The display optical leakage is characterized by polynomial interpolation against the active kernel backlight level ($BL \in [0, 4095]$):

$$\text{Leak}_{\text{clear}} = f_{\text{clear}}(BL), \quad \text{Leak}_{\text{IR}} = f_{\text{IR}}(BL)$$

$$\text{Net}_{\text{clear}} = \max(0, \text{Raw}_{\text{clear}} - \text{Leak}_{\text{clear}})$$

$$\text{Net}_{\text{IR}} = \max(0, \text{Raw}_{\text{IR}} - \text{Leak}_{\text{IR}})$$

If $\text{Net}_{\text{clear}} \ge 80.0$, the ambient illuminance (lux) is computed based on the infrared-to-clear ratio:

$$\text{Ratio} = \frac{\text{Net}_{\text{IR}}}{\text{Net}_{\text{clear}} + 0.001}$$

For $\text{Ratio} < 4.9$:
$$\text{Lux} = \max\left((\text{Net}_{\text{clear}} - 0.265 \cdot \text{Net}_{\text{IR}}) \cdot S, \, (0.797 \cdot \text{Net}_{\text{clear}} - 0.160 \cdot \text{Net}_{\text{IR}}) \cdot S\right)$$

Where $S = \frac{79.832}{6400.0}$. If $\text{Ratio} \ge 4.9$:
$$\text{Lux} = (\text{Net}_{\text{clear}} \cdot 0.10) \cdot S$$

If $\text{Net}_{\text{clear}} < 80.0$, $\text{Lux} = 0.0$ (true darkness).

---

## 4. Framework Spline Evaluation

When `DisplayPowerController` receives the synthesized lux event:
1. It validates the lux against `brighteningLightDebounceMillis` (400 ms) or `darkeningLightDebounceMillis` (300 ms).
2. It looks up the target nit level from `display_brightness_config_common.xml`.
3. It maps target nits to a panel backlight level using `display_brightness_config_default.xml` (`min="1"`, `max="8191"`).
4. It ramps the backlight using the high-rate transition parameters (`screenBrightnessRampFastDecrease=1.00`, max 1200 ms).
