#include "network.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_wps.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include "mdns.h"

#include <string.h>

#include "ui.h"


extern char* osd_getromdata();
extern size_t osd_getromdata_length();


static int host_state_sock = -1;

#define NETWORK_EMUSTATE_PORT (1234)
#define NETWORK_TRANSMIT_MAX (128)


#define EXAMPLE_ESP_WIFI_SSID      "ODROID-GO-"
#define EXAMPLE_ESP_WIFI_PASS      "password"
#define EXAMPLE_MAX_STA_CONN       1

const int WIFI_CONNECTED_BIT = BIT0;
const int WIFI_SCAN_DONE = BIT1;


static const char* device_name = NULL;


/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

static const char *TAG = "wifi softAP";

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) 
    {
    case SYSTEM_EVENT_AP_STACONNECTED:
        ESP_LOGI(TAG, "station:"MACSTR" join, AID=%d",
                 MAC2STR(event->event_info.sta_connected.mac),
                 event->event_info.sta_connected.aid);
        break;
    case SYSTEM_EVENT_AP_STADISCONNECTED:
        ESP_LOGI(TAG, "station:"MACSTR"leave, AID=%d",
                 MAC2STR(event->event_info.sta_disconnected.mac),
                 event->event_info.sta_disconnected.aid);
        break;

    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        ESP_LOGI(TAG, "got SYSTEM_EVENT_STA_GOT_IP:%s",
                 ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
        //s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        {
            //if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            //    esp_wifi_connect();
                xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
                //s_retry_num++;
            //    ESP_LOGI(TAG,"retry to connect to the AP");
            //}
            ESP_LOGI(TAG,"SYSTEM_EVENT_STA_DISCONNECTED\n");
            break;
        }
    case SYSTEM_EVENT_SCAN_DONE:
        ESP_LOGI(TAG, "got SYSTEM_EVENT_SCAN_DONE\n");
        xEventGroupSetBits(s_wifi_event_group, WIFI_SCAN_DONE);
        break;

    default:
        break;
    }
    return ESP_OK;
}

void wifi_init_softap()
{
    uint8_t mac[6];   // base MAC address, length: 6 bytes.
    ESP_ERROR_CHECK(esp_efuse_mac_get_default(&mac));

    if (device_name) abort();
    device_name = (const char*)malloc(strlen(EXAMPLE_ESP_WIFI_SSID) + 11 + 1); // Name + 4 hex bytes + zero termination
    if (!device_name) abort();

    sprintf(device_name, "%s%02X-%02X-%02X-%02X", EXAMPLE_ESP_WIFI_SSID, mac[2], mac[3], mac[4], mac[5]);
    printf("device_name='%s'\n", device_name);


    s_wifi_event_group = xEventGroupCreate();

    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    

    wifi_config_t wifi_config = {
        .ap = {
 //           .ssid = device_name,
            .ssid_len = strlen(device_name),
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    strcpy(&wifi_config.ap.ssid, device_name);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished.SSID:%s password:%s",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
}

#if 0
static void network_host_proc(void *arg)
{
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) abort();

    struct sockaddr_in si_me;
    memset((void*) &si_me, 0, sizeof(si_me));
    
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(NETWORK_EMUSTATE_PORT);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, &si_me, sizeof(si_me)) == -1)
    {
        printf("bind failed.\n");
        abort();
    }

    printf("socket OK.\n");

    if (listen(sock, 1) < 0)
    {
        printf("listen failed.\n");
        abort();
    }

    struct sockaddr_in cli_addr;
    socklen_t clilen = sizeof(cli_addr);
   
    while (1) 
    {
        int newsockfd = accept(sock, (struct sockaddr *)&cli_addr, &clilen);            
        if (newsockfd < 0)
        {
            printf("accept failed.\n");
            abort();
        }

        bool error_flag = false;
        while(!error_flag)
        {
            uint8_t command;
            int r = recv(newsockfd, &command, sizeof(command), 0);
            if (r < 1) break;

            switch(command)
            {
                case NETWORK_CONTROL_SEND_ROM:
                {
                    printf("NETWORK_CONTROL_SEND_ROM started.\n");

                    uint32_t romlength = osd_getromdata_length();
                    send(newsockfd, &romlength, sizeof(romlength), 0);

                    char* rom_ptr = osd_getromdata();
                    uint32_t total_sent = 0;
                    while (total_sent < romlength)
                    {
                        ssize_t sent = send(newsockfd, rom_ptr + total_sent, NETWORK_TRANSMIT_MAX, 0);
                        if (sent < 0)
                        {
                            error_flag = true;
                            break;
                        } 

                        printf("sent %d of %d rom data.\n", total_sent, romlength);
                        total_sent += sent;
                    }

                    printf("NETWORK_CONTROL_SEND_ROM ended.\n");

                    break;
                }

                default:
                    printf("Unknonwn client command %d\n", command);
                    break;

            }
        }

        close(newsockfd);
    }
}
#endif

