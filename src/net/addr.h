#pragma once

#include "net/net.h"

// TYPES ////////////////////////////////////////////////////////////////////
typedef bool(*address_work_cb)(addrinfo*, void *user_arg);


// FUNCTION PROTOTYPES //////////////////////////////////////////////////////
addrinfo* AllocAddressesForHost(char const *host,
   char const *service,
   int family,
   int socktype,
   bool binding);

void FreeAddresses(addrinfo *addresses);

size_t GetAddressName(char *buffer, size_t const buffer_size, addrinfo *addr);

void ForEachAddress(addrinfo *addresses, address_work_cb cb, void *user_arg);

