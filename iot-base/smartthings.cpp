#include <HardwareSerial.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <stdlib.h>
#include "SPIFFS.h"
#include "StreamString.h"
#include "ESP32SSDP.h"

#include "network.h"
#include "sensor.h"
#include "ble.h"
#include "smartthings.h"

stconfig_t m_stconfig = { ST_HUB, "\0", "\0", };
stconfig_t *ST_CONFIG = &m_stconfig;
	
uint8_t st_send_event(char *body) {
    int MSG_SIZE = 1000;
    char *header;

    char size[4];
    char port[6];

    sprintf(size, "%u", strlen(size));
    sprintf(port, "%i", ST_PORT);

    header = (char*)malloc(MSG_SIZE * (sizeof(*header)));

    header[0] = '\0';
    strcat(header, "NOTIFY / HTTP/1.1");
    strcat(header, "\r\nHost: ");
    strcat(header, ST_CONFIG->hubip);
    strcat(header, ":");
    strcat(header, port);
    strcat(header, "\r\nUser-Agent: custom-network/0.1");
    strcat(header, "\r\nAccept: */*");
    strcat(header, "\r\ncustom-network: true");
    strcat(header, "\r\nContent-type: application/json");
    strcat(header, "\r\nContent-length: ");
    strcat(header, size);
    strcat(header, "\r\n\r\n");

    if(!client->connect(ST_CONFIG->hubip, ST_PORT, TCP_CONN_TIMEOUT)) {
	    Serial.println("failed to connect to st_hub");
		client->stop();
		free(header);
	    return false;
    }
  
    client->write(header);
    client->write(body);

  	client->stop();
    free(header);
	return true;
}

void parseUrl(char **buf, char *str) {
	int i = 0, j = 0;
	char *begin, *end;
	
	while(i < 2) {
		if(str[j] == '/') {
			i++;
			j++;
			begin = &str[j];
			while(str[j] != '/') {
				j++;
			}
			end = &str[j];
		} else {
			j++;
		}
	}

	strncpy(buf[0], begin, end-begin);
	strcpy(buf[1], end);
}


uint16_t stApiReq(char *verb, char *endpoint, char *body, char *resp, size_t resp_sz) {
    int MSG_SIZE = 1000;
    int MAX_SEG_SIZE = 200;
    char size[4];
	uint16_t err_code;

    char *resource = (char*)calloc(MAX_SEG_SIZE, sizeof(char));
    char *basepath = (char*)calloc(MAX_SEG_SIZE, sizeof(char));
    char *port;
  
    char **p = (char**)malloc(2*sizeof(char*));
    p[0] = resource;
    p[1] = basepath;
  
    parseUrl(p, ST_CONFIG->apiurl);
  
    const char *host = strtok(resource, ":");

    Serial.printf("connecting to host: %s\n", host);
  
   if (!ssl_client->connect(host, 443, TCP_CONN_TIMEOUT)) {
       char err[100];
       ssl_client->lastError(err, sizeof(err));
       Serial.println(err);
	   ssl_client->stop();
	   free(p[0]);
	   free(p[1]);
	   free(p);
	   return false;
    }
   
    char *header = (char*)malloc(MSG_SIZE*sizeof(char));

    header[0] = '\0';
    strcat(header, verb);
    strcat(header, " ");
    strcat(header, basepath);
    strcat(header, endpoint);
    strcat(header, " HTTP/1.1");
    strcat(header, "\r\nHost: ");
    strcat(header, host);
    strcat(header, "\r\nAuthorization: Bearer ");
    strcat(header, ST_CONFIG->token);
    strcat(header, "\r\nUser-Agent: custom-network/0.1");
    if(strlen(body) > 0) {
	    sprintf(size, "%u", strlen(size));
        strcat(header, "\r\nAccept: */*");
        strcat(header, "\r\nContent-type: application/json");
        strcat(header, "\r\nContent-length: ");
        strcat(header, size);
    }
    strcat(header, "\r\n\r\n\0");
  
    ssl_client->print(header);

    if(strlen(body) > 0) {
        ssl_client->print(body);
	}

 	ssl_client->readBytesUntil('\r', resp, resp_sz);
	
 	if(strcmp(resp, "HTTP/1.1 200 OK") != 0) {
	 	char code[4] = { '\0' };
	 	memcpy(code, resp+9, 3);
	 	Serial.printf("CODE: %s\n", code);
	 	err_code = (uint16_t)atoi(code);
 	} else {
		ssl_client->readBytes(resp, resp_sz);
		err_code = 200;
	}

 	ssl_client->stop();
    free(p[0]);
    free(p[1]);
    free(p);
    free(header);
    return err_code;
}

