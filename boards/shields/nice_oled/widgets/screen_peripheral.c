#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/usb.h>

#include "animation.h"
#include "battery.h"
#include "output.h"
#include "screen_peripheral.h"
#include "util.h"
#include <fonts.h>

#if IS_ENABLED(CONFIG_NICE_OLED_WIDGET_ANIMATION_PERIPHERAL_SMART_BATTERY)
#include <zmk/events/position_state_changed.h>

// Pre-rotated bongo cat frames (assets/bongo_rot.c) drawn on the canvas.
// The cat slaps (alternating paws) on every local key press of this half and
// returns to idle via an LVGL one-shot timer.
LV_IMG_DECLARE(bongo_rot_idle);
LV_IMG_DECLARE(bongo_rot_tap_l);
LV_IMG_DECLARE(bongo_rot_tap_r);

#define BONGO_X 9   // (68 - 50) / 2
#define BONGO_Y 100
#define BONGO_IDLE_MS 400

// 0 = idle, 1 = left slap, 2 = right slap
static uint8_t bongo_frame;
static lv_timer_t *bongo_idle_timer;
#endif

#if IS_ENABLED(CONFIG_ZMK_SPLIT_PERIPHERAL_HID_INDICATORS)
#include <zmk/events/hid_indicators_changed.h>

static bool caps_active;
#endif

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

/**
 * sleep status
 **/

#if IS_ENABLED(CONFIG_NICE_OLED_SHOW_SLEEP_ART_ON_IDLE) ||                                         \
    IS_ENABLED(CONFIG_NICE_OLED_SHOW_SLEEP_ART_ON_SLEEP)
#include "sleep_status.h"
static struct zmk_widget_sleep_status sleep_status_widget;
#endif

/**
 * luna
 **/

#if IS_ENABLED(CONFIG_NICE_OLED_WIDGET_ANIMATION_PERIPHERAL_WPM)
#include "luna.h"
static struct zmk_widget_luna luna_widget;
#endif

/**
 * Draw canvas
 **/

static void draw_canvas(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 0);

    // Draw widgets
    draw_background(canvas);
    draw_output_status(canvas, state);
    draw_battery_status(canvas, state);

#if IS_ENABLED(CONFIG_NICE_OLED_WIDGET_ANIMATION_PERIPHERAL_SMART_BATTERY)
    {
        lv_draw_img_dsc_t img_dsc;
        lv_draw_img_dsc_init(&img_dsc);
        const lv_img_dsc_t *frame = bongo_frame == 1   ? &bongo_rot_tap_l
                                    : bongo_frame == 2 ? &bongo_rot_tap_r
                                                       : &bongo_rot_idle;
        lv_canvas_draw_img(canvas, BONGO_X, BONGO_Y, frame, &img_dsc);
    }
#endif

#if IS_ENABLED(CONFIG_ZMK_SPLIT_PERIPHERAL_HID_INDICATORS)
    if (caps_active) {
        lv_draw_label_dsc_t caps_dsc;
        init_label_dsc(&caps_dsc, LVGL_FOREGROUND, &pixel_operator_mono_16, LV_TEXT_ALIGN_LEFT);
        lv_canvas_draw_text(canvas, 0, 36, 68, &caps_dsc, "CAPS");
    }
#endif

    // Rotate for horizontal display
    rotate_canvas(canvas, cbuf);
}

/**
 * Battery status
 **/

static void set_battery_status(struct zmk_widget_screen *widget,
                               struct battery_status_state state) {

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    widget->state.charging = state.usb_present;
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

    widget->state.battery = state.level;

    draw_canvas(widget->obj, widget->cbuf, &widget->state);

    // draw_animation(widget->obj, widget);

    // Charging is indicated by the bolt next to BAT; the canvas bongo cat
    // replaces the old animimg crystal (which leaked one object per event).
}

static void battery_status_update_cb(struct battery_status_state state) {
    struct zmk_widget_screen *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_battery_status(widget, state); }
}

static struct battery_status_state battery_status_get_state(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);

    return (struct battery_status_state){
        .level = (ev != NULL) ? ev->state_of_charge : zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_status, struct battery_status_state,
                            battery_status_update_cb, battery_status_get_state);

ZMK_SUBSCRIPTION(widget_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_usb_conn_state_changed);
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

/**
 * Peripheral status
 **/

static struct peripheral_status_state get_state(const zmk_event_t *_eh) {
    return (struct peripheral_status_state){.connected = zmk_split_bt_peripheral_is_connected()};
}

static void set_connection_status(struct zmk_widget_screen *widget,
                                  struct peripheral_status_state state) {
    widget->state.connected = state.connected;

    draw_canvas(widget->obj, widget->cbuf, &widget->state);
}

static void output_status_update_cb(struct peripheral_status_state state) {
    struct zmk_widget_screen *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_connection_status(widget, state); }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_peripheral_status, struct peripheral_status_state,
                            output_status_update_cb, get_state)
ZMK_SUBSCRIPTION(widget_peripheral_status, zmk_split_peripheral_status_changed);

