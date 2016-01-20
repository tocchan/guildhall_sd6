#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "net/net.h"
#include "net/addr.h"

char const *gPort = "5413";


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
static bool PrintAddress( addrinfo *addr, void* )
{
   char addr_name[INET6_ADDRSTRLEN];
   GetAddressName( addr_name, INET6_ADDRSTRLEN, addr );
   printf("Address family[%i] type[%i] %s\n", addr->ai_family, addr->ai_socktype, addr_name );

   return false;
}

//-------------------------------------------------------------------------------------------------------
// This method of looping through addresses is going to be important for both
// hosting and connection. 
static void ListAddressesForHost( char const *host_name, char const *service )
{
   addrinfo *addr = AllocAddressesForHost( host_name, service, AF_UNSPEC, SOCK_STREAM, true );
   ForEachAddress( addr, PrintAddress, nullptr );
   freeaddrinfo(addr);
}

//-------------------------------------------------------------------------------------------------------
static bool TryToBind( addrinfo *addr, void *sock_ptr )
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
static bool TryToConnect(addrinfo *addr, void *sock_ptr)
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
static SOCKET BindAddress( char const *ip, char const *port, int family = AF_UNSPEC, int type = SOCK_STREAM )
{
   SOCKET host_sock = INVALID_SOCKET;

   addrinfo *addr = AllocAddressesForHost( ip, port, family, type, true ); 
   ForEachAddress( addr, TryToBind, &host_sock );
   FreeAddresses(addr);

   return host_sock;
}


//-------------------------------------------------------------------------------------------------------
static void NetworkHost( char const *host_name, char const *port )
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
static void NetworkJoin( char const *addrname, char const *port, char const *msg )
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
   if (!NetSystemInit()) {
      printf( "Failed to initialize net system.\n" );
      _getch();
      return false;
   }

   // List Addresses
   char const *hostname = AllocLocalHostName();
   ListAddressesForHost( hostname, gPort );
   FreeLocalHostName(hostname);

   // Host/Client Logic
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

   NetSystemDeinit();

   printf( "Press any key to continue...\n" );
   _getch();
   return 0;
}

