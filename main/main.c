/* MQTT Broker for ESP32

	 This code is in the Public Domain (or CC0 licensed, at your option.)

	 Unless required by applicable law or agreed to in writing, this
	 software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
	 CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_mac.h"
#include <sys/fcntl.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_vfs_fat.h"
#include "mdns.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "mqtt_client.h"
#include "json_generator.h"
#include "json_parser.h"
#include "math.h"
#include "OLEDDisplay.h"
#include "driver/i2c.h"
#include "mongoose.h"
#include "app_http_server.h"
#include "wifi_connecting.h"

static nvs_handle_t NVS_HANDLE;
static const char *NVS_KEY = "Node1";
EventGroupHandle_t xCreatedEventGroup;

int WIFI_RECV_INFO = BIT0;
int WIFI_CONNECTED_BIT = BIT1;
int WIFI_FAIL_BIT = BIT2;
int MQTT_EVENT_DATA_RX = BIT3;
int MQTT_EVENT_CONNECT = BIT4;
const int MESSAGE_ARRIVE_BIT = BIT5;
const int MESSAGE_TX_ARRIVE_BIT = BIT6;
int WIFI_RESET = BIT7;
int LOCAL_RECV = BIT8;
int TCP_SERVER_RECV = BIT9;
int WIFI_CONNECTED_BIT_2 = BIT10;
void wifi_data_callback(char *data, int len);
const char *TAG1 = "wifi softAP";

#define _I2C_NUMBER(num) I2C_NUM_##num
#define I2C_NUMBER(num) _I2C_NUMBER(num)
#define I2C_MASTER_SCL_IO CONFIG_I2C_MASTER_SCL				  /*!< gpio number for I2C master clock */
#define I2C_MASTER_SDA_IO CONFIG_I2C_MASTER_SDA				  /*!< gpio number for I2C master data  */
#define I2C_MASTER_NUM I2C_NUMBER(CONFIG_I2C_MASTER_PORT_NUM) /*!< I2C port number for master dev */

static const char *TAG_MQTT = "MQTT_EXAMPLE";
static esp_mqtt_client_handle_t client = NULL;
static TaskHandle_t loopHandle = NULL;
#define WEB_SERVER "api.thingspeak.com"
#define WEB_PORT "80"
char dht11_temp[5];
char dht11_humi[5] = "95";
char REQUEST[512];
char SUB_REQUEST[100];
char recv_buf[512];

typedef enum
{
	INITIAL_STATE,
	NORMAL_STATE,
	LOST_WIFI_STATE,
	CHANGE_PASSWORD_STATE,

} wifi_state_t;

struct wifi_info_t
{
	char SSID[20];
	char PASSWORD[10];
	char SSID_AP[15];
	char PASSWORD_AP[10];
	wifi_state_t state;
} __attribute__((packed)) wifi_info = {
	.SSID_AP = "bungdz",
	.PASSWORD_AP = "12345678",
	.state = INITIAL_STATE,
};

typedef struct
{
	char buf[256];
	size_t offset;
} json_gen_test_result_t;

json_gen_test_result_t result;

typedef struct
{
	int Device;
	char Tem[10];
	int Lux;
	int Gas;
} data_sensor_t;
data_sensor_t data_sensor;
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0))
#define esp_vfs_fat_spiflash_mount esp_vfs_fat_spiflash_mount_rw_wl
#define esp_vfs_fat_spiflash_unmount esp_vfs_fat_spiflash_unmount_rw_wl
#endif

static int s_retry_num = 0;

EventGroupHandle_t s_mqtt_event_group;
static EventGroupHandle_t s_wifi_event_group;

static const char *TAG = "MAIN";

char *MOUNT_POINT = "/root";

char *json_gen(json_gen_test_result_t *result, char *key1, char *value1,
			   char *key2, int value2, char *key3, int value3);
/**
 *  void nvs_save_wifiInfo(nvs_handle_t c_handle, const char *key, void *out_value)
 *  @brief lưu lại thông tin wifi từ nvs
 *
 *  @param[in] c_handle nvs_handle_t
 *  @param[in] key Key để lấy dữ liệu
 *  @param[in] value Data output
 *  @param[in] length chiều dài dữ liệu
 *  @return None
 */
void nvs_save_wifiInfo(nvs_handle_t c_handle, const char *key, const void *value, size_t length)
{
	esp_err_t err;
	nvs_open("storage", NVS_READWRITE, &c_handle);
	nvs_set_blob(c_handle, key, value, length);
	err = nvs_commit(c_handle);
	if (err != ESP_OK)
		return err;

	// Close
	nvs_close(c_handle);
}
/**
 *  void nvs_get_wifiInfo(nvs_handle_t c_handle, const char *key, void *out_value)
 *  @brief Lấy lại trong tin wifi từ nvs
 *
 *  @param[in] c_handle nvs_handle_t
 *  @param[in] key Key để lấy dữ liệu
 *  @param[in] out_value Data output
 *  @return None
 */
