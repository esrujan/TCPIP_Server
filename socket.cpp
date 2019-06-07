/* 
 * A TCP server implemenation where the port number is passed as an argument 
 * The server processes the TLV blobs sent by the client
 * It processes the information and writes it into the respective socket
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <boost/algorithm/hex.hpp>

using namespace std;

/**
*	error()
*	Prints the relevant error message using perror()
*	and then exits out of the function.
*/
void error( const char *msg )
{
	perror( msg );
	exit( 1 );
}

/**
*	ReadXBytes()
*	The function tries to read from socket given by "sockfd".
*	It stores the bytes it reads in "buffer".
*/
void ReadXBytes( int sockfd, unsigned int x, void *buffer )
{
	long bytesRead = 0;
	int result;
	while ( bytesRead < x ) {
		result = read( sockfd, (void *)( buffer + bytesRead ), x - bytesRead );
		if ( result < 1 )
			error( "ERROR reading from the socket\n" );
		bytesRead += result;
	}
	//cout << "Successfully read " << bytesRead << " bytes" << endl;
}

/**
*	ServeClient()
*	The function of the program which is relevant to serving a single client.
*	This is where the input TLV blobs are read from the socket given by "newsockfd"
*	and the server then writes the processed information back into the client socket.
*
*	Returns "true" if the client connection is closed. Otherwise, returns "false".
*/
bool ServeClient( int newsockfd, struct sockaddr_in& cli_addr )
{
	int i, n, slen;
        unsigned short type = 0;
        unsigned int length = 0;
	char * buffer = 0;
        bool last = false;
	bool closed = false;
	time_t futur = time( NULL ) + 10; // Setting a time limit for a single client
	
        while ( true )
        {
                // First read two bytes
                ReadXBytes( newsockfd, 2, (void*)(&type) );
                type = htons( type );
                //cout << std::hex << type << endl;

                // Then read 4 bytes and store in integer "Len"
                ReadXBytes( newsockfd, sizeof(length), (void*)(&length) );
                length = htonl( length );
                //cout << length << endl;

                // Allocating the memory for "buffer"
                buffer = new char[ length ];
                bzero( buffer, length );
                // Then read as many bytes as "length" indicated above
                ReadXBytes( newsockfd, length, buffer );

                write( newsockfd, "[", 1 );
                char *cli_ipaddr = inet_ntoa( cli_addr.sin_addr );
                int slen = strlen( cli_ipaddr );
                write( newsockfd, cli_ipaddr, slen );
                write( newsockfd, ":", 1 );

                string str_port = to_string( ntohs( cli_addr.sin_port ) );
                slen = str_port.size();
                write( newsockfd, ( char * ) str_port.c_str(), slen );
                write( newsockfd, "] ", 2 );

                // Using a switch statement to detect the correct type of message
                switch( type )
                {
                        case 0xE110:
                                n = write( newsockfd, "[Hello] ", 8 );
                        break;

                        case 0xDA7A:
                                n = write( newsockfd, "[Data] ", 7 );
                        break;

                        case 0x0B1E:
                                n = write( newsockfd, "[Goodbye] ", 10 );
				// Since the client has sent a "Good bye", 
				// I assume it indicates the last TLV blob 
				// from the client
                                last = true;
                        break;

                        default:
                                error( "The given TYPE cannot be parsed\n" );
                }
                if ( n < 0 )
                        error( "ERROR writing to socket" );

                n = write( newsockfd, "[", 1 );
		if ( n < 0 )
			error( "ERROR writing to the socket" );
                string str_len = to_string( length );
                slen = str_len.size();
                n = write( newsockfd, str_len.c_str(), slen );
		if ( n < 0 )
			error( "ERROR writing to the socket" );
                n = write( newsockfd, "] [", 3 );
                if ( n < 0 )
			error( "ERROR writing to the socket" );
		
                i = 0;
                vector<unsigned char> v;
                string res;
                if ( length > 0 ) {
                        while ( length > 0 && i < 4 ) {
                                v.push_back( buffer[ i++ ] );
                                length--;
                        }
                        boost::algorithm::hex( v.begin(), v.end(), back_inserter( res ) );
                }
		 
                char C_res[ res.size()+1 ];
                strcpy( C_res, res.c_str() );
		
                for ( int j = 0; j < i; j++ ) {
                        n = write( newsockfd, "0x", 2 );
			if ( n < 0 )
				error( "ERROR writing to the socket" );
                        n = write( newsockfd, C_res + 2*j, 1 );
			if ( n < 0 )
				error( "ERROR writing to the socket" );
                        n = write( newsockfd, C_res + 2*j+1, 1 );
			if ( n < 0 )
				error( "ERROR writing to the socket" );
                        if ( j != i-1 )
			{
				n = write ( newsockfd, " ", 1 );
				if ( n < 0 )
					error( "ERROR writing to the socket" );
			}
                }
		
                n = write( newsockfd, "]\n", 2 );
		if ( n < 0 )
			error( "ERROR writing to the socket" );
		
                // Releasing the memory allocated for the "buffer"
                delete buffer;
		
		if ( time( NULL ) > futur )
        	{
			// If it is long enough since the server is serving with the client,
			// the server will go to serve other clients in the mean time
			break;
        	}
		
		if ( last )
		{
			close( newsockfd );
			closed = true;
			break;
		}
        }
	return closed;
}

