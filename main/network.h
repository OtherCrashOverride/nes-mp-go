#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>


typedef enum
{
    NETWORK_CONTROL_SEND_ROM = 0,
    NETWORK_CONTROL_SEND_STATE
} NETWORK_CONTROL_COMMAND;

bool network_version_check();

void network_host_init();
void network_host_wait_for_connection();

void network_client_init();
void network_client_connect_to_host();

uint32_t network_host_send_rom();
uint32_t network_client_load_rom();

ssize_t network_send(uint8_t* data, size_t len);
ssize_t network_recv(uint8_t* data, size_t len);
void network_close();


bool network_client_sync_with_host();
