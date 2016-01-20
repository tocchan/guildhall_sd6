#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WinSock2.h>
#include <WS2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

char const *gPort = "5413";

class NetworkSystemOld
{
   private:
      char const *local_host_name;

   public:
      bool init();
      void deinit();

      char const* get_local_host_name() const { return local_host_name; }
};


//-------------------------------------------------------------------------------------------------------
static char* AllocLocalHostName()
{
   // from docs, 256 is max namelen allowed.
   char buffer[256];
   if (SOCKET_ERROR == gethostname( buffer, 256 )) {
      return nullptr;
   }

   size_t len = strlen(buffer);
   if (len == 0) {
      return nullptr; 
   }

   char *ret = (char*)malloc(len + 1);
   memcpy( ret, buffer, len + 1 );

   return ret;
}

//-------------------------------------------------------------------------------------------------------
static std::string WindowsErrorAsString( DWORD error_id ) 
{
   if (error_id != 0) {
      LPSTR buffer;
      DWORD size = FormatMessageA( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
         NULL, 
         error_id, 
         MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT),
         (LPSTR)(&buffer),
         0, NULL );

      std::string msg( buffer, size );
      LocalFree( buffer );
             
      return msg;
   } else {
      return "";
   }
}

//-------------------------------------------------------------------------------------------------------
bool NetworkSystemOld::init() 
{
   WSADATA wsa_data;
   int error = WSAStartup( MAKEWORD(2, 2), &wsa_data );
   if (error == 0) {
      local_host_name = AllocLocalHostName();
      return true;
   } else {
      printf( "Failed to initialize WinSock.  Error[%u]: %s\n", error, WindowsErrorAsString(error).c_str() );
      return false;
   }
}

//-------------------------------------------------------------------------------------------------------
void NetworkSystemOld::deinit() 
{
   free((void*) local_host_name);
   local_host_name = nullptr;

   WSACleanup();
}

//-------------------------------------------------------------------------------------------------------
// get sockaddr, IPv4 or IPv6:
static void* GetInAddr(sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((sockaddr_in*)sa)->sin_addr);
    } else {
      return &(((sockaddr_in6*)sa)->sin6_addr);
    }
}

addrinfo* AllocAddressesForHost( char const *host, 
   char const *service,
   int family,
   int socktype, 
   bool binding )
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
      printf("Failed to find addresses for [%s:%s]: %s\n", host, service, gai_strerror(status));
      return nullptr;
   }

   return addr;
}

//-------------------------------------------------------------------------------------------------------
void FreeAddresses( addrinfo *addresses )
{
   freeaddrinfo(addresses);
}

//-------------------------------------------------------------------------------------------------------
typedef bool (*address_work_cb)( addrinfo*, void *user_arg );

//-------------------------------------------------------------------------------------------------------
static void ForEachAddress( addrinfo *addresses, address_work_cb cb, void *user_arg )
{
   addrinfo *iter = addresses;
   while (nullptr != iter) {
      if (cb( iter, user_arg )) {
         break;
      }

      iter = iter->ai_next;
   }
}

//-------------------------------------------------------------------------------------------------------
static size_t GetAddressName( char *buffer, size_t const buffer_size, addrinfo *addr )
{
   char addr_name[INET6_ADDRSTRLEN];
   memset(addr_name, 0, sizeof(addr_name));
   inet_ntop(addr->ai_family, GetInAddr(addr->ai_addr), addr_name, INET6_ADDRSTRLEN);

   size_t len = min( buffer_size - 1, strlen(addr_name) );
   memcpy( buffer, addr_name, len );
   buffer[len] = NULL;
   return len;
}

//-------------------------------------------------------------------------------------------------------
bool PrintAddress( addrinfo *addr, void* )
{
   char addr_name[INET6_ADDRSTRLEN];
   GetAddressName( addr_name, INET6_ADDRSTRLEN, addr );
   printf("Address family[%i] type[%i] %s\n", addr->ai_family, addr->ai_socktype, addr_name );

   return false;
}

//-------------------------------------------------------------------------------------------------------
// This method of looping through addresses is going to be important for both
// hosting and connection. 
void ListAddressesForHost( char const *host_name, char const *service )
{
   addrinfo *addr = AllocAddressesForHost( host_name, service, AF_UNSPEC, SOCK_STREAM, true );
   ForEachAddress( addr, PrintAddress, nullptr );
   freeaddrinfo(addr);
}

//-------------------------------------------------------------------------------------------------------
bool TryToBind( addrinfo *addr, void *sock_ptr )
{
   SOCKET *sock = (SOCKET*)sock_ptr;

   SOCKET host_sock = INVALID_SOCKET;
   char addr_name[INET6_ADDRSTRLEN];
   GetAddressName( addr_name, INET6_ADDRSTRLEN, addr );
   printf("Attempt to bind on: %s\n", addr_name);

   host_sock = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);

   if (host_sock != INVALID_SOCKET) {
      if (bind(host_sock, addr->ai_addr, (int)(addr->ai_addrlen)) == SOCKET_ERROR) {
         closesocket(host_sock);
         host_sock = INVALID_SOCKET;
         return false;
      }
      else {
         // Connecting on address 
         printf("Bound to : %s\n", addr_name);
         *sock = host_sock;
         return true;
      }
   } else {
      printf( "Failed to create socket?!\n" );
   }

   return false;
}

