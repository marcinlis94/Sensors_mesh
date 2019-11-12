#include <string.h>
#include <openssl/ssl.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_mesh.h"
#include "esp_mesh_internal.h"
#include "mesh_light.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "ds18b20.h"
#include "bh1750.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"

/*******************************************************
 *                Macros
 *******************************************************/
#ifndef MESH_SET_ROOT
#define MESH_SET_NODE
#endif


/*******************************************************
 *                Constants
 *******************************************************/
#define RX_SIZE          		50
#define TX_SIZE          		10
#define HTTPS_RECV_BUF_LEN      1024
#define HTTPS_LOCAL_TCP_PORT    443
#define temp_sensor 			1
#define moisture_sensor 		2
#define illuminance_sensor 		3
/*******************************************************
 *                Variable Definitions
 *******************************************************/
static const char *MESH_TAG = "mesh_main";
static const uint8_t MESH_ID[6] = { 0x77, 0x77, 0x77, 0x77, 0x77, 0x77};
static mesh_addr_t mesh_parent_addr;
static int mesh_layer = -1;
const static char *TAG = "thingspeak_client_ds18b20";
bool thingspeak_task_created=false;
bool is_rx_running=false;
bool is_tx_running=false;
static float cache_data_send;
static float cache_data_temp;
static float cache_data_moisture ;
static float cache_data_illuminance;
bool new_data_temp=false;
bool new_data_moisture=false;
bool new_data_illuminance=false;
const int DS_PIN = 14;
static SemaphoreHandle_t cacheSemaphore = NULL;
xTaskHandle get_temp_handle;
xTaskHandle get_moisture_handle;
xTaskHandle get_illuminance_handle;
xTaskHandle p2p_rx_handle;
xTaskHandle p2p_tx_handle;
char template[150];
int sensor=temp_sensor;				//define sensor
/*******************************************************
 *                Function Declarations
 *******************************************************/
void esp_mesh_p2p_tx_main(void *arg);
void esp_mesh_p2p_rx_main(void *arg);
esp_err_t esp_mesh_comm_p2p_start(void);
void mesh_event_handler(mesh_event_t event);
void mesh_scan_done_handler(int num);
void get_temp_task(void *pvParameters);
void get_moisture_task(void *pvParameters);
void get_illuminance_task(void *pvParameters);
static void update_thingspeak_task(void *pvParameters);
static void HTTPS_client_init(void);
void network_init(void);
void suspend_tasks(void);
void resume_tasks(void);
void sensor_init(void);
void build_http_msg(void);
/*******************************************************
 *                Function Definitions
 *******************************************************/
void esp_mesh_p2p_tx_main(void *arg)
{
    mesh_data_t data_out;
    char tx_buf[TX_SIZE];
    data_out.data = (uint8_t*)tx_buf;
    data_out.size = strlen(tx_buf);
    data_out.proto = MESH_PROTO_BIN;
    data_out.tos = MESH_TOS_P2P;
    is_tx_running=true;
    printf("TX started\n");
    while(1){
    	switch(sensor){
			case temp_sensor:
				cache_data_send=cache_data_temp;
				printf("Sending message - temp_sensor: %.2f \n", cache_data_temp);
				break;
    		case moisture_sensor:
				cache_data_send=cache_data_moisture;
				printf("Sending message - moisture_sensor: %.2f \n", cache_data_moisture);
				break;
			case illuminance_sensor:
				cache_data_send=cache_data_illuminance;
				printf("Sending message - illuminance_sensor: %.2f \n", cache_data_illuminance);
				break;
			default:
				printf("Sensor not recognized \n");
				break;
        }
		snprintf(tx_buf, sizeof tx_buf, "%.2f", cache_data_send);
		for(int i=9;i>=0;i--){
			tx_buf[i+1]=tx_buf[i];
		}
		tx_buf[0]=sensor;
		esp_mesh_send(NULL, &data_out, MESH_DATA_P2P, NULL, 0);
		vTaskDelay(5*60*1000 / portTICK_RATE_MS); //5min
    }
}

