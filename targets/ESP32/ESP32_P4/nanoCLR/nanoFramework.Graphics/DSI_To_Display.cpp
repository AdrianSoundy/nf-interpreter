//
// Copyright (c) .NET Foundation and Contributors
// See LICENSE file in the project root for full license information.
//

#include "DisplayInterface.h"
#include <nanoCLR_Interop.h>
#include <stdarg.h>
#include <stdio.h>

#include "esp_lcd_panel_ops.h"
//#include "esp_lcd_mipi_dsi.h"
//#include "hal/lcd_types.h"


#include "esp_check.h"
#include "esp_log.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_ldo_regulator.h"
//#include "freertos/FreeRTOS.h"
//#include "freertos/task.h"
//#include "driver/gpio.h"
#include "esp_lcd_generic.h"
//#include "i2c_bus.h"


#define BSP_ERROR_CHECK_RETURN_ERR(x) do { \
        esp_err_t err_rc_ = (x);            \
        if (unlikely(err_rc_ != ESP_OK)) {  \
            return err_rc_;                 \
        }                                   \
    } while(0)

// Backlight LEDC channel number 
// TODO pass this as a parameter to the DisplayInterfaceConfig
#define LCD_LEDC_CH            (ledc_channel_t)0 // LEDC channel 0


struct DisplayInterface g_DisplayInterface;
DisplayInterfaceConfig g_DisplayInterfaceConfig;

#define UNUSED(X) (void)X // To avoid gcc/g++ warnings

#define EXAMPLE_LCD_IO_RST  27  // Default value for reset GPIO number, set to -1 if not used
#define EXAMPLE_LCD_BIT_PER_PIXEL 16 // Default value for bits per pixel, can be 16, 18 or 24

#define GENERIC_CMD_PAGE (0xE0)
#define GENERIC_PAGE_USER (0x00)

#define GENERIC_CMD_DSI_INT0 (0x80)
#define GENERIC_DSI_1_LANE (0x00)
#define GENERIC_DSI_2_LANE (0x01)
#define GENERIC_DSI_3_LANE (0x10)
#define GENERIC_DSI_4_LANE (0x11)

#define GENERIC_CMD_GS_BIT (1 << 0)
#define GENERIC_CMD_SS_BIT (1 << 1)

#define EK79007_PAD_CONTROL     (0xB2)
#define EK79007_DSI_2_LANE      (0x10)
#define EK79007_DSI_4_LANE      (0x00)

#define EK79007_CMD_SHLR_BIT    (1ULL << 0)
#define EK79007_CMD_UPDN_BIT    (1ULL << 1)
#define EK79007_MDCTL_VALUE_DEFAULT   (0x01)

typedef struct
{
    esp_lcd_panel_io_handle_t io;
    int reset_gpio_num;
    uint8_t madctl_val; // save current value of LCD_CMD_MADCTL register
    uint8_t colmod_val; // save surrent value of LCD_CMD_COLMOD register
    const generic_lcd_init_cmd_t *init_cmds;
    uint16_t init_cmds_size;
    uint8_t lane_num;
    struct
    {
        unsigned int reset_level : 1;
    } flags;
    // To save the original functions of MIPI DPI panel
    esp_err_t (*del)(esp_lcd_panel_t *panel);
    esp_err_t (*init)(esp_lcd_panel_t *panel);
} generic_panel_t;

static const char *TAG = "DsiToDisplay";

static esp_err_t panel_generic_del(esp_lcd_panel_t *panel);
static esp_err_t panel_generic_init(esp_lcd_panel_t *panel);
static esp_err_t panel_generic_reset(esp_lcd_panel_t *panel);
static esp_err_t panel_generic_invert_color(esp_lcd_panel_t *panel, bool invert_color_data);
static esp_err_t panel_generic_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y);
static esp_err_t panel_generic_swap_xy(esp_lcd_panel_t *panel, bool swap_axes);
static esp_err_t panel_generic_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap);
static esp_err_t panel_generic_disp_on_off(esp_lcd_panel_t *panel, bool on_off);


