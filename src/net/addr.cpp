#include "net/addr.h"

// INTERNAL FUNCTIONS //////////////////////////////////////////////////////////////
//-------------------------------------------------------------------------------------------------------
// get sockaddr, IPv4 or IPv6:
static void* GetInAddr(sockaddr *sa)
{
   if (sa->sa_family == AF_INET) {
      return &(((sockaddr_in*)sa)->sin_addr);
   }
   else {
      return &(((sockaddr_in6*)sa)->sin6_addr);
   }
}

// EXTERNAL FUNCTIONS //////////////////////////////////////////////////////////////
//-------------------------------------------------------------------------------------------------------
addrinfo* AllocAddressesForHost(char const *host,
   char const *service,
   int family,
   int socktype,
   bool binding)
{
   addrinfo hints;
   addrinfo *addr;

   if (nullptr == host) {
      host = "localhost";
   }

   memset(&hints, 0, sizeof(hints));

   // Which network layer it's using - usually want to UNSPEC, since it doesn't matter.  But since we're hard coding
   // the client sides connection, we will likely want to use AF_INET when we want to bind an address
   hints.ai_family = family;

   hints.ai_socktype = socktype; // STREAM based, determines transport layer (TCP)
   hints.ai_flags = binding ? AI_PASSIVE : 0; // used for binding/listening

   int status = getaddrinfo(host, service, &hints, &addr);
   if (status != 0) {
      return nullptr;
   }

   return addr;
}

//-------------------------------------------------------------------------------------------------------
void FreeAddresses(addrinfo *addresses)
{
   freeaddrinfo(addresses);
}

//-------------------------------------------------------------------------------------------------------
size_t GetAddressName(char *buffer, size_t const buffer_size, addrinfo *addr)
{
   char addr_name[INET6_ADDRSTRLEN];
   memset(addr_name, 0, sizeof(addr_name));
   inet_ntop(addr->ai_family, GetInAddr(addr->ai_addr), addr_name, INET6_ADDRSTRLEN);

   size_t len = min(buffer_size - 1, strlen(addr_name));
   memcpy(buffer, addr_name, len);
   buffer[len] = NULL;
   return len;
}

//-------------------------------------------------------------------------------------------------------
void ForEachAddress(addrinfo *addresses, address_work_cb cb, void *user_arg)
{
   addrinfo *iter = addresses;
   while (nullptr != iter) {
      if (cb(iter, user_arg)) {
         break;
      }

      iter = iter->ai_next;
   }
}
