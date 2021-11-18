#include <mbedtls/esp_config.h>
#include <soc/rtc.h>
#include <HardwareSerial.h>
#include "app.h"

void IRAM_ATTR rtc_reset() {
	rtc_sleep_config_t sleep_cfg = RTC_SLEEP_CONFIG_DEFAULT(RTC_SLEEP_PD_DIG);
	rtc_sleep_init(sleep_cfg);
	rtc_sleep_set_wakeup_time(10000);
	rtc_deep_sleep_start(RTC_TIMER_TRIG_EN, 0);
}

void uart_rx_mgr(void *pvx) {
	Serial1.begin(115200, SERIAL_8N1, UART_COMMON_PIN, -1);
	for(;;) {
		while(Serial1.available()) {
			Serial.write(Serial1.read());
		}
		vTaskDelay(50 / portTICK_RATE_MS);
	}
}

void setup(void) {
#ifdef UART_SLAVE
	Serial.begin(115200, SERIAL_8N1, -1, UART_COMMON_PIN);
#else
  #ifdef UART_MASTER
	xTaskCreatePinnedToCore(uart_rx_mgr, "uart_rx_mgr", UART_STACK_SZ, NULL, 6, NULL, 1);
  #endif
	Serial.begin(115200);
#endif

	app_start();
}

void loop(void) {
	vTaskDelete(NULL);
}