esp_err_t display_brightness_init(int gpionum)
{
    // Setup LEDC peripheral for PWM backlight control
    ledc_channel_config_t LCD_backlight_channel = {};

    LCD_backlight_channel.gpio_num = gpionum,
    LCD_backlight_channel.speed_mode = LEDC_LOW_SPEED_MODE;
    LCD_backlight_channel.channel = (ledc_channel_t)LCD_LEDC_CH;
    LCD_backlight_channel.intr_type = LEDC_INTR_DISABLE;
    LCD_backlight_channel.timer_sel = (ledc_timer_t)1;
    LCD_backlight_channel.duty = 0;
    LCD_backlight_channel.hpoint = 0;

    ledc_timer_config_t LCD_backlight_timer = {};
    LCD_backlight_timer.speed_mode = LEDC_LOW_SPEED_MODE;
    LCD_backlight_timer.duty_resolution = LEDC_TIMER_10_BIT;
    LCD_backlight_timer.timer_num = (ledc_timer_t)1;
    LCD_backlight_timer.freq_hz = 5000;
    LCD_backlight_timer.clk_cfg = LEDC_AUTO_CLK;

    BSP_ERROR_CHECK_RETURN_ERR(ledc_timer_config(&LCD_backlight_timer));
    BSP_ERROR_CHECK_RETURN_ERR(ledc_channel_config(&LCD_backlight_channel));
    return ESP_OK;
}

esp_err_t display_brightness_set(int brightness_percent)
{
    if (brightness_percent > 100) {
        brightness_percent = 100;
    }
    if (brightness_percent < 0) {
        brightness_percent = 0;
    }

    ESP_LOGI(TAG, "Setting LCD backlight: %d%%", brightness_percent);
    uint32_t duty_cycle = (1023 * brightness_percent) / 100; // LEDC resolution set to 10bits, thus: 100% = 1023
    BSP_ERROR_CHECK_RETURN_ERR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CH, duty_cycle));
    BSP_ERROR_CHECK_RETURN_ERR(ledc_update_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CH));
    return ESP_OK;
}

esp_err_t esp_lcd_new_panel_generic(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config,
                                   esp_lcd_panel_handle_t *ret_panel)
{
    esp_lcd_panel_handle_t panel_handle = NULL;

    generic_vendor_config_t *vendor_config = (generic_vendor_config_t *)panel_dev_config->vendor_config;
    ESP_RETURN_ON_FALSE(vendor_config && vendor_config->mipi_config.dpi_config && vendor_config->mipi_config.dsi_bus, ESP_ERR_INVALID_ARG, TAG,
                        "invalid vendor config");

    esp_err_t ret = ESP_OK;
    generic_panel_t *generic = (generic_panel_t *)calloc(1, sizeof(generic_panel_t));
    ESP_RETURN_ON_FALSE(generic, ESP_ERR_NO_MEM, TAG, "no mem for generic panel");

    if (panel_dev_config->reset_gpio_num >= 0)
    {
        gpio_config_t io_conf = {};

        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num;
        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "configure GPIO for RST line failed");
    }

    switch (panel_dev_config->rgb_ele_order)
    {
    case LCD_RGB_ELEMENT_ORDER_RGB:
        generic->madctl_val = 0;
        break;
    case LCD_RGB_ELEMENT_ORDER_BGR:
        generic->madctl_val |= LCD_CMD_BGR_BIT;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported color space");
        break;
    }

    switch (panel_dev_config->bits_per_pixel)
    {
    case 16: // RGB565
        generic->colmod_val = 0x55;
        break;
    case 18: // RGB666
        generic->colmod_val = 0x66;
        break;
    case 24: // RGB888
        generic->colmod_val = 0x77;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported pixel width");
        break;
    }

    generic->io = io;
    generic->init_cmds = vendor_config->init_cmds;
    generic->init_cmds_size = vendor_config->init_cmds_size;
    generic->lane_num = vendor_config->mipi_config.lane_num;
    generic->reset_gpio_num = panel_dev_config->reset_gpio_num;
    generic->flags.reset_level = panel_dev_config->flags.reset_active_high;