bool network_version_check()
{
    // TODO
    return true;
}



static int host_sock = -1;
static int comm_sock = -1;

void network_host_init()
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
    wifi_init_softap();

    tcpip_adapter_ip_info_t ip_info;
    esp_err_t r = tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ip_info);
    if (r != ESP_OK) abort();

    printf("IP=" IPSTR ", NM=" IPSTR ", GW=" IPSTR "\n", 
        IP2STR(&ip_info.ip),
        IP2STR(&ip_info.netmask),
        IP2STR(&ip_info.gw));

    //xTaskCreatePinnedToCore(&network_host_proc, "host_proc", 1024 * 2, NULL, 5, NULL, 1);

    host_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (host_sock < 0) abort();

    struct sockaddr_in si_me;
    memset((void*) &si_me, 0, sizeof(si_me));
    
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(NETWORK_EMUSTATE_PORT);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(host_sock, &si_me, sizeof(si_me)) == -1)
    {
        printf("host_sock bind failed.\n");
        abort();
    }

    printf("host_sock socket OK.\n");

    if (listen(host_sock, 1) < 0)
    {
        printf("host_sock listen failed.\n");
        abort();
    }
}

void network_host_wait_for_connection()
{
    printf("network_host_wait_for_connection started\n");

    if (host_sock < 0) abort();

    struct sockaddr_in cli_addr;
    socklen_t clilen = sizeof(cli_addr);
   
    comm_sock = accept(host_sock, (struct sockaddr *)&cli_addr, &clilen);            
    if (comm_sock < 0)
    {
        printf("accept failed.\n");
        abort();
    }

    printf("network_host_wait_for_connection finished\n");
}




void network_client_init()
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");

    s_wifi_event_group = xEventGroupCreate();

    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL) );

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    

    wifi_config_t wifi_config = {
//        .sta = {
//            .ssid = EXAMPLE_ESP_WIFI_SSID,
//            .password = EXAMPLE_ESP_WIFI_PASS
//        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start());


    const char* host_ssid = NULL;
    while(!host_ssid)
    {
        // Scan
        wifi_scan_config_t scan_config = {
                .ssid = NULL,
                .bssid = NULL,
                .channel = 0,
                .show_hidden = 0,
                .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    //            .scan_time.min = 0,
    //            .scan_time.max = 0
        };
        ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));

        //xEventGroupWaitBits(s_wifi_event_group, WIFI_SCAN_DONE,
        //    false, true, portMAX_DELAY);

        uint16_t ap_count;
        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
        printf("ap_count=%d\n", ap_count);

        wifi_ap_record_t* ap_records = malloc(sizeof(wifi_ap_record_t) * ap_count);
        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_records));
        printf("ap_count=%d\n", ap_count);

        const char** hosts = malloc(sizeof(char*) * ap_count);
        memset(hosts, 0, sizeof(char*) * ap_count);

        uint16_t host_count = 0;
        for(uint16_t i = 0; i < ap_count; ++i)
        {            
            size_t len = strlen((char*)ap_records[i].ssid);
            printf("AP: ssid='%s' (%d), rssi=%d\n", ap_records[i].ssid, len, ap_records[i].rssi);

            if (strncmp(EXAMPLE_ESP_WIFI_SSID, (char*)ap_records[i].ssid, strlen(EXAMPLE_ESP_WIFI_SSID)) == 0)
            {
                hosts[host_count] = malloc(sizeof(char) * len + 1);
                if (!hosts[host_count]) abort();

                //memset(hosts[i], 0, len + 1);
                strcpy(hosts[host_count], (char*)ap_records[i].ssid);
                ++host_count;
            }
        }

        if (host_count > 0)
        {
            printf("starting host selection UI.\n");


            // Select SSID
            host_ssid = ui_choose_host(hosts, host_count);
            printf("SSID selected=%s\n", host_ssid);

            // Configure wifi
            strcpy((char*)wifi_config.sta.ssid, host_ssid);
            strcpy((char*)wifi_config.sta.password, EXAMPLE_ESP_WIFI_PASS);
        }

        // Free resources
        for(uint16_t i = 0; i < ap_count; ++i)
        {
            free(hosts[i]);            
        }

        free(hosts);
        free(ap_records);
    }


    ESP_ERROR_CHECK(esp_wifi_stop());
            
    //ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start());

    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
        false, true, portMAX_DELAY);

    ESP_LOGI(TAG, "wifi_init_sta finished.");
    ESP_LOGI(TAG, "connect to ap SSID:%s password:%s",
        EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
}

