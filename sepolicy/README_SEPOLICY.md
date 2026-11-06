# SELinux Policy Requirements for ColorOS Display & Auto-Brightness

When running with SELinux in **Enforcing** mode (`permissive=0`), the sensor multihal service and system server require specific file permissions and object contexts to read the display backlight and access configuration files.

## 1. File Labels

The deployed files must have the following security contexts:

| File Path | SELinux Context | Description |
|:---|:---|:---|
| `/vendor/etc/displayconfig/display_id_*.xml` | `u:object_r:vendor_configs_file:s0` | Read by `system_server` via DisplayDeviceConfig |
| `/vendor/lib64/hw/sensors.*.so` | `u:object_r:vendor_file:s0` | Executed by `hal_sensors_default` |
| `/system_ext/etc/display_brightness_config_*.xml` | `u:object_r:system_file:s0` | Read by `system_server` via OplusDisplaySplineManager |

Apply labels manually via:
```bash
chcon u:object_r:vendor_configs_file:s0 /vendor/etc/displayconfig/display_id_*.xml
chcon u:object_r:vendor_file:s0 /vendor/lib64/hw/sensors.*.so
chcon u:object_r:system_file:s0 /system_ext/etc/display_brightness_config_*.xml
```

## 2. Sensor Multihal Backlight Access

The sensor shim reads the real-time panel backlight level from sysfs to perform display optical leakage subtraction:
- Primary node: `/sys/class/mi_display/disp-DSI-0/backlight`
- Fallback node: `/sys/devices/virtual/mi_display/disp-DSI-0/backlight`

### Required CIL Policy Rule
If `hal_sensors_default` does not already possess read access to the display sysfs class, add the following rule to your vendor or ODM policy (`odm_sepolicy.cil` / `vendor_sepolicy.cil`):

```cil
(allow hal_sensors_default sysfs_mi_display (dir (read open search getattr)))
(allow hal_sensors_default sysfs_mi_display (file (read open getattr)))
```

Or for generic leds backlight nodes:
```cil
(allow hal_sensors_default sysfs_leds (dir (read open search getattr)))
(allow hal_sensors_default sysfs_leds (file (read open getattr)))
```

Verify after cold reboot that `dmesg | grep -i "avc: denied"` produces 0 denials for `hal_sensors_default`.
