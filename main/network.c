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


extern char* osd_getromdata();
extern size_t osd_getromdata_length();


static int host_state_sock = -1;

#define NETWORK_EMUSTATE_PORT (1234)


#define EXAMPLE_ESP_WIFI_SSID      "odroid-go-"
#define EXAMPLE_ESP_WIFI_PASS      "password"
#define EXAMPLE_MAX_STA_CONN       1

const int WIFI_CONNECTED_BIT = BIT0;


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

    default:
        break;
    }
    return ESP_OK;
}

void wifi_init_softap()
{
    s_wifi_event_group = xEventGroupCreate();

    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished.SSID:%s password:%s",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
}

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
                        ssize_t sent = send(newsockfd, rom_ptr + total_sent, 256, 0);
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


bool network_version_check()
{
    // TODO
    return true;
}

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

    xTaskCreatePinnedToCore(&network_host_proc, "host_proc", 1024 * 2, NULL, 5, NULL, 1);
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
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
        false, true, portMAX_DELAY);

    ESP_LOGI(TAG, "wifi_init_sta finished.");
    ESP_LOGI(TAG, "connect to ap SSID:%s password:%s",
        EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
}

void network_client_load_rom()
{
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) abort();

    printf("socket OK.\n");

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

    if (connect(sock, (struct sockaddr *)&si_me, sizeof(si_me)) < 0) 
    { 
        printf("connect failed \n"); 
        abort();
    } 

    uint8_t command = NETWORK_CONTROL_SEND_ROM;
    int r = send(sock, &command, sizeof(command), 0);
    if (r < 1)
    {
        printf("send failed.\n");
        abort();
    }

    uint32_t rom_length;
    r = recv(sock, &rom_length, sizeof(rom_length), 0);
    if (r != sizeof(rom_length))
    {
        printf("recv failed.\n");
        abort();
    }

    char* rom_ptr = osd_getromdata();
    size_t total_length = 0;
    while(total_length < rom_length)
    {
        ssize_t read = recv(sock, rom_ptr + total_length, 512, 0);
        if (read < 0)
        {
            printf("recv failed.\n");
            abort();
        }

        printf("received %d of %d rom data.\n", total_length, rom_length);
        total_length += read;
    }
    
    close(sock);
}

bool network_client_sync_with_host()
{
    return true;
}
