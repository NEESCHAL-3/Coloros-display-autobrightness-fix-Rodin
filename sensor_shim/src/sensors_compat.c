/*
 * ColorOS Display & Auto-Brightness Sensor Compatibility Shim
 * 
 * Hardware Abstraction Layer (HAL) wrapper designed to restore responsive
 * and calibrated auto-brightness on non-OPPO hardware running ColorOS / OxygenOS ports.
 *
 * Primary capabilities:
 * 1. Under-Display OLED Optical Leakage Compensation:
 *    Under-display Ambient Light Sensors (ALS) register optical leakage emitted by the
 *    OLED panel itself. At high display backlight levels, this causes a runaway positive
 *    feedback loop. This shim reads the real-time panel backlight level from sysfs and
 *    subtracts calibrated clear and infrared leakage from the photodiode readings.
 *
 * 2. ALS Synthesis & Event Injection:
 *    Translates proprietary vendor raw ALS events (e.g. Xiaomi ALS raw type 33171111)
 *    into standard Android SENSOR_TYPE_LIGHT (type 5) events, bypassing ColorOS dual-sensor
 *    fusion assumptions and restoring instantaneous lux updates to DisplayPowerController.
 *
 * 3. Transparent Hardware Sensor Passthrough:
 *    All other physical sensors (accelerometer, gyroscope, proximity, magnetometer, etc.)
 *    are forwarded directly to the underlying vendor HAL without modification.
 */

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <android/log.h>

#define LOG_TAG "ColorOSAlsCompat"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)

/* Path to genuine underlying vendor sensor HAL */
#ifndef REAL_HAL_PATH
#define REAL_HAL_PATH "/vendor/lib64/hw/sensors.mediatek.V2.0.so"
#endif

/* Kernel backlight sysfs node for reading active OLED brightness */
#ifndef BACKLIGHT_SYSFS_PATH
#define BACKLIGHT_SYSFS_PATH "/sys/class/mi_display/disp-DSI-0/backlight"
#endif

#ifndef BACKLIGHT_FALLBACK_PATH
#define BACKLIGHT_FALLBACK_PATH "/sys/devices/virtual/mi_display/disp-DSI-0/backlight"
#endif

#define HW_MODULE_TAG 0x48574d54u

/* Sensor types */
#ifndef XIAOMI_ALS_RAW_TYPE
#define XIAOMI_ALS_RAW_TYPE   33171111
#endif

#define SENSOR_TYPE_LIGHT     5

typedef struct hw_module_t hw_module_t;
typedef struct hw_device_t hw_device_t;
typedef struct sensors_module_t sensors_module_t;
typedef struct sensor_dev_prefix sensor_dev_prefix;

typedef struct {
    int (*open)(const hw_module_t *, const char *, hw_device_t **);
} hw_module_methods_t;

struct hw_module_t {
    uint32_t tag;
    uint16_t module_api_version;
    uint16_t hal_api_version;
    const char *id;
    const char *name;
    const char *author;
    hw_module_methods_t *methods;
    void *dso;
    uint64_t reserved[25];
};

struct hw_device_t {
    uint32_t tag;
    uint32_t version;
    hw_module_t *module;
    uint64_t reserved[12];
    int (*close)(hw_device_t *);
};

typedef struct {
    const char *name;
    const char *vendor;
    int32_t version;
    int32_t handle;
    int32_t type;
    float maxRange;
    float resolution;
    float power;
    int32_t minDelay;
    uint32_t fifoReservedEventCount;
    uint32_t fifoMaxEventCount;
    const char *stringType;
    const char *requiredPermission;
    int64_t maxDelay;
    uint32_t flags;
    void *reserved[2];
} sensor_t;

typedef struct {
    int32_t version;
    int32_t sensor;
    int32_t type;
    int32_t reserved0;
    int64_t timestamp;
    float data[16];
    uint32_t flags;
    uint32_t reserved1[3];
} sensors_event_t;

struct sensors_module_t {
    hw_module_t common;
    int (*get_sensors_list)(const sensors_module_t *module, const sensor_t **list);
    int (*set_operation_mode)(uint32_t mode);
};

typedef int (*activate_fn)(sensor_dev_prefix *, int, int);
typedef int (*setdelay_fn)(sensor_dev_prefix *, int, int64_t);
typedef int (*poll_fn)(sensor_dev_prefix *, sensors_event_t *, int);
typedef int (*batch_fn)(sensor_dev_prefix *, int, int, int64_t, int64_t);
typedef int (*flush_fn)(sensor_dev_prefix *, int);
typedef int (*inject_fn)(sensor_dev_prefix *, const sensors_event_t *);

