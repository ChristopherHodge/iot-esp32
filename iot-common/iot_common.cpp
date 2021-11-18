#include "iot-common.h"
#include "driver/uart.h"
#include "ctype.h"

HEAP_TAGS(HEAP_INIT)

void uart_rx_mgr(void *pvx) {
	for(;;) {
		vTaskDelay(50 / portTICK_RATE_MS);
	}
}

void uart_init() {
  uart_config_t uart_config = {
      .baud_rate = 115200,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_CTS_RTS,
      .rx_flow_ctrl_thresh = 122,
  };
  uart_param_config(UART_NUM_2, &uart_config);
  uart_set_pin(UART_NUM_2, UART_COMMON_PIN, UART_PIN_NO_CHANGE, \
               UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
#ifdef UART_PRIMARY
  xTaskCreatePinnedToCore(uart_rx_mgr, "uart_rx_mgr", UART_STACK_SZ, NULL, DEFAULT_TASK_PRIO, NULL, 1);
#endif
}

void IRAM_ATTR rtc_reset() {
	rtc_sleep_config_t sleep_cfg;
	rtc_sleep_get_default_config(RTC_SLEEP_PD_DIG, &sleep_cfg);
  	vTaskDelay(500 / portTICK_RATE_MS);
	rtc_sleep_init(sleep_cfg);
	rtc_sleep_set_wakeup_time(10000);
	rtc_deep_sleep_start(RTC_TIMER_TRIG_EN, 0);
}

void subchrs(char *str, char old_c, char new_c) {
	for(; *str; ++str) {
		if(*str == old_c) *str = new_c;
	}
}

void lowerchrs(char *str) {
	for(; *str; ++str) *str = tolower(*str);
}

void upperchrs(char *str) {
	for(; *str; ++str) *str = toupper(*str);
}

void stripchrs(char *str) {
  str = strtok(str, "\r");
  str = strtok(str, "\n");
}

void enum_to_str(char *str) {
  lowerchrs(str);
  subchrs(str, '_', ' ');
}
