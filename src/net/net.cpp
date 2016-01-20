#include "net/net.h"

#include <malloc.h>

#pragma comment(lib, "ws2_32.lib")

//-------------------------------------------------------------------------------------------------------
bool NetSystemInit()
{
   WSADATA wsa_data;
   int error = WSAStartup(MAKEWORD(2, 2), &wsa_data);
   if (error != 0) {
      return false;
   }

   // other init stuff here

   return true;
}

//-------------------------------------------------------------------------------------------------------
void NetSystemDeinit()
{
   WSACleanup();
}

//-------------------------------------------------------------------------------------------------------
char const* AllocLocalHostName()
{
   // from docs, 256 is max namelen allowed.
   char buffer[256];
   if (SOCKET_ERROR == gethostname(buffer, 256)) {
      return nullptr;
   }

   size_t len = strlen(buffer);
   if (len == 0) {
      return nullptr;
   }

   char *ret = (char*)malloc(len + 1);
   memcpy(ret, buffer, len + 1);

   return ret;
}

//-------------------------------------------------------------------------------------------------------
void FreeLocalHostName( char const *str )
{
   free((void*)str);
}
