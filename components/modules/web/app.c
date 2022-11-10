/************************************ appwifi.c *********************************/
/* Mulitipart Upload Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_vfs.h"
//#include "esp_spiffs.h"

#include "lwip/err.h"
#include "lwip/sys.h"


/*************************** cmd.h *******************************/

#define CMD_SEND 100
#define CMD_HALT 400


typedef struct {
	uint16_t command;
	uint8_t* pdata;
	int size;
	TaskHandle_t taskHandle;
} REQUEST_t;

typedef struct {
	uint16_t command;
	char response[256];
	TaskHandle_t taskHandle;
} RESPONSE_t;
/*********************************************************************/

///#include "cmd.h"

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID	   CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS	   CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT	   BIT1

static const char *TAG_MAIN = "MAIN";

static int s_retry_num = 0;

static QueueHandle_t xQueueRequest = NULL;
static QueueHandle_t xQueueResponse = NULL;

static void event_handler(void* arg, esp_event_base_t event_base,
								int32_t event_id, void* event_data)
{
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		esp_wifi_connect();
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
			esp_wifi_connect();
			s_retry_num++;
			ESP_LOGI(TAG_MAIN, "retry to connect to the AP");
		} else {
			xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
		}
		ESP_LOGI(TAG_MAIN,"connect to the AP fail");
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
		ESP_LOGI(TAG_MAIN, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
		s_retry_num = 0;
		xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
	}
}