void esp_mesh_p2p_rx_main(void *arg)
{
    esp_err_t err;
    mesh_addr_t from;
    mesh_data_t data;
    int flag = 0;
    int sensor_field=0;
    static char rx_buf[RX_SIZE] = { 0, };
    data.data = (uint8_t*)rx_buf;
    data.size = RX_SIZE;
    is_rx_running=true;
    printf("RX started\n");
    while (1) {
        data.size = RX_SIZE;
    	err = esp_mesh_recv(&from, &data, portMAX_DELAY, &flag, NULL, 0);
    	if (err != ESP_OK || !data.size) {
            ESP_LOGE(MESH_TAG, "err:0x%x, size:%d", err, data.size);
            continue;
        }else if(err==ESP_OK){
        	printf("rx_got message\n");
        snprintf(rx_buf, RX_SIZE, "%s", data.data);
        sensor_field=(int)rx_buf[0];
        for(int i=0;i<=9;i++){
                    rx_buf[i]=rx_buf[i+1];
        }
        printf("sensor_field= %i", sensor_field);
        switch(sensor_field){
			case temp_sensor:
				cache_data_temp=atof(rx_buf);
				printf("cache_data_temp: %f\n", cache_data_temp);
				new_data_temp=true;
				break;
        	case moisture_sensor:
				cache_data_moisture=atof(rx_buf);
				printf("cache_data_moisture: %f\n", cache_data_moisture);
				new_data_moisture=true;
				break;
			case illuminance_sensor:
				cache_data_illuminance=atof(rx_buf);
				printf("cache_data_illuminance: %f\n", cache_data_illuminance);
				new_data_illuminance=true;
				break;
			default:
				printf("Sensor not recognized \n");
				break;
        }
        vTaskDelay(1*1000 / portTICK_RATE_MS);
        ESP_LOGW(MESH_TAG, "GOT MESSAGE FROM NODE "MACSTR" :%s", MAC2STR(from.addr), data.data);
        }

    }
}

esp_err_t esp_mesh_comm_p2p_start(void)
{
	if(!is_tx_running){
        if(!esp_mesh_is_root())
        xTaskCreate(esp_mesh_p2p_tx_main, "MPTX", 3072, NULL, 5, &p2p_tx_handle);
        else if(!is_rx_running)
        xTaskCreate(esp_mesh_p2p_rx_main, "MPRX", 3072, NULL, 5, &p2p_rx_handle);
	}
    return ESP_OK;
}

