#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "net/net.h"
#include "net/addr.h"

char const *gHostPort = "5413";
char const *gClientPort = "5414";


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
   GetAddressName( addr_name, INET6_ADDRSTRLEN, addr->ai_addr );
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
   GetAddressName( addr_name, INET6_ADDRSTRLEN, addr->ai_addr );
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
   GetAddressName(addr_name, INET6_ADDRSTRLEN, addr->ai_addr);
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
static void NetworkHost( char const *port )
{
   char const *host_name = AllocLocalHostName();
   SOCKET sock = BindAddress( host_name, port, AF_INET, SOCK_DGRAM );
   FreeLocalHostName(host_name);

   if (sock == INVALID_SOCKET) {
      printf( "Failed to create listen socket.\n" );
      return;
   }

   // Don't need to listen for DGRAM sockets

   /*
   // For setting blocking status
   u_long non_blocking = 1;
   ioctlsocket( sock, FIONBIO, &non_blocking )
   */

    printf( "Waiting for messages...\n" );

    sockaddr_storage their_addr;
    char buffer[2048];

    for (;;) {
      int addr_size = sizeof(their_addr);
      int recvd = recvfrom( sock, buffer, 2048, 0, (sockaddr*)&their_addr, &addr_size );

      char from_name[128];
      GetAddressName( from_name, 128, (sockaddr*)&their_addr );

      if (recvd > 0) {
         buffer[recvd] = NULL;
         printf( "Received Message[%s] from %s\n", buffer, from_name );
      } else {
         int error = WSAGetLastError();
         printf( "recvfrom error: %i, %i\n", recvd, error );
      }
    }

    closesocket(sock);
}

class SpamHelper 
{
   public:
      SOCKET sock;
      char const *msg;
};

static bool SpamMessage( addrinfo *addr, void *user_arg ) 
{
   SpamHelper *helper = (SpamHelper*)user_arg;

   int sent = sendto( helper->sock, helper->msg, strlen(helper->msg), 0, 
      addr->ai_addr, addr->ai_addrlen );

   char name[128];
   GetAddressName( name, 128, addr->ai_addr );
   printf( "Spammed %iB message to [%s]\n", sent, name );
   if (sent <= 0) {
      printf( "Error: %i\n", WSAGetLastError() );
   }

   return false;
}

//-------------------------------------------------------------------------------------------------------
static void NetworkClient( char const *target, char const *port, char const *msg )
{
   char const *host_name = AllocLocalHostName();
   SOCKET sock = BindAddress(host_name, gClientPort, AF_INET, SOCK_DGRAM);
   FreeLocalHostName(host_name);

   if (sock == INVALID_SOCKET) {
      printf( "Could not bind adddress.\n" );
      return;
   }
   
   SpamHelper helper;
   helper.sock = sock;
   helper.msg = msg;

   addrinfo *spam = AllocAddressesForHost( target, port, AF_UNSPEC, SOCK_DGRAM, false );
   ForEachAddress( spam, SpamMessage, &helper ); 
   FreeAddresses( spam );
   
   closesocket( sock );
}

static void NetworkBroadcast( char const *msg ) 
{
   SOCKET sock = socket( PF_INET, SOCK_DGRAM, IPPROTO_UDP );

   sockaddr_in addr;
   memset( &addr, 0, sizeof(addr) );

   addr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
   addr.sin_port = htons(0);
   addr.sin_family = PF_INET;

   int broadcast = 1;
   int error = setsockopt( sock, SOL_SOCKET, SO_BROADCAST, (char*)&broadcast, sizeof(broadcast) );
   if (error == SOCKET_ERROR) {
      printf( "Failed to set broadcast. %u\n", WSAGetLastError() );
      closesocket(sock);
      return;
   }

   error = bind( sock, (sockaddr*)&addr, sizeof(addr) );
   if (error == SOCKET_ERROR) {
      printf( "Failed to bind broadcast. %u\n", WSAGetLastError() );
      closesocket(sock);
      return;
   }


   sockaddr_in out_addr;
   memset( &out_addr, 0, sizeof(out_addr) );
   out_addr.sin_addr.S_un.S_addr = htonl(-1);
   out_addr.sin_port = htons(5413);
   out_addr.sin_family = PF_INET;

   int sent = sendto( sock, msg, strlen(msg), 0, (sockaddr*)&out_addr, sizeof(out_addr) );
   printf( "Broadcast message: %i sent.\n", sent );
   closesocket(sock);
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
   ListAddressesForHost( hostname, gHostPort );
   FreeLocalHostName(hostname);

   // Host/Client Logic
   if ((argc <= 1) || (_strcmpi( argv[1], "sock" ) == 0)) {
      printf( "Hosting...\n" );
      NetworkHost( gHostPort ); 
   } else if (argc > 2) {
      char const *addr = argv[1];
      char const *msg = argv[2];
      printf( "Sending message \"%s\" to [%s]\n", msg, addr );
      NetworkClient( addr, gHostPort, msg );
   } else {
      char const *msg = argv[1];
      printf( "Broadcast message \"%s\".\n", msg );
      NetworkBroadcast( msg );
   }

   NetSystemDeinit();

   printf( "Press any key to continue...\n" );
   _getch();
   return 0;
}

