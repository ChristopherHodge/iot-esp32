#include <math.h>
#include "esp_http_client.h"
#include "iot-common.h"
#include "ble.h"
#include "smartthings.h"
#include "sensor.h"
#include "influx.h"
#include "esp_crt_bundle.h"

static const char *TAG = "sensor";

const	char	*MOTION_STRING[]	= { MOTION_STATE(BUILD_STRINGS) };
const	char	*CONTACT_STRING[]	= { CONTACT_STATE(BUILD_STRINGS) };
const	char	*PRESENCE_STRING[]	= { PRESENCE_STATE(BUILD_STRINGS) };
const	char	*ACTION_STRING[]	= { ACTION_STATE(BUILD_STRINGS) };

static			sensor_t	SENSOR_TYPE[]	= { SENSOR_TYPES(SENSOR_ENTRIES) };
extern const	sensor_t	INVALID_SENSOR	= SENSOR_DEFAULTS;

const sensor_t* sensor_get_by_type_name(attribute_t type) {
	LOGD("sensor_get_by_type_name: %s", (char*)type);
	upperchrs((char*)type);
	for (uint8_t i=0; i < (sizeof(SENSOR_TYPE) / sizeof(SENSOR_TYPE[0])); i++) {
		const sensor_t *s = &SENSOR_TYPE[i];
		if(strncmp(type, s->type, sizeof(attribute_t)) == 0) {
			return s;
		}
	}
	return nullptr;
}

const sensor_t* sensor_get_by_type(sensor_type_t type) {
	return &SENSOR_TYPE[type];
}

void sensor_type_get_name(sensor_type_t id, attribute_t buf) {
	char *p = (char*)buf;
    strncpy(p, SENSOR_TYPE[id].type, sizeof(attribute_t));
    lowerchrs(p);
}

uint8_t sensor_process_payload(sensor_t *sensor, sensor_multi_data_t *data) {
	unsigned long int now = MILLIS;
	uint16_t *p_val = (uint16_t*)data->values[0].value;
	sensor_val_type_t val_type = data->values[0].val_type;
	bool force = false;
	if(sensor) {
	    force = (now > (sensor->updatedAt + FORCE_UPDATE_MS));
	}

	switch(data->type) {
		case SENSOR_BATTERY:
		    data->scopes |= SCOPE_SMARTTHINGS;
			if(data->sensor_id == 0) {
			    data->scopes |= SCOPE_INFLUX;
			}
			return true;
		case SENSOR_CONTACT:
		case SENSOR_MOTION:
		case SENSOR_IDENTITY:
		case SENSOR_PRESENCE:
		    data->scopes |= (SCOPE_SMARTTHINGS);
			force = true;
			break;
		case SENSOR_LIGHT:
			data->scopes |= SCOPE_INFLUX;
			force = true;
			if(*p_val > 0xfff) {
				(*p_val) = 0;
			}; break;
		case SENSOR_PARTICLES:
			data->scopes |= SCOPE_INFLUX;
			break;
		default:
			data->scopes |= SCOPE_INFLUX;
			break;
	}

#ifdef SCOPE_ADD_DEFAULT
	data->scopes |= SCOPE_ADD_DEFAULT;
#endif

	if(val_type == VAL_U16) {
		if(*p_val == sensor->state) {
			if(!force) {
				return false;
			}
		}
		sensor->state = *p_val; 
	}
	sensor->updatedAt = now;
	return true;
}

void sensor_update_interface(sensor_type_t entry, interface_t interface) {
	SENSOR_TYPE[entry].interface = interface;
}

uint8_t sensor_payload_alloc_entry(sensor_multi_data_t *data) {
	uint8_t idx = data->num_values++;
	size_t len;
	void *ptr;

	len = data->num_values * VAL_ENTRY_SZ;
	data->values = (sensor_val_t*)realloc(data->values, len);
    ptr = &data->values[idx];
	memset(ptr, 0, VAL_ENTRY_SZ);
	LOGD("payload alloc for values: %d bytes", len);

	len = data->num_values * VAL_TAG_SZ;
	data->tags = (sensor_tag_t*)realloc(data->tags, len);
    ptr = &data->tags[idx];
	memset(ptr, 0, VAL_TAG_SZ);
	LOGD("payload alloc for tags: %d bytes", len);

	return idx;
}

sensor_data_entry_t sensor_payload_get_entry(sensor_multi_data_t *data, uint8_t idx) {
	sensor_data_entry_t entry = {
		.sensor_id = &data->sensor_id,
		.type = &data->type,
		.value = &data->values[idx],
		.tag = data->tags[idx],
	};
	return entry;
}
void sensor_payload_entry_id(sensor_data_entry_t entry, uint8_t sensor_index, sensor_type_t type) {
	*entry.sensor_id = sensor_index;
	*entry.type = type;
}

void sensor_payload_entry_tag(sensor_data_entry_t entry, sensor_tag_t tag) {
	memcpy(entry.tag.key, tag.key, sizeof(tag_val_t));
	memcpy(entry.tag.val, tag.val, sizeof(tag_val_t));
}

void sensor_payload_entry_attr(sensor_data_entry_t entry, const char *attr, sensor_val_type_t val_type, void *val) {
	memset(entry.value, 0, VAL_ENTRY_SZ);
	memcpy(entry.value->attribute, attr, sizeof(attribute_t));
	switch(val_type) {
		case VAL_U16: {
			entry.value->u16 = *(uint16_t*)val;
			entry.value->value = (void*)&entry.value->u16;
		} break;

		default:
			break;
	}
}