//-------------------------------------------------------------------------------------------------------
bool TryToConnect(addrinfo *addr, void *sock_ptr)
{
   SOCKET *sock = (SOCKET*)sock_ptr;

   SOCKET host_sock = INVALID_SOCKET;
   char addr_name[INET6_ADDRSTRLEN];
   GetAddressName(addr_name, INET6_ADDRSTRLEN, addr);
   printf("Attempt to connect to: %s\n", addr_name);

   host_sock = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);

   if (host_sock != INVALID_SOCKET) {
      if (connect(host_sock, addr->ai_addr, (int)(addr->ai_addrlen)) == SOCKET_ERROR) {
         closesocket(host_sock);
         host_sock = INVALID_SOCKET;
         return false;
      }
      else {
         // Connecting on address 
         printf("Connected to : %s\n", addr_name);
         *sock = host_sock;
         return true;
      }
   }
   else {
      printf("Failed to create socket?!\n");
   }

   return false;
}

//-------------------------------------------------------------------------------------------------------
SOCKET BindAddress( char const *ip, char const *port, int family = AF_UNSPEC, int type = SOCK_STREAM )
{
   SOCKET host_sock = INVALID_SOCKET;

   addrinfo *addr = AllocAddressesForHost( ip, port, family, type, true ); 
   ForEachAddress( addr, TryToBind, &host_sock );
   FreeAddresses(addr);

   return host_sock;
}


//-------------------------------------------------------------------------------------------------------
void NetworkHost( char const *host_name, char const *port )
{
   SOCKET host = BindAddress( host_name, port, AF_INET );

   if (host == INVALID_SOCKET) {
      printf( "Failed to create listen socket.\n" );
      return;
   }

   // Now listen for incoming connections
   int backlog_count = 8; // max number of connections to queue up
   if (listen( host, backlog_count ) == SOCKET_ERROR) {
      closesocket(host);
      printf( "Failed to listen.  %i\n", WSAGetLastError() );
      return;
   }

   /*
   // For setting blocking status
   u_long non_blocking = 1;
   ioctlsocket( host, FIONBIO, &non_blocking )
   */

    printf( "Waiting for connections...\n" );

    sockaddr_storage their_addr;
    SOCKET sock_them;
    char buffer[2048];

    for (;;) {
      int addr_size = sizeof(their_addr);
      sock_them = accept( host, (sockaddr*)&their_addr, &addr_size );

      if (sock_them == SOCKET_ERROR) {
         int const error = WSAGetLastError();
         if (error == WSAEWOULDBLOCK) {
            // happens when no connection is available and we would normally block, it's harmless
         } else {
            // failed to get a socket?
            printf( "Failed to accept: [%i] %s", error, WindowsErrorAsString(error).c_str() );
         }
      } else {
         printf( "Got a connection!\n" );
         int len = recv( sock_them, buffer, 2048, 0 );
         buffer[len] = 0;
         printf( "Got message: %s\n", buffer );
         send( sock_them, "pong", 4, 0 ); 
         closesocket(sock_them);
      }
    }
}

//-------------------------------------------------------------------------------------------------------
void NetworkJoin( char const *addrname, char const *port, char const *msg )
{
   SOCKET host_sock = INVALID_SOCKET;

   addrinfo *addr = AllocAddressesForHost( addrname, port, AF_UNSPEC, SOCK_STREAM, false );
   ForEachAddress( addr, TryToConnect, &host_sock );
   FreeAddresses(addr);


   if (host_sock != INVALID_SOCKET) {
      send( host_sock, msg, (int)strlen(msg), 0 ); 

      char buffer[128];
      int len = recv( host_sock, buffer, 128, 0 );
      if (len > 0) {
         buffer[len] = NULL;
         printf( "Received response: %s\n", buffer );
      }
   } else {
      printf( "Could not connect...\n" );
   }

   closesocket( host_sock );
}

//-------------------------------------------------------------------------------------------------------
int main( int argc, char const **argv )
{
   NetworkSystemOld net;
   if (!net.init()) {
      printf( "Failed to initialize net system.\n" );
      _getch();
      return false;
   }

   // List Addresses
   ListAddressesForHost( net.get_local_host_name(), gPort );

   if ((argc <= 1) || (_strcmpi( argv[1], "host" ) == 0)) {
      printf( "Hosting...\n" );
      NetworkHost( "localhost", gPort ); 
   } else if (argc > 2) {
      char const *addr = argv[1];
      char const *msg = argv[2];
      printf( "Sending message \"%s\" to [%s]\n", msg, addr );
      NetworkJoin( addr, gPort, msg );
   } else {
      printf( "Either past \"host\" or \"<addr> <msg>\"\n" );
   }

   net.deinit();

   printf( "Press any key to continue...\n" );
   _getch();
   return 0;
}

