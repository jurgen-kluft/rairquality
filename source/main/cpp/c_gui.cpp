#include "rcore/c_timer.h"
#include "rcore/c_serial.h"
#include "rcore/c_str.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_lcd_types.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"

#include "rlcd/esp_lcd_touch.h"
#include "rlcd/c_dwo_amoled.h"

#include "rlvgl/lvgl.h"
#include "rlvgl/lvgl_private.h"
#include "rlvgl/private/misc/lv_color.h"

#include "rairquality/c_airquality.h"
#include "rairquality/ui.h"

// The pixel number in horizontal and vertical
#define EXAMPLE_LCD_H_RES 410
#define EXAMPLE_LCD_V_RES 502

#define EXAMPLE_LVGL_TICK_PERIOD_MS    2
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 1
#define EXAMPLE_LVGL_TASK_STACK_SIZE   (4 * 1024)
#define EXAMPLE_LVGL_TASK_PRIORITY     2

static uint32_t my_tick_cb(void) { return ncore::ntimer::millis(); }

static void my_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    if (disp == nullptr || area == nullptr || px_map == nullptr || disp->user_data == nullptr)
    {
        ncore::nserial::println("my_flush_cb, some parameter is null");
        return;
    }
    //ncore::nserial::println("my_flush_cb:begin");

    /*Write px_map to the area->x1, area->x2, area->y1, area->y2 area of the
     *frame buffer or external display controller. */
    const int offsetx1 = area->x1;  //+ 0x16;
    const int offsetx2 = area->x2;  //+ 0x16;
    const int offsety1 = area->y1;
    const int offsety2 = area->y2;

    // copy a buffer's content to a specific area of the display
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)disp->user_data;
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, px_map);

    //ncore::nserial::println("my_flush_cb:end");
}

static void my_amoled_flush_ready(void *user_ctx)
{
    if (user_ctx == nullptr)
        return;
    //ncore::nserial::println("my_amoled_flush_cb:begin");
    lv_display_t *display = *((lv_display_t **)user_ctx);
    if (display != nullptr)
    {
        lv_disp_flush_ready(display);
        //ncore::nserial::println("my_amoled_flush_cb:end");
    }
}

static void my_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    // TODO this can move behind the amoled interface, we just need to get
    //      the touch x, y and count from there.
    ncore::nserial::println("my_touch_read_cb");
    data->state = LV_INDEV_STATE_RELEASED;
    return;

    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)indev->user_data;
    assert(tp);

    uint16_t tp_x;
    uint16_t tp_y;
    uint8_t  tp_cnt = 0;

    /* Read data from touch controller into memory */
    esp_lcd_touch_read_data(tp);

    /* Read data from touch controller */
    bool tp_pressed = esp_lcd_touch_get_coordinates(tp, &tp_x, &tp_y, NULL, &tp_cnt, 1);
    if (tp_pressed && tp_cnt > 0)
    {
        data->point.x = tp_x;
        data->point.y = tp_y;
        data->state   = LV_INDEV_STATE_PRESSED;
        // ESP_LOGD("AirQuality", "Touch position: %d,%d", tp_x, tp_y);
    }
    else
    {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static lv_display_t *display = nullptr;
static ndwo::user_notify_flush_ready_t my_flush_ready;

static lv_obj_t *label = nullptr;

void setup_lvgl()
{
    // Initialize the LCD panel to obtain the panel handle for later use
    ncore::nserial::println("init_dwo_display");

    my_flush_ready.m_cb = my_amoled_flush_ready;
    my_flush_ready.m_user_ctx = &display;

    ndwo::panel_handles_t panel_handles;
    if (!ndwo::init_dwo_display(EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES, LV_COLOR_DEPTH, &my_flush_ready, &panel_handles))
    {
        ncore::nserial::println("Failed to initialize the LCD panel");
        return;
    }

    ncore::nserial::println("lv_init");
    lv_init();

    /*Set millisecond-based tick source for LVGL so that it can track time.*/
    ncore::nserial::println("lv_tick_set_cb");
    lv_tick_set_cb(my_tick_cb);

    // Create a display where screens and widgets can be added
    ncore::nserial::println("lv_display_create");
    display = lv_display_create(EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);

    // allocate draw buffers used by LVGL
    // it's recommended to choose the size of the draw buffer(s) to be at least 1/10 screen sized
    ncore::nserial::println("Allocate LVGL draw buffers");
    const int32_t buf_size = (EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * sizeof(lv_color16_t)) / 8;
    lv_color16_t *buf1 = (lv_color16_t *)heap_caps_malloc(buf_size, MALLOC_CAP_DMA );
    assert(buf1);
    lv_color16_t *buf2 = (lv_color16_t *)heap_caps_malloc(buf_size, MALLOC_CAP_DMA );
    assert(buf2);

    // initialize LVGL draw buffers
    lv_display_set_buffers(display, buf1, buf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    // Add a callback that can flush the content from `buf` when it has been rendered
    lv_display_set_flush_cb(display, my_flush_cb);
    lv_display_set_user_data(display, panel_handles.panel_handle);

    // Create an input device for touch handling
    // {
    //     lv_indev_t *indev = lv_indev_create();
    //     lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    //     lv_indev_set_read_cb(indev, my_touch_read_cb);
    //     lv_indev_set_user_data(indev, panel_handles.touch_handle);
    // }

    // Initialize the UI
    ui_init();

    /*The drivers are in place; now we can create the UI*/
    // label = lv_label_create(lv_screen_active());
    // lv_label_set_text(label, "Hello world");
    // lv_obj_center(label);
}

// static char labelText[64];
// static ncore::s32 tickCount = 0;

void loop_lvgl()
{
    // Execute the LVGL-related tasks in a loop
    lv_timer_handler();
    ncore::ntimer::delay(5);  // Wait a little to let the system breathe

    // ncore::str_t labelStr =    ncore::str_mutable(labelText, sizeof(labelText));
    // ncore::str_append(labelStr, "Hello world ");
    // ncore::to_str(labelStr, tickCount);
    // lv_label_set_text(label, labelStr.m_const);

    // tickCount = (tickCount + 1) & 0xffff;
}