static void update_thingspeak_task(void *p)
{
    bool sock_connection=true;
	bool root_tasks_running=true;
    int ret;
    SSL_CTX *ctx;
    SSL *ssl;
    int sock;
    struct sockaddr_in sock_addr;
    struct hostent *hp;
    struct ip4_addr *ip4_addr;
    int recv_bytes = 0;
    int send_bytes;
    char recv_buf[HTTPS_RECV_BUF_LEN];
	char send_data[sizeof(template)];
		/* Build the message */
	while(1){
		if(sock_connection&root_tasks_running){
			suspend_tasks();
			vTaskDelay(5*1000 / portTICK_RATE_MS);
			build_http_msg();
			vTaskDelay(10*1000 / portTICK_RATE_MS);
			printf("Root tasks suspended\n");
			root_tasks_running=false;
		}
		sprintf(send_data, template);
		send_bytes = strlen(send_data);
		ESP_LOGI(TAG, "OpenSSL demo thread start OK");
		ESP_LOGI(TAG, "get target IP address");
		hp = gethostbyname("api.thingspeak.com");
		while (!hp) {

			ESP_LOGI(TAG, "failed");
			hp = gethostbyname("api.thingspeak.com");
		}
		ESP_LOGI(TAG, "OK");
		ip4_addr = (struct ip4_addr *)hp->h_addr;
		ESP_LOGI(TAG, IPSTR, IP2STR(ip4_addr));

		ESP_LOGI(TAG, "create SSL context ......");
		ctx = SSL_CTX_new(TLSv1_1_client_method());
		if (!ctx) {
			ESP_LOGI(TAG, "failed");
		}
		ESP_LOGI(TAG, "OK");
		ESP_LOGI(TAG, "create socket ......");
		sock = socket(AF_INET, SOCK_STREAM, 0);
		if (sock < 0) {
			ESP_LOGI(TAG, "failed");
		}
		ESP_LOGI(TAG, "OK");
		ESP_LOGI(TAG, "bind socket ......");
		memset(&sock_addr, 0, sizeof(sock_addr));
		sock_addr.sin_family = AF_INET;
		sock_addr.sin_addr.s_addr = 0;
		sock_addr.sin_port = htons(8192);
		ret = bind(sock, (struct sockaddr*)&sock_addr, sizeof(sock_addr));
		if (ret) {
			ESP_LOGI(TAG, "failed");
		}
		ESP_LOGI(TAG, "OK");
		ret=1;
		ESP_LOGI(TAG, "socket connect to server ......");
		memset(&sock_addr, 0, sizeof(sock_addr));
		sock_addr.sin_family = AF_INET;
		sock_addr.sin_addr.s_addr = ip4_addr->addr;
		sock_addr.sin_port = htons(443);
		ret=connect(sock, (struct sockaddr*)&sock_addr, sizeof(sock_addr));
		for(int i=0;ret!=0;i++){
			vTaskDelay(1*1000 / portTICK_RATE_MS);
			printf("waiting for socket \n");
			if(i>10)
				break;
		}
		if (ret) {
			ESP_LOGI(TAG, "failed");
			sock_connection=false;
		}else
			sock_connection=true;
		ESP_LOGI(TAG, "OK");
		ESP_LOGI(TAG, "create SSL ......");
		ssl = SSL_new(ctx);
		if (!ssl) {
			ESP_LOGI(TAG, "failed");
		}
		ESP_LOGI(TAG, "OK");
		SSL_set_fd(ssl, sock);
		ESP_LOGI(TAG, "SSL connected ......");
		ret = SSL_connect(ssl);
		if (!ret) {
			ESP_LOGI(TAG, "failed " );
		}
		ESP_LOGI(TAG, "OK");
			ESP_LOGI(TAG, "send https request '%s'...",send_data);
			ret = SSL_write(ssl, send_data, send_bytes);
			if (ret <= 0) {
				ESP_LOGI(TAG, "failed");
			}
			ESP_LOGI(TAG, "OK");
			do {
				ret = SSL_read(ssl, recv_buf, HTTPS_RECV_BUF_LEN - 1);
				if (ret <= 0) {
					break;
				}
				recv_buf[ret] = '\0';
				recv_bytes += ret;
				ESP_LOGI(TAG, "%s", recv_buf);
			} while (1);
			ESP_LOGI(TAG, "read %d bytes of data ......", recv_bytes);
			    SSL_shutdown(ssl);
			    SSL_free(ssl);
			    ssl = NULL;
			    close(sock);
			    sock = -1;
			    SSL_CTX_free(ctx);
			    ctx = NULL;
			if(sock_connection&(!root_tasks_running)){
				printf("RX resume\n");
				vTaskResume(p2p_rx_handle);
				vTaskDelay(((5*60-30)*1000) / portTICK_RATE_MS); //5min (20sec for a loop+another delays)
				resume_tasks();
				vTaskDelay((1*13*1000) / portTICK_RATE_MS);
				printf("Measuring resume\n");
				root_tasks_running=true;
			}
	}
    return ;
}

static void HTTPS_client_init(void)
{
    int ret;
    xTaskHandle openssl_handle;

    if(!thingspeak_task_created){
    ret = xTaskCreate(update_thingspeak_task, "https_connection", 10240, NULL, 8, &openssl_handle);

		if (ret != pdPASS)  {
			ESP_LOGI(TAG, "create thread %s failed", "https_connection");
		}else{
			thingspeak_task_created=true;
		}
    }
}