void nvs_get_wifiInfo(nvs_handle_t c_handle, const char *key, void *out_value)
{
	esp_err_t err;
	err = nvs_open("storage", NVS_READWRITE, &c_handle);
	if (err != ESP_OK)
		return err;
	size_t required_size = 0; // value will default to 0, if not set yet in NVS
	err = nvs_get_blob(c_handle, NVS_KEY, NULL, &required_size);
	if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
		return err;
	if (required_size == 0)
	{
		printf("Nothing saved yet!\n");
	}
	else
	{
		nvs_get_blob(c_handle, NVS_KEY, out_value, &required_size);

		err = nvs_commit(c_handle);
		if (err != ESP_OK)
			return err;

		// Close
		nvs_close(c_handle);
	}
}
/**
 *  void wifi_data_callback(char *data, int len)
 *  @brief Hàm này được tạo lại mỗi khi nhận được dữ liệu wifi local
 *
 *  @param[in] data dữ liệu
 *  @param[in] len chiều dài dữ liệu
 *  @return None
 */
void wifi_data_callback(char *data, int len)
{
	printf("%.*s", len, data);
	char data_local[30];
	sprintf(data_local, "%.*s", len, data);
	char *pt = strtok(data_local, "/");
	strcpy(wifi_info.SSID, pt);
	pt = strtok(NULL, "/");
	strcpy(wifi_info.PASSWORD, pt);
	printf("\nssid: %s \n pass: %s\n", wifi_info.SSID, wifi_info.PASSWORD);
	nvs_save_wifiInfo(NVS_HANDLE, NVS_KEY, &wifi_info, sizeof(wifi_info));
	xEventGroupSetBits(xCreatedEventGroup, WIFI_RECV_INFO);
}

uint8_t flag_signal_local = 10;
uint8_t flag_signal_local_2 = 10;
/**
 *  void local_data_callback(char *data, int len)
 *  @brief Xử lý dữ liệu nhận về từ User ở chế độ Local
 *
 *  @param[in] data dữ liệu
 *  @param[in] len chiều dài dữ liệu
 *  @return None
 */
void local_data_callback(char *data, int len)
{
	char data_local[30];
	sprintf(data_local, "%.*s", len, data);
	printf("%.*s\n", len, data);
	if (strstr(data_local, "OFF") != NULL)
	{
		flag_signal_local = 0;
		xEventGroupSetBits(xCreatedEventGroup, LOCAL_RECV);
		printf("Da tat den\n");
	}
	else if (strstr(data_local, "ON") != NULL)
	{
		flag_signal_local = 1;
		xEventGroupSetBits(xCreatedEventGroup, LOCAL_RECV);
		printf("Da bat den\n");
	}
	else if (strstr(data_local, "CHANGE") != NULL)
	{
		flag_signal_local = 2;
		wifi_info.state = NORMAL_STATE;
		xEventGroupSetBits(xCreatedEventGroup, LOCAL_RECV);
		nvs_save_wifiInfo(NVS_HANDLE, NVS_KEY, &wifi_info, sizeof(wifi_info));
	}
	else if (strstr(data_local, "/") != NULL)
	{
		flag_signal_local = 3;
		char *pt = strtok(data_local, "/");
		strcpy(wifi_info.SSID, pt);
		pt = strtok(NULL, "/");
		strcpy(wifi_info.PASSWORD, pt);
		wifi_info.state = CHANGE_PASSWORD_STATE;
		nvs_save_wifiInfo(NVS_HANDLE, NVS_KEY, &wifi_info, sizeof(wifi_info));
		xEventGroupSetBits(xCreatedEventGroup, LOCAL_RECV);
	}
}
/**
 *  void local_data_callback_2(char *data, int len)
 *  @brief Xử lý dữ liệu nhận về từ User ở chế độ Local cho device 2
 *
 *  @param[in] data dữ liệu
 *  @param[in] len chiều dài dữ liệu
 *  @return None
 */
void local_data_callback_2(char *data, int len)
{
	char data_local[30];
	sprintf(data_local, "%.*s", len, data);
	printf("%.*s\n", len, data);
	if (strstr(data_local, "OFF") != NULL)
	{
		flag_signal_local_2 = 0;
		xEventGroupSetBits(xCreatedEventGroup, LOCAL_RECV);
		printf("Da tat den\n");
	}
	else if (strstr(data_local, "ON") != NULL)
	{
		flag_signal_local_2 = 1;
		xEventGroupSetBits(xCreatedEventGroup, LOCAL_RECV);
		printf("Da bat den\n");
	}
}
/**
static void local_oled_task(void *pvParameters)
 *  @brief Task hiện thị dữ liệu từ các device gửi đến lên OLED
 *
*  @param[in] pvParameter tham số truyền vào
 *  @return None
 */
