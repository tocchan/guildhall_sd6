#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <WinSock2.h>
#include <WS2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#include <stdio.h>
#include <conio.h>
#include <malloc.h>

class NetworkSystem
{
   public:
      bool init();
      void deinit();
};

bool NetworkSystem::init()
{
   WSADATA wsa_data;
   int error = WSAStartup( MAKEWORD(2, 2), &wsa_data );

   if (error == 0) {
      return true;
   } else {
      printf( "Failed to initialize WinSock.  Error[%u]\n", error );
      return false;
   }
}

void NetworkSystem::deinit()
{
   WSACleanup();
}

static char* AllocLocalHostName()
{
   // from docs, 256 is max namelen allowed.
   char buffer[256];

   // gethostname - socket function
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
// get sockaddr, IPv4 or IPv6:
static void* GetInAddr(sockaddr *sa)
{
   if (sa->sa_family == AF_INET) {
      return &(((sockaddr_in*)sa)->sin_addr);
   } else {
      return &(((sockaddr_in6*)sa)->sin6_addr);
   }
}

void PrintAddressesForHost( char const *host_name, char const *service )
{
   addrinfo hints;
   addrinfo *addr;

   if (nullptr == host_name) {
      host_name = "localhost";
   }

   memset( &hints, 0, sizeof(hints) );

   // Which network layer it's using - usually want to UNSPEC, since it doesn't matter.  But since we're hard coding
   // the client sides connection, we will likely want to use AF_INET when we want to bind an address
   hints.ai_family = AF_UNSPEC;  
   hints.ai_socktype = SOCK_STREAM; // STREAM based, determines transport layer (TCP)
   hints.ai_flags = AI_PASSIVE; // used for binding/listening

   int status = getaddrinfo( host_name, service, &hints, &addr );
   if (status != 0) {
      printf( "Failed to create socket address: %s\n", gai_strerror(status) );
      return;
   }

   addrinfo *iter;
   char addr_name[INET6_ADDRSTRLEN];
   for (iter = addr; iter != nullptr; iter = iter->ai_next) {
      inet_ntop( iter->ai_family, GetInAddr(iter->ai_addr), addr_name, INET6_ADDRSTRLEN );
      printf( "Address family[%i] type[%i] %s : %s\n", iter->ai_family, iter->ai_socktype, addr_name, service );
   }

   freeaddrinfo(addr);
}

void ServerLoop( SOCKET host_socket )
{
   int result = listen( host_socket, 8 );
   if (result == SOCKET_ERROR) {
      printf( "Failed to listen.\n" );
      return;
   }

   printf( "Waiting for connections...\n" );
   while (true) {
      sockaddr_storage their_addr;
      int their_addr_len = sizeof(their_addr);

      printf( "Waiting on Accept...\n" );
      SOCKET their_socket = accept( host_socket, (sockaddr*)&their_addr, &their_addr_len );

      if (their_socket == INVALID_SOCKET) {
         // Special note - non-blocking sockets... 
         printf( "Failed to accept.\n");
         continue;
      }

      char buffer[1024];
      printf( "Waiting to recv...\n" );
      int recvd = recv( their_socket, buffer, 1024, 0 );
      if (recvd > 0) {
         buffer[recvd] = NULL;
         printf( "Received data: %s\n", buffer );

         printf( "Sending data...\n" );
         send( their_socket, buffer, recvd, 0 );
      }

      closesocket(their_socket);
   }
}

void StartHost( char const *host_name, 
   char const *service, 
   int addr_family = AF_INET )
{
   addrinfo hints;
   addrinfo *addr;

   if (nullptr == host_name) {
      host_name = "localhost";
   }

   memset( &hints, 0, sizeof(hints) );

   // Which network layer it's using - usually want to UNSPEC, since it doesn't matter.  But since we're hard coding
   // the client sides connection, we will likely want to use AF_INET when we want to bind an address
   hints.ai_family = addr_family;  
   hints.ai_socktype = SOCK_STREAM; // STREAM based, determines transport layer (TCP)
   hints.ai_flags = AI_PASSIVE; // used for binding/listening

   int status = getaddrinfo( host_name, service, &hints, &addr );
   if (status != 0) {
      printf( "Failed to create socket address: %s\n", gai_strerror(status) );
      return;
   }

   addrinfo *iter;
   char addr_name[INET6_ADDRSTRLEN];

   SOCKET host_socket = INVALID_SOCKET;
   for (iter = addr; iter != nullptr; iter = iter->ai_next) {
      inet_ntop( iter->ai_family, GetInAddr(iter->ai_addr), addr_name, INET6_ADDRSTRLEN );
      printf( "Trying to bind addr: family[%i] type[%i] %s : %s\n", 
         iter->ai_family, 
         iter->ai_socktype, 
         addr_name, service );

      host_socket = socket( iter->ai_family, iter->ai_socktype, iter->ai_protocol );
      if (host_socket == INVALID_SOCKET) {
         int error = WSAGetLastError();
         printf( "Failed to create socket:  Socket Error[%i]\n", error );
         continue;
      }

      int result = bind( host_socket, iter->ai_addr, (int)(iter->ai_addrlen) );
      if (SOCKET_ERROR == result) {
         closesocket(host_socket);
         host_socket = INVALID_SOCKET;
         int error = WSAGetLastError();
         printf( "Failed to bind:  Socket Error[%i]\n", error );
         continue;
      }
      break;
   }

   freeaddrinfo(addr);

   if (host_socket == INVALID_SOCKET) {
      printf( "Could not create host.\n" );
      return;
   }

   printf( "Socket bound...\n" );
   // DO HOST STUFF!
   
   // To be continued...
   ServerLoop( host_socket );

   // DONE WITH HOST STUFF!
   closesocket(host_socket);
   host_socket = INVALID_SOCKET;
}

void ClientLoop( SOCKET host_socket, char const *msg ) 
{
   int sent = send( host_socket, msg, strlen(msg), 0 );
   if (sent == SOCKET_ERROR) {
      printf("Failed to send.\n");
   }

   char buffer[1024];
   int recvd = recv( host_socket, buffer, 1024, 0 );

   if (recvd > 0) {
      buffer[recvd] = NULL;
      printf( "received: %s\n", buffer );
   }
}

void StartClient( char const *host_name, 
   char const *service, 
   char const *msg ) 
{
   addrinfo hints;
   addrinfo *addr;

   memset( &hints, 0, sizeof(hints) );

   // Which network layer it's using - usually want to UNSPEC, since it doesn't matter.  But since we're hard coding
   // the client sides connection, we will likely want to use AF_INET when we want to bind an address
   hints.ai_family = AF_INET;  
   hints.ai_socktype = SOCK_STREAM; // STREAM based, determines transport layer (TCP)

   int status = getaddrinfo( host_name, service, &hints, &addr );
   if (status != 0) {
      printf( "Failed to create socket address: %s\n", gai_strerror(status) );
      return;
   }

   addrinfo *iter;
   char addr_name[INET6_ADDRSTRLEN];

   SOCKET host_socket = INVALID_SOCKET;
   for (iter = addr; iter != nullptr; iter = iter->ai_next) {
      inet_ntop( iter->ai_family, GetInAddr(iter->ai_addr), addr_name, INET6_ADDRSTRLEN );
      printf( "Trying to bind addr: family[%i] type[%i] %s : %s\n", 
         iter->ai_family, 
         iter->ai_socktype, 
         addr_name, service );
    
      host_socket = socket( iter->ai_family, iter->ai_socktype, iter->ai_protocol );
      if (host_socket == INVALID_SOCKET) {
         int error = WSAGetLastError();
         printf( "Failed to create socket:  Socket Error[%i]\n", error );
         continue;
      }

      int result = connect( host_socket, iter->ai_addr, iter->ai_addrlen );
      if (result == SOCKET_ERROR) {
         printf( "Failed to connect.\n" );
         closesocket( host_socket );
         host_socket = INVALID_SOCKET;
      } else {
         printf( "Connected!\n" );
         break;
      }
   }

   freeaddrinfo(addr);

   if (host_socket != INVALID_SOCKET) {
      ClientLoop( host_socket, msg );
      closesocket( host_socket );
   }
}

int main( int argc, char const **argv )
{
   NetworkSystem net;

   if (!net.init()) {
      return 1;
   }

   // Do rest of program here
   char const *my_host_name = AllocLocalHostName();
   printf( "Host name: %s\n", my_host_name );

   PrintAddressesForHost( my_host_name, "1234" );

   if (argc > 2) {
      char const *host_name = argv[1];
      char const *msg = argv[2];
      // Start a client
      StartClient( host_name, "1234", msg );
   } else {
      StartHost( my_host_name, "1234" );
   }

   net.deinit();

   printf( "Press any key to continue..." );
   _getch();
   return 0;
}