/**
*	The main function takes care of multiple client connections
*	Instead of multiple threads, the Linux command select() is used
*	to serve all the client requests
*/
int main( int argc, char *argv[] )
{
	int sockfd, newsockfd, portno, activity, sd, max_sd, listening;
	int master_socket, new_socket;
	socklen_t clilen;
	struct sockaddr_in serv_addr, cli_addr;
	stringstream ss;
	
	int *client_socket = 0;
	int max_clients = 1000;
	
	
	if ( argc < 2 )
	{
		fprintf( stderr, "ERROR, no port provided\n" );
		exit( 1 );
	}
	
        cout << "Enter the maximum number of clients that can be served:";
        cin >> max_clients;
	if ( max_clients < 1 )
		error( "The maximum number of clients must be 1 or greater\n" );
	client_socket = new int[ max_clients ];
	
	// Setting socket descriptors
	fd_set readfds;
	
	// Initialize all client sockets to zeroes
	for ( int i = 0; i < max_clients; i++ )
		client_socket[ i ] = 0;
	
	master_socket = socket( AF_INET, SOCK_STREAM, 0 );
	
	if ( master_socket < 0 )
		error( "ERROR opening socket" );
	
	bzero( ( char * ) &serv_addr, sizeof( serv_addr ) );
	// Filling in the values into serv_addr structure 
	portno = atoi( argv[ 1 ] );
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons( portno );
	
	// Binding the master_socket to the serv_addr
	int binding = bind( master_socket, ( struct sockaddr * ) &serv_addr, sizeof( serv_addr ) );
	if ( binding < 0 )
		error( "ERROR on binding" );
	
	// Listening on the master socket. Putting a reasonably large "backlog" value
	// to handle many simultaneous clients willing to connect with the server
	listening = listen( master_socket, 128 );
	if ( listening < 0 )
		error( "Error in listening" );
	
	clilen = sizeof( cli_addr );
	
	while ( true ) 
	{
		// Clear the socket set
		FD_ZERO( &readfds );
		
		// Add the master socket to the set
		FD_SET( master_socket, &readfds );
		max_sd = master_socket;
		
		// Add the client sockets to the FD_SET
		for ( int i = 0; i < max_clients; i++ )
		{
			sd = client_socket[i];
			if ( sd > 0 )
				FD_SET( sd, &readfds );
			
			if ( sd > max_sd )
				max_sd = sd;
		}
		
		// Wait as long as we see activity on one of the sockets
		activity = select( max_sd + 1, &readfds, NULL, NULL, NULL );
		
		// If there is an error, report it but do not exit
		if ( activity < 0 && errno != EINTR )
			perror( "Error in Select function" );
		
		// Whenever there is activity on master socket,
		// it implies there is a new connection attempt by a client
		if ( FD_ISSET( master_socket, &readfds ) ) 
		{
			// Try to accept the new connection
			newsockfd = accept( master_socket, ( struct sockaddr * ) &cli_addr, &clilen );
        		if ( newsockfd < 0 )
                		error( "ERROR on accepting a connection" );
			
			// If we are able to connect with the client, let us place that in the next available slot in client_socket
			for ( int i = 0; i < max_clients; i++ )
			{
				if ( client_socket[i] == 0 )
				{
					client_socket[i] = newsockfd;
					break;
				}
			}
		}
		
		// Now, let us check for all the client sockets, if any of it is set
		// If a client socket is set, that client will be served
		for ( int i = 0; i < max_clients; i++ )
		{
			sd = client_socket[i];
			
			if ( FD_ISSET( sd, &readfds ) )
			{
				// Process the client request(s)
				if ( ServeClient( sd, cli_addr ) )
					client_socket[i] = 0;
			}
		}
	}
	
	// Close the master socket
	close( master_socket );
	
	return 0;
}