void network_client_connect_to_host()
{
    printf("network_client_connect_to_host started\n");

    comm_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (comm_sock < 0) abort();

    printf("comm_sock socket OK.\n");

    struct sockaddr_in si_me;
    memset((void*) &si_me, 0, sizeof(si_me));
    
    
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(NETWORK_EMUSTATE_PORT);
    //si_me.sin_addr.s_addr = htonl(INADDR_ANY);
    if(inet_pton(AF_INET, "192.168.4.1", &si_me.sin_addr)<=0)  
    { 
        printf("inet_pton failed.\n"); 
        abort();
    } 

    if (connect(comm_sock, (struct sockaddr *)&si_me, sizeof(si_me)) < 0) 
    { 
        printf("comm_sock connect failed \n"); 
        abort();
    }

    printf("network_client_connect_to_host finished\n");
}

uint32_t network_host_send_rom()
{
    printf("network_host_send_rom started.\n");

    uint32_t total_sent = 0;

    uint32_t romlength = osd_getromdata_length();
    if (send(comm_sock, &romlength, sizeof(romlength), 0) > 0)
    {
        char* rom_ptr = osd_getromdata();

        while (total_sent < romlength)
        {
            ssize_t sent = send(comm_sock, rom_ptr + total_sent, NETWORK_TRANSMIT_MAX, 0);
            if (sent < 1)
            {
                total_sent = 0;
                break;
            } 

            printf("sent %d of %d rom data.\n", total_sent, romlength);
            total_sent += sent;

            vTaskDelay(1);
        }
    }

    printf("network_host_send_rom ended.\n");
    return total_sent;
}

uint32_t network_client_load_rom()
{
    printf("network_client_load_rom started.\n");

    size_t total_length = 0;

    uint32_t rom_length;
    if (recv(comm_sock, &rom_length, sizeof(rom_length), 0) == sizeof(rom_length))
    {
        char* rom_ptr = osd_getromdata();

        while(total_length < rom_length)
        {
            ssize_t read = recv(comm_sock, rom_ptr + total_length, NETWORK_TRANSMIT_MAX, 0);
            if (read < 1)
            {
                total_length = 0;
                break;
            }

            printf("received %d of %d rom data.\n", total_length, rom_length);
            total_length += read;
        }
    }

    printf("network_client_load_rom ended.\n");
    return total_length;
}



ssize_t network_send(uint8_t* data, size_t len)
{
    return send(comm_sock, data, len, 0);
}

ssize_t network_recv(uint8_t* data, size_t len)
{
    return recv(comm_sock, data, len, 0);
}

void network_close()
{
    close(comm_sock);
}


bool network_client_sync_with_host()
{
    return true;
}

const char* network_hostname_get()
{
    return device_name;
}