struct sensor_dev_prefix {
    hw_device_t common;
    activate_fn activate;
    setdelay_fn setDelay;
    poll_fn poll;
    batch_fn batch;
    flush_fn flush;
    inject_fn inject_sensor_data;
};

/*
 * Optical display leakage calibration table for under-display ambient light sensors.
 * Format: { Backlight Level, Clear Count Leakage, IR Count Leakage }
 * Calibrated against physical OLED panel emissions.
 */
struct cali_point {
    float bl;
    float clear;
    float ir;
};

static const struct cali_point k_cali[] = {
    { 0.0f,    0.0f,     0.0f },
    { 10.0f,   125.0f,   15.0f },
    { 50.0f,   547.0f,   71.0f },
    { 100.0f,  1071.0f,  137.0f },
    { 150.0f,  1573.0f,  198.0f },
    { 200.0f,  2108.0f,  269.0f },
    { 250.0f,  2619.0f,  331.0f },
    { 300.0f,  3151.0f,  400.0f },
    { 350.0f,  3520.0f,  446.0f },
    { 400.0f,  3914.0f,  497.0f },
    { 450.0f,  4333.0f,  549.0f },
    { 500.0f,  4779.0f,  605.0f },
    { 550.0f,  5294.0f,  670.0f },
    { 600.0f,  5833.0f,  739.0f },
    { 650.0f,  6422.0f,  814.0f },
    { 800.0f,  8297.0f,  1041.0f },
    { 950.0f,  9742.0f,  1224.0f },
    { 1100.0f, 11436.0f, 1433.0f },
    { 1250.0f, 13139.0f, 1651.0f },
    { 1400.0f, 14667.0f, 1838.0f },
    { 1550.0f, 16309.0f, 2045.0f },
    { 1700.0f, 18034.0f, 2259.0f },
    { 1850.0f, 19634.0f, 2457.0f },
    { 2047.0f, 21931.0f, 2740.0f },
    { 2300.0f, 25363.0f, 3171.0f },
    { 2600.0f, 30336.0f, 3766.0f },
};
#define CALI_LEN (sizeof(k_cali) / sizeof(k_cali[0]))

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static void *g_real_handle = NULL;
static const sensors_module_t *g_real_module = NULL;
static int (*g_orig_open)(const hw_module_t *, const char *, hw_device_t **) = NULL;
static int (*g_orig_close)(hw_device_t *) = NULL;
static activate_fn g_orig_activate = NULL;
static setdelay_fn g_orig_setDelay = NULL;
static poll_fn g_orig_poll = NULL;
static batch_fn g_orig_batch = NULL;
static flush_fn g_orig_flush = NULL;
static inject_fn g_orig_inject = NULL;

static sensor_dev_prefix *g_dev = NULL;
static sensor_t *g_sensor_list = NULL;
static int g_sensor_count = 0;
static int g_als_handle = -1;
static int g_als_raw_handle = -1;
static int g_als_enabled = 0;
static float g_last_lux = -1.0f;
static int64_t g_last_lux_report_ts = 0;

static int g_backlight_fd = -1;

#define PENDING_CAP 32
static sensors_event_t g_pending[PENDING_CAP];
static int g_pending_head = 0;
static int g_pending_count = 0;

static sensors_event_t *g_scratch = NULL;
static int g_scratch_cap = 0;

static float get_current_backlight(void) {
    if (g_backlight_fd < 0) {
        g_backlight_fd = open(BACKLIGHT_SYSFS_PATH, O_RDONLY | O_CLOEXEC);
        if (g_backlight_fd < 0) {
            g_backlight_fd = open(BACKLIGHT_FALLBACK_PATH, O_RDONLY | O_CLOEXEC);
        }
    }
    if (g_backlight_fd >= 0) {
        char buf[32];
        ssize_t n = pread(g_backlight_fd, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            buf[n] = '\0';
            return (float)atoi(buf);
        }
    }
    return 0.0f;
}