void mesh_scan_done_handler(int num)
{
    int i;
    int ie_len = 0;
    mesh_assoc_t assoc;
    mesh_assoc_t parent_assoc = { .layer = CONFIG_MESH_MAX_LAYER, .rssi = -120 };
    wifi_ap_record_t record;
    wifi_ap_record_t parent_record = { 0, };
    bool parent_found = false;
    mesh_type_t my_type = MESH_IDLE;
    int my_layer = -1;
    wifi_config_t parent = { 0, };
    wifi_scan_config_t scan_config = { 0 };

    for (i = 0; i < num; i++) {
        esp_mesh_scan_get_ap_ie_len(&ie_len);
        esp_mesh_scan_get_ap_record(&record, &assoc);
        if (ie_len == sizeof(assoc)) {
            ESP_LOGW(MESH_TAG,
                     "<MESH>[%d]%s, layer:%d/%d, assoc:%d/%d, %d, "MACSTR", channel:%u, rssi:%d, ID<"MACSTR"><%s>",
                     i, record.ssid, assoc.layer, assoc.layer_cap, assoc.assoc,
                     assoc.assoc_cap, assoc.layer2_cap, MAC2STR(record.bssid),
                     record.primary, record.rssi, MAC2STR(assoc.mesh_id), assoc.encrypted ? "IE Encrypted" : "IE Unencrypted");

#ifdef MESH_SET_NODE
            if (assoc.mesh_type != MESH_IDLE && assoc.layer_cap
                    && assoc.assoc < assoc.assoc_cap && record.rssi > -70) {
                if (assoc.layer < parent_assoc.layer || assoc.layer2_cap < parent_assoc.layer2_cap) {
                    parent_found = true;
                    memcpy(&parent_record, &record, sizeof(record));
                    memcpy(&parent_assoc, &assoc, sizeof(assoc));
                    if (parent_assoc.layer_cap != 1) {
                        my_type = MESH_NODE;
                    } else {
                        my_type = MESH_LEAF;
                    }
                    my_layer = parent_assoc.layer + 1;
                    break;
                }
            }
#endif
        } else {
            ESP_LOGI(MESH_TAG, "[%d]%s, "MACSTR", channel:%u, rssi:%d", i,
                     record.ssid, MAC2STR(record.bssid), record.primary,
                     record.rssi);
#ifdef MESH_SET_ROOT
            if (!strcmp(CONFIG_MESH_ROUTER_SSID, (char *) record.ssid)) {
                parent_found = true;
                memcpy(&parent_record, &record, sizeof(record));
                my_type = MESH_ROOT;
                my_layer = MESH_ROOT_LAYER;
            }
#endif
        }
    }
    esp_mesh_flush_scan_result();
    if (parent_found) {
        /*
         * parent
         * Both channel and SSID of the parent are mandatory.
         */
        parent.sta.channel = parent_record.primary;
        memcpy(&parent.sta.ssid, &parent_record.ssid,
               sizeof(parent_record.ssid));
        parent.sta.bssid_set = 1;
        memcpy(&parent.sta.bssid, parent_record.bssid, 6);
        ESP_ERROR_CHECK(esp_mesh_set_ap_authmode(parent_record.authmode));
        if (my_type == MESH_ROOT) {
            if (parent_record.authmode != WIFI_AUTH_OPEN) {
                memcpy(&parent.sta.password, CONFIG_MESH_ROUTER_PASSWD,
                       strlen(CONFIG_MESH_ROUTER_PASSWD));
            }
            ESP_LOGW(MESH_TAG, "<PARENT>%s, "MACSTR", channel:%u, rssi:%d",
                     parent_record.ssid, MAC2STR(parent_record.bssid),
                     parent_record.primary, parent_record.rssi);
        } else {
            if (parent_record.authmode != WIFI_AUTH_OPEN) {
                memcpy(&parent.sta.password, CONFIG_MESH_AP_PASSWD,
                       strlen(CONFIG_MESH_AP_PASSWD));
            }
            ESP_LOGW(MESH_TAG,
                     "<PARENT>%s, layer:%d/%d, assoc:%d/%d, %d, "MACSTR", channel:%u, rssi:%d",
                     parent_record.ssid, parent_assoc.layer,
                     parent_assoc.layer_cap, parent_assoc.assoc,
                     parent_assoc.assoc_cap, parent_assoc.layer2_cap,
                     MAC2STR(parent_record.bssid), parent_record.primary,
                     parent_record.rssi);
        }
        ESP_ERROR_CHECK(esp_mesh_set_parent(&parent, (mesh_addr_t *)&parent_assoc.mesh_id, my_type, my_layer));

    } else {
        ESP_LOGW(MESH_TAG,
                 "<Warning>no parent found, modify IE crypto configuration and scan");
        if (CONFIG_MESH_IE_CRYPTO_FUNCS) {
            /* modify IE crypto key */
            ESP_LOGW(MESH_TAG, "<Config>modify IE crypto key to %s", CONFIG_MESH_IE_CRYPTO_KEY);
            ESP_ERROR_CHECK(esp_mesh_set_ie_crypto_funcs(&g_wifi_default_mesh_crypto_funcs));
            ESP_ERROR_CHECK(esp_mesh_set_ie_crypto_key(CONFIG_MESH_IE_CRYPTO_KEY, strlen(CONFIG_MESH_IE_CRYPTO_KEY)));
        } else {
            /* disable IE crypto */
            ESP_LOGW(MESH_TAG, "<Config>disable IE crypto");
            ESP_ERROR_CHECK(esp_mesh_set_ie_crypto_funcs(NULL));
        }
        esp_wifi_scan_stop();
        scan_config.show_hidden = 1;
        scan_config.scan_type = WIFI_SCAN_TYPE_PASSIVE;
        ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, 0));
    }
}

