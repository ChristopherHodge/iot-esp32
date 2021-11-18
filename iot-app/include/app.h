
#ifndef APP_H_
#define APP_H_

#include "iot-config.h"
#include "iot-common.h"
#include "soc/rtc.h"


void app_start();
extern "C" {
  void app_main(void);
}

#endif /* APP_H_ */
