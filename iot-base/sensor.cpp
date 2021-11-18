#include <ArduinoJson.h>
#include "network.h"
#include "sensor.h"
#include "ble.h"
#include "smartthings.h"

const PROGMEM char *MOTION_STRING[] = {"inactive", "active"};
const PROGMEM char *CONTACT_STRING[] = {"closed", "open"};
const PROGMEM sensor_t INVALID_SENSOR = { false, { "invalid", }, { "invalid", }, INT_NONE, 0, 0, 255, };

const PROGMEM sensor_t SENSOR_TYPE[] = {
	// in_use     name					interface threshold  state   id
	{"true",	"Battery",				INT_ADC,      1,         0,      0 },
	{"true",	"Light",    			INT_ADC,      0,         0,      1 },
	{"true",	"Motion",   			INT_INT,      0,         0,      2 },
	{"true",	"Moisture", 			INT_ADC,      2,         0,      3 },
	{"true",	"Temperature",			INT_TEMP,	  0,         0,      4 },
	{"true",	"Contact",				INT_INT,      0,         0,      5 },
	{"true",	"Face Recognition",		INT_NONE,     0,         0,      6 },
	{"true",	"Air Particles (SHARP)", INT_AIR,     2,         0,      7 },
	{"true",	"UV Light",				INT_ADC,      1,         0,      8 },
	{"true",	"Air Gasses (AGS)",		INT_AGS,      1,         0,      9 },
	{"true",	"Air Particles (PMS)",  INT_PMS,      1,         0,     10 },
	{"true",	"Air Gasses (CCS)",     INT_CCS,      1,         0,     11 },
	{"true",	"Force (kgs)",          INT_NONE    , 0,         0,     12 },
};

static uint8_t wifi_err_cnt = 0;

static sensor_t* getSensor(device_t *device, sensor_data_t *data) {
	sensor_t *sensor = &device->sensors[data->sensor_id];
	if(!sensor->in_use) {
		memcpy(sensor, &SENSOR_TYPE[data->type], sizeof(sensor_t));
	}
	return sensor;
}

bool influxApiReq(char *verb, char *endpoint, char *body) {
  char size[4];
  char status[32] = {0};
  
  if (!client->connect(INFLUX_HOST, INFLUX_PORT, TCP_CONN_TIMEOUT)) {
	  Serial.println("failed to connect to influxdb");
	  client->stop();
	  return false;
   }
   
  char *header = (char*)
  malloc(MSG_BODY_SZ);

  header[0] = '\0';
  strcat(header, verb);
  strcat(header, " ");
  strcat(header, endpoint);
  strcat(header, " HTTP/1.1");
  strcat(header, "\r\nHost: ");
  strcat(header, INFLUX_HOST);
  if(strlen(body) > 0) {
	sprintf(size, "%u", strlen(size));
    strcat(header, "\r\nAccept: */*");
    strcat(header, "\r\nContent-type: application/csv");
    strcat(header, "\r\nContent-length: ");
    strcat(header, size);
  }
  strcat(header, "\r\n\r\n\0");
  
  client->print(header);
  if(strlen(body) > 0)
      client->print(body);

  client->readBytesUntil('\r', status, sizeof(status));
  client->stop();

  free(header);
  return true;
}

void wifi_err_check() {
	if(wifi_err_cnt > WIFI_ERR_THRESH) {
		Serial.println("wifi_err_cnt exceeds threshold: resetting");
		ESP.restart();
	}
}

static void subchrs(char *str, char old_c, char new_c) {
	for(; *str; ++str) {
		if(*str == old_c) *str = new_c;
	}
}

static void lowerchrs(char *str) {
	for(; *str; ++str) *str = tolower(*str);
}