void mesh_event_handler(mesh_event_t event)
{
    mesh_addr_t id = {0,};
    static uint8_t last_layer = 0;
    ESP_LOGD(MESH_TAG, "esp_event_handler:%d", event.id);

    switch (event.id) {
    case MESH_EVENT_STARTED:
        esp_mesh_get_id(&id);
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_STARTED>ID:"MACSTR"", MAC2STR(id.addr));
        mesh_layer = esp_mesh_get_layer();
        ESP_ERROR_CHECK(esp_mesh_set_self_organized(1, 1));
        esp_wifi_scan_stop();
        wifi_scan_config_t scan_config = { 0 };
        /* mesh softAP is hidden */
        scan_config.show_hidden = 1;
        scan_config.scan_type = WIFI_SCAN_TYPE_PASSIVE;
        ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, 0));
        break;
    case MESH_EVENT_STOPPED:
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_STOPPED>");
        mesh_layer = esp_mesh_get_layer();
        break;
    case MESH_EVENT_CHILD_CONNECTED:
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHILD_CONNECTED>aid:%d, "MACSTR"",
                 event.info.child_connected.aid,
                 MAC2STR(event.info.child_connected.mac));
        break;
    case MESH_EVENT_CHILD_DISCONNECTED:
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHILD_DISCONNECTED>aid:%d, "MACSTR"",
                 event.info.child_disconnected.aid,
                 MAC2STR(event.info.child_disconnected.mac));
        break;
    case MESH_EVENT_ROUTING_TABLE_ADD:
        ESP_LOGW(MESH_TAG, "<MESH_EVENT_ROUTING_TABLE_ADD>add %d, new:%d",
                 event.info.routing_table.rt_size_change,
                 event.info.routing_table.rt_size_new);
        break;
    case MESH_EVENT_ROUTING_TABLE_REMOVE:
        ESP_LOGW(MESH_TAG, "<MESH_EVENT_ROUTING_TABLE_REMOVE>remove %d, new:%d",
                 event.info.routing_table.rt_size_change,
                 event.info.routing_table.rt_size_new);
        break;
    case MESH_EVENT_NO_PARENT_FOUND:
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_NO_PARENT_FOUND>scan times:%d",
                 event.info.no_parent.scan_times);
        break;
    case MESH_EVENT_PARENT_CONNECTED:
        esp_mesh_get_id(&id);
        mesh_layer = event.info.connected.self_layer;
        memcpy(&mesh_parent_addr.addr, event.info.connected.connected.bssid, 6);
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_PARENT_CONNECTED>layer:%d-->%d, parent:"MACSTR"%s, ID:"MACSTR"",
                 last_layer, mesh_layer, MAC2STR(mesh_parent_addr.addr),
                 esp_mesh_is_root() ? "<ROOT>" :
                 (mesh_layer == 2) ? "<layer2>" : "", MAC2STR(id.addr));
        if(last_layer>0){
        	if(last_layer!=mesh_layer){
        		if(last_layer==1){
        			esp_deep_sleep(1000000);
        		}else if(is_tx_running){
        			vTaskDelete(p2p_tx_handle);
        			is_tx_running=false;
        		}

        	}
        }
        last_layer = mesh_layer;
        mesh_connected_indicator(mesh_layer);
        if (esp_mesh_is_root()) {
            tcpip_adapter_dhcpc_start(TCPIP_ADAPTER_IF_STA);
        }
        break;
    case MESH_EVENT_PARENT_DISCONNECTED:
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_PARENT_DISCONNECTED>reason:%d",
                 event.info.disconnected.reason);
        mesh_disconnected_indicator();
        mesh_layer = esp_mesh_get_layer();
        if (event.info.disconnected.reason == WIFI_REASON_ASSOC_TOOMANY) {
            esp_wifi_scan_stop();
            scan_config.show_hidden = 1;
            scan_config.scan_type = WIFI_SCAN_TYPE_PASSIVE;
            ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, 0));
        }
        break;
    case MESH_EVENT_LAYER_CHANGE:
        mesh_layer = event.info.layer_change.new_layer;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_LAYER_CHANGE>layer:%d-->%d%s",
                 last_layer, mesh_layer,
                 esp_mesh_is_root() ? "<ROOT>" :
                 (mesh_layer == 2) ? "<layer2>" : "");

        last_layer = mesh_layer;
        mesh_connected_indicator(mesh_layer);
        break;
    case MESH_EVENT_ROOT_ADDRESS:
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_ADDRESS>root address:"MACSTR"",
                 MAC2STR(event.info.root_addr.addr));
        vTaskDelay(1500 / portTICK_RATE_MS);
        esp_mesh_comm_p2p_start();
        break;
    case MESH_EVENT_ROOT_GOT_IP:
        /* root starts to connect to server */
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_ROOT_GOT_IP>sta ip: " IPSTR ", mask: " IPSTR ", gw: " IPSTR,
                 IP2STR(&event.info.got_ip.ip_info.ip),
                 IP2STR(&event.info.got_ip.ip_info.netmask),
                 IP2STR(&event.info.got_ip.ip_info.gw));
        vTaskDelay(1500 / portTICK_RATE_MS);
        if (esp_mesh_is_root()) {
        	HTTPS_client_init();
        }
        break;
    case MESH_EVENT_ROOT_LOST_IP:
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_LOST_IP>");
        break;
    case MESH_EVENT_ROOT_FIXED:
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_FIXED>%s",
                 event.info.root_fixed.is_fixed ? "fixed" : "not fixed");
        break;
    case MESH_EVENT_SCAN_DONE:
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_SCAN_DONE>number:%d",
                 event.info.scan_done.number);
        mesh_scan_done_handler(event.info.scan_done.number);
        break;
    default:
        ESP_LOGI(MESH_TAG, "unknown id:%d", event.id);
        break;
    }
}