static void get_display_leakage(float bl, float *out_clear, float *out_ir) {
    if (bl <= 0.0f) {
        *out_clear = 0.0f;
        *out_ir = 0.0f;
        return;
    }
    if (bl >= k_cali[CALI_LEN - 1].bl) {
        float d_bl = bl - k_cali[CALI_LEN - 1].bl;
        *out_clear = k_cali[CALI_LEN - 1].clear + 16.577f * d_bl;
        *out_ir = k_cali[CALI_LEN - 1].ir + 1.983f * d_bl;
        return;
    }
    for (size_t i = 1; i < CALI_LEN; ++i) {
        if (bl <= k_cali[i].bl) {
            float frac = (bl - k_cali[i-1].bl) / (k_cali[i].bl - k_cali[i-1].bl);
            *out_clear = k_cali[i-1].clear + frac * (k_cali[i].clear - k_cali[i-1].clear);
            *out_ir = k_cali[i-1].ir + frac * (k_cali[i].ir - k_cali[i-1].ir);
            return;
        }
    }
    *out_clear = k_cali[CALI_LEN - 1].clear;
    *out_ir = k_cali[CALI_LEN - 1].ir;
}

static void queue_pending_locked(const sensors_event_t *ev) {
    if (g_pending_count >= PENDING_CAP) {
        LOGW("pending queue overflow, dropping event type=%d", ev->type);
        return;
    }
    int tail = (g_pending_head + g_pending_count) % PENDING_CAP;
    g_pending[tail] = *ev;
    g_pending_count++;
}

static int pop_pending_locked(sensors_event_t *out, int max_count) {
    int n = 0;
    while (g_pending_count > 0 && n < max_count) {
        out[n++] = g_pending[g_pending_head];
        g_pending_head = (g_pending_head + 1) % PENDING_CAP;
        g_pending_count--;
    }
    return n;
}

static int ensure_scratch(int required) {
    if (g_scratch_cap >= required) return 0;
    sensors_event_t *new_buf = (sensors_event_t *)realloc(g_scratch, sizeof(sensors_event_t) * required);
    if (!new_buf) {
        LOGE("failed to realloc scratch buffer size %d", required);
        return -1;
    }
    g_scratch = new_buf;
    g_scratch_cap = required;
    return 0;
}

static int ensure_real(void) {
    if (g_real_module) return 0;
    g_real_handle = dlopen(REAL_HAL_PATH, RTLD_NOW);
    if (!g_real_handle) {
        LOGE("failed to dlopen %s: %s", REAL_HAL_PATH, dlerror());
        return -1;
    }
    const sensors_module_t *mod = (const sensors_module_t *)dlsym(g_real_handle, "HMI");
    if (!mod) {
        LOGE("dlsym HMI failed from %s: %s", REAL_HAL_PATH, dlerror());
        dlclose(g_real_handle);
        g_real_handle = NULL;
        return -1;
    }
    g_real_module = mod;
    if (mod->common.methods && mod->common.methods->open) {
        g_orig_open = mod->common.methods->open;
    }
    LOGI("loaded real HAL %s successfully", REAL_HAL_PATH);
    return 0;
}

static void build_sensor_list_locked(void) {
    if (g_sensor_list) return;
    if (ensure_real() != 0) return;

    const sensor_t *real_list = NULL;
    int real_count = g_real_module->get_sensors_list(g_real_module, &real_list);
    if (real_count < 0) {
        LOGE("real get_sensors_list failed: %d", real_count);
        return;
    }

    g_sensor_list = (sensor_t *)malloc(sizeof(sensor_t) * (real_count + 1));
    if (!g_sensor_list) return;
    memcpy(g_sensor_list, real_list, sizeof(sensor_t) * real_count);
    g_sensor_count = real_count;

    for (int i = 0; i < real_count; ++i) {
        if (g_sensor_list[i].type == SENSOR_TYPE_LIGHT) {
            g_als_handle = g_sensor_list[i].handle;
        } else if (g_sensor_list[i].type == XIAOMI_ALS_RAW_TYPE) {
            g_als_raw_handle = g_sensor_list[i].handle;
        }
    }
    if (g_als_handle < 0) {
        g_als_handle = 5;
        sensor_t *als = &g_sensor_list[g_sensor_count++];
        memset(als, 0, sizeof(*als));
        als->name = "OLED Compensated Light Sensor";
        als->vendor = "ColorOS Port Shim";
        als->version = 1;
        als->handle = g_als_handle;
        als->type = SENSOR_TYPE_LIGHT;
        als->maxRange = 65535.0f;
        als->resolution = 1.0f;
        als->power = 0.15f;
        als->minDelay = 10000;
        als->stringType = "android.sensor.light";
        als->flags = 2; /* SENSOR_FLAG_ON_CHANGE_MODE */
    }
    LOGI("ALS registered: handle=%d, raw_handle=%d", g_als_handle, g_als_raw_handle);
}