void updateAttrs(char *device, uint8_t type, uint8_t nFields, attribute_t *attrs, uint16_t *values) {
	const sensor_t *sensor = &SENSOR_TYPE[type];

	char sensor_name[sizeof(sensor->name)];
	strncpy(sensor_name, sensor->name, sizeof(sensor_name)-1);
	subchrs(sensor_name, ' ', '_');
	
	device_id_t device_id;
	strncpy(device_id, device, sizeof(device_id_t)-1);
	lowerchrs(device_id);
	
	char *query = (char*)malloc(INFLUX_QUERY_SZ);
	int pos = sprintf(query, INFLUX_BASE_QUERY, sensor_name, device_id, 0);

	for(uint8_t i=0; i<nFields; i++) {
		pos += sprintf(query+pos, "%s=%d,", attrs[i], values[i]);
	}
	query[pos-1] = '\0';

	Serial.println(query);

	influxApiReq("POST\0", INFLUX_ENDPOINT, query);
	free(query);
}

void updateAttr(char *device, uint8_t sensor, uint8_t type, char *attr, char *value, bool local) {
	DynamicJsonDocument root(200);
	char endpoint[32] = {0};
	char sensor_buf[3];
	
	wifi_err_check();
	
	itoa(sensor, sensor_buf, 10);

	if(type > sizeof(SENSOR_TYPE)-1) {
		Serial.println("unknown device type");
		return;
	}

	root["deviceId"] = device;
	root["sensorId"] = sensor_buf;
	root["deviceType"] = SENSOR_TYPE[type].name;

	JsonObject update = root.createNestedObject("update");
	update[attr] = value;

	char *body = (char*)malloc(MSG_BODY_SZ);
	
	serializeJson(root, body, MSG_BODY_SZ);

	if(!local) {
	  if(st_send_event(body)) {
		  wifi_err_cnt = 0;
	  } else {
		  wifi_err_cnt++; 
	  }
	}
	
	String sensor_type = String(SENSOR_TYPE[type].name);
	sensor_type.toLowerCase();
	sensor_type.replace(" ", "_");
	
	sprintf(body, "%s,device_id=\"%s\",sensor_id=%hhu value=%s",sensor_type.c_str(),device,sensor,value);
	sprintf(endpoint, "/write?db=sensors");

	if(influxApiReq("POST\0", endpoint, body)) {
		wifi_err_cnt = 0;
	} else {
		wifi_err_cnt++;
	}
	
	free(body);
}

static uint16_t calcPercent(uint16_t value) {
	if(value > ADC_PRECISION) {
		return 0;
	}
	float f = (float)value / ADC_PRECISION * 100;
	return (uint16_t)round(f);
}

boolean aboveThreshold(sensor_t *sensor, uint16_t val) {
	uint16_t delta = max(sensor->state, val) - min(sensor->state, val);
	return (max(sensor->state, val) - min(sensor->state, val)) >= sensor->threshold;
}