/*
    Touch interface
      i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = 7,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = 8,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };

    i2c_bus_handle_t i2c0_bus = i2c_bus_create(I2C_NUM_1, &conf);
    i2c_bus_device_handle_t i2c0_device1 = i2c_bus_device_create(i2c0_bus, 0x45, 0);

    uint8_t data = 0x11;
    i2c_bus_write_bytes(i2c0_device1, 0x95, 1, &data);
    data = 0x17;
    i2c_bus_write_bytes(i2c0_device1, 0x95, 1, &data);
    data = 0x00;
    i2c_bus_write_bytes(i2c0_device1, 0x96, 1, &data);
    vTaskDelay(pdMS_TO_TICKS(100));
    data = 0xFF;
    i2c_bus_write_bytes(i2c0_device1, 0x96, 1, &data);

    i2c_bus_device_delete(&i2c0_device1);
    // i2c_bus_delete(&i2c0_bus);

    vTaskDelay(pdMS_TO_TICKS(1000));
*/

    // Create MIPI DPI panel
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_dpi(vendor_config->mipi_config.dsi_bus, vendor_config->mipi_config.dpi_config, &panel_handle), err, TAG,
                      "create MIPI DPI panel failed");
    ESP_LOGD(TAG, "new MIPI DPI panel @%p", panel_handle);

    // Save the original functions of MIPI DPI panel
    generic->del = panel_handle->del;
    generic->init = panel_handle->init;
    // Overwrite the functions of MIPI DPI panel
    panel_handle->del = panel_generic_del;
    panel_handle->init = panel_generic_init;
    panel_handle->reset = panel_generic_reset;
    panel_handle->mirror = panel_generic_mirror;
    panel_handle->swap_xy = panel_generic_swap_xy;
    panel_handle->set_gap = panel_generic_set_gap;
    panel_handle->invert_color = panel_generic_invert_color;
    panel_handle->disp_on_off = panel_generic_disp_on_off;
    panel_handle->user_data = generic;
    *ret_panel = panel_handle;
    ESP_LOGD(TAG, "new generic panel @%p", generic);

    return ESP_OK;

err:
    if (generic)
    {
        if (panel_dev_config->reset_gpio_num >= 0)
        {
            gpio_reset_pin((gpio_num_t)panel_dev_config->reset_gpio_num);
        }
        platform_free(generic);
    }
    return ret;
}

static esp_err_t panel_generic_del(esp_lcd_panel_t *panel)
{
    generic_panel_t *generic = (generic_panel_t *)panel->user_data;

    if (generic->reset_gpio_num >= 0)
    {
        gpio_reset_pin((gpio_num_t)generic->reset_gpio_num);
    }
    // Delete MIPI DPI panel
    generic->del(panel);

    ESP_LOGD(TAG, "del generic panel @%p", generic);
    platform_free(generic);

    return ESP_OK;
}

static const generic_lcd_init_cmd_t vendor_specific_init_default[] = {
//  {cmd, { data }, data_size, delay_ms}
    {0x80, (uint8_t []){0x8B}, 1, 0},
    {0x81, (uint8_t []){0x78}, 1, 0},
    {0x82, (uint8_t []){0x84}, 1, 0},
    {0x83, (uint8_t []){0x88}, 1, 0},
    {0x84, (uint8_t []){0xA8}, 1, 0},
    {0x85, (uint8_t []){0xE3}, 1, 0},
    {0x86, (uint8_t []){0x88}, 1, 0},
    {0x11, (uint8_t []){0x00}, 0, 120},
};

