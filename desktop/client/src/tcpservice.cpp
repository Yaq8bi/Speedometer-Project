#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h> 
#include <unistd.h>     
#include <cstring>
#include "tcpservice.h"
#include "comservice.h"

constexpr int buffer_length = 3; // Setting::BUFLEN

void TCPClient::run(void)
{
    // Connection loop
    // Everything related to TCP has to go in here so that it can reconnect if the connection is lost.
    while (client_window_closed == false)
    {
        sockfd = -1;

        // Tries to create a socket until it succeeds.
        while (sockfd == -1)
        {
            sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        }

        // Create instance of sockaddr_in for the server
        sockaddr_in servaddr{};

        // Assign Server IP and PORT for connection
        servaddr.sin_family = AF_INET;
        servaddr.sin_port = htons(Setting::TCPIP::PORT);
        servaddr.sin_addr.s_addr = inet_addr(Setting::TCPIP::IP);

        // Connect the client socket to server socket
        int connect_check{-1};

        while (connect_check < 0)
        {
            if (client_window_closed)
            {
                close(sockfd);
                status = false;
                return; // Exit the run function if the client window is closed
            }
            
            connect_check = connect(sockfd, (sockaddr *)&servaddr, sizeof(servaddr));

            if (connect_check < 0)
            {
                // Sleep to avoid spamming connection requests. Uses the INTERVAL from settings.
                std::this_thread::sleep_for(std::chrono::milliseconds(Setting::INTERVAL));
            }
        }

        // Set status to true, indicating the connection is established.
        status = true;


        uint8_t _buffer[BUFLEN];         // Create a buffer to store received data
        bzero(_buffer, sizeof(_buffer)); // Fill/initialize the buffer with zero.

        // While the connection is active, we read data from the server.:
        while (status)
        {

            // READ INCOMING DATA.
            ssize_t bytes_read{-1};
            bytes_read = read(sockfd, _buffer, sizeof(_buffer));


            // Copy the data to the COMService's buffer if bytes_read is greater than 0.
            if (bytes_read > 0)
            {
                {
                    std::scoped_lock lock{mtx};                                      // Lock the mutex to protect shared data
                    memcpy(COMService::buffer, _buffer, sizeof(COMService::buffer)); // Copy the received data to COMService's buffer
                }
            }
            else if (bytes_read == 0)
            {
                // If you reach here, it means the server and clienth have closed/lost connection.

                // Reset the status and buffer, then close the socket.
                // This is to ensure that the client can reconnect later.

                status = false;
                bzero(COMService::buffer, sizeof(COMService::buffer));
                connect_check = -1;
                close(sockfd);

                break;
            }
            else
            {
                //If we are here, it means something has gone very wrong.
                ; 
            }
        }
    }

    // If we reach here, the client window has been closed.
    close(sockfd);  // Close the socket connection
    status = false; // Update the status
}