

#ifndef SENSOR_H_
#define SENSOR_H_

#include "BLEAddress.h"

#define MAX_SENSORS            2
#define ADC_PRECISION          1024
#define ADC_REF_MV			   600
#define ADC_SCALING_FACTOR     3

#define AIR_QUALITY_COEFF	   6.0f / 35
#define AIR_QUALITY_CALC(x)    ((0.18f * x) - 0.15f)

#define TCP_CONN_TIMEOUT       5000
#define WIFI_ERR_THRESH        10

#define INFLUX_HOST           "influxdb.localdomain"
#define INFLUX_PORT           80
#define INFLUX_ENDPOINT       "/write?db=sensors"
#define INFLUX_BASE_QUERY     "%s,device_id=%s,sensor_id=%hhu "
#define MSG_BODY_SZ       250
#define INFLUX_QUERY_SZ   100

typedef enum MOTION {
	FALSE,
	TRUE,
} motion_t;

typedef enum CONTACT {
	CLOSED,
	OPEN,
} contact_t;

typedef enum INTERFACE {
	INT_NONE = 0,
	INT_ADC  = (1 << 0),
	INT_INT  = (1 << 1),
	INT_TEMP = (1 << 2),
	INT_AIR  = (1 << 3),
	INT_AGS  = (1 << 4),
	INT_PMS  = (1 << 5),
	INT_CCS  = (1 << 6),
} interface_t;

typedef char sensor_name_t[24];
typedef char sensor_type_t[12];
typedef uint8_t sensor_type_id_t;
typedef char attribute_t[12];

typedef struct sensor {
	bool in_use;
	sensor_name_t name;
	sensor_type_t type;
	interface_t interface;
	uint8_t threshold;
	uint16_t state;
	sensor_type_id_t id;
	unsigned long int updatedAt;
} sensor_t;

typedef enum sensor_data_type {
	VAL_U16,
} sensor_val_type_t;

typedef struct sensor_val {
	attribute_t attribute;
	sensor_val_type_t val_type;
	union  {
		uint16_t u16;
	};
	void *value;
} sensor_val_t;

typedef struct sensor_multi_data {
	uint8_t sensor_id;
	sensor_type_id_t type;
	uint8_t num_values;
	sensor_val_t *values;
} sensor_multi_data_t;

typedef struct sensor_data {
	uint8_t sensor_id;
	sensor_type_id_t type;
	sensor_val_type_t val_type;
	union  {
		uint16_t u16;
	};
	void *value;
} sensor_data_t;

extern const PROGMEM sensor_t INVALID_SENSOR;
extern const PROGMEM sensor_t SENSOR_TYPE[];
	
void process_sensor_ble_payload(BLEAddress*, uint32_t*);
void updateAttr(char*, uint8_t, uint8_t, char*, char*, bool local=false);
void updateAttrs(char*, uint8_t, uint8_t, attribute_t*, uint16_t*);
#endif 