uint8_t st_init_device(char *deviceId) {
	int RESP_SIZE = 1000;
	int ID_SIZE = 5;
	
	char *endpoint = (char*)calloc(50, sizeof(char));
	char *resp = (char*)calloc(RESP_SIZE, sizeof(char));

	sprintf(endpoint, "/new/%s", deviceId);
	
	if(stApiReq("GET\0", endpoint, "\0", resp, RESP_SIZE) != 200) {
		free(endpoint);
		free(resp);
		return false;
	}
	
	free(endpoint);

	StreamString *s = new StreamString();
	s->write((uint8_t*)resp, RESP_SIZE);
	memset(resp, 0, RESP_SIZE);
	s->readBytesUntil('{', resp, RESP_SIZE);
	s->readBytesUntil(':', resp, RESP_SIZE);
	s->readBytesUntil('"', resp, RESP_SIZE);
	s->readBytesUntil('"', deviceId, ID_SIZE);

	delete(s);
	free(resp);
	
	return true;
}

uint16_t getSensors(device_t *device) {
	uint16_t err_code;
	int RESP_SIZE = 1000;
	DynamicJsonDocument root(300);
	char *endpoint = (char*)calloc(80, sizeof(char));
	sprintf(endpoint, "/config/%s", device->id);

	Serial.println("getting device configuration from ST API");
	Serial.print("GET ");
	Serial.println(endpoint);

	char *resp = (char*)calloc(RESP_SIZE, sizeof(char));
	
	if((err_code = stApiReq("GET\0", endpoint, "\0", resp, RESP_SIZE)) != 200) {
		Serial.printf("unable to send request: got %d\n", err_code);
		free(resp);
		free(endpoint);
		return err_code;
	}

	char *p1 = index(resp, '[');
	char *p2 = strtok(p1, "\r");

	Serial.printf("resp: 200: %s\n", p2);

	deserializeJson(root, p2);
	JsonArray arr = root.as<JsonArray>();

	if(arr.isNull()) {
		Serial.println("failed to parse response");
		free(resp);
		free(endpoint);
		return false;
	}
	
	sensor_t sensors[MAX_SENSORS] = { INVALID_SENSOR };
	Serial.println("updating device config");
	for(int i=0; i<arr.size(); i++) {
		JsonObject root = arr[i];
		sensors[i] = SENSOR_TYPE[(uint8_t)root["type"]];
	}
	
	memcpy(device->sensors, sensors, sizeof(sensors));
	
	free(resp);
	free(endpoint);
	
	return err_code;
}

void configureSSDP() {
	SSDP.setSchemaURL("schema.xml");
	SSDP.setDeviceType("urn:schemas-upnp-org:device:CustomNet:1");
	SSDP.setHTTPPort(88);
	SSDP.setName("BLE Sensor Controller");
	SSDP.setSerialNumber("1");
	SSDP.setURL("index.html");
	SSDP.setModelName("BLESensor v1");
	SSDP.setModelNumber("1");
	SSDP.setManufacturer("Hodge Industries");
}

class CustomNetServer
{
	public:
	CustomNetServer();
	void setup(WebServer *server, WiFiManager *wifiManager, stconfig_t *hubip);
	private:
	WebServer *_server;
	WiFiManager *_wifi;
	stconfig_t *_stconfig;
};

CustomNetServer::CustomNetServer()
{
	_server = NULL;
	_wifi = NULL;
	_stconfig = NULL;
}