static esp_err_t panel_generic_send_init_cmds(generic_panel_t *generic)
{
    esp_lcd_panel_io_handle_t io = generic->io;
    const generic_lcd_init_cmd_t *init_cmds = NULL;
    uint16_t init_cmds_size = 0;
    uint8_t lane_command = EK79007_DSI_2_LANE;
    bool is_cmd_overwritten = false;

    ets_printf("panel_generic_send_init_cmds\n");

    switch (generic->lane_num) {
    case 0:
    case 2:
        lane_command = EK79007_DSI_2_LANE;
        break;
    case 4:
        lane_command = EK79007_DSI_4_LANE;
        break;
    default:
        ESP_LOGE(TAG, "Invalid lane number %d", generic->lane_num);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, EK79007_PAD_CONTROL, (uint8_t[]) {
        lane_command,
    }, 1), TAG, "send command failed");

    // vendor specific initialization, it can be different between manufacturers
    // should consult the LCD supplier for initialization sequence code
    if (generic->init_cmds) {
    ets_printf("generic->init_cmds\n");
        init_cmds = generic->init_cmds;
        init_cmds_size = generic->init_cmds_size;
    } else {
    ets_printf("vendor_specific_init_default\n");
        init_cmds = vendor_specific_init_default;
        init_cmds_size = sizeof(vendor_specific_init_default) / sizeof(generic_lcd_init_cmd_t);
    }

    for (int i = 0; i < init_cmds_size; i++) {
        // Check if the command has been used or conflicts with the internal
        if (init_cmds[i].data_bytes > 0) {
            switch (init_cmds[i].cmd) {
            case LCD_CMD_MADCTL:
                is_cmd_overwritten = true;
                generic->madctl_val = ((uint8_t *)init_cmds[i].data)[0];
                break;
            default:
                is_cmd_overwritten = false;
                break;
            }

            if (is_cmd_overwritten) {
                is_cmd_overwritten = false;
                ESP_LOGW(TAG, "The %02Xh command has been used and will be overwritten by external initialization sequence",
                         init_cmds[i].cmd);
            }
        }

        // Send command
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, init_cmds[i].cmd, init_cmds[i].data, init_cmds[i].data_bytes), TAG, "send command failed");
        vTaskDelay(pdMS_TO_TICKS(init_cmds[i].delay_ms));
    }

    ESP_LOGD(TAG, "send init commands success");

    return ESP_OK;
}


static esp_err_t panel_generic_init(esp_lcd_panel_t *panel)
{
    generic_panel_t *generic = (generic_panel_t *)panel->user_data;

    ESP_RETURN_ON_ERROR(panel_generic_send_init_cmds(generic), TAG, "send init commands failed");
    ESP_RETURN_ON_ERROR(generic->init(panel), TAG, "init MIPI DPI panel failed");

    return ESP_OK;
}

// static esp_err_t panel_generic_init(esp_lcd_panel_t *panel)
// {
//     generic_panel_t *generic = (generic_panel_t *)panel->user_data;
//     esp_lcd_panel_io_handle_t io = generic->io;
//     const generic_lcd_init_cmd_t *init_cmds = NULL;
//     uint16_t init_cmds_size = 0;
//     uint8_t lane_command = GENERIC_DSI_2_LANE;
//     bool is_user_set = true;
//     bool is_cmd_overwritten = false;
// ets_printf("panel_generic_init\n");
//     switch (generic->lane_num)
//     {
//     case 1:
//         lane_command = GENERIC_DSI_1_LANE;
//         break;
//     case 2:
//         lane_command = GENERIC_DSI_2_LANE;
//         break;
//     case 3:
//         lane_command = GENERIC_DSI_3_LANE;
//         break;
//     case 4:
//         lane_command = GENERIC_DSI_4_LANE;
//         break;
//     default:
//         ESP_LOGE(TAG, "Invalid lane number %d", generic->lane_num);
//         return ESP_ERR_INVALID_ARG;
//     }

//     ets_printf("panel_generic_init 2\n");

//     uint8_t ID[3];
//     ESP_RETURN_ON_ERROR(esp_lcd_panel_io_rx_param(io, 0x04, ID, 3), TAG, "read ID failed");
//     ESP_LOGI(TAG, "LCD ID: %02X %02X %02X", ID[0], ID[1], ID[2]);
//     ets_printf("panel_generic_init 3\n");


