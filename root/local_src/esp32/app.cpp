// Copyright (c) 2022 Artur Wiebe <artur@4wiebe.de>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
// associated documentation files (the "Software"), to deal in the Software without restriction,
// including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
// WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


#include <esp_log.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <cJSON.h>
#include <esp_http_client.h>
#include <esp_ota_ops.h>

static const char* TAG = "app";

void init(const cJSON *cfg);
void start();
void loop();

#include <app.hpp>


static void main_wifiScan(void* arg, esp_event_base_t, int32_t, void*)
{
	ESP_LOGI(TAG, "wifiScan");
	wifi_scan_config_t scanConfig = {};
	scanConfig.bssid = ((wifi_config_t*)arg)->sta.bssid;
	ESP_ERROR_CHECK(esp_wifi_scan_start(&scanConfig, false));
}

static void main_wifiConnect(void* arg, esp_event_base_t, int32_t, void*)
{
	ESP_LOGI(TAG, "wifiConnect");
	
	uint16_t apMax = 1, apCount = 0; wifi_ap_record_t apInfo = {};
	ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&apMax, &apInfo));
	ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&apCount));
	if (!apCount) {
		ESP_LOGE(TAG, "wifi ap not found");
		esp_restart();
	}
	wifi_config_t* wifiConfig = (wifi_config_t*) arg;
	strncpy((char*) wifiConfig->sta.ssid, (char*) apInfo.ssid, sizeof(wifiConfig->sta.ssid));
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, wifiConfig));
	ESP_ERROR_CHECK(esp_wifi_connect());
}

static void main_wifiConnected(void* arg, esp_event_base_t, int32_t, void*) {
	ESP_LOGI(TAG, "wifiConnected");
	xSemaphoreGive(*((SemaphoreHandle_t*)arg));
}

static void main_wifiDisconnected(void*, esp_event_base_t, int32_t, void*) {
	ESP_LOGE(TAG, "wifiDisconnected");
	esp_restart();
}


static void main_init()
{
	//### wifi sta init
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_t	*wifiSta = esp_netif_create_default_wifi_sta();
	wifi_init_config_t wifiInitConfig = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&wifiInitConfig));
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	
	//### open cfg partition
	const esp_partition_t *cfgPart = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "cfg");
	const char *cfgData; spi_flash_mmap_handle_t cfgDataHandle;
	ESP_ERROR_CHECK(esp_partition_mmap(cfgPart, 0, cfgPart->size, SPI_FLASH_MMAP_DATA, (const void**) &cfgData, &cfgDataHandle));
	cJSON *cfg = cJSON_Parse(cfgData);
	
	//### wifi config
	const cJSON *cfgWifi = cJSON_GetObjectItem(cfg, "wifi");
	const cJSON *cfgWifiPsk   = cJSON_GetObjectItem(cfgWifi, "psk");
	const cJSON *cfgWifiBssid = cJSON_GetObjectItem(cfgWifi, "bssid");
	wifi_config_t wifiConfig = {};
	strncpy((char*) wifiConfig.sta.password, cfgWifiPsk->valuestring, sizeof(wifiConfig.sta.password));
	const cJSON *bssidByte = NULL; int i = 0; 
	cJSON_ArrayForEach(bssidByte, cfgWifiBssid) {
		wifiConfig.sta.bssid[i++] = bssidByte->valueint;
	}
	
	//### wifi start
	StaticSemaphore_t wifiConnectedSemaphoreBuffer; SemaphoreHandle_t wifiConnectedSemaphore = xSemaphoreCreateBinaryStatic(&wifiConnectedSemaphoreBuffer);
	esp_event_handler_instance_t wifiScanHandler, wifiConnectHandler, wifiConnectedHandler, wifiDisconnectedHandler;
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_START,        &main_wifiScan,         &wifiConfig, &wifiScanHandler));
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE,        &main_wifiConnect,      &wifiConfig, &wifiConnectHandler));
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED,    &main_wifiConnected,    &wifiConnectedSemaphore, &wifiConnectedHandler));
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &main_wifiDisconnected, NULL, &wifiDisconnectedHandler));
	ESP_ERROR_CHECK(esp_wifi_start());
	
	//### static ip
	esp_netif_ip_info_t ipInfo = {};
	IP4_ADDR(&ipInfo.netmask, 255, 255, 255, 0);
	ipInfo.ip.addr = ipaddr_addr(MAIN_NET_IP);
	ESP_ERROR_CHECK(esp_netif_dhcpc_stop(wifiSta));
	ESP_ERROR_CHECK(esp_netif_set_ip_info(wifiSta, &ipInfo));
	
	//### init nvs
	esp_err_t nvsErr = nvs_flash_init();
	if (nvsErr == ESP_ERR_NVS_NO_FREE_PAGES || nvsErr == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		//ESP_ERROR_CHECK(nvs_flash_erase());
		//nvsErr = nvs_flash_init();
	}
	ESP_ERROR_CHECK(nvsErr);
	
	//### app init
	ESP_LOGI(TAG, "app init");
	init(cfg);
	
	//### wait wifi connected
	xSemaphoreTake(wifiConnectedSemaphore, portMAX_DELAY);
	
	//### ota connect
	char otaBuffer[65];
	esp_http_client_config_t otaClientConfig = {};
	otaClientConfig.url = cJSON_GetObjectItem(cfg, "ota")->valuestring;
	esp_http_client_handle_t otaClient = esp_http_client_init(&otaClientConfig);
	esp_ota_get_app_elf_sha256(otaBuffer, sizeof(otaBuffer));
	ESP_ERROR_CHECK(esp_http_client_set_header(otaClient, "Hash", otaBuffer));
	ESP_ERROR_CHECK(esp_http_client_open(otaClient, 0));
	
	//### cleanup
	ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_STA_START,     wifiScanHandler));
	ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_SCAN_DONE,     wifiConnectHandler));
	ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, wifiConnectedHandler));
	vSemaphoreDelete(wifiConnectedSemaphore);
	cJSON_Delete(cfg);
	spi_flash_munmap(cfgDataHandle);
	
	//### ota finish
	esp_http_client_fetch_headers(otaClient);
	if (esp_http_client_get_status_code(otaClient) == HttpStatus_Ok)
	{
		ESP_LOGI(TAG, "ota update");
		esp_ota_handle_t updateHandle;
		const esp_partition_t *updatePart = esp_ota_get_next_update_partition(NULL);
		ESP_ERROR_CHECK(esp_ota_begin(updatePart, esp_http_client_get_content_length(otaClient), &updateHandle));
		while (!esp_http_client_is_complete_data_received(otaClient)) {
			int chunkSize = esp_http_client_read(otaClient, otaBuffer, sizeof(otaBuffer));
			ESP_ERROR_CHECK(esp_ota_write(updateHandle, (const void *)otaBuffer, chunkSize));
		}
		ESP_ERROR_CHECK(esp_ota_end(updateHandle));
		ESP_ERROR_CHECK(esp_ota_set_boot_partition(updatePart));
		esp_restart();
	}
	ESP_ERROR_CHECK_WITHOUT_ABORT(esp_http_client_cleanup(otaClient));
}


extern "C" void app_main(void)
{
	main_init();
	
	ESP_LOGI(TAG, "app start");
	start();
	
#ifdef MAIN_LOOP_PERIOD
	ESP_LOGI(TAG, "app loop");
	for (TickType_t xLastWakeTime = xTaskGetTickCount();;) {
		loop();
		vTaskDelayUntil(&xLastWakeTime, MAIN_LOOP_PERIOD/portTICK_PERIOD_MS);
	}
#endif
}
