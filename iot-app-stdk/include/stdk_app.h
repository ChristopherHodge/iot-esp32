#ifndef STDK_APP_H_
#define STDK_APP_H_

#include "iot-config.h"
#include "iot-common.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "st_dev.h"

#define IOT_SUCCESS_BITS       (IOT_STATUS_CONNECTING + IOT_STAT_LV_DONE)
#define IOT_FAIL_BITS          (IOT_STATUS_CONNECTING + IOT_STAT_LV_FAIL)

#define IOT_PROV_CONFIRM_BITS  IOT_STATUS_NEED_INTERACT

#define CONNECTED_BIT       (1 << 1)
#define RESET_BIT           (1 << 2)

#define ALL_BITS            ((1 << 8) - 1)

extern "C" {
	void app_main(void);
	void app_start();
}

void app_start();

#endif // STDK_APP_H_
