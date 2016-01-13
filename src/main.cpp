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



class NetworkSystem
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
bool NetworkSystem::init() 
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
void NetworkSystem::deinit() 
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

// This method of looping through addresses is going to be important for both
// hosting and connection. 
void ListAddressesForHost( char const *host_name, char const *service )
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
   for (iter = addr; iter != nullptr; iter = iter->ai_next) {
      char addr_name[INET6_ADDRSTRLEN];
      inet_ntop( iter->ai_family, GetInAddr(iter->ai_addr), addr_name, INET6_ADDRSTRLEN );
      printf( "Address family[%i] type[%i] %s : %s\n", iter->ai_family, iter->ai_socktype, addr_name, service );
   }

   freeaddrinfo(addr);
}


//-------------------------------------------------------------------------------------------------------
SOCKET BindAddress( char const *ip, char const *port, int af_family = AF_UNSPEC, int type = SOCK_STREAM )
{
   SOCKET host_sock = INVALID_SOCKET;
   addrinfo hints;
   addrinfo *addr;

   if (ip == nullptr) {
      ip = "localhost";
   }

   memset( &hints, 0, sizeof(hints) );
   hints.ai_family   = af_family;       // if it matters, us AF_INET for IPv4, and AF_INET6 for IPv6
   hints.ai_socktype = type;
   hints.ai_flags    = AI_PASSIVE;      // Is used for binding (ie, listening)

   // helper method - removes a lot of manual setup or sockaddr construction
   // but will also allocate on the heap on success, so will need to be freed.
   int status = getaddrinfo( ip, port, &hints, &addr );
   if (status != 0) {
      printf( "Failed to create socket address: %s\n", gai_strerror(status) );
   } else {
      // Alright, walk the list, and bind when able
      addrinfo *iter;
      
      for (iter = addr; iter != nullptr; iter = iter->ai_next) {
         char addr_name[INET6_ADDRSTRLEN];
         inet_ntop( iter->ai_family, GetInAddr(iter->ai_addr), addr_name, INET6_ADDRSTRLEN );
         printf( "Attempt to bind on: %s : %s\n", addr_name, port );
         
         host_sock = socket( iter->ai_family, iter->ai_socktype, iter->ai_protocol );

         if (host_sock != INVALID_SOCKET) {
            if (bind( host_sock, iter->ai_addr, (int)(iter->ai_addrlen) ) == SOCKET_ERROR) {
               closesocket( host_sock );
               host_sock = INVALID_SOCKET;
            } else {
               // Connecting on address 
               printf( "Bound to : %s\n", addr_name );
               break;
            }
         }
      }

      // We're dont with the address, clean up my memory
      freeaddrinfo(addr);
   }

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
   addrinfo hints;
   addrinfo *addr;
       
   memset( &hints, 0, sizeof(hints) );
   hints.ai_family   = AF_UNSPEC;       // if it matters, us AF_INET for IPv4, and AF_INET6 for IPv6
   hints.ai_socktype = SOCK_STREAM;  

   // helper method - removes a lot of manual setup or sockaddr construction
   // but will also allocate on the heap on success, so will need to be freed.
   int status = getaddrinfo( addrname, port, &hints, &addr );
   if (status != 0) {
      printf( "Failed to create socket address: %s\n", gai_strerror(status) );
   } else {
      // Alright, walk the list, and bind when able
      addrinfo *iter;
      
      for (iter = addr; iter != nullptr; iter = iter->ai_next) {
         char addr_name[INET6_ADDRSTRLEN];
         inet_ntop( iter->ai_family, GetInAddr(iter->ai_addr), addr_name, INET6_ADDRSTRLEN );
         printf( "Attempt to bind on: %s\n", addr_name );
         
         host_sock = socket( iter->ai_family, iter->ai_socktype, iter->ai_protocol );

         if (host_sock != INVALID_SOCKET) {
            if (connect( host_sock, iter->ai_addr, (int)iter->ai_addrlen ) == SOCKET_ERROR) {
               closesocket( host_sock );
               host_sock = INVALID_SOCKET;
            } else {
               // Connecting on address 
               printf( "Connected to : %s\n", addr_name );
               break;
            }
         }
      }

      // We're dont with the address, clean up my memory
      freeaddrinfo(addr);
   }

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
   NetworkSystem net;
   if (!net.init()) {
      printf( "Failed to initialize net system.\n" );
      _getch();
      return false;
   }

   // List Addresses
   ListAddressesForHost( net.get_local_host_name(), gPort );

   if ((argc <= 1) || (_strcmpi( argv[1], "host" ) == 0)) {
      printf( "Hosting...\n" );
      NetworkHost( net.get_local_host_name(), gPort ); 
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

