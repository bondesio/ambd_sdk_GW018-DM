
#if defined(CONFIG_PLATFORM_8711B)
#include "rtl8710b_ota.h"
#include <FreeRTOS.h>
#include <task.h>
#elif defined(CONFIG_PLATFORM_8195A)
#include <ota_8195a.h>
#elif defined(CONFIG_PLATFORM_8195BHP)
#include <ota_8195b.h>
#elif defined(CONFIG_PLATFORM_8710C)
#include <ota_8710c.h>
#elif defined(CONFIG_PLATFORM_8721D)
#include <platform/platform_stdlib.h>
#include "rtl8721d_ota.h"
#include <FreeRTOS.h>
#include <task.h>
#endif
#include <wifi_constants.h>
#include "wifi_conf.h"
#include "gpio_api.h"
#include "gw018_portal.h"

#define LED_BLUE   _PB_22 	//Blue LED
#define PUSH_BTN   _PB_23 	//Push button

gpio_t led_blue;
gpio_t push_btn;

#define PORT	8080
#include "gw018_ota_config.h"
#define RESOURCE "OTA_All.bin"     //"051103061600.bin"


#ifdef HTTP_OTA_UPDATE
void http_update_ota_task(void *param){
	(void)param;

	// Init blue LED
	gpio_init(&led_blue, LED_BLUE);
	gpio_dir(&led_blue, PIN_OUTPUT);    // Direction: Output
	gpio_mode(&led_blue, PullNone);     // No pull

	// Initial Push Button pin
    gpio_init(&push_btn, PUSH_BTN);
    gpio_dir(&push_btn, PIN_INPUT);     // Direction: Input
    gpio_mode(&push_btn, PullNone);       // No pull

#if defined(configENABLE_TRUSTZONE) && (configENABLE_TRUSTZONE == 1)
	rtw_create_secure_context(configMINIMAL_SECURE_STACK_SIZE);
#endif

	printf("\n\r\n\r\n\r\n\r<<<<<< OTA HTTP Example >>>>>>>\n\r\n\r\n\r\n\r");

	vTaskDelay(pdMS_TO_TICKS(5000));
	int down = 0;
	int long_press_done = 0;
	TickType_t pressed_at = 0;
	for (;;) {
		int now_down = !gpio_read(&push_btn);
		TickType_t now = xTaskGetTickCount();
		if (now_down && !down) {
			down = 1;
			long_press_done = 0;
			pressed_at = now;
			gpio_write(&led_blue, 1);
		} else if (now_down && down && !long_press_done &&
		           (TickType_t)(now - pressed_at) >= pdMS_TO_TICKS(3000)) {
			long_press_done = 1;
			printf("GW018: long press, starting OTA update\n");
			while (wifi_is_ready_to_transceive(RTW_STA_INTERFACE) != RTW_SUCCESS)
				vTaskDelay(pdMS_TO_TICKS(1000));
			int ret = http_update_ota(HOST, PORT, RESOURCE);
			if (ret == 0) ota_platform_reset();
			printf("GW018: OTA update failed; button remains available\n");
		} else if (!now_down && down) {
			TickType_t held = now - pressed_at;
			down = 0;
			gpio_write(&led_blue, 0);
			if (!long_press_done && held >= pdMS_TO_TICKS(60) &&
			    held <= pdMS_TO_TICKS(1200)) gw018_portal_toggle();
		}
		gw018_portal_tick();
		vTaskDelay(pdMS_TO_TICKS(50));
	}
}


void example_ota_http(void){
		if(xTaskCreate(http_update_ota_task, (char const *)"http_update_ota_task", 1024, NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS){
		printf("\n\r[%s] Create update task failed", __FUNCTION__);
	}
}
#endif
