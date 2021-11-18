#ifndef SENSOR_H_
#define SENSOR_H_

#include "iot-config.h"
#include "iot-common.h"
#include "NimBLEDevice.h"

/*!
    @file
	@brief Manage specific capabilities/data types of devices

 */

#ifndef MAX_SENSORS
 #define MAX_SENSORS          2
#endif

#define ADC_SCALING_FACTOR    3
#define ADC_PRECISION         1024.0f
#define ADC_REF_MV			  600.0f
#define ADC_REF_MAX           (ADC_REF_MV * ADC_SCALING_FACTOR)
#define ADC_UNIT_VAL          (ADC_REF_MAX / ADC_PRECISION)
#define ADC_CALC_PERCENT(x)   (uint16_t)round((x / ADC_PRECISION) * 100)

#define PARTICLE_COEFF(x)     ((0.18f * (ADC_UNIT_VAL * x)) - 0.15f)

#define WIFI_ERR_THRESH       12
#define FORCE_UPDATE_MS       120000

#define VAL_TRANSPORT_SZ      sizeof((device_data_t){ {}, {}, })
#define VAL_ENTRY_SZ          sizeof((sensor_val_t){ {}, {}, { 0, }, NULL})
#define VAL_TAG_SZ            sizeof((sensor_tag_t){ {}, {}, })

#define SENSOR_TYPES(SENSOR)  \
    SENSOR(0,  BATTERY,      INT_ADC )   \
    SENSOR(1,  LIGHT,        INT_ADC )   \
    SENSOR(2,  MOTION,       INT_INT )   \
    SENSOR(3,  HUMIDITY,     INT_ADC )   \
    SENSOR(4,  TEMPERATURE,  INT_TEMP)   \
    SENSOR(5,  CONTACT,      INT_INT )   \
    SENSOR(6,  IDENTITY,     INT_NONE)   \
    SENSOR(7,  NA7,          INT_NONE)   \
    SENSOR(8,  UV_LIGHT,     INT_ADC )   \
    SENSOR(9,  PROXIMITY,    INT_NONE)   \
    SENSOR(10, PARTICLES,    INT_HPM )   \
    SENSOR(11, GASSES,       INT_CCS )   \
    SENSOR(12, FORCE,        INT_HX  )   \
    SENSOR(13, PRESENCE,     INT_NONE)   \
    SENSOR(14, CURRENT,      INT_CUR)    \
	SENSOR(255, INVALID,     INT_NONE)

#define SENSOR_ENTRIES(id, type, iface)   SENSOR_ENTRY(id, type, iface),
#define SENSOR_ENTRY(id, type, iface) \
    { (sensor_type_t)id, BUILD_STRING(type), iface, false, 0, 255, 0, }

#define SENSOR_ENUM(_x, Name, _y)       SENSOR_##Name = _x,

/*!
    @enum sensor_type_t 
	@brief Types of sensors

	Identifiers for the sensor types or capabilites we support
 */
typedef enum sensor_type { SENSOR_TYPES(SENSOR_ENUM) } sensor_type_t;
#define SENSOR_NONE   SENSOR_INVALID

#define PRESENCE_STATE(STATE) \
    STATE(PRESENT)            \
	STATE(NOT_PRESENT)        \
	STATE(UNKNOWN)            \
	STATE(MAX)

#define PRESENCE_ENUM(Name)       PRESENCE_##Name,

/*!
    @enum presence_t 
	@brief `presence` states

	State values to represent 'presence'
 */
typedef enum presence { PRESENCE_STATE(PRESENCE_ENUM) } presence_t;

#define MOTION_STATE(STATE) \
	STATE(INACTIVE)            \
	STATE(ACTIVE)             \
	STATE(MAX)

#define MOTION_ENUM(Name)       MOTION_##Name,

/*!
    @enum motion_t 
	@brief `motion` states

	State values to represent 'motion'
 */
typedef enum motion { MOTION_STATE(MOTION_ENUM) } motion_t;

#define CONTACT_STATE(STATE) \
	STATE(CLOSED)            \
	STATE(OPEN)              \
	STATE(MAX)

#define CONTACT_ENUM(Name)       CONTACT_##Name,

/*!
    @enum contact_t 
	@brief `contact` states

	State values to represent 'contact'
 */
typedef enum contact { CONTACT_STATE(CONTACT_ENUM) } contact_t;

/*!
    @enum update_scope_t 
	@brief Services to provide update scope

	Bitmask representing which service 'scopes' an update belongs
	to.  This will dictate which handlers are called for delivery.
 */
typedef enum update_scope {
	SCOPE_NONE         = (0x0),
	SCOPE_INFLUX       = (1 << 0),
	SCOPE_SMARTTHINGS  = (1 << 1),
	SCOPE_NOTIFY       = (1 << 2),
	SCOPE_MAX          = (0xFF)
} update_scope_t;

typedef uint8_t update_scopes_t;
typedef uint8_t failed_scopes_t;

/*!
    @enum interface_t 
	@brief Sensor physical interfaces

	Defines what type of interface the sensor type uses on the device.
	Interfaces must be implementd on the device, and can be reused between
	sensors.  When a device receives its configuration, these are the
	physical interfaces that will be configured to provide data for the
	sensor types.
 */
