#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "stdk_app.h"

const char *TAG = "stdk_app";

extern  const uint8_t   onboarding_config_start[]       asm("_binary_onboarding_config_json_start");
extern  const uint8_t   onboarding_config_end[]         asm("_binary_onboarding_config_json_end");
extern  const uint8_t   device_info_start[]             asm("_binary_device_info_json_start");
extern  const uint8_t   device_info_end[]               asm("_binary_device_info_json_end");

static  iot_status_t            g_iot_status = IOT_STATUS_IDLE;
static  iot_stat_lv_t           g_iot_stat_lv;

static uint8_t g_iot_ready = false;

IOT_CTX* ctx = NULL;

static void iot_noti_cb(iot_noti_data_t *noti_data, void *noti_usr_data) {
        printf("Notification message received\n");

        if (noti_data->type == IOT_NOTI_TYPE_DEV_DELETED) {
                printf("[device deleted]\n");
        } else if (noti_data->type == IOT_NOTI_TYPE_RATE_LIMIT) {
                printf("[rate limit] Remaining time:%d, sequence number:%d\n",
                                noti_data->raw.rate_limit.remainingTime,
                                noti_data->raw.rate_limit.sequenceNumber);
        } else if(noti_data->type == IOT_NOTI_TYPE_SEND_FAILED) {
            rtc_reset();
        }
}

static void iot_status_cb(iot_status_t status, iot_stat_lv_t stat_lv, void *usr_data) {
        g_iot_status = status;
        g_iot_stat_lv = stat_lv;

        LOGI("status: %d, stat: %d", g_iot_status, g_iot_stat_lv);

        switch(status) {
                case IOT_PROV_CONFIRM_BITS:
                    st_conn_ownership_confirm(ctx, true);
                    break;
                default:
                    break;
        }

        switch(status + stat_lv) {
                case IOT_SUCCESS_BITS:
					g_iot_ready = true;
                    break;
                case IOT_FAIL_BITS:
                    rtc_reset();
                    break;
                default:
                    break;
        }

}

void stdk_conn_start() {
    unsigned char *onboarding_config = (unsigned char *) onboarding_config_start;
    unsigned int onboarding_config_len = onboarding_config_end - onboarding_config_start;
    unsigned char *device_info = (unsigned char *) device_info_start;
    unsigned int device_info_len = device_info_end - device_info_start;

    int iot_err;
    ctx = st_conn_init(onboarding_config, onboarding_config_len, device_info, device_info_len);

    if (ctx != NULL) {
            iot_err = st_conn_set_noti_cb(ctx, iot_noti_cb, NULL);
            if (iot_err) {
                    LOGE("fail to set notification callback function");
    	}
    } else {
            LOGE("failed to create st connection context");
    }

    iot_err = st_conn_start(ctx, (st_status_cb)&iot_status_cb, IOT_STATUS_ALL, NULL, NULL);
    if (iot_err) {
    	LOGE("fail to start connection. err: %d", iot_err);
            rtc_reset();
    }

	while(!g_iot_ready) {
		vTaskDelay(1000);
	}
}

void stdk_main(void *ptx) {
    stdk_conn_start();
    LOGI("all services are connected: yielding to app_start()");
    app_start();
	vTaskDelete(NULL);
}

void app_main(void)  {
    esp_log_level_set("*", LOG_LEVEL);
	xTaskCreatePinnedToCore(stdk_main, "stdk_main", DEFAULT_STACK_SZ, NULL,  DEFAULT_TASK_PRIO, NULL, 1);
	vTaskDelete(NULL);
}
