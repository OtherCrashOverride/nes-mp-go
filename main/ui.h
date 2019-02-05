#pragma once

typedef enum
{
    NET_ROLE_SERVER = 0,
    NET_ROLE_CLIENT,
    NET_ROLE_MAX
} NET_ROLE;


const char* ui_choosefile(const char* path, const char* extension, const char* current);
NET_ROLE ui_choose_role();
const char* ui_choose_host(const char** hosts, int host_count);
void ui_message(const char* message, const char* title);
