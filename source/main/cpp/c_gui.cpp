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

#include "rairquality/c_airquality.h"
#include "rairquality/ui.h"

// The pixel number in horizontal and vertical
#define EXAMPLE_LCD_H_RES 410
#define EXAMPLE_LCD_V_RES 502

static void my_amoled_flush_ready(void *user_ctx)
{
    if (user_ctx == nullptr)
        return;
        
    // ncore::nserial::println("my_amoled_flush_cb:begin");
    //lv_display_t *display = *((lv_display_t **)user_ctx);
    //if (display != nullptr)
    {
        // lv_disp_flush_ready(display);
        // ncore::nserial::println("my_amoled_flush_cb:end");
    }
}

static ndwo::user_notify_flush_ready_t my_flush_ready;

void setup_display()
{
    // Initialize the LCD panel to obtain the panel handle for later use
    ncore::nserial::println("init_dwo_display");

    my_flush_ready.m_cb       = my_amoled_flush_ready;
    my_flush_ready.m_user_ctx = nullptr;

    ndwo::panel_handles_t panel_handles;
    if (!ndwo::init_dwo_display(EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES, 16, &my_flush_ready, &panel_handles))
    {
        ncore::nserial::println("Failed to initialize the LCD panel");
        return;
    }
}

void loop_display()
{
    // if received a new frame, flush it to the LCD panel
    bool flush = false;

    if (flush)
    {
        // Write px_map to the area->x1, area->x2, area->y1, area->y2 area of the
        // frame buffer or external display controller.
        const int offsetx1 = area->x1;  //+ 0x16;
        const int offsetx2 = area->x2;  //+ 0x16;
        const int offsety1 = area->y1;
        const int offsety2 = area->y2;

        // copy a buffer's content to a specific area of the display
        esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)disp->user_data;
        esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, px_map);
    }
}