static void local_oled_task(void *pvParameters)
{
	OLEDDisplay_t *oled = OLEDDisplay_init(0, 0x78, I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
	OLEDDisplay_setFont(oled, ArialMT_Plain_16);
	OLEDDisplay_drawString(oled, 00, 00, "Welcome to");
	OLEDDisplay_drawString(oled, 20, 25, "my Channel !");
	OLEDDisplay_display(oled);
	vTaskDelay(50 / portTICK_PERIOD_MS);
	char temper[15];
	char anhsang[25];
	char temper2[15];
	char anhsang2[25];
	// printf("da tao Oled\n");
	while (1)
	{
		xEventGroupWaitBits(s_mqtt_event_group,
							TCP_SERVER_RECV,
							pdFALSE,
							pdFALSE,
							portMAX_DELAY);
		// printf("Hien Oled\n");
		OLEDDisplay_clear(oled);
		if (data_sensor.Device == 1)
		{
			sprintf(temper, "%sC", data_sensor.Tem);
			sprintf(anhsang, "Anh Sang: %d", data_sensor.Lux);
		}
		else if (data_sensor.Device == 2)
		{
			sprintf(temper2, "%sC", data_sensor.Tem);
			sprintf(anhsang2, "Anh Sang: %d", data_sensor.Lux);
		}
		OLEDDisplay_setFont(oled, ArialMT_Plain_10);
		OLEDDisplay_drawString(oled, 00, 00, "Nhiet do :");
		OLEDDisplay_drawString(oled, 70, 00, temper);
		OLEDDisplay_drawString(oled, 0, 15, anhsang);
		OLEDDisplay_drawString(oled, 00, 30, "Nhiet do :");
		OLEDDisplay_drawString(oled, 70, 30, temper2);
		OLEDDisplay_drawString(oled, 0, 45, anhsang2);
		OLEDDisplay_display(oled);
		vTaskDelay(50 / portTICK_PERIOD_MS);

		xEventGroupClearBits(s_mqtt_event_group, TCP_SERVER_RECV);
		// ESP_LOGI(TAG, "Starting again!");
		vTaskDelay(5000 / portTICK_RATE_MS);
	}
	vTaskDelete(NULL);
}

#define LISTENQ 3
int json_parse_data_sensor(char *json, data_sensor_t *out_data);
char data_tcp[64];
/**
void tcp_server(void *pvParam)
 *  @brief Task kết nối Stream Socket đến các device và truyền nhận dữ liệu ở chế độ Local
 *
*  @param[in] pvParam tham số truyền vào TASK
 *  @return None
 */
void tcp_server(void *pvParam)
{
	ESP_LOGI(TAG, "tcp_server task started \n");
	struct sockaddr_in tcpServerAddr;
	// đăng ký địa chỉ cho socket server
	tcpServerAddr.sin_addr.s_addr = htonl(INADDR_ANY); // hàm htonl 32bit để chuyển đổi từ littel sang big edian
	// INADDR_ANY địa chỉ server thay đổi theo router giúp linh hoạt hơn
	tcpServerAddr.sin_family = AF_INET; /// domain IPv4
	tcpServerAddr.sin_port = htons(3000);
	int s, r;
	char recv_buf[64];
	static struct sockaddr_in remote_addr;
	static unsigned int socklen;
	socklen = sizeof(remote_addr);
	int cs; // client socket
	char message[35];
	while (1)
	{
		s = socket(AF_INET, SOCK_STREAM, 0); // STREAM SOCKET IPv4
		if (s < 0)
		{
			ESP_LOGE(TAG, "... Failed to allocate socket.\n");
			vTaskDelay(1000 / portTICK_PERIOD_MS);
			continue;
		}
		if (bind(s, (struct sockaddr *)&tcpServerAddr, sizeof(tcpServerAddr)) != 0) // dán địa chỉ vừa tạo lên socket
		{
			ESP_LOGE(TAG, "... socket bind failed errno=%d \n", errno);
			close(s);
			vTaskDelay(4000 / portTICK_PERIOD_MS);
			continue;
		}
		if (listen(s, LISTENQ) != 0) // lắng nghe tối đa 2 Client cùng 1 lúc, các client khác sẽ trong hàng đợi
		{
			ESP_LOGE(TAG, "... socket listen failed errno=%d \n", errno);
			close(s);
			vTaskDelay(4000 / portTICK_PERIOD_MS);
			continue;
		}
		while (1) // ngồi đợi client đến kết nối
		{
			xEventGroupWaitBits(xCreatedEventGroup, LOCAL_RECV, false, true, portMAX_DELAY);
			cs = accept(s, (struct sockaddr *)&remote_addr, &socklen); // cho phép client kết nối và tạo 1 fd socket khác để phục vụ truyền nhận
			ESP_LOGI(TAG, "New connection request,Request data:");
			fcntl(cs, F_SETFL, O_NONBLOCK);

			// do
			//{
			bzero(recv_buf, sizeof(recv_buf));
			r = recv(cs, recv_buf, sizeof(recv_buf) - 1, 0);
			if (r > 0)
			{
				xEventGroupSetBits(s_mqtt_event_group, TCP_SERVER_RECV);
				sprintf(data_tcp, "%.*s", r, recv_buf);
				json_parse_data_sensor(data_tcp, &data_sensor);
			}

			ESP_LOGI(TAG, "... done reading from socket. Last read return=%d errno=%d\r\n", r, errno);
			if (flag_signal_local == 0)
			{
				strcpy(message, "OFF");
			}
			else if (flag_signal_local_2 == 0)
			{
				strcpy(message, "OFF");
			}
			else if (flag_signal_local == 1)
			{
				strcpy(message, "ON");
			}
			else if (flag_signal_local_2 == 1)
			{
				strcpy(message, "ON");
			}
			else if (flag_signal_local == 2)
			{
				strcpy(message, "CHANGE");
			}
			else if (flag_signal_local == 3)
			{
				sprintf(message, "%s/%s", wifi_info.SSID, wifi_info.PASSWORD);
			}
			printf("%s\n", message);
			if (write(cs, message, strlen(message)) < 0)
			{
				ESP_LOGE(TAG, "... Send failed \n");
				close(s);
				vTaskDelay(4000 / portTICK_PERIOD_MS);
				continue;
			}
			vTaskDelay(50 / portTICK_PERIOD_MS);
			ESP_LOGI(TAG, "... socket send success");
			if ((flag_signal_local == 2) | (flag_signal_local == 3))
			{
				// esp_restart();
			}
			close(cs);
		}
		xEventGroupClearBits(xCreatedEventGroup, LOCAL_RECV);
		ESP_LOGI(TAG, "... server will be opened in 1 seconds");
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}
	ESP_LOGI(TAG, "...tcp_client task closed\n");
}

static void flush_str(char *buf, void *priv)
{
	json_gen_test_result_t *result = (json_gen_test_result_t *)priv;
	if (result)
	{
		if (strlen(buf) > sizeof(result->buf) - result->offset)
		{
			printf("Result Buffer too small\r\n");
			return;
		}
		memcpy(result->buf + result->offset, buf, strlen(buf));
		result->offset += strlen(buf);
	}
}
extern char data_rx[100];
char data_tx[100];
/**
static void http_get_task(void *pvParameters)
*  @brief Task hiển thị dữ liệu lên OLED ở chế độ online, gửi dữ liệu lên Cloud
*
*  @param[in] pvParameters tham số truyền vào TASK
*  @return None
*/
static void http_get_task(void *pvParameters)
{
	OLEDDisplay_t *oled = OLEDDisplay_init(0, 0x78, I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
	OLEDDisplay_setFont(oled, ArialMT_Plain_16);
	OLEDDisplay_drawString(oled, 00, 00, "Welcome to");
	OLEDDisplay_drawString(oled, 20, 25, "my Channel !");
	OLEDDisplay_display(oled);
	vTaskDelay(50 / portTICK_PERIOD_MS);
	const struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};
	struct addrinfo *res;
	struct in_addr *addr;
	int s, r;
	char temper[15];
	char anhsang[25];
	char temper2[15];
	char anhsang2[25];
	// printf("da tao Oled\n");
	while (1)
	{
		xEventGroupWaitBits(s_mqtt_event_group,
							MESSAGE_ARRIVE_BIT,
							pdFALSE,
							pdFALSE,
							portMAX_DELAY);
		// printf("Hien Oled\n");
		OLEDDisplay_clear(oled);
		if (data_sensor.Device == 1)
		{
			sprintf(temper, "%sC", data_sensor.Tem);
			sprintf(anhsang, "Anh Sang: %d", data_sensor.Lux);
		}
		else if (data_sensor.Device == 2)
		{
			sprintf(temper2, "%s C", data_sensor.Tem);
			sprintf(anhsang2, "Anh Sang: %d", data_sensor.Lux);
		}
		OLEDDisplay_setFont(oled, ArialMT_Plain_10);
		OLEDDisplay_drawString(oled, 00, 00, "Nhiet do :");
		OLEDDisplay_drawString(oled, 70, 00, temper);
		OLEDDisplay_drawString(oled, 0, 15, anhsang);
		OLEDDisplay_drawString(oled, 00, 30, "Nhiet do :");
		OLEDDisplay_drawString(oled, 70, 30, temper2);
		OLEDDisplay_drawString(oled, 0, 45, anhsang2);
		OLEDDisplay_display(oled);
		vTaskDelay(50 / portTICK_PERIOD_MS);
		if (data_sensor.Device == 1)
		{
			int msg_id = esp_mqtt_client_publish(client, "/smarthome/devices", data_rx, 0, 1, 0);

			int err = getaddrinfo(WEB_SERVER, WEB_PORT, &hints, &res);

			if (err != 0 || res == NULL)
			{
				ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
				vTaskDelay(1000 / portTICK_PERIOD_MS);
				continue;
			}

			addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
			s = socket(res->ai_family, res->ai_socktype, 0);
			if (s < 0)
			{
				ESP_LOGE(TAG, "... Failed to allocate socket.");
				freeaddrinfo(res);
				vTaskDelay(1000 / portTICK_PERIOD_MS);
				continue;
			}
			// ESP_LOGI(TAG, "... allocated socket");

			if (connect(s, res->ai_addr, res->ai_addrlen) != 0)
			{
				ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
				close(s);
				freeaddrinfo(res);
				vTaskDelay(4000 / portTICK_PERIOD_MS);
				continue;
			}
			freeaddrinfo(res);
			// D3MLUF2YEDGFZB7B
			// OMRASFVUCHSVNKDO
			sprintf(SUB_REQUEST, "api_key=D3MLUF2YEDGFZB7B&field1=%s&field2=%d&field3=%d", data_sensor.Tem, data_sensor.Lux, data_sensor.Gas);
			sprintf(REQUEST, "POST /update HTTP/1.1\nHost: api.thingspeak.com\nConnection: close\nContent-Type: application/x-www-form-urlencoded\nContent-Length:%d\n\n%s", strlen(SUB_REQUEST), SUB_REQUEST);
			if (write(s, REQUEST, strlen(REQUEST)) < 0)
			{
				ESP_LOGE(TAG, "... socket send failed");
				close(s);
				vTaskDelay(4000 / portTICK_PERIOD_MS);
				continue;
			}
			close(s);
			xEventGroupClearBits(s_mqtt_event_group, MESSAGE_ARRIVE_BIT);
			vTaskDelay(4000 / portTICK_RATE_MS);
		}
		else if (data_sensor.Device == 2)
		{
			int msg_id = esp_mqtt_client_publish(client, "/smarthome/led2", data_rx, 0, 1, 0);
			// ESP_LOGI(TAG_MQTT, "sent publish successful, msg_id=%d", msg_id);

			int err = getaddrinfo(WEB_SERVER, WEB_PORT, &hints, &res);

			if (err != 0 || res == NULL)
			{
				ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
				vTaskDelay(1000 / portTICK_PERIOD_MS);
				continue;
			}
			addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
			s = socket(res->ai_family, res->ai_socktype, 0);
			if (s < 0)
			{
				ESP_LOGE(TAG, "... Failed to allocate socket.");
				freeaddrinfo(res);
				vTaskDelay(1000 / portTICK_PERIOD_MS);
				continue;
			}
			if (connect(s, res->ai_addr, res->ai_addrlen) != 0)
			{
				ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
				close(s);
				freeaddrinfo(res);
				vTaskDelay(4000 / portTICK_PERIOD_MS);
				continue;
			}
			freeaddrinfo(res);
			// D3MLUF2YEDGFZB7B
			// OMRASFVUCHSVNKDO
			sprintf(SUB_REQUEST, "api_key=D3MLUF2YEDGFZB7B&field4=%s&field5=%d&field6=%d", data_sensor.Tem, data_sensor.Lux, data_sensor.Gas);
			sprintf(REQUEST, "POST /update HTTP/1.1\nHost: api.thingspeak.com\nConnection: close\nContent-Type: application/x-www-form-urlencoded\nContent-Length:%d\n\n%s", strlen(SUB_REQUEST), SUB_REQUEST);
			if (write(s, REQUEST, strlen(REQUEST)) < 0)
			{
				ESP_LOGE(TAG, "... socket send failed");
				close(s);
				vTaskDelay(4000 / portTICK_PERIOD_MS);
				continue;
			}
			// ESP_LOGI(TAG, "... socket send success");
			close(s);
			xEventGroupClearBits(s_mqtt_event_group, MESSAGE_ARRIVE_BIT);
			// ESP_LOGI(TAG, "Starting again!");
			vTaskDelay(4000 / portTICK_RATE_MS);
		}
	}
	vTaskDelete(NULL);
}
/**
 *  static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
 *  @brief Xử lý các sự kiện từ Mqtt
 *
 *  @param[in] event Data về event được gửi đến
 *  @return ESP_OK
 */
static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
	esp_mqtt_client_handle_t client = event->client;
	int msg_id;
	// your_context_t *context = event->context;

	switch (event->event_id)
	{
	case MQTT_EVENT_CONNECTED:

		ESP_LOGI(TAG_MQTT, "MQTT_EVENT_CONNECTED");
		msg_id = esp_mqtt_client_subscribe(client, "/device/led1", 0);
		ESP_LOGI(TAG_MQTT, "sent subscribe successful, msg_id=%d", msg_id);
		break;
	case MQTT_EVENT_DISCONNECTED:
		ESP_LOGI(TAG_MQTT, "MQTT_EVENT_DISCONNECTED");
		break;

	case MQTT_EVENT_SUBSCRIBED:
		ESP_LOGI(TAG_MQTT, "MQTT_EVENT_SUBSCRIBED");
		break;
	case MQTT_EVENT_DATA:
	{
		ESP_LOGI(TAG_MQTT, "MQTT_EVENT_DATA");
		printf("DATA=%.*s\r\n", event->data_len, event->data);
		sprintf(data_tx, "%.*s", event->data_len, event->data);
		xEventGroupSetBits(s_mqtt_event_group, MESSAGE_TX_ARRIVE_BIT);
		break;
	}
	default:
		ESP_LOGI(TAG_MQTT, "Other event id:%d", event->event_id);
		break;
	}

	return ESP_OK;
}
/**
 *  static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
 *  @brief Nhận các event được kich hoạt
 *  @param[in] handler_args argument
 *  @param[in] base Tên Event
 *  @param[in] event_id Mã Event
 *  @param[in] event_data dữ liệu từ event loop
 *  @return None
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
	ESP_LOGD(TAG_MQTT, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
	mqtt_event_handler_cb(event_data);
}
/**
 *  static void mqtt_app_start(void)
 *  @brief Kết nối đến Broker để truyền nhận cho User ở chế độ online
 *  @return None
 */
static void mqtt_app_start(void)
{
	esp_mqtt_client_config_t mqtt_cfg = {
		.uri = "mqtt://broker.mqttdashboard.com:1883",
	};

	client = esp_mqtt_client_init(&mqtt_cfg);
	esp_mqtt_client_register_event(client, MQTT_EVENT_CONNECTED, mqtt_event_handler, client);
	esp_mqtt_client_register_event(client, MQTT_EVENT_DISCONNECTED, mqtt_event_handler, client);
	esp_mqtt_client_register_event(client, MQTT_EVENT_DATA, mqtt_event_handler, client);
	esp_mqtt_client_start(client);
}
/**
 *  static void event_handler(void *arg, esp_event_base_t event_base,
						  int32_t event_id, void *event_data)
 *  @brief Event Handler xử lý các sự kiện về kết nối đến Router
 *
 *  @param[in] arg argument
 *  @param[in] event_base Tên Event
 *  @param[in] event_id Mã Event
 *  @param[in] event_data IP được trả về
 *  @return None
 */
static void event_handler(void *arg, esp_event_base_t event_base,
						  int32_t event_id, void *event_data)
{
	if (event_base == WIFI_EVENT)
		ESP_LOGI(TAG, "WIFI_EVENT event_id=%" PRIi32, event_id);
	if (event_base == IP_EVENT)
		ESP_LOGI(TAG, "IP_EVENT event_id=%" PRIi32, event_id);

	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
	{
		esp_wifi_connect();
	}
	else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
	{
		if (s_retry_num < 10)
		{
			gpio_set_level(25, 1);
			esp_wifi_connect();
			xEventGroupClearBits(xCreatedEventGroup, WIFI_CONNECTED_BIT);
			s_retry_num++;
			ESP_LOGI(TAG, "retry to connect to the AP");
		}
		else
		{
			wifi_info.state = LOST_WIFI_STATE;
			nvs_save_wifiInfo(NVS_HANDLE, NVS_KEY, &wifi_info, sizeof(wifi_info));
			esp_restart();
		}
		ESP_LOGI(TAG, "connect to the AP fail");
	}
	else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
	{
		ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
		ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
		s_retry_num = 0;
		xEventGroupSetBits(xCreatedEventGroup, WIFI_CONNECTED_BIT);
		xEventGroupSetBits(xCreatedEventGroup, WIFI_CONNECTED_BIT_2);
	}
	else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED)
	{
		gpio_set_level(25, 0);
		printf("WIFI_EVENT EVENT WIFI_EVENT_STA_CONNECTED : the event id is %d \n", event_id);
		printf("Starting the MQTT app \n");
		mqtt_app_start();
	}
}
/**
 *  void wifi_init_sta(void)
 *  @brief Kết nối đến router ở chế độ Station
 *
 *  @return None
 */
