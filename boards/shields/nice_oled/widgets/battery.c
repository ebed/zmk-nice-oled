#include "battery.h"
#include <fonts.h>
#include <stdio.h>
#include <zephyr/kernel.h>

LV_IMG_DECLARE(bolt);

// Smart-battery mode: the peripheral art is drawn on the canvas in
// screen_peripheral.c (bongo cat reacting to local key presses). The old
// animimg-based crystal was removed: animation_smart_battery_on() leaked a
// new animimg on every battery event, leaving orphan spinning crystals.

static void draw_level(lv_obj_t *canvas, const struct status_state *state) {
    lv_draw_label_dsc_t label_right_dsc;
#if IS_ENABLED(CONFIG_NICE_EPAPER_ON)
    init_label_dsc(&label_right_dsc, LVGL_FOREGROUND, &pixel_operator_mono_16, LV_TEXT_ALIGN_RIGHT);
#else
    init_label_dsc(&label_right_dsc, LVGL_FOREGROUND, &pixel_operator_mono_16, LV_TEXT_ALIGN_LEFT);
#endif // CONFIG_NICE_EPAPER_ON

    char text[10] = {};

    sprintf(text, "%i%%", state->battery);
    // x, y, width, dsc, text
    lv_canvas_draw_text(canvas, CONFIG_NICE_OLED_WIDGET_BATTERY_CUSTOM_X, CONFIG_NICE_OLED_WIDGET_BATTERY_CUSTOM_Y, 42, &label_right_dsc, text);
}

static void draw_charging_level(lv_obj_t *canvas, const struct status_state *state) {
    lv_draw_img_dsc_t img_dsc;
    lv_draw_img_dsc_init(&img_dsc);
    lv_draw_label_dsc_t label_right_dsc;
#if IS_ENABLED(CONFIG_NICE_EPAPER_ON)
    init_label_dsc(&label_right_dsc, LVGL_FOREGROUND, &pixel_operator_mono_16, LV_TEXT_ALIGN_RIGHT);
#else
    init_label_dsc(&label_right_dsc, LVGL_FOREGROUND, &pixel_operator_mono_16, LV_TEXT_ALIGN_LEFT);
#endif // CONFIG_NICE_EPAPER_ON

    char text[10] = {};

    sprintf(text, "%i", state->battery);
    lv_canvas_draw_text(canvas, CONFIG_NICE_OLED_WIDGET_BATTERY_CUSTOM_X, CONFIG_NICE_OLED_WIDGET_BATTERY_CUSTOM_Y, 35, &label_right_dsc, text);
#if IS_ENABLED(CONFIG_NICE_EPAPER_ON)
    lv_canvas_draw_img(canvas, CONFIG_NICE_OLED_WIDGET_BATTERY_CUSTOM_X + 36, CONFIG_NICE_OLED_WIDGET_BATTERY_CUSTOM_Y + 2, &bolt, &img_dsc);
#else
    lv_canvas_draw_img(canvas, CONFIG_NICE_OLED_WIDGET_BATTERY_CUSTOM_X + 25, CONFIG_NICE_OLED_WIDGET_BATTERY_CUSTOM_Y, &bolt, &img_dsc);
#endif // CONFIG_NICE_EPAPER_ON
}

void draw_battery_status(lv_obj_t *canvas, const struct status_state *state) {
#if IS_ENABLED(CONFIG_NICE_EPAPER_ON)
    lv_draw_label_dsc_t label_left_dsc;
    init_label_dsc(&label_left_dsc, LVGL_FOREGROUND, &pixel_operator_mono_16, LV_TEXT_ALIGN_LEFT);
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    // Active BT profile instead of the static "BAT" label; the shared widget
    // state carries active_profile_index (updated by the output listener).
    // Peripheral halves never receive profile updates, so they keep "BAT".
    char profile_text[4] = {};
    snprintf(profile_text, sizeof(profile_text), "P%d", state->active_profile_index + 1);
    lv_canvas_draw_text(canvas, 0, 19, 25, &label_left_dsc, profile_text);
#else
    lv_canvas_draw_text(canvas, 0, 19, 25, &label_left_dsc, "BAT");
#endif
#endif // CONFIG_NICE_EPAPER_ON
    if (state->charging) {
        draw_charging_level(canvas, state);
    } else {
        draw_level(canvas, state);
    }
}
