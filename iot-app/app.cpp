#include "app.h"

static const char *TAG = "app";

void app_main(void) {
  esp_log_level_set("*", LOG_LEVEL);
  LOGI("init logging");

#ifdef UART_FORWARDING
  uart_init();
#endif
 
  LOGI("goto app entry: app_start()");
  app_start();
}