void CustomNetServer::setup(WebServer *server, WiFiManager *wifiManager, stconfig_t *stconfig)
{
	_server = server;
	_wifi = wifiManager;
	_stconfig = stconfig;
	
	_server->on("/register", HTTP_PUT, [&](){
		if (!_server->hasArg("hubip") || \
				!_server->hasArg("apiurl") || \
				!_server->hasArg("token")) {
			_server->send(200, F("text/html"), String(F("Update error")));
			return;
		}
		String hubip = _server->arg("hubip");
		char *hubptr = (char*)hubip.c_str();
		if(!strcmp(hubptr, _stconfig->hubip)) {
			Serial.println("hub IP is up-to-date");
		} else {
			strncpy(_stconfig->hubip, hubptr, hubip.length());
			File f = SPIFFS.open("/hubip", "w");
			f.print(hubip);
			f.close();
			Serial.print("updated ST_HUB = ");
			Serial.println(_stconfig->hubip);
			
			String apiurl = _server->arg("apiurl");
			char *apiptr = (char*)apiurl.c_str();
			if(!strcmp(apiptr, _stconfig->apiurl)) {
				Serial.println("API URL is up-to-date");
			} else {
				strncpy(_stconfig->apiurl, apiptr, apiurl.length());
				File f = SPIFFS.open("/apiurl", "w");
				f.print(apiptr);
				f.close();
				Serial.print("updated API URL = ");
			}
			String token = _server->arg("token");
			char *tknptr = (char*)token.c_str();
			if(!strcmp(tknptr, _stconfig->token)) {
				Serial.println("token is up-to-date");
			} else {
				strncpy(_stconfig->token, tknptr, token.length());
				File f = SPIFFS.open("/token", "w");
				f.print(tknptr);
				f.close();
				Serial.print("updated API token = ");
				Serial.println(_stconfig->token);
			}
		}
		_server->send_P(200, PSTR("text/html"), "done");
	});
	
	_server->on("/erase/wifi", HTTP_GET, [&](){
		_wifi->resetSettings();
		if(SPIFFS.exists("/hubip")) {
			SPIFFS.remove("/hubip");
		}
		_server->send_P(200, PSTR("text/html"), "done");
		ESP.restart();
	});
};

void formatSPIFFS() {
	Serial.println("formatting SPIFFS..");
	SPIFFS.end();
	SPIFFS.format();
	SPIFFS.begin();
	File f = SPIFFS.open("/formatted", "w");
	f.close();
}

uint8_t st_config_loaded() {
	return (strlen(ST_CONFIG->apiurl) && strlen(ST_CONFIG->token));
}

void wait_for_config() {
	configureSSDP();
    while(!SSDP.begin()) {
		Serial.printf("failed to start ssdp. retrying..\n");
		vTaskDelay(1000 / portTICK_RATE_MS);
	}
	Serial.printf("waiting for ssdp broadcast..\n");
	while(!st_config_loaded()) {
		vTaskDelay(100 / portTICK_RATE_MS);
	}
	SSDP.end();
}

void load_st_config() {
	SPIFFS.begin();

	if(!SPIFFS.exists("/formatted")) {
		formatSPIFFS();
	}
	/*
	if(SPIFFS.exists("/hubip")) {
		File f = SPIFFS.open("/hubip", "r");
		String hubip = f.readString();
		strncpy(ST_CONFIG->hubip, hubip.c_str(), sizeof(ST_CONFIG->hubip));
		f.close();
		Serial.print("hub IP set: ");
		Serial.println(ST_CONFIG->hubip);
	} else {
		Serial.println("waiting to obtain hub IP");
	}
	*/
	if(SPIFFS.exists("/apiurl")) {
		File f = SPIFFS.open("/apiurl", "r");
		String apiurl = f.readString();
		strncpy(ST_CONFIG->apiurl, apiurl.c_str(), sizeof(ST_CONFIG->apiurl)-1);
		f.close();
		Serial.printf("API URL set: %s\n", ST_CONFIG->apiurl);
	} else {
		Serial.println("waiting to obtain API URL");
	}
	
	if(SPIFFS.exists("/token")) {
		File f = SPIFFS.open("/token", "r");
		String token = f.readString();
		strncpy(ST_CONFIG->token, token.c_str(), sizeof(ST_CONFIG->token));
		f.close();
		Serial.printf("API token set: %s\n", ST_CONFIG->token);
	} else {
		Serial.println("waiting to obtain API token");
	}
	
	if(!st_config_loaded()) {
		Serial.printf("waiting for smartthings config..\n");
		wait_for_config();
	}
	Serial.printf("stconfig loaded.\n");
}

void smartthings_task(void *ptx) {
	CustomNetServer customNetServer;
	WebServer httpServer(88);

	customNetServer.setup(&httpServer, wifiManager, ST_CONFIG);
	httpServer.begin();

	for(;;) {
		vTaskDelay(100 / portTICK_RATE_MS);
		httpServer.handleClient();
	}

}

void smartthings_init() {
	xTaskCreatePinnedToCore(smartthings_task, "smartthings_task", ST_STACK_SZ, NULL, 7, NULL, 1);
	load_st_config();
}
