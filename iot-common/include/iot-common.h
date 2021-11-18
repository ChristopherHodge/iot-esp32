#ifndef IOT_COMMON_H_
#define IOT_COMMON_H_

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "iot-config.h"
#include "soc/rtc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "iot-ota.h"

#ifdef UART_FORWARD
 #define UART_MASTER
 #ifndef UART_MASTER
  #define UART_SLAVE
 #endif
#endif

#define UART_COMMON_PIN        27
#define UART_STACK_SZ          2048

#ifndef DEFAULT_STACK_SZ
 #define DEFAULT_STACK_SZ      4096
#endif

#ifndef DEFAULT_TASK_PRIO
 #define DEFAULT_TASK_PRIO     7
#endif

#define DELAY_BASE_MS          5 
#define DELAY_LEVEL(s)         ((DELAY_BASE_MS << s) / portTICK_RATE_MS)

#define DELAY_S0               DELAY_LEVEL(0)
#define DELAY_S1               DELAY_LEVEL(1)
#define DELAY_S2               DELAY_LEVEL(2)
#define DELAY_S3               DELAY_LEVEL(3)
#define DELAY_S4               DELAY_LEVEL(4)
#define DELAY_S5               DELAY_LEVEL(5)

#define BUILD_STRING(x)        #x
#define BUILD_STRINGS(Name)    BUILD_STRING(Name),

#define LOGD(...) ESP_LOGD(TAG __VA_OPT__(,) __VA_ARGS__)
#define LOGI(...) ESP_LOGI(TAG __VA_OPT__(,) __VA_ARGS__)
#define LOGW(...) ESP_LOGW(TAG __VA_OPT__(,) __VA_ARGS__)
#define LOGE(...) ESP_LOGE(TAG __VA_OPT__(,) __VA_ARGS__)

#ifdef HEAP_DEBUG
  #define HEAP_TAG_TYPE(tag, type)    type##_##tag
  #define HEAP_TAG_INIT(tag, type)    int HEAP_TAG_TYPE(tag, type) = 0;
  #define HEAP_TAG_EXTERN(tag, type)  extern int HEAP_TAG_TYPE(tag, type);
  #define HEAP_INIT(tag)              HEAP_TAG_INIT(tag, BEGIN) \
                                      HEAP_TAG_INIT(tag, END) \
                                      HEAP_TAG_INIT(tag, TOTAL)
  #define HEAP_EXTERN(tag)            HEAP_TAG_EXTERN(tag, BEGIN) \
                                      HEAP_TAG_EXTERN(tag, END) \
                                      HEAP_TAG_EXTERN(tag, TOTAL)
  #define HEAP_TAG_SET(tag, type)     HEAP_TAG_TYPE(tag, type) = heap_caps_get_free_size(MALLOC_CAP_INTERNAL)

  #define HEAP_LOG(tag, type)         LOGI("== HEAP STATS: %s: %s: %d free", #tag, #type, HEAP_TAG_TYPE(tag, type))
  #define HEAP_BEGIN(tag)             HEAP_TAG_SET(tag, BEGIN); \
                                      HEAP_LOG(tag, BEGIN)

  #define HEAP_DIFF(tag)              (HEAP_TAG_TYPE(tag, BEGIN) - HEAP_TAG_TYPE(tag, END))
  #define HEAP_UPDATE(tag)            HEAP_TAG_TYPE(tag, TOTAL) +=  HEAP_DIFF(tag); \
                                      LOGI("== HEAP CHANGE: %s: %d", #tag, HEAP_DIFF(tag))

  #define HEAP_SUMMARY(tag)           LOGI("== HEAP SUMMARY: %s: %d", #tag, HEAP_TAG_TYPE(tag, TOTAL))

  #define HEAP_END(tag)               HEAP_TAG_SET(tag, END); \
                                      HEAP_LOG(tag, END); \
                                      HEAP_UPDATE(tag); \
                                      HEAP_SUMMARY(tag)
  HEAP_TAGS(HEAP_EXTERN)
#else
  #undef HEAP_TAGS
  #define HEAP_TAGS(x)
  #define HEAP_INIT(x)
  #define HEAP_EXTERN(x)
  #define HEAP_BEGIN(x)
  #define HEAP_END(x)
#endif

#ifdef SHOW_STACK_STATS
 #define STACK_STATS              LOGI("(%s) HIGH_STACK: %d (free)", \
                                      pcTaskGetName(NULL), uxTaskGetStackHighWaterMark(NULL));
#else
 #define STACK_STATS
#endif

#define PTR_OFFSET(base, ptr)       ((int)ptr - (int)base)

#define MIN(x, y)  (x < y ? x : y)
#define MAX(x, y)  (x < y ? y : x)
#define MILLIS     (esp_timer_get_time() / 1000)

void IRAM_ATTR rtc_reset();
void subchrs(char*, char, char);
void lowerchrs(char*);
void upperchrs(char*);
void stripchrs(char*);
void enum_to_str(char*);

#endif /* APP_H_ */