void wifi_init_sta(void)
{
	s_wifi_event_group = xEventGroupCreate();

	ESP_ERROR_CHECK(esp_netif_init());

	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_create_default_wifi_sta();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	esp_event_handler_instance_t instance_any_id;
	esp_event_handler_instance_t instance_got_ip;
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
														ESP_EVENT_ANY_ID,
														&event_handler,
														NULL,
														&instance_any_id));
	ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
														IP_EVENT_STA_GOT_IP,
														&event_handler,
														NULL,
														&instance_got_ip));

	wifi_config_t wifi_config = {
		.sta = {
			.ssid = EXAMPLE_ESP_WIFI_SSID,
			.password = EXAMPLE_ESP_WIFI_PASS,
			/* Setting a password implies station will connect to all security modes including WEP/WPA.
			 * However these modes are deprecated and not advisable to be used. Incase your Access point
			 * doesn't support WPA2, these mode can be enabled by commenting below line */
		 .threshold.authmode = WIFI_AUTH_WPA2_PSK,

			.pmf_cfg = {
				.capable = true,
				.required = false
			},
		},
	};
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
	ESP_ERROR_CHECK(esp_wifi_start() );

	ESP_LOGI(TAG_MAIN, "wifi_init_sta finished.");

	/* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
	 * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
	EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
			WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
			pdFALSE,
			pdFALSE,
			portMAX_DELAY);

	/* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
	 * happened. */
	if (bits & WIFI_CONNECTED_BIT) {
		ESP_LOGI(TAG_MAIN, "connected to ap SSID:%s password:%s",
				 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
	} else if (bits & WIFI_FAIL_BIT) {
		ESP_LOGI(TAG_MAIN, "Failed to connect to SSID:%s, password:%s",
				 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
	} else {
		ESP_LOGE(TAG_MAIN, "UNEXPECTED EVENT");
	}

	/* The event will not be processed after unregister */
	ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
	ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
	vEventGroupDelete(s_wifi_event_group);
}

#if 0
static void SPIFFS_Directory(char * path) {
	DIR* dir = opendir(path);
	assert(dir != NULL);
	while (true) {
		struct dirent*pe = readdir(dir);
		if (!pe) break;
		ESP_LOGI(__FUNCTION__,"d_name=%s d_ino=%d d_type=%x", pe->d_name,pe->d_ino, pe->d_type);
	}
	closedir(dir);
}
#endif


void wifi_init()
{
	//Initialize NVS
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
	  ESP_ERROR_CHECK(nvs_flash_erase());
	  ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	ESP_LOGI(TAG_MAIN, "ESP_WIFI_MODE_STA");
	wifi_init_sta();

}


/************************************* multipart-upload.c **********************************/
/* HTTP POST Example using plain POSIX sockets

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
//#include <vector>

///#include "cmd.h"

/* Constants that are configurable in menuconfig */
#if 1
#define CONFIG_WEB_SERVER "172.16.35.46"
#define CONFIG_WEB_PORT "8080"
#define CONFIG_WEB_PATH "/upload_multipart"
#endif

#define BOUNDARY "X-ESPIDF_MULTIPART"

extern QueueHandle_t xQueueRequest;
extern QueueHandle_t xQueueResponse;

static const char *TAG_POST = "POST";

void http_post_task(void *pvParameters);

void register_post()
{
	// Create Queue
	xQueueRequest = xQueueCreate( 1, sizeof(REQUEST_t) );
	xQueueResponse = xQueueCreate( 1, sizeof(RESPONSE_t) );
	configASSERT( xQueueRequest );
	configASSERT( xQueueResponse );

	// Create Task 
	xTaskCreate(&http_post_task, "POST", 4096, NULL, 5, NULL);
}

void send_photo(uint8_t* pdata, int size)
{
	if (pdata == NULL || size == 0)
	{
		ESP_LOGE(TAG_POST, "Empty data");
		return;
	}
	REQUEST_t requestBuf;
	requestBuf.command = CMD_SEND;
	requestBuf.taskHandle = xTaskGetCurrentTaskHandle();
	RESPONSE_t responseBuf;
	
	requestBuf.pdata = pdata;
	requestBuf.size = size;
	if (xQueueSend(xQueueRequest, &requestBuf, 10) != pdPASS) {
		ESP_LOGE(TAG_POST, "xQueueSend fail");
	} else {
		xQueueReceive(xQueueResponse, &responseBuf, portMAX_DELAY);
		ESP_LOGI(TAG_POST, "\n%s", responseBuf.response);
#if 0
		for(int i = 0; i < strlen(responseBuf.response); i++) {
			putchar(responseBuf.response[i]);
		}
		printf("\n");
#endif
	}
	ESP_LOGI(TAG_POST, "Data uploded");
}


void http_post_task(void *pvParameters)
{
	const struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};
	struct addrinfo *res;
	struct in_addr *addr;
	char recv_buf[64];

	REQUEST_t requestBuf;
	RESPONSE_t responseBuf;

	while(1)
	{
		ESP_LOGI(TAG_POST,"Waitting....");
		xQueueReceive(xQueueRequest, &requestBuf, portMAX_DELAY);
		ESP_LOGI(TAG_POST,"requestBuf.command=%d", requestBuf.command);
		if (requestBuf.command == CMD_HALT) break;

		int err = getaddrinfo(CONFIG_WEB_SERVER, CONFIG_WEB_PORT, &hints, &res);

		if(err != 0 || res == NULL) {
			strcpy(responseBuf.response,"DNS lookup failed");
			xQueueSend(xQueueResponse, &responseBuf, 10);
			ESP_LOGE(TAG_POST, "DNS lookup failed err=%d res=%p", err, res);
			vTaskDelay(1000 / portTICK_PERIOD_MS);
			continue;
		}

		/* Code to print the resolved IP.
		   Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
		addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
		ESP_LOGI(TAG_POST, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));

		int s = socket(res->ai_family, res->ai_socktype, 0);
		if(s < 0) {
			strcpy(responseBuf.response,"Failed to allocate socket");
			xQueueSend(xQueueResponse, &responseBuf, 10);
			ESP_LOGE(TAG_POST, "... Failed to allocate socket.");
			freeaddrinfo(res);
			vTaskDelay(1000 / portTICK_PERIOD_MS);
			continue;
		}
		ESP_LOGI(TAG_POST, "... allocated socket");

		if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
			strcpy(responseBuf.response,"socket connect failed");
			xQueueSend(xQueueResponse, &responseBuf, 10);
			ESP_LOGE(TAG_POST, "... socket connect failed errno=%d", errno);
			close(s);
			freeaddrinfo(res);
			vTaskDelay(4000 / portTICK_PERIOD_MS);
			continue;
		}

		ESP_LOGI(TAG_POST, "... connected");
		freeaddrinfo(res);

		char HEADER[512];
		char header[128];

		sprintf(header, "POST %s HTTP/1.1\r\n", CONFIG_WEB_PATH);
		strcpy(HEADER, header);
		sprintf(header, "Host: %s:%s\r\n", CONFIG_WEB_SERVER, CONFIG_WEB_PORT);
		strcat(HEADER, header);
		sprintf(header, "User-Agent: esp-idf/%d.%d.%d esp32\r\n", ESP_IDF_VERSION_MAJOR, ESP_IDF_VERSION_MINOR, ESP_IDF_VERSION_PATCH);
		strcat(HEADER, header);
		sprintf(header, "Accept: */*\r\n");
		strcat(HEADER, header);
		sprintf(header, "Content-Type: multipart/form-data; boundary=%s\r\n", BOUNDARY);
		strcat(HEADER, header);
	
		char BODY[512];
		sprintf(header, "--%s\r\n", BOUNDARY);
		strcpy(BODY, header);
		sprintf(header, "Content-Disposition: form-data; name=\"upfile\"; filename=\"%lu.jpg\"\r\n", (unsigned long) (esp_timer_get_time() / 1000ULL));
		strcat(BODY, header);
		sprintf(header, "Content-Type: application/octet-stream\r\n\r\n");
		strcat(BODY, header);

		char END[128];
		sprintf(header, "\r\n--%s--\r\n\r\n", BOUNDARY);
		strcpy(END, header);
		
		int dataLength = strlen(BODY) + strlen(END) + requestBuf.size;
		sprintf(header, "Content-Length: %d\r\n\r\n", dataLength);
		strcat(HEADER, header);

		ESP_LOGD(TAG_POST, "[%s]", HEADER);
		if (write(s, HEADER, strlen(HEADER)) < 0) {
			ESP_LOGE(TAG_POST, "... socket send failed");
			close(s);
			vTaskDelay(4000 / portTICK_PERIOD_MS);
			continue;
		}
		ESP_LOGI(TAG_POST, "HEADER socket send success");

		ESP_LOGD(TAG_POST, "[%s]", BODY);
		if (write(s, BODY, strlen(BODY)) < 0) {
			ESP_LOGE(TAG_POST, "... socket send failed");
			close(s);
			vTaskDelay(4000 / portTICK_PERIOD_MS);
			continue;
		}
		ESP_LOGI(TAG_POST, "BODY socket send success");

#if 0
		FILE* f=fopen(requestBuf.localFileName, "rb");
		uint8_t dataBuffer[128];
		if (f == NULL) {
			ESP_LOGE(TAG_POST, "Failed to open file for reading");
		}
		while(!feof(f)) {
			int len = fread(dataBuffer, 1, sizeof(dataBuffer), f);
			if (write(s, dataBuffer, len) < 0) {
				ESP_LOGE(TAG_POST, "... socket send failed");
				close(s);
				vTaskDelay(4000 / portTICK_PERIOD_MS);
				continue;
			}
		}
		fclose(f);
#else
		if (requestBuf.size == 0)
		{
			ESP_LOGE(TAG_POST, "Data is empty");
		}
		if (write(s, requestBuf.pdata, requestBuf.size) < 0)
		{
			ESP_LOGE(TAG_POST, "... socket send failed");
			close(s);
			vTaskDelay(4000 / portTICK_PERIOD_MS);
			continue;
		}
		/*while(len > 0)
		{
			if (len >= 1024)
			{
				res = write(s, requestBuf.pdata[p], 1024);
			}
			else
			{
				res = write(s, requestBuf.pdata[p], len);
			}
			if (res < 0)
			{
				ESP_LOGE(TAG_POST, "... socket send failed");
				close(s);
				vTaskDelay(4000 / portTICK_PERIOD_MS);
				continue;
			}
			len -= res;
			p += res;
		}*/
#endif
		ESP_LOGI(TAG_POST, "DATA socket send success");

		if (write(s, END, strlen(END)) < 0) {
			ESP_LOGE(TAG_POST, "... socket send failed");
			close(s);
			vTaskDelay(4000 / portTICK_PERIOD_MS);
			continue;
		}
		ESP_LOGI(TAG_POST, "END socket send success");

		struct timeval receiving_timeout;
		receiving_timeout.tv_sec = 5;
		receiving_timeout.tv_usec = 0;
		if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout,
				sizeof(receiving_timeout)) < 0) {
			ESP_LOGE(TAG_POST, "... failed to set socket receiving timeout");
			close(s);
			vTaskDelay(4000 / portTICK_PERIOD_MS);
			continue;
		}
		ESP_LOGI(TAG_POST, "... set socket receiving timeout success");

		/* Read HTTP response */
		int readed;
		bzero(responseBuf.response, sizeof(responseBuf.response));
		do {
			bzero(recv_buf, sizeof(recv_buf));
			ESP_LOGD(TAG_POST, "Start read now=%d", xTaskGetTickCount());
			readed = read(s, recv_buf, sizeof(recv_buf)-1);
			ESP_LOGD(TAG_POST, "End read now=%d readed=%d", xTaskGetTickCount(), readed);
#if 0
			for(int i = 0; i < readed; i++) {
				putchar(recv_buf[i]);
			}
#endif
			strcat(responseBuf.response, recv_buf);
		} while(readed > 0);
#if 0
		printf("\n");
#endif

		/* send HTTP response */
		if (xQueueSend(xQueueResponse, &responseBuf, 10) != pdPASS) {
			ESP_LOGE(TAG_POST, "xQueueSend fail");
		}

		ESP_LOGI(TAG_POST, "... done reading from socket. Last read return=%d errno=%d.", readed, errno);
		close(s);

	}
}
