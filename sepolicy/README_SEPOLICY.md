# SELinux Policy Requirements for ColorOS Display & Auto-Brightness

When running with SELinux in **Enforcing** mode (`permissive=0`), `system_server` requires standard file contexts to read the display configuration files and brightness splines.

## 1. File Labels

The deployed files must have the following security contexts:

| File Path | SELinux Context | Description |
|:---|:---|:---|
| `/vendor/etc/displayconfig/display_id_*.xml` | `u:object_r:vendor_configs_file:s0` | Read by `system_server` via DisplayDeviceConfig |
| `/system_ext/etc/display_brightness_config_*.xml` | `u:object_r:system_file:s0` | Read by `system_server` via OplusDisplaySplineManager |

Apply labels manually via:
```bash
chcon u:object_r:vendor_configs_file:s0 /vendor/etc/displayconfig/display_id_*.xml
chcon u:object_r:system_file:s0 /system_ext/etc/display_brightness_config_*.xml
```

When building or repacking images with `mkfs.erofs` or `make_ext4fs`, ensure your `file_contexts` define:
```text
/vendor/etc/displayconfig(/.*)?                   u:object_r:vendor_configs_file:s0
/system_ext/etc/display_brightness_config_.*\.xml u:object_r:system_file:s0
```

Verify after cold reboot that `dmesg | grep -i "avc: denied"` produces 0 denials for `system_server` reading these paths.