void network_init(void)
{
    ESP_ERROR_CHECK(mesh_light_init());
    ESP_ERROR_CHECK(nvs_flash_init());
    /*  tcpip initialization */
    tcpip_adapter_init();

    ESP_ERROR_CHECK(tcpip_adapter_dhcps_stop(TCPIP_ADAPTER_IF_AP));
    ESP_ERROR_CHECK(tcpip_adapter_dhcpc_stop(TCPIP_ADAPTER_IF_STA));
    /*  wifi initialization */
    ESP_ERROR_CHECK(esp_event_loop_init(NULL, NULL));
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_start());
    /*  mesh initialization */
    ESP_ERROR_CHECK(esp_mesh_init());
    /* mesh enable IE crypto */
    mesh_cfg_t cfg = MESH_INIT_CONFIG_DEFAULT();
    /* mesh ID */
    memcpy((uint8_t *) &cfg.mesh_id, MESH_ID, 6);
    /* mesh event callback */
    cfg.event_cb = &mesh_event_handler;
    /* router */
    cfg.channel = CONFIG_MESH_CHANNEL;
    cfg.router.ssid_len = strlen(CONFIG_MESH_ROUTER_SSID);
    memcpy((uint8_t *) &cfg.router.ssid, CONFIG_MESH_ROUTER_SSID, cfg.router.ssid_len);
    memcpy((uint8_t *) &cfg.router.password, CONFIG_MESH_ROUTER_PASSWD,
           strlen(CONFIG_MESH_ROUTER_PASSWD));
    /* mesh softAP */
    ESP_ERROR_CHECK(esp_mesh_set_ap_authmode(CONFIG_MESH_AP_AUTHMODE));
    cfg.mesh_ap.max_connection = CONFIG_MESH_AP_CONNECTIONS;
    memcpy((uint8_t *) &cfg.mesh_ap.password, CONFIG_MESH_AP_PASSWD,
           strlen(CONFIG_MESH_AP_PASSWD));
    ESP_ERROR_CHECK(esp_mesh_set_config(&cfg));
    /* mesh start */
    ESP_ERROR_CHECK(esp_mesh_start());
    ESP_LOGI(MESH_TAG, "mesh starts successfully, heap:%d\n",  esp_get_free_heap_size());
}

void get_temp_task(void *pvParameter)
{
    ds18b20_init(DS_PIN);
	while(1){
		if (xSemaphoreTake(cacheSemaphore, 1000 / portTICK_PERIOD_MS) == pdTRUE){ 	// wait 1s
			cache_data_temp = ds18b20_get_temp();
			new_data_temp=true;
			xSemaphoreGive(cacheSemaphore);
		}else{
			printf("Could not obtain cache semaphore for rewrite data.\n");
		}
		printf("Temperature: %.2f *C\n", ds18b20_get_temp());
		vTaskDelay(6*60*1000 / portTICK_RATE_MS);	//6min
	}
}