typedef enum INTERFACE {
	INT_NONE = 0,
	INT_ADC  = (1 << 0),
	INT_INT  = (1 << 1),
	INT_TEMP = (1 << 2),
	INT_ADS  = (1 << 3),
	INT_PMS  = (1 << 5),
	INT_CCS  = (1 << 6),
	INT_HX   = (1 << 7),
	// the rest are out of BLE range
	INT_CUR  = (1 << 8),
	INT_SHT  = (1 << 9),
	INT_HPM  = (1 << 10),
} interface_t;

/*!
	@enum update_status_t
    @brief State of a requested update/event

	Track the state of the update for the various scopes
	provided on the request tracking the delivery lifecycle.
 */
typedef enum update_stats {
	UPDATE_SUCCESS,
	UPDATE_FAIL,
	UPDATE_MAX,
} update_status_t;

/*!
	@var typedef char[] sensor_name
	@brief Unique string name for sensor type

	Key for serialized communication (i.e. JSON) where we
	don't maintain a common struct like we do for the GATT 
	interface.
*/
typedef	char		sensor_name_t[24];

/*!
	@var typedef uint16_t sensor_idx_t
    @brief The device index of the installed sensor

 */
typedef	uint8_t		sensor_idx_t;

/*!
	@var typedef char[] attribute_t
    @brief A string representing a sensors attribute

 */
typedef	char		attribute_t[14];
typedef char		tag_val_t[12];

typedef struct sensor_s {
	sensor_type_t	    id;
    attribute_t			type;
	interface_t			interface;
	bool				in_use;
	uint16_t			state;
	sensor_idx_t        device_idx;
	unsigned long int	updatedAt;
} sensor_t;

typedef enum sensor_data_type {
	VAL_U16,
} sensor_val_type_t;

typedef struct sensor_val {
	attribute_t       attribute;
	sensor_val_type_t val_type;
	union  {
		uint16_t      u16;
	};
	void              *value;
} sensor_val_t;

typedef struct sensor_tag {
	tag_val_t key;
	tag_val_t val;
} sensor_tag_t;

typedef struct sensor_multi_data {
	uint8_t           sensor_id;
	sensor_type_t     type;
	uint8_t           num_values;
    update_scopes_t	  scopes;
	sensor_tag_t      *tags;
	sensor_val_t      *values;
} sensor_multi_data_t;

typedef struct sensor_data_entry {
	uint8_t           *sensor_id;
	sensor_type_t     *type;
	sensor_val_t      *value;
	sensor_tag_t      tag;
} sensor_data_entry_t;

typedef struct sensor_data {
	uint8_t           sensor_id;
	sensor_type_t     type;
	sensor_val_type_t val_type;
	union  {
		uint16_t      u16;
	};
	void              *value;
} sensor_data_t;

#define SENSOR_DEFAULTS   SENSOR_ENTRY(255, INVALID, INT_NONE)

extern  const  char       *MOTION_STRING[];
extern  const  char       *PRESENCE_STRING[];
extern  const  char       *CONTACT_STRING[];
extern  const  char       *ACTION_STRING[];

extern const	sensor_t	INVALID_SENSOR;

/*!
    @brief Prepare sensor layer of payload stack

    The payload layer is modified in place  for the calling higher order entity
    @param sensora  ptr to the appropriate sensor index of the originating device 
    @param data one sensor slice from the device payload
    @return uint8_t 
 */
uint8_t			sensor_process_payload(sensor_t*, sensor_multi_data_t*);

/*!
    @brief Update sensor entry of payload with device index and type 

    @param entry  ptr to a sensor entry of a device payload 
    @param sensor_index  the devices index number for this sensor   
    @param type the sensors type
 */
void 			sensor_payload_entry_id(sensor_data_entry_t , uint8_t , sensor_type_t);

/*!
    @brief Update sensor entry of payload with 'tags'

    The 'tags' allow us to tack on additional key/value info to the same entry
    attribute (data point), providing for any number of dimesions in the time
    series data.
    @param entry  ptr to a sensor entry of a device payload 
    @param tag Tag (k/v) to add.  sensor_tag_t =~ attribute_t / attribute_t 
 */

void			sensor_payload_entry_tag(sensor_data_entry_t , sensor_tag_t);

/*!
    @brief Update sensor entry of payload with the primary data 

    Each sensor entry contains only one attribute / value pair which provides
    the measurement output.  The sensor may provide several entries from a reading,
    each representing a unique measurement.
    @param entry  ptr to a sensor entry of a device payload 
    @param attr  the string which describe the unie of measurement receieved
    @param val_type how to interpret the value represented in binary
    @param val  the value as a stdint type up to 32-bits
 */
void			sensor_payload_entry_attr(sensor_data_entry_t , const char*, sensor_val_type_t , void *);

/*!
    @brief Return a usable type string provided a TYPE_ENUM

    Necessary evil for the ability to serialize between services without coupling.
    @param type 
    @return const sensor_t* 
*/
void			sensor_type_get_name(sensor_type_t, attribute_t);

/*!
    @brief Get reference to definition of SENSOR_TYPE 

    @param type 
    @return const sensor_t* 
 */
const	sensor_t*	sensor_get_by_type_name(attribute_t);

/*!
    @brief Return a sensor type refernce using the string name

    Necessary evil for the ability to serialize between services without coupling.
    @param type string representation of the type (sensor.type)
    @return const sensor_t* 
 */
const	sensor_t*	sensor_get_by_type(sensor_type_t);

uint8_t sensor_payload_alloc_entry(sensor_multi_data_t*);
sensor_data_entry_t sensor_payload_get_entry(sensor_multi_data_t*, uint8_t);
void sensor_update_interface(sensor_type_t entry, interface_t interface);

#endif 