void wifi_init_sta(void)
{
	printf("wifi_init_sta\n");
	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
	ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
	wifi_config_t wifi_config = {
		.sta = {
			.threshold.authmode = WIFI_AUTH_WPA2_PSK,

			.pmf_cfg = {
				.capable = true,
				.required = false},
		},
	};
	strcpy((char *)wifi_config.ap.ssid, wifi_info.SSID);
	strcpy((char *)wifi_config.ap.password, wifi_info.PASSWORD);
	printf("%s\n", wifi_info.SSID);
	printf("%s\n", wifi_info.PASSWORD);
	esp_wifi_stop();
	// esp_netif_create_default_wifi_sta();
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
	ESP_ERROR_CHECK(esp_wifi_start());

	ESP_LOGI(TAG, "wifi_init_sta finished.");

	while (1)
	{
		/* Wait forever for WIFI_CONNECTED_BIT to be set within the event group.
		Clear the bits beforeexiting. */
		EventBits_t uxBits = xEventGroupWaitBits(xCreatedEventGroup,
												 WIFI_CONNECTED_BIT, /* The bits within the event group to waitfor. */
												 pdTRUE,			 /* WIFI_CONNECTED_BIT should be cleared before returning. */
												 pdFALSE,			 /* Don't waitfor both bits, either bit will do. */
												 portMAX_DELAY);	 /* Wait forever. */
		if ((uxBits & WIFI_CONNECTED_BIT) == WIFI_CONNECTED_BIT)
		{
			wifi_info.state = NORMAL_STATE;
			nvs_save_wifiInfo(NVS_HANDLE, NVS_KEY, &wifi_info, sizeof(wifi_info));
			gpio_set_level(25, 0);
			ESP_LOGI(TAG, "WIFI_CONNECTED_BIT");
			s_mqtt_event_group = xEventGroupCreate();
			xTaskCreate(&http_get_task, "http_get_task", 4096, NULL, 6, NULL);
			break;
		}
	}
	ESP_LOGI(TAG, "Got IP Address.");
}