void get_moisture_task(void *pvParameter)
{
	adc1_config_width(ADC_WIDTH_BIT_12);
	adc1_config_channel_atten(ADC1_CHANNEL_7,ADC_ATTEN_DB_11);	//gpio 35 attenuation 11dB
	float moisture_adc;

	while(1){
		moisture_adc=adc1_get_raw(ADC1_CHANNEL_7);
		if(moisture_adc<2000){
			moisture_adc=2000;
		}
		if (xSemaphoreTake(cacheSemaphore, 1000 / portTICK_PERIOD_MS) == pdTRUE){ 	// wait 1sec
			cache_data_moisture = 100-((moisture_adc-2000)/(4095-2000)*100); 		//percent
			new_data_moisture=true;
			xSemaphoreGive(cacheSemaphore);
		}else{
			printf("Could not obtain cache semaphore for rewrite data.\n");
		}
		printf("Moisture: %.2f percent \n", cache_data_moisture);
		vTaskDelay(6*60*1000 / portTICK_RATE_MS); //6min
	}
}

void get_illuminance_task(void *pvParameter){
	bh1750_init();
	while(1){
			if (xSemaphoreTake(cacheSemaphore, 1000 / portTICK_PERIOD_MS) == pdTRUE){ 	// wait 1s,
				cache_data_illuminance = bh1750_read();						//semaphore - rewrite data
				new_data_illuminance=true;
				xSemaphoreGive(cacheSemaphore);
			}else{
				printf("Could not obtain cache semaphore for rewrite data.\n");
			}
			printf("Illuminance: %.2f Lux\n", bh1750_read());
			vTaskDelay(6*60*1000 / portTICK_RATE_MS);	//6min
		}
}

void sensor_init(void){
	switch(sensor){
		case moisture_sensor:
			xTaskCreate(&get_moisture_task, "get_moisture_task", 2048, NULL, 5, &get_moisture_handle);
			break;
		case temp_sensor:
			xTaskCreate(&get_temp_task, "get_temp_task", 2048, NULL, 5, &get_temp_handle);
			break;
		case illuminance_sensor:
			xTaskCreate(&get_illuminance_task, "get_illuminance_task", 2048, NULL, 5, &get_illuminance_handle);
			break;
		default:
			printf("Sensor not recognized");
			break;
	}
}

void resume_tasks(void){
	switch(sensor){
		case moisture_sensor:
			vTaskResume(get_moisture_handle);
			break;
		case temp_sensor:
			vTaskResume(get_temp_handle);
			break;
		case illuminance_sensor:
			vTaskResume(get_illuminance_handle);
			break;
		default:
			printf("Sensor not recognized");
			break;
	}
}

void suspend_tasks(){
	vTaskSuspend(p2p_rx_handle);
	switch(sensor){
		case moisture_sensor:
			vTaskSuspend(get_moisture_handle);
			break;
		case temp_sensor:
			vTaskSuspend(get_temp_handle);
			break;
		case illuminance_sensor:
			vTaskSuspend(get_illuminance_handle);
			break;
		default:
			printf("Sensor not recognized");
			break;
	}

}

void build_http_msg(void){

	char end_of_template[] = " HTTP/1.1\r\n" "Host: Marcin \r\n""Connection: close\r\n\r\n";
	char field1[15];
	char field2[15];
	char field3[15];

	memset(template, 0, 150 * sizeof(char));
	strcat(template, "GET /update?api_key=xxxxxxxxxxxxxxxx");
	if(new_data_temp){
		sprintf(field1,"&field1=%.2f", cache_data_temp);
		strcat(template,field1);
		new_data_temp=false;
	}
	if(new_data_moisture){
		sprintf(field2,"&field2=%.2f", cache_data_moisture);
		strcat(template,field2);
		new_data_moisture=false;
	}
	if(new_data_illuminance){
		sprintf(field3,"&field3=%.2f", cache_data_illuminance);
		strcat(template,field3);
		new_data_illuminance=false;
	}
	strcat(template, end_of_template);

}

void app_main(void)
{
	network_init();
    cacheSemaphore = xSemaphoreCreateMutex();
    sensor_init();
}