//     ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, GENERIC_CMD_PAGE, (uint8_t[]){GENERIC_PAGE_USER}, 1), TAG, "send command failed");
//     ets_printf("panel_generic_init 4\n");
//     ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, (uint8_t[]){
//                                                                           generic->madctl_val,
//                                                                       },
//                                                   1),
//                         TAG, "send command failed");
//     ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_COLMOD, (uint8_t[]){
//                                                                           generic->colmod_val,
//                                                                       },
//                                                   1),
//                         TAG, "send command failed");
//     ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, GENERIC_CMD_DSI_INT0, (uint8_t[]){
//                                                                                lane_command,
//                                                                            },
//                                                   1),
//                         TAG, "send command failed");

//     // vendor specific initialization, it can be different between manufacturers
//     // should consult the LCD supplier for initialization sequence code
//     if (generic->init_cmds)
//     {
//         init_cmds = generic->init_cmds;
//         init_cmds_size = generic->init_cmds_size;
//     }
//     else
//     {
//         init_cmds = vendor_specific_init_default;
//         init_cmds_size = sizeof(vendor_specific_init_default) / sizeof(generic_lcd_init_cmd_t);
//     }

//     for (int i = 0; i < init_cmds_size; i++)
//     {
//         // Check if the command has been used or conflicts with the internal
//         if (is_user_set && (init_cmds[i].data_bytes > 0))
//         {
//             switch (init_cmds[i].cmd)
//             {
//             case LCD_CMD_MADCTL:
//                 is_cmd_overwritten = true;
//                 generic->madctl_val = ((uint8_t *)init_cmds[i].data)[0];
//                 break;
//             case LCD_CMD_COLMOD:
//                 is_cmd_overwritten = true;
//                 generic->colmod_val = ((uint8_t *)init_cmds[i].data)[0];
//                 break;
//             default:
//                 is_cmd_overwritten = false;
//                 break;
//             }

//             if (is_cmd_overwritten)
//             {
//                 is_cmd_overwritten = false;
//                 ESP_LOGW(TAG, "The %02Xh command has been used and will be overwritten by external initialization sequence",
//                          init_cmds[i].cmd);
//             }
//         }

//         // Send command
//         ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, init_cmds[i].cmd, init_cmds[i].data, init_cmds[i].data_bytes), TAG, "send command failed");
//         vTaskDelay(pdMS_TO_TICKS(init_cmds[i].delay_ms));

//         // Check if the current cmd is the "page set" cmd
//         if ((init_cmds[i].cmd == GENERIC_CMD_PAGE) && (init_cmds[i].data_bytes > 0))
//         {
//             is_user_set = (((uint8_t *)init_cmds[i].data)[0] == GENERIC_PAGE_USER);
//         }
//     }
//     ESP_LOGD(TAG, "send init commands success");

//     ESP_RETURN_ON_ERROR(generic->init(panel), TAG, "init MIPI DPI panel failed");

//     return ESP_OK;
// }

static esp_err_t panel_generic_reset(esp_lcd_panel_t *panel)
{
    generic_panel_t *generic = (generic_panel_t *)panel->user_data;
    esp_lcd_panel_io_handle_t io = generic->io;

    // Perform hardware reset
    if (generic->reset_gpio_num >= 0)
    {
        gpio_set_level((gpio_num_t)generic->reset_gpio_num, !generic->flags.reset_level);
        vTaskDelay(pdMS_TO_TICKS(5));
        gpio_set_level((gpio_num_t)generic->reset_gpio_num, generic->flags.reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level((gpio_num_t)generic->reset_gpio_num, !generic->flags.reset_level);
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    else if (io)
    { // Perform software reset
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_SWRESET, NULL, 0), TAG, "send command failed");
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    return ESP_OK;
}

static esp_err_t panel_generic_invert_color(esp_lcd_panel_t *panel, bool invert_color_data)
{
    generic_panel_t *generic = (generic_panel_t *)panel->user_data;
    esp_lcd_panel_io_handle_t io = generic->io;
    uint8_t command = 0;

    ESP_RETURN_ON_FALSE(io, ESP_ERR_INVALID_STATE, TAG, "invalid panel IO");

    if (invert_color_data)
    {
        command = LCD_CMD_INVON;
    }
    else
    {
        command = LCD_CMD_INVOFF;
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), TAG, "send command failed");

    return ESP_OK;
}