void mqtt_server(void *pvParameters);
void http_server(void *pvParameters);
void mqtt_subscriber(void *pvParameters);
void mqtt_publisher(void *pvParameters);

wl_handle_t mountFATFS(char *partition_label, char *mount_point)
{
	ESP_LOGI(TAG, "Initializing FAT file system");
	// To mount device we need name of device partition, define base_path
	// and allow format partition in case if it is new one and was not formated before
	const esp_vfs_fat_mount_config_t mount_config = {
		.max_files = 4,
		.format_if_mount_failed = true,
		.allocation_unit_size = CONFIG_WL_SECTOR_SIZE};
	wl_handle_t s_wl_handle;
	esp_err_t err = esp_vfs_fat_spiflash_mount(mount_point, partition_label, &mount_config, &s_wl_handle);
	if (err != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(err));
		return -1;
	}
	ESP_LOGI(TAG, "Mount FAT filesystem on %s", mount_point);
	ESP_LOGI(TAG, "s_wl_handle=%" PRIi32, s_wl_handle);
	return s_wl_handle;
}
/**
 *  char *json_gen(json_gen_test_result_t *result, char *key0, int *value0, char *key1, char *value1, // dong goi thanh json
			   char *key2, int value2, char *key3, int value3)
 *  @brief Đóng gói các key và value thành chuỗi JSON đầu ra
 *
 *  @param[in] result Chuỗi jSON đầu ra
 *  @param[in] key0 key Device
 *  @param[in] value0 value của key Device
 *  @param[in] key1 key Temperature
 *  @param[in] value1 value của key Temperature
 *  @param[in] key2 key Lux
 *  @param[in] value2 value của key Lux
 *  @param[in] key3  key Gas
 *  @param[in] value3 value của key Gas
 *  @return result->buf
 */
char *json_gen(json_gen_test_result_t *result, char *key1, char *value1,
			   char *key2, int value2, char *key3, int value3)
{
	char buf[20];
	memset(result, 0, sizeof(json_gen_test_result_t));
	json_gen_str_t jstr;
	json_gen_str_start(&jstr, buf, sizeof(buf), flush_str, result);
	json_gen_start_object(&jstr);
	json_gen_obj_set_string(&jstr, key1, value1);
	json_gen_obj_set_int(&jstr, key2, value2);
	json_gen_obj_set_int(&jstr, key3, value3);
	json_gen_end_object(&jstr);
	json_gen_str_end(&jstr);
	return result->buf;
}
/**
 *  int json_parse_data_sensor(char *json, data_sensor_t *out_data)
 *  @brief Phân giã các key và value của chuỗi JSON đầu vào
 *
 *  @param[in] json Chuỗi jSON đầu vào
 *  @param[in] out_data Data sau khi được Parse
 *  @return 0 if OK, −1 on error
 */
int json_parse_data_sensor(char *json, data_sensor_t *out_data)
{
	jparse_ctx_t jctx;
	int ret = json_parse_start(&jctx, json, strlen(json));
	if (ret != OS_SUCCESS)
	{
		printf("Parser failed\n");
		return -1;
	}
	if (json_obj_get_int(&jctx, "Device", &out_data->Device) != OS_SUCCESS)
	{
		printf("Parser failed\n");
	}
	if (json_obj_get_string(&jctx, "Temperature", &out_data->Tem, 20) != OS_SUCCESS)
	{
		printf("Parser failed\n");
	}
	if (json_obj_get_int(&jctx, "illuminance", &out_data->Lux) != OS_SUCCESS)
	{
		printf("Parser failed\n");
	}
	if (json_obj_get_int(&jctx, "Gas", &out_data->Gas) != OS_SUCCESS)
	{
		printf("Parser failed\n");
	}
	json_parse_end(&jctx);
	return 0;
}
/**
 *  void output_create(int pin)
 *  @brief Config các chân đầu ra
 *
 *  @return None
 */
void output_create(int pin)
{
	gpio_config_t io_conf;
	// disable interrupt
	io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
	// set as output mode
	io_conf.mode = GPIO_MODE_OUTPUT;
	// bit mask of the pins that you want to set,e.g.GPIO18/19
	io_conf.pin_bit_mask = (1ULL << pin);
	// disable pull-down mode
	io_conf.pull_down_en = 0;
	io_conf.pull_up_en = 0;
	// configure GPIO with the given settings
	gpio_config(&io_conf);
}
/**
 *  void http_get_callback(char *data, int len)
 *  @brief Hàm này được gọi lại mỗi khi nhận request Get từ User ở chế độ Local
 *
 *  @param[in] data dữ liệu
 *  @param[in] len chiều dài dữ liệu
 *  @return None
 */
void http_get_callback(char *data, int len)
{
	char bufe[100];
	if (data_sensor.Device == 1) // thiết bị 1
	{
		sprintf(bufe, "1 %s %d  ", data_sensor.Tem, data_sensor.Lux);
	}
	else // thiết bị 2
	{
		sprintf(bufe, "2 %s %d  ", data_sensor.Tem, data_sensor.Lux);
	}
	http_send_response(bufe, strlen(bufe));
}
/**
 *  void set_IpAdress(void)
 *  @brief Set Static IP Address cho Gateway
 *
 *  @return None
 */
void set_IpAdress(void)
{
	esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
	assert(sta_netif);

	ESP_LOGI(TAG, "CONFIG_STATIC_IP_ADDRESS=[%s]", CONFIG_STATIC_IP_ADDRESS);
	ESP_LOGI(TAG, "CONFIG_STATIC_GW_ADDRESS=[%s]", CONFIG_STATIC_GW_ADDRESS);
	ESP_LOGI(TAG, "CONFIG_STATIC_NM_ADDRESS=[%s]", CONFIG_STATIC_NM_ADDRESS);

	/* Stop DHCP client */
	ESP_ERROR_CHECK(esp_netif_dhcpc_stop(sta_netif));
	ESP_LOGI(TAG, "Stop DHCP Services");

	/* Set STATIC IP Address */
	esp_netif_ip_info_t ip_info;
	memset(&ip_info, 0, sizeof(esp_netif_ip_info_t));
	ip_info.ip.addr = ipaddr_addr(CONFIG_STATIC_IP_ADDRESS);
	ip_info.netmask.addr = ipaddr_addr(CONFIG_STATIC_NM_ADDRESS);
	ip_info.gw.addr = ipaddr_addr(CONFIG_STATIC_GW_ADDRESS);
	;
	printf("Da den esp_netif_set_ip_info\n");
	esp_netif_set_ip_info(sta_netif, &ip_info);

	ip_addr_t d;
	d.type = IPADDR_TYPE_V4;
	d.u_addr.ip4.addr = 0x08080808; // 8.8.8.8 dns
	dns_setserver(0, &d);
	d.u_addr.ip4.addr = 0x08080404; // 8.8.4.4 dns
	dns_setserver(1, &d);
	printf("Da den dns_setserver\n");
}

