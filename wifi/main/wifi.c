#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

// DEFINITIONS // 
#define WIFI_SUCCESS 1 << 0
#define WIFI_FAILURE 1 << 1
#define TCP_SUCCESS  1 << 0
#define TCP_FAILURE  1 << 1
#define MAX_FAILURES 10

// Global variables //

// event group containing status info
static EventGroupHandle_t wifi_event_group;

// retry tracker
static int retry = 0;

// task tag
static const char *TAG = "WIFI";

// FUNCTIONS // 

// wifi event handler

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
	{
		ESP_LOGI(TAG, "Connecting...");
		esp_wifi_connect();
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
	{
		if (retry < MAX_FAILURES)
		{
			ESP_LOGI(TAG, "Reconnecting...");
			esp_wifi_connect();
			retry++
		} else {
			xEventGroupSetBits(wifi_event_group, WIFI_FAILURE);
		}
	}
}

// ip event handler

static void ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
	if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
	{
		ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
		ESP_LOGI(TAG, "STA IP: " IPSTR, IP2STR(&event->ip_info.ip));
		retry = 0;
		xEventGroupSetBits(wifi_event_group, WIFI_SUCCESS);
	}
}

// connect to wifi and return result

esp_err_t connect_wifi()
{
	int status = WIFI_FAILURE;

	// initializations//

	// initialize esp network interface
	ESP_ERROR_CHECK(esp_netif_init());

	//initialize default event loop
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	// create wifi station
	esp_netif_create_default_wifi_sta();

	// setup wifi station with default configuration
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	// event loop
	wifi_event_group = xEventGroupCreate();

	esp_event_handler_instance_t wifi_handler_event_instance;
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
														ESP_EVENT_ANY_ID,
														&wifi_event_handler,
														NULL,
														&wifi_handler_event_instance));

	esp_event_handler_instance_t got_ip_event_instance;
	ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
														IP_EVENT_STA_GOT_IP,
														&ip_event_handler,
														NULL,
														&ip_handler_event_instance));

	wifi_config_t wifi_config = {
		.sta = {
			.ssid = "Castletown",
			.password = "tallpoodle413",
			.threshold.authmode = WIFI_AUTH_WPA2_PSK,
			.pm_cfg = {
				.capable = true,
				.required = false
			},
		},
	};

	// set wifi controller to be a station
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );

	// set wifi config
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );

	// start wifi driver
	ESP_ERROR_CHECK(esp_wifi_start());

	ESP_LOGI(TAG, "STA initialization complete")

	// wait for bits to indicate success or failure
	EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
			WIFI_SUCCESS | WIFI_FAILURE,
			pdFALSE,
			pdFALSE,
			portMAX_DELAY);

	// xEventGroupWaitBits returns bits before the call is returned, test which event actually happened
	if (bits & WIFI_SUCCESS) {
		ESP_LOGI(TAG, "Connected to AP");
		status = WIFI_SUCCESS;
	} else if (bits & WIFI_FAILURE) {
		ESP_LOGI(TAG, "Failed to connect to AP");
		status = WIFI_FAILURE;
	} else {
		ESP_LOGE(TAG, "Unexpected event");
		status = WIFI_FAILURE;
	}

	// event not processed after unregister
	ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, got_ip_event_instance));
	ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler_event_instance));
	vEventGroupDelete(wifi_event_group);

	return status;

}

esp_err_t connect_tcp_server(void){
	struct sockaddr_in serverInfo = {0};
	char readBuffer[1024] = {0};

	serverInfo.sin_family = AF_INET;
	serverInfo.sin_addr.s_addr = 0x0100007f;
	serverInfo.sin_port= htons(12345);

	int sock = socket(AF_INET, SOCK_STREAM, 0);

	if (sock < 0){
		ESP_LOGE(TAG, "Failed to create socket");
		return TCP_FAILURE;
	}

	if (connect(sock, (struct sockaddr *)&serverInfo, sizeof(serverInfo)) != 0){
		ESP_LOGE(TAG, "Failed to connect to %s", inet_ntoa(serverInfo.sin_addr.s_addr));
		close(sock);
		return TCP_FAILURE;
	}

	ESP_LOGI(TAG, "Connected to TCP server");
	bzero(readBuffer, sizeof(readBuffer));
	int r = read(sock, readBuffer, sizeof(readBuffer)-1);
	for(int i = 0; i < r; i++){
		putchar(readBuffer[i]);
	}

	if (strcomp(readBuffer, "HELLO") == 0){
		ESP_LOGI(TAG, "Connected");
	}

	return TCP_SUCCESS;
}

void app_main(void)
{
	esp_err_t status = WIFI_FAILURE;

	//initialize flash storage
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	// connect to wireless
	status = connect_wifi();
	if (WIFI_SUCCESS != status) {
		ESP_LOGI(TAG, "Failed to associate AP");
		return;
	}

	status = connect_tcp_server();
	if (TCP_SUCCESS != status) {
		ESP_LOGI(TAG, "Failed to connect to remote server");
		return;
	}
}
