#include <stdbool.h>

typedef enum
{
    NETWORK_CONTROL_SEND_ROM = 0,
    NETWORK_CONTROL_SEND_STATE
} NETWORK_CONTROL_COMMAND;

bool network_version_check();
void network_host_init();
void network_client_init();
void network_client_load_rom();
bool network_client_sync_with_host();