#if IS_ENABLED(CONFIG_NICE_OLED_WIDGET_ANIMATION_PERIPHERAL_SMART_BATTERY)
/**
 * Bongo cat typing slaps (local key positions of this half only)
 **/

static void bongo_redraw_all(void) {
    struct zmk_widget_screen *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        draw_canvas(widget->obj, widget->cbuf, &widget->state);
    }
}

static void bongo_idle_timer_cb(lv_timer_t *timer) {
    bongo_frame = 0;
    lv_timer_pause(timer);
    bongo_redraw_all();
}

struct bongo_slap_state {
    bool pressed;
};

static struct bongo_slap_state bongo_slap_get_state(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    return (struct bongo_slap_state){.pressed = ev != NULL && ev->state};
}

static void bongo_slap_update_cb(struct bongo_slap_state state) {
    if (!state.pressed) {
        return;
    }
    bongo_frame = (bongo_frame == 1) ? 2 : 1;
    if (bongo_idle_timer != NULL) {
        lv_timer_reset(bongo_idle_timer);
        lv_timer_resume(bongo_idle_timer);
    }
    bongo_redraw_all();
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_bongo_slap, struct bongo_slap_state,
                            bongo_slap_update_cb, bongo_slap_get_state)
ZMK_SUBSCRIPTION(widget_bongo_slap, zmk_position_state_changed);
#endif // CONFIG_NICE_OLED_WIDGET_ANIMATION_PERIPHERAL_SMART_BATTERY

#if IS_ENABLED(CONFIG_ZMK_SPLIT_PERIPHERAL_HID_INDICATORS)
/**
 * Caps lock (HID indicator state relayed from the central by ZMK)
 **/

struct peripheral_caps_state {
    bool caps;
};

static struct peripheral_caps_state peripheral_caps_get_state(const zmk_event_t *eh) {
    const struct zmk_hid_indicators_changed *ev = as_zmk_hid_indicators_changed(eh);
    return (struct peripheral_caps_state){.caps = ev != NULL && (ev->indicators & BIT(1))};
}

static void peripheral_caps_update_cb(struct peripheral_caps_state state) {
    caps_active = state.caps;
    struct zmk_widget_screen *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        draw_canvas(widget->obj, widget->cbuf, &widget->state);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_peripheral_caps, struct peripheral_caps_state,
                            peripheral_caps_update_cb, peripheral_caps_get_state)
ZMK_SUBSCRIPTION(widget_peripheral_caps, zmk_hid_indicators_changed);
#endif // CONFIG_ZMK_SPLIT_PERIPHERAL_HID_INDICATORS

/**
 * Initialization
 **/

int zmk_widget_screen_init(struct zmk_widget_screen *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, CANVAS_HEIGHT, CANVAS_WIDTH);

    lv_obj_t *canvas = lv_canvas_create(widget->obj);
    lv_obj_align(canvas, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_canvas_set_buffer(canvas, widget->cbuf, CANVAS_HEIGHT, CANVAS_HEIGHT, LV_IMG_CF_TRUE_COLOR);

    sys_slist_append(&widgets, &widget->node);

#if !IS_ENABLED(CONFIG_NICE_OLED_WIDGET_ANIMATION_PERIPHERAL_SMART_BATTERY)
    draw_animation(canvas, widget);
#endif
    widget_battery_status_init();
    widget_peripheral_status_init();
#if IS_ENABLED(CONFIG_NICE_OLED_WIDGET_ANIMATION_PERIPHERAL_SMART_BATTERY)
    bongo_idle_timer = lv_timer_create(bongo_idle_timer_cb, BONGO_IDLE_MS, NULL);
    lv_timer_pause(bongo_idle_timer);
    widget_bongo_slap_init();
#endif
#if IS_ENABLED(CONFIG_ZMK_SPLIT_PERIPHERAL_HID_INDICATORS)
    widget_peripheral_caps_init();
#endif

#if IS_ENABLED(CONFIG_NICE_OLED_WIDGET_ANIMATION_PERIPHERAL_WPM)
    zmk_widget_luna_init(&luna_widget, canvas);
    lv_obj_align(zmk_widget_luna_obj(&luna_widget), LV_ALIGN_TOP_LEFT, CONFIG_NICE_OLED_WIDGET_LUNA_CUSTOM_X, CONFIG_NICE_OLED_WIDGET_LUNA_CUSTOM_Y);
#endif

#if IS_ENABLED(CONFIG_NICE_OLED_SHOW_SLEEP_ART_ON_IDLE) ||                                         \
    IS_ENABLED(CONFIG_NICE_OLED_SHOW_SLEEP_ART_ON_SLEEP)
    zmk_widget_sleep_status_init(&sleep_status_widget, canvas);
    lv_obj_align(zmk_widget_sleep_status_obj(&sleep_status_widget), LV_ALIGN_TOP_LEFT, CONFIG_NICE_OLED_WIDGET_SLEEP_STATUS_CUSTOM_X, CONFIG_NICE_OLED_WIDGET_SLEEP_STATUS_CUSTOM_Y);
#endif

    return 0;
}

lv_obj_t *zmk_widget_screen_obj(struct zmk_widget_screen *widget) { return widget->obj; }
