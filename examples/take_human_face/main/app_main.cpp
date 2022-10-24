#include "who_camera.h"
#include "who_take_human_face.hpp"
#include "who_button.h"
#include "event_logic.hpp"
//#include "who_adc_button.h"
#include "who_led.h"

static QueueHandle_t xQueueAIFrame = NULL;
static QueueHandle_t xQueueKeyState = NULL;
static QueueHandle_t xQueueLedState = NULL;
static QueueHandle_t xQueueEventLogic = NULL;
//static button_adc_config_t buttons[4] = {{1, 2800, 3000}, {2, 2250, 2450}, {3, 300, 500}, {4, 850, 1050}};

#define GPIO_BOOT GPIO_NUM_15//GPIO_NUM_0
#define GPIO_WHITE_LED  GPIO_NUM_21
#define GPIO_RED_LED    GPIO_NUM_22

extern "C" void app_main()
{
    xQueueAIFrame = xQueueCreate(2, sizeof(camera_fb_t *));
    xQueueKeyState = xQueueCreate(1, sizeof(int *));
    xQueueLedState = xQueueCreate(1, sizeof(int *));
    xQueueEventLogic = xQueueCreate(1, sizeof(int *));

    register_button(GPIO_BOOT, xQueueKeyState);
    register_led(GPIO_WHITE_LED, xQueueLedState);
    //register_led(GPIO_RED_LED, xQueueLedState);
    register_camera(PIXFORMAT_RGB565, FRAMESIZE_VGA
     /*FRAMESIZE_SVGA*//*FRAMESIZE_240X240*/, 2, xQueueAIFrame);
    register_event(xQueueKeyState, xQueueEventLogic);
    register_take_human_face(xQueueAIFrame, xQueueEventLogic, NULL, NULL, true);
}