void app_main()
{
	// Initialize NVS
	esp_err_t err;
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
	{
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}

	ESP_ERROR_CHECK(ret);
	output_create(25); // led báo mạng
	gpio_set_level(25, 1);
	nvs_get_wifiInfo(NVS_HANDLE, NVS_KEY, &wifi_info);
	printf("%s \n %s\n", wifi_info.SSID, wifi_info.PASSWORD);
	xCreatedEventGroup = xEventGroupCreate();
	if (wifi_info.state == INITIAL_STATE) // chế độ ban đầu
	{
		ESP_ERROR_CHECK(esp_netif_init());
		ESP_ERROR_CHECK(esp_event_loop_create_default());
		set_IpAdress();
		wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
		ESP_ERROR_CHECK(esp_wifi_init(&cfg));
		ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
		wifi_init_softap();
	}
	else if (wifi_info.state == NORMAL_STATE) // chế độ bình thường của wifi
	{

		ESP_ERROR_CHECK(esp_netif_init());
		ESP_ERROR_CHECK(esp_event_loop_create_default());
		set_IpAdress();
		wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
		ESP_ERROR_CHECK(esp_wifi_init(&cfg));
		ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
		wifi_init_sta();
		ESP_LOGI(TAG1, "Đã thiết lập wifi\n");
	}
	else if (wifi_info.state == CHANGE_PASSWORD_STATE) // chế độ wifi đã thay đổi password
	{
		ESP_ERROR_CHECK(esp_netif_init());
		ESP_ERROR_CHECK(esp_event_loop_create_default());
		set_IpAdress();

		wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
		ESP_ERROR_CHECK(esp_wifi_init(&cfg));
		ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
		printf("Da den wifi_init_sta\n");
		wifi_init_sta();
	}
	else if (wifi_info.state == LOST_WIFI_STATE) // chế độ mất wifi
	{
		// ESP_ERROR_CHECK(esp_event_loop_create_default());
		ESP_ERROR_CHECK(esp_netif_init());
		ESP_ERROR_CHECK(esp_event_loop_create_default());
		esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
		assert(sta_netif);
		s_mqtt_event_group = xEventGroupCreate();
		xTaskCreate(&local_oled_task, "local_oled_task", 4096 * 3, NULL, 5, NULL);
		xTaskCreate(&tcp_server, "tcp_server", 4096, NULL, 5, NULL);
		wifi_init_local_ap();
	}
	xEventGroupWaitBits(xCreatedEventGroup, WIFI_CONNECTED_BIT_2, true, false, portMAX_DELAY);
	ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
	esp_netif_ip_info_t ip_info_2;
	ESP_ERROR_CHECK(esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip_info_2));
	ESP_LOGI(TAG, "ESP32 is STA MODE");

	/* Print the local IP address */
	ESP_LOGI(TAG, "IP Address : " IPSTR, IP2STR(&ip_info_2.ip));
	ESP_LOGI(TAG, "Subnet Mask: " IPSTR, IP2STR(&ip_info_2.netmask));
	ESP_LOGI(TAG, "Gateway    : " IPSTR, IP2STR(&ip_info_2.gw));

	// Initializing FAT file system
	char *partition_label = "storage1";
	wl_handle_t s_wl_handle = mountFATFS(partition_label, MOUNT_POINT);
	if (s_wl_handle < 0)
	{
		ESP_LOGE(TAG, "mountFATFS fail");
		while (1)
		{
			vTaskDelay(1);
		}
	}
	ESP_LOGI(TAG, "MQTT broker started on " IPSTR " using Mongoose v%s", IP2STR(&ip_info_2.ip), MG_VERSION);
	xTaskCreate(mqtt_server, "BROKER 123", 1024 * 10, NULL, 2, NULL);
	vTaskDelay(10); // You need to wait until the task launch is complete.

#if CONFIG_SUBSCRIBE
	/* Start Subscriber */
	char cparam1[64];
	// sprintf(cparam1, "mqtt://%s:1883", ip4addr_ntoa(&ip_info.ip));
	sprintf(cparam1, "mqtt://" IPSTR ":8000", IP2STR(&ip_info_2.ip));
	xTaskCreate(mqtt_subscriber, "SUBSCRIBE", 1024 * 4, (void *)cparam1, 2, NULL);
	vTaskDelay(10); // You need to wait until the task launch is complete.
#endif

#if CONFIG_PUBLISH
	/* Start Publisher */
	char cparam2[64];
	// sprintf(cparam2, "mqtt://%s:1883", ip4addr_ntoa(&ip_info.ip));
	sprintf(cparam2, "mqtt://" IPSTR ":8000", IP2STR(&ip_info_2.ip));
	xTaskCreate(mqtt_publisher, "PUBLISH", 1024 * 4, (void *)cparam2, 2, NULL);
	vTaskDelay(10); // You need to wait until the task launch is complete.
#endif
}