static void process_sensor_payload(device_id_t *device_id, sensor_data_t *data) {
	char tag[] = "processPayload()";
	BLEAddress addr((char*)device_id);
	device_t *device = getDevice(&addr);
	if(!device) {
		Serial.printf("%s: no such device: %s\n", tag, (char*)device_id);
		return;
	}
	sensor_t *sensor = getSensor(device, data);
	if(!sensor) {
		printf("%s: unable to get sensor data for device: %s\n", tag, (char*)device_id);
		return;
	}
	unsigned long int now = millis();
	device->last_seen = now;
	uint8_t motion = 0;
	char c_val[8];
	bool force = now > sensor->updatedAt + 120000;
	uint16_t value = data->u16;
	switch(data->type) {
		case 0: { // Battery
			if(aboveThreshold(sensor, value)) {
				itoa(value, c_val, 10);
				updateAttr(device->id, data->sensor_id, data->type, "battery", c_val);
			}
			break;
		}
		case 1: { // Light
			if(value > 0xfff0)
				value = 0;
			if(aboveThreshold(sensor, value) || force) {
				itoa(value, c_val, 10);
				updateAttr(device->id, data->sensor_id, data->type, sensor->type, c_val, true);
				sensor->updatedAt = now;
				sensor->state = value;
			}
			break;
		}
		case 2: { // Motion
			if(aboveThreshold(sensor, value) || force) {
				if(value) {
					motion = 1;
				}
				updateAttr(device->id, data->sensor_id, data->type, sensor->type, (char*)MOTION_STRING[motion]);
				sensor->updatedAt = now;
				sensor->state = value;
			}
			break;
		}
		case 3: { // Moisture
			value = calcPercent(value);
			if(aboveThreshold(sensor, value) || force) {
				itoa(value, c_val, 10);
				updateAttr(device->id, data->sensor_id, data->type, sensor->type, c_val);
				sensor->updatedAt = now;
				sensor->state = value;
			}
			break;
		}
		case 4: { // Temperature
			if(aboveThreshold(sensor, value) || force) {
				itoa(value, c_val, 10);
				updateAttr(device->id, data->sensor_id, data->type, sensor->type, c_val);
				sensor->updatedAt = now;
				sensor->state = value;
			}
			break;
		}
		case 5: { // Contact
			updateAttr(device->id, data->sensor_id, data->type, sensor->type, (char*)CONTACT_STRING[value]);
			sensor->updatedAt = now;
			sensor->state = value;
		} break;
		//
		// type 6 - face recognition - handled in fr code
		//
		case 7: { // Air Quality
			/*
			float vAdj = data.value * (5.0f / 1024);
			if(vAdj < 0.583 || vAdj > 5) {
				data.value = 0;
			} else {
				data.value = (uint16_t)round((AIR_QUALITY_COEFF * vAdj - 0.1) * 1000);
			}
			*/
			float vAdj = value * (3.6f / 1024);
			value = (uint16_t)round(AIR_QUALITY_CALC(vAdj));	
			if(aboveThreshold(sensor, value) || force) {
				itoa(value, c_val, 10);
				updateAttr(device->id, data->sensor_id, data->type, sensor->type, c_val);
				sensor->updatedAt = now;
				sensor->state = value;
			}
			break;
		}
		case 8: { // UV Light
			if(value > 0xfff0)
			    value = 0;
			if(aboveThreshold(sensor, value) || force) {
				itoa(value, c_val, 10);
				updateAttr(device->id, data->sensor_id, data->type, sensor->type, c_val);
				sensor->updatedAt = now;
				sensor->state = value;
			}
			break;
		}
		case 9: { // AGS Air
			if(aboveThreshold(sensor, value) || force) {
				float val = value / 10.0f;
				sprintf(c_val, "%.2f", val);
				updateAttr(device->id, data->sensor_id, data->type, sensor->type, c_val);
				sensor->updatedAt = now;
				sensor->state = value;
			}
			break;
		}
		case 10: { // PMS Air
			if(aboveThreshold(sensor, value) || force) {
				itoa(value, c_val, 10);
				updateAttr(device->id, data->sensor_id, data->type, sensor->type, c_val);
				sensor->updatedAt = now;
				sensor->state = value;
			}
			break;
		}
		case 11: { // CCS Air
			if(aboveThreshold(sensor, value) || force) {
				itoa(value, c_val, 10);
				updateAttr(device->id, data->sensor_id, data->type, sensor->type, c_val);
				sensor->updatedAt = now;
				sensor->state = value;
			}
			break;
		}
		case 12: { // Force kgs
			if(value > 0xfff0)
				value = 0;
			if(aboveThreshold(sensor, value) || force) {
				itoa(value, c_val, 10);
				updateAttr(device->id, data->sensor_id, data->type, sensor->type, c_val);
				sensor->updatedAt = now;
				sensor->state = value;
			}
			break;
		}
	}
}

void process_sensor_ble_payload(BLEAddress *addr, uint32_t *payload) {
	uint8_t *data = (uint8_t*)payload;
	device_id_t *device_id = (device_id_t*)addr->toString().c_str();
	sensor_data_t sensor_data;
	sensor_data.val_type = VAL_U16;
	sensor_data.u16 = (data[2] << 8) | data[3];
	sensor_data.type = data[1];
	sensor_data.sensor_id = data[0];
	sensor_data.value = &sensor_data.u16;
	process_sensor_payload(device_id, &sensor_data);
}