static esp_err_t panel_generic_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y)
{
    generic_panel_t *generic = (generic_panel_t *)panel->user_data;
    esp_lcd_panel_io_handle_t io = generic->io;
    uint8_t madctl_val = generic->madctl_val;

    ESP_RETURN_ON_FALSE(io, ESP_ERR_INVALID_STATE, TAG, "invalid panel IO");

    // Control mirror through LCD command
    if (mirror_x)
    {
        madctl_val |= GENERIC_CMD_GS_BIT;
    }
    else
    {
        madctl_val &= ~GENERIC_CMD_GS_BIT;
    }
    if (mirror_y)
    {
        madctl_val |= GENERIC_CMD_SS_BIT;
    }
    else
    {
        madctl_val &= ~GENERIC_CMD_SS_BIT;
    }

    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, LCD_CMD_MADCTL, (uint8_t[]){madctl_val}, 1), TAG, "send command failed");
    generic->madctl_val = madctl_val;

    return ESP_OK;
}

static esp_err_t panel_generic_swap_xy(esp_lcd_panel_t *panel, bool swap_axes)
{
    ESP_LOGW(TAG, "swap_xy is not supported by this panel");
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t panel_generic_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap)
{
    ESP_LOGE(TAG, "set_gap is not supported by this panel");
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t panel_generic_disp_on_off(esp_lcd_panel_t *panel, bool on_off)
{
    generic_panel_t *generic = (generic_panel_t *)panel->user_data;
    esp_lcd_panel_io_handle_t io = generic->io;
    int command = 0;

    if (on_off)
    {
        command = LCD_CMD_DISPON;
    }
    else
    {
        command = LCD_CMD_DISPOFF;
    }
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(io, command, NULL, 0), TAG, "send command failed");
    return ESP_OK;
}






#define EXAMPLE_MIPI_DSI_DPI_CLK_MHZ  48

// Default to landscape 880 x 400
CLR_UINT32 lcd_x_size = 800;
CLR_UINT32 lcd_y_size = 480;

static esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
static esp_lcd_panel_io_handle_t mipi_dbi_io = NULL;
static esp_lcd_panel_handle_t mipi_dpi_panel = NULL;

//void WaitForFIFOEmpty();

extern CLR_UINT32 graphicsStartingAddress; // Framebuffer set externally


#define EXAMPLE_MIPI_DSI_PHY_PWR_LDO_CHAN       3  // LDO_VO3 is connected to VDD_MIPI_DPHY
#define EXAMPLE_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV 2500

