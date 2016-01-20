#pragma once

#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <stdint.h>

bool NetSystemInit();
void NetSystemDeinit();

char const* AllocLocalHostName();
void FreeLocalHostName( char const *str );