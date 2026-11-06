# ColorOS Auto-Brightness & Spline Porting Guide (Unpacked Partition Images)

ColorOS ports are created by unpacking OEM partition images (`vendor.img`, `system_ext.img`, `odm.img`), injecting device-specific display configurations and brightness tables, and repacking them with `mkfs.erofs` or `lpmake`.

This guide walks through both:
1. **Baking directly into unpacked partition images** during ROM port creation.
2. **Live testing on a running device** via root ADB remount.

> [!IMPORTANT]
> **Sensor HAL Architecture**: This repository delivers DisplayDeviceConfig, spline tuning, and display properties. Sensor wrappers (such as `sensors.mt6899.so` which bridges `als_raw` photodiode events to `android.sensor.light` and handles AOD/FOD) belong to **[NEESS-COS-AOD-FOD-FIX](https://github.com/NEESCHAL-3/NEESS-COS-AOD-FOD-FIX)**. This clean separation prevents `/vendor/lib64/hw/sensors.mt6899.so` from being overwritten or conflicting.

---

## 1. Unpacked Partition Structure & File Destinations

When unpacking your target ColorOS ROM images, place the files into the following exact partition directories:

### A. Inside Unpacked `vendor.img`:
| Source File | Destination Inside Unpacked `vendor` | Permissions | SELinux Context |
|:---|:---|:---|:---|
| `displayconfig/display_id_<ID>.xml` | `vendor/etc/displayconfig/display_id_<ID>.xml` | `0644` (rw-r--r--) | `u:object_r:vendor_configs_file:s0` |

### B. Inside Unpacked `system_ext.img`:
| Source File | Destination Inside Unpacked `system_ext` | Permissions | SELinux Context |
|:---|:---|:---|:---|
| `displayconfig/display_brightness_config_default.xml` | `system_ext/etc/display_brightness_config_default.xml` | `0644` (rw-r--r--) | `u:object_r:system_file:s0` |
| `displayconfig/display_brightness_config_common.xml` | `system_ext/etc/display_brightness_config_common.xml` | `0644` (rw-r--r--) | `u:object_r:system_file:s0` |

### C. Inside Unpacked `odm.img` (or `vendor.img`):
Append the contents of `props/display_brightness.prop` to `/odm/etc/build.prop` (or `/vendor/build.prop`):
```properties
# Enable sensor brightness event reporting to ColorOS SensorDelegate
ro.vendor.oplus.sensor.report_brightness=true

# Enable smooth primary display UI dimming interpolation
vendor.display.enable_uidimming_primary=1
vendor.display.uir_support=1

# ColorOS Eye Care and Ambient Lux Adaptation
ro.oplus.display.smarteyecare.v5=1

# Display Remap Brightness Tolerances and Silky Brightness Interpolation
ro.vendor.display.RemapBrightness.ToleranceEnable=1
ro.vendor.display.RemapBrightness.ToleranceNit=500
ro.vendor.display.RemapBrightness.MinTolerancefac=1
ro.vendor.display.RemapBrightness.MaxTolerancefac=1.09
ro.vendor.display.RemapBrightness.MaxRemapSilkybrightness=16379

# MediaTek Silky Brightness HWC acceleration
persist.vendor.sys.pq.disp.gamma.silky.brightness=1
persist.vendor.sys.pq.disp_gamma_silky_brightness_by_hwc=1
```

---

## 2. Automated Baking into Unpacked Images

If you have unpacked your ROM images into a workspace directory (e.g. `~/coloros_work/vendor` and `~/coloros_work/system_ext`):

```bash
# Run the automated image bake script
./scripts/bake_unpacked_images.sh ~/coloros_work <PHYSICAL_DISPLAY_ID>
```

Example for POCO X7 Pro (`rodin`):
```bash
./scripts/bake_unpacked_images.sh ~/coloros_work 4627039422300187648
```

---

## 3. Repacking Partition Images & SELinux `file_contexts`

When repacking with `mkfs.erofs`, `make_ext4fs`, or ROM repacking tools (such as CRB, Carliv, or erofs-utils), ensure your partition's `file_contexts` and `fs_config` files define the proper contexts:

### In `vendor_file_contexts`:
```
/vendor/etc/displayconfig(/.*)?                   u:object_r:vendor_configs_file:s0
```

### In `system_ext_file_contexts`:
```
/system_ext/etc/display_brightness_config_.*\.xml u:object_r:system_file:s0
```

### In `fs_config`:
```
vendor/etc/displayconfig 0 0 755
vendor/etc/displayconfig/display_id_4627039422300187648.xml 0 0 644
system_ext/etc/display_brightness_config_default.xml 0 0 644
system_ext/etc/display_brightness_config_common.xml 0 0 644
```

---

## 4. How to Determine Your 64-bit Physical Display ID

Android 15 / ColorOS `DisplayDeviceConfig` loads panel parameters based on the 64-bit EDID hardware physical ID.

To retrieve this ID from a booted device:
```bash
./scripts/get_physical_display_id.sh
```

Or manually:
```bash
adb shell dumpsys display | grep -E "local:[0-9]+"
```
Example output:
```
DisplayDeviceInfo{"Built-in Screen": uniqueId="local:4627039422300187648", address StablePhysical{id=4627039422300187648, port=0}, ...}
```
Here, the ID is `4627039422300187648`. Name your config:
`display_id_4627039422300187648.xml`.

---

## 5. Live Testing on Connected Device (Without Repacking)

To test changes on a running device before repacking images:
```bash
# Push files to live partitions and set SELinux contexts
./scripts/deploy_partition.sh displayconfig/display_id_4627039422300187648.xml

# Reboot to reload DisplayDeviceConfig and spline tables
adb reboot

# Monitor live auto-brightness response
./scripts/test_ambient_lux.sh
```