static int compat_activate(sensor_dev_prefix *dev, int handle, int enabled) {
    pthread_mutex_lock(&g_lock);
    if (handle == g_als_handle) {
        g_als_enabled = enabled ? 1 : 0;
        if (!enabled) {
            g_last_lux = -1.0f;
            g_last_lux_report_ts = 0;
            if (g_backlight_fd >= 0) {
                close(g_backlight_fd);
                g_backlight_fd = -1;
            }
        }
        int target_raw = (g_als_raw_handle >= 0) ? g_als_raw_handle : handle;
        int rc = g_orig_activate ? g_orig_activate(dev, target_raw, enabled) : 0;
        pthread_mutex_unlock(&g_lock);
        return rc;
    }
    int rc = g_orig_activate ? g_orig_activate(dev, handle, enabled) : -EINVAL;
    pthread_mutex_unlock(&g_lock);
    return rc;
}

static int compat_setDelay(sensor_dev_prefix *dev, int handle, int64_t ns) {
    if (handle == g_als_handle && g_als_raw_handle >= 0) {
        handle = g_als_raw_handle;
    }
    return g_orig_setDelay ? g_orig_setDelay(dev, handle, ns) : 0;
}

static int compat_batch(sensor_dev_prefix *dev, int handle, int flags, int64_t sampling_period_ns, int64_t max_report_latency_ns) {
    if (handle == g_als_handle && g_als_raw_handle >= 0) {
        handle = g_als_raw_handle;
    }
    return g_orig_batch ? g_orig_batch(dev, handle, flags, sampling_period_ns, max_report_latency_ns) : 0;
}

static int compat_flush(sensor_dev_prefix *dev, int handle) {
    return g_orig_flush ? g_orig_flush(dev, handle) : 0;
}

static int compat_inject(sensor_dev_prefix *dev, const sensors_event_t *ev) {
    return g_orig_inject ? g_orig_inject(dev, ev) : -ENOSYS;
}

static int compat_poll(sensor_dev_prefix *dev, sensors_event_t *out, int count) {
    if (!out || count <= 0) return -EINVAL;

    pthread_mutex_lock(&g_lock);
    int popped = pop_pending_locked(out, count);
    if (popped > 0) {
        pthread_mutex_unlock(&g_lock);
        return popped;
    }
    pthread_mutex_unlock(&g_lock);

    if (ensure_scratch(count) != 0) return -ENOMEM;
    int n = g_orig_poll ? g_orig_poll(dev, g_scratch, count) : 0;
    if (n <= 0) return n;

    pthread_mutex_lock(&g_lock);
    int written = 0;

    for (int i = 0; i < n; ++i) {
        sensors_event_t ev = g_scratch[i];

        int is_als = (g_als_handle >= 0 && ev.sensor == g_als_handle);
        if (is_als) {
            /* Drop uncompensated raw/stale events from genuine HAL (e.g. boot-time 1.0) */
            continue;
        }

        int is_als_raw = ((g_als_raw_handle >= 0 && ev.sensor == g_als_raw_handle) ||
                          ev.type == XIAOMI_ALS_RAW_TYPE);
        if (is_als_raw) {
            float clear = ev.data[0];
            float ir = ev.data[1];

            float bl = get_current_backlight();
            float leak_c = 0.0f, leak_ir = 0.0f;
            get_display_leakage(bl, &leak_c, &leak_ir);

            float c_net = clear - leak_c;
            float ir_net = ir - leak_ir;
            if (c_net < 0.0f) c_net = 0.0f;
            if (ir_net < 0.0f) ir_net = 0.0f;

            float lux = 0.0f;
            if (c_net >= 80.0f) {
                float ratio = ir_net / (c_net + 0.001f);
                float scale = 79.832f / 6400.0f;
                if (ratio < 4.9f) {
                    float l1 = (c_net - 0.265f * ir_net) * scale;
                    float l2 = (0.797f * c_net - 0.160f * ir_net) * scale;
                    lux = (l1 > l2) ? l1 : l2;
                } else {
                    lux = (c_net * 0.10f) * scale;
                }
                if (lux < 0.0f) lux = 0.0f;
            }

            if (written < count) {
                out[written++] = ev;
            } else {
                queue_pending_locked(&ev);
            }

            if (g_als_enabled) {
                int64_t now_ts = ev.timestamp;
                float diff = fabsf(lux - g_last_lux);
                if (g_last_lux < 0.0f || diff >= 1.0f || (diff / (g_last_lux + 0.1f) >= 0.05f) ||
                    (lux == 0.0f && g_last_lux > 0.0f) ||
                    (now_ts - g_last_lux_report_ts >= 1000000000LL)) {

                    sensors_event_t syn;
                    memset(&syn, 0, sizeof(syn));
                    syn.version = sizeof(sensors_event_t);
                    syn.sensor = g_als_handle >= 0 ? g_als_handle : 5;
                    syn.type = SENSOR_TYPE_LIGHT;
                    syn.timestamp = now_ts;
                    syn.data[0] = lux;

                    g_last_lux = lux;
                    g_last_lux_report_ts = now_ts;

                    if (written < count) {
                        out[written++] = syn;
                    } else {
                        queue_pending_locked(&syn);
                    }

                    LOGI("synthesized front ALS lux=%.2f (c_net=%.1f ir_net=%.1f c_raw=%.1f bl=%.0f) -> handle=%d",
                         lux, c_net, ir_net, clear, bl, syn.sensor);
                }
            }
            continue;
        }

        /* Forward all other hardware sensors untouched */
        if (written < count) {
            out[written++] = ev;
        } else {
            queue_pending_locked(&ev);
        }
    }

    pthread_mutex_unlock(&g_lock);
    if (written == 0) {
        return compat_poll(dev, out, count);
    }
    return written;
}

