
#ifndef APP_H_
#define APP_H_

#define UART_MASTER

#ifndef UART_MASTER
#define UART_SLAVE
#endif

#define UART_COMMON_PIN    27
#define UART_STACK_SZ      2048

void IRAM_ATTR rtc_reset();
void app_start();

#endif /* APP_H_ */