void DisplayInterface::Initialize(DisplayInterfaceConfig &config)
{
    g_DisplayInterfaceConfig = config;

    ESP_LOGI(TAG, "DisplayInterface::Initialize");

    if (config.Dsi.backlight >= 0)
    {
        display_brightness_init(config.Dsi.backlight);
        ESP_LOGI(TAG, "Display brightness initialized");
    }

    // Turn on the power for MIPI DSI PHY, go from "No Power" state to "Shutdown" state
    esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
    esp_ldo_channel_config_t ldo_mipi_phy_config = { };

    ldo_mipi_phy_config.chan_id = EXAMPLE_MIPI_DSI_PHY_PWR_LDO_CHAN;
    ldo_mipi_phy_config.voltage_mv = EXAMPLE_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV;
    
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_mipi_phy_config, &ldo_mipi_phy));
    ESP_LOGI(TAG, "MIPI DSI PHY Powered on");

    ESP_LOGI(TAG, "Install MIPI DSI Bus");

    // create DSI bus first
    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = config.Dsi.numLanes, 
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps =  (float)config.Dsi.laneBitrateMbps
    };
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus));

    ESP_LOGI(TAG, "Install MIPI DSI LCD control IO");

    // DBI interface is used to send LCD commands and parameters
    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,   // according to the LCD spec
        .lcd_param_bits = 8, // according to the LCD spec
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &mipi_dbi_io));

    ESP_LOGI(TAG, "Install MIPI DSI LCD generic data panel");
    const esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = EXAMPLE_MIPI_DSI_DPI_CLK_MHZ,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565, 
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .out_color_format = LCD_COLOR_FMT_RGB565,     
        .num_fbs = 1, 
        .video_timing = {
            .h_size = config.Dsi.horizontal_resolution,
            .v_size = config.Dsi.vertical_resolution,
            .hsync_pulse_width = config.Dsi.hsync_pulse_width,
            .hsync_back_porch = config.Dsi.hsync_back_porch, 
            .hsync_front_porch = config.Dsi.hsync_front_porch,
            .vsync_pulse_width = config.Dsi.vsync_pulse_width,
            .vsync_back_porch = config.Dsi.vsync_back_porch,
            .vsync_front_porch = config.Dsi.vsync_front_porch
        },
        .flags = {
            .use_dma2d = true, // use DMA2D to copy user buffer to the frame buffer when necessary
            .disable_lp = false // disable low-power for DPI
        }
    };

    generic_vendor_config_t vendor_config = { };
    vendor_config.flags.use_mipi_interface = 1; // Use MIPI interface
    vendor_config.mipi_config.lane_num = 2; // Use 2 lanes for MIPI DSI
    vendor_config.mipi_config.dsi_bus = mipi_dsi_bus; // Use the created DSI bus    
    vendor_config.mipi_config.dpi_config = &dpi_config; // Use the DPI configuration

    esp_lcd_panel_dev_config_t panel_config = { };
    panel_config.reset_gpio_num = EXAMPLE_LCD_IO_RST; // GPIO number for reset, set to -1 if not used
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB; // RGB element order
    panel_config.bits_per_pixel = EXAMPLE_LCD_BIT_PER_PIXEL; // Bits per pixel, 16/18/24    
    panel_config.vendor_config = &vendor_config; // Vendor-specific configuration
    
    // create generic panel
    ESP_ERROR_CHECK(esp_lcd_new_panel_generic(mipi_dbi_io, &panel_config, &mipi_dpi_panel));

    esp_lcd_panel_reset(mipi_dpi_panel); // Reset the panel
    esp_lcd_panel_init(mipi_dpi_panel); // Initialize the panel
    esp_lcd_panel_disp_on_off(mipi_dpi_panel, true);

    display_brightness_set(100); // Set the backlight to 100%
}

void DisplayInterface::GetTransferBuffer(CLR_UINT8 *&TransferBuffer, CLR_UINT32 &TransferBufferSize)
{
    CLR_UINT8 * framebuffer;

    esp_lcd_dpi_panel_get_frame_buffer(mipi_dpi_panel, 1 ,  (void**)&framebuffer);

    TransferBuffer = (CLR_UINT8 *)framebuffer;
    TransferBufferSize = (lcd_x_size * lcd_y_size * 2);

    ets_printf("DisplayInterface::GetTransferBuffer: TransferBuffer=%X, TransferBufferSize=%d\n", TransferBuffer, TransferBufferSize);  
}

void DisplayInterface::ClearFrameBuffer()
{
    CLR_UINT8 *frameBuffer;
    CLR_UINT32 frameBufferSize;

    GetTransferBuffer(frameBuffer, frameBufferSize);

    CLR_INT32 DataCount32 = frameBufferSize / 4;
    for (CLR_INT32 i = 0; i < DataCount32; i++)
    {
        // Note: Write out 4 byte ints measured to be faster than bytes (* 3.5-4 times)
        *frameBuffer = 0;
        frameBuffer++;
    }
}