static int compat_close(hw_device_t *hw) {
    sensor_dev_prefix *dev = (sensor_dev_prefix *)hw;
    pthread_mutex_lock(&g_lock);
    int (*orig_close)(hw_device_t *) = g_orig_close;
    if (dev == g_dev) {
        dev->common.close = g_orig_close;
        dev->activate = g_orig_activate;
        dev->setDelay = g_orig_setDelay;
        dev->poll = g_orig_poll;
        dev->batch = g_orig_batch;
        dev->flush = g_orig_flush;
        dev->inject_sensor_data = g_orig_inject;
        g_dev = NULL;
        g_als_enabled = 0;
        if (g_backlight_fd >= 0) {
            close(g_backlight_fd);
            g_backlight_fd = -1;
        }
    }
    pthread_mutex_unlock(&g_lock);
    if (orig_close) return orig_close(hw);
    return 0;
}

static int compat_open(const hw_module_t *module, const char *id, hw_device_t **device) {
    if (ensure_real() != 0) return -ENODEV;
    if (!g_orig_open) return -ENOSYS;

    hw_device_t *real_dev = NULL;
    int rc = g_orig_open((const hw_module_t *)g_real_module, id, &real_dev);
    if (rc != 0 || !real_dev) {
        LOGE("real open failed: %d", rc);
        return rc;
    }

    sensor_dev_prefix *dev = (sensor_dev_prefix *)real_dev;
    pthread_mutex_lock(&g_lock);
    g_dev = dev;
    g_orig_close = dev->common.close;
    g_orig_activate = dev->activate;
    g_orig_setDelay = dev->setDelay;
    g_orig_poll = dev->poll;
    g_orig_batch = dev->batch;
    g_orig_flush = dev->flush;
    g_orig_inject = dev->inject_sensor_data;

    dev->common.module = (hw_module_t *)module;
    dev->common.close = compat_close;
    dev->activate = compat_activate;
    dev->setDelay = compat_setDelay;
    dev->poll = compat_poll;
    dev->batch = compat_batch;
    dev->flush = compat_flush;
    dev->inject_sensor_data = compat_inject;

    *device = &dev->common;
    pthread_mutex_unlock(&g_lock);
    LOGI("sensor compat layer successfully attached to HAL device %s", id);
    return 0;
}

static int compat_get_sensors_list(const sensors_module_t *module, const sensor_t **list) {
    (void)module;
    pthread_mutex_lock(&g_lock);
    build_sensor_list_locked();
    *list = g_sensor_list;
    int count = g_sensor_count;
    pthread_mutex_unlock(&g_lock);
    return count;
}

static int compat_set_operation_mode(uint32_t mode) {
    if (ensure_real() != 0) return -ENODEV;
    if (g_real_module->set_operation_mode) {
        return g_real_module->set_operation_mode(mode);
    }
    return 0;
}

static hw_module_methods_t g_module_methods = {
    .open = compat_open,
};

const struct sensors_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HW_MODULE_TAG,
        .module_api_version = 1,
        .hal_api_version = 0,
        .id = "sensors",
        .name = "ColorOS Port Ambient Light & Sensor Shim",
        .author = "Neeschal / Antigravity",
        .methods = &g_module_methods,
        .dso = NULL,
        .reserved = {0},
    },
    .get_sensors_list = compat_get_sensors_list,
    .set_operation_mode = compat_set_operation_mode,
};
