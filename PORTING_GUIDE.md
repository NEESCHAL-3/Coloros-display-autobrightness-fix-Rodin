# ColorOS Auto-Brightness & Spline Porting Guide (Unpacked Partition Images)

ColorOS ports are created by unpacking OEM partition images (`vendor.img`, `system_ext.img`, `odm.img`, `product.img`), injecting device-specific shims and configurations, and repacking them with `mkfs.erofs` or `lpmake`.

This guide walks through both:
1. **Baking directly into unpacked partition images** during ROM port creation.
2. **Live testing on a running device** via root ADB remount.

---

## 1. Unpacked Partition Structure & File Destinations

When unpacking your target ColorOS ROM images, place the files into the following exact partition directories:

### A. Inside Unpacked `vendor.img`:
| Source File | Destination Inside Unpacked `vendor` | Permissions | SELinux Context |
|:---|:---|:---|:---|
| `displayconfig/display_id_<ID>.xml` | `vendor/etc/displayconfig/display_id_<ID>.xml` | `0644` (rw-r--r--) | `u:object_r:vendor_configs_file:s0` |
| `sensor_shim/build/sensors.<platform>.so` | `vendor/lib64/hw/sensors.<platform>.so` | `0644` (rw-r--r--) | `u:object_r:vendor_file:s0` |

### B. Inside Unpacked `system_ext.img`:
| Source File | Destination Inside Unpacked `system_ext` | Permissions | SELinux Context |
|:---|:---|:---|:---|
| `displayconfig/display_brightness_config_default.xml` | `system_ext/etc/display_brightness_config_default.xml` | `0644` (rw-r--r--) | `u:object_r:system_file:s0` |
| `displayconfig/display_brightness_config_common.xml` | `system_ext/etc/display_brightness_config_common.xml` | `0644` (rw-r--r--) | `u:object_r:system_file:s0` |

---

## 2. Automated Baking into Unpacked Images

If you have unpacked your ROM images into a workspace directory (e.g. `~/coloros_work/vendor` and `~/coloros_work/system_ext`):

```bash
# 1. Compile the sensor shim
cd sensor_shim
./build.sh
cd ..

# 2. Run the automated image bake script
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
/vendor/lib64/hw/sensors\..*\.so                 u:object_r:vendor_file:s0
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
vendor/lib64/hw/sensors.mt6899.so 0 0 644
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

## 5. Kernel Backlight Node Customization

If your target device exposes the kernel display backlight under a different sysfs path, customize `BACKLIGHT_SYSFS_PATH` in `sensor_shim/src/sensors_compat.c`:
* Xiaomi / MediaTek: `/sys/class/mi_display/disp-DSI-0/backlight`
* Qualcomm / Common: `/sys/class/backlight/panel0-backlight/brightness`
* Samsung: `/sys/class/lcd/panel/device/backlight/panel/brightness`

---

## 6. Live Testing on Connected Device (Without Repacking)

To test changes on a running device before repacking images:
```bash
# Push files to live partitions and set SELinux contexts
./scripts/deploy_partition.sh displayconfig/display_id_4627039422300187648.xml

# Reboot to reload DisplayDeviceConfig and spline tables
adb reboot

# Monitor live auto-brightness response
./scripts/test_ambient_lux.sh
```