void DisplayInterface::WriteToFrameBuffer(
    CLR_UINT8 command,
    CLR_UINT8 data[],
    CLR_UINT32 dataCount,
    CLR_UINT32 frameOffset)
{
    UNUSED(command);
    UNUSED(data);
    UNUSED(frameOffset);    
    UNUSED(dataCount);

  
    // CLR_UINT16 *pFrameBuffer = (CLR_UINT16 *)&graphicsStartingAddress;
    // pFrameBuffer += frameOffset;

    // CLR_UINT16 *p16Data = (CLR_UINT16 *)&data[0];
    // dataCount /= 2; // copy 16 bits

    // for (CLR_UINT32 i = 0; i < dataCount; i++)
    // {
    //     *pFrameBuffer++ = *p16Data++;
    // }
}

void DisplayInterface::DisplayBacklight(bool on)
{
    ets_printf("DisplayInterface::DisplayBacklight: %s\n", on ? "ON" : "OFF");
    
    if (g_DisplayInterfaceConfig.Dsi.backlight >= 0)
    {
        display_brightness_set(on ? 50 : 0); 
    }
}

void DisplayInterface::SendCommand(CLR_UINT8 arg_count, ...)
{
    va_list ap;
    va_start(ap, arg_count);

    // Parse arguments into parameters buffer
    CLR_UINT8 parameters[arg_count];
    for (int i = 0; i < arg_count; i++)
    {
        parameters[i] = va_arg(ap, int);
    }

    esp_lcd_panel_io_tx_param(mipi_dbi_io, parameters[0], &parameters[1], arg_count - 1);
}

void DisplayInterface::SendData16Windowed(
    CLR_UINT16 *data,
    CLR_UINT32 startX,
    CLR_UINT32 startY,
    CLR_UINT32 width,
    CLR_UINT32 height,
    CLR_UINT32 stride,
    bool doByteSwap)
{
    (void)data;
    (void)startX;   
    (void)startY;
    (void)width;
    (void)height;
    (void)stride;
    (void)doByteSwap;

    esp_err_t err;

    ets_printf("DisplayInterface::SendData16Windowed: startX=%d, startY=%d, width=%d, height=%d, stride=%d\n", 
        startX, startY, width, height, stride);    

    
    err = esp_lcd_panel_draw_bitmap(mipi_dpi_panel, startX, startY, startX + width, startY + height, data);
    if (err != ESP_OK)
    {
        ets_printf("DisplayInterface::SendData16Windowed: esp_lcd_panel_draw_bitmap failed with error %d\n", err);
        return;
    }
    ets_printf("DisplayInterface::SendData16Windowed: esp_lcd_panel_draw_bitmap succeeded\n");

    // Offset for window start
    // CLR_UINT16 *startOfLine = data + (startY * stride) + startX;

    // if (width == stride) // Optimize full-stride writes
    // {
    //     SendData16(startOfLine, width * height, doByteSwap);
    // }
    // else
    // {
    //     while (height--)
    //     {
    //         SendData16(startOfLine, width, doByteSwap);

    //         startOfLine += stride;
    //     }
    // }

    // FlushData();

    return;
}


void DisplayInterface::FillData16(CLR_UINT16 fillValue, CLR_UINT32 fillLength)
{
    (void)fillValue;
    (void)fillLength;
    // // Fill the current buffer full of fillValue, or as much as we need
    // memset(currentBuffer, fillValue, ((fillLength < SPI_MAX_TRANSFER_16) ? fillLength : SPI_MAX_TRANSFER_16) * 2);

    // // If our length doesn't evenly divide our buffer size, send out the small buffer first
    // // This gives us the maximum freed up time at the end
    // CLR_UINT32 firstSize = fillLength % SPI_MAX_TRANSFER_16;

    // InternalSendBytes((CLR_UINT8 *)currentBuffer, firstSize * 2, true);

    // fillLength -= firstSize;

    // while (fillLength > 0)
    // {
    //     InternalSendBytes((CLR_UINT8 *)currentBuffer, SPI_MAX_TRANSFER_SIZE, true);

    //     fillLength -= SPI_MAX_TRANSFER_16;
    // }

    // // Swap buffers at the end so the next call doesn't overwrite our data
    // SwapBuffers();
}


