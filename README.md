# ColorOS Display & Auto-Brightness Compatibility Framework

Production-grade compatibility framework to resolve broken, sluggish, or erratic auto-brightness and brightness mapping issues on non-OPPO devices ported to ColorOS (Android 15 / ColorOS 15/17).

Designed specifically for ROM porters working with **unpacked partition images** (`vendor.img`, `system_ext.img`) and live on-device testing.

---

## Companion Sensor Repository

> [!IMPORTANT]
> **Sensor HAL Separation**: On the POCO X7 Pro (`rodin`), raw front ambient light sensor events (`als_raw` type 33171111) are bridged into `android.sensor.light` (handle 5) and filtered for under-display OLED optical leakage by the unified sensor HAL (`sensors.mt6899.so`).
> 
> To prevent binary overwrite conflicts on `/vendor/lib64/hw/sensors.mt6899.so`, the sensor HAL is maintained in the dedicated repository:
> - **[NEESCHAL-3/NEESS-COS-AOD-FOD-FIX](https://github.com/NEESCHAL-3/NEESS-COS-AOD-FOD-FIX)**
>
> Pair this display framework with `NEESS-COS-AOD-FOD-FIX` for full end-to-end auto-brightness, AOD, and in-display fingerprint functionality.

---

## The Problems on ColorOS Ports

When ColorOS is ported from stock OPPO/OnePlus hardware to third-party devices (such as POCO, Xiaomi, Realme, or Vivo), display brightness fails due to three distinct framework incompatibilities:

1. **64-bit Display Hardware ID Mismatch**: Android 15 `DisplayDeviceConfig` fails to match the panel, falling back to an 8-second darkening debounce and 0.23 ramp speed.
2. **Hardcoded Minimum Brightness Floor (222 vs 1)**: Stock ColorOS enforces a floor clamp at level 222 (~100 nits), preventing the screen from dimming down to true 2.0 nits at night.
3. **Missing Display Remap Properties**: ColorOS UI dimming and remap interpolation are disabled by default on foreign vendor trees.

---

## Solution Components

* **DisplayDeviceConfig XML (`display_id_<ID>.xml`)**: Implements responsive 400ms/300ms debouncing, 1.00 ramp rates, and OLED nit spline mappings.
* **ColorOS Brightness Splines (`display_brightness_config_*.xml`)**: Adjusts the level table floor from 222 to 1, unlocking true 2.0-nit night mode.
* **Display Properties (`display_brightness.prop`)**: Configures display remap tolerances, UI dimming interpolation, and MediaTek silky brightness.
* **Image Baking & Deployment Tooling**: Automated script to copy files directly into unpacked partition directories, physical display ID query tool, and live root ADB installer.

---

## Repository Structure

```
coloros-display-autobrightness-fix/
├── displayconfig/
│   ├── display_id_4627039422300187648.xml     # Reference DisplayDeviceConfig for POCO X7 Pro (rodin)
│   ├── display_id_template.xml                # Parameterized template for other displays
│   ├── display_brightness_config_default.xml  # Patched 1-8191 level table (floor min=1)
│   └── display_brightness_config_common.xml   # Lux-to-nits mapping tables
├── props/
│   └── display_brightness.prop                # Essential display remap & sensor report properties
├── scripts/
│   ├── bake_unpacked_images.sh                # Bake fixes directly into unpacked partition images
│   ├── get_physical_display_id.sh             # Query 64-bit physical display ID via ADB
│   ├── test_ambient_lux.sh                    # Live monitoring of backlight and lux
│   └── deploy_partition.sh                    # Direct on-partition deployment via ADB
├── sepolicy/
│   └── README_SEPOLICY.md                     # SELinux contexts for Enforcing mode
├── ARCHITECTURE.md                            # Detailed technical design & spline analysis
├── PORTING_GUIDE.md                           # Step-by-step guide for unpacked partition images
└── LICENSE                                    # Apache 2.0 License
```

---

## Where to Place Files in Unpacked ROM Images

ColorOS ports are typically prepared by unpacking `.img` partition files. Place the files into the following paths:

### 1. In Unpacked `vendor.img`:
* **Display Config**: `vendor/etc/displayconfig/display_id_<PHYSICAL_ID>.xml`
  * Permissions: `0644`
  * SELinux context: `u:object_r:vendor_configs_file:s0`

### 2. In Unpacked `system_ext.img`:
* **Default Table**: `system_ext/etc/display_brightness_config_default.xml`
  * Permissions: `0644`
  * SELinux context: `u:object_r:system_file:s0`
* **Common Table**: `system_ext/etc/display_brightness_config_common.xml`
  * Permissions: `0644`
  * SELinux context: `u:object_r:system_file:s0`

### 3. In Unpacked `odm.img` (or `vendor.img`):
* Append `props/display_brightness.prop` to `/odm/etc/build.prop`.

### Automated Baking:
```bash
# Bake directly into unpacked ROM workspace
./scripts/bake_unpacked_images.sh /path/to/unpacked_rom [PHYSICAL_DISPLAY_ID]
```

---

## Live Testing on Connected Device (Without Repacking)

1. Connect your device with ADB root/su authorized.
2. Deploy directly:
   ```bash
   ./scripts/deploy_partition.sh displayconfig/display_id_4627039422300187648.xml
   ```
3. Reboot to load the new spline and display configs:
   ```bash
   adb reboot
   ```
4. Verify real-time ambient lux tracking:
   ```bash
   ./scripts/test_ambient_lux.sh
   ```

---

## Documentation

* Refer to [ARCHITECTURE.md](ARCHITECTURE.md) for framework spline analysis, debounce mechanics, and nit curves.
* Refer to [PORTING_GUIDE.md](PORTING_GUIDE.md) for full instructions on unpacking, repacking, and customizing for other devices.
* Refer to [sepolicy/README_SEPOLICY.md](sepolicy/README_SEPOLICY.md) for SELinux Enforcing compliance.

---

## License

Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE) for details.
