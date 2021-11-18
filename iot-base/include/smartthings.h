
#ifndef SMARTTHINGS_H_
#define SMARTTHINGS_H_

#define ST_HUB                "sthub.localdomain"
#define ST_PORT               39500
#define ST_STACK_SZ           4096

typedef struct stconfig {
	char hubip[20];
	char apiurl[200];
	char token[80];
} stconfig_t;

void smartthings_init();
uint16_t getSensors(device_t*);
uint8_t st_init_device(char*);
uint8_t st_send_event(char*);

#endif /* SMARTTHINGS_H_ */
