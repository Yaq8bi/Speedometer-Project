#include "tcpservice.h"
// temporary in testing phase
#include <netinet/in.h>
#include <iostream>
#include <thread>
#include <unistd.h>
#include <sys/socket.h>
#include <string.h>

void TCPService::run(void)
{
    int connfd{-1}; // Error for accept.

    uint8_t _buffer[BUFLEN];

    while (false == server_window_closed)
    {
        // Create socket
        sockfd = -1;

        // while sockfd fails to create.
        while (sockfd == -1)
        {
            sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);

            // If socket failed, print a message
            if (sockfd == -1)
            {
                ;
            }
            else
            {
                int optval = 1;
                setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
            }
            // whle loop continues until sockfd is valid
        }

        // create instance of sockaddr_in
        sockaddr_in servaddr{};

        // Assign IP and PORT
        servaddr.sin_family = AF_INET;
        servaddr.sin_port = htons(Setting::TCPIP::PORT);
        servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

        // Binding newly created socket to given IP and verification
        int bind_check{-1};
        while (0 != bind_check)
        {
            if (server_window_closed)
            {
                shutdown(sockfd, SHUT_RDWR);
                close(sockfd);
                status = false;
                return;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(Setting::INTERVAL));
            bind_check = bind(sockfd, (sockaddr *)&servaddr, sizeof(servaddr));
        }

        // Listen to only once connection
        if (0 == listen(sockfd, 1))
        {
            sockaddr_in cli{};
            socklen_t len = sizeof(cli);

            // acceptcomservice{comms} and check the connection.
            connfd = accept(sockfd, (sockaddr *)&cli, &len);

            // Connection got accepted, below runs:
            if (connfd >= 0)
            {
                status = true; // Connected to the client.

                bzero(_buffer, sizeof(_buffer)); // Setting the buffer to 0

                // While we are connected to the cllient:
                while (false == server_window_closed)
                {
                    // Sleep for interval from settings.
                    std::this_thread::sleep_for(std::chrono::milliseconds(Setting::INTERVAL));

                    {
                        std::scoped_lock lock{mtx};
                        memcpy(_buffer, COMService::buffer, sizeof(_buffer));
                    }

                    // SEND OUT DATA.
                    ssize_t bytes_written{-1};
                    bytes_written = write(connfd, _buffer, BUFLEN);

                    if (BUFLEN != bytes_written)
                    {
                        std::cout << "Server lost connection to the client" << std::endl;
                        shutdown(sockfd, SHUT_RDWR);
                        shutdown(connfd, SHUT_RDWR);
                        
                        close(connfd);
                        close(sockfd);
                        break;
                    }
                    else
                    {
                        ;
                    }
                }
            }
            else
            {
                ;
            }
        }
        else
        {
            ;
        }
    }
    // Close the socket when the server window is closed
    if (sockfd >= 0) {
        shutdown(sockfd, SHUT_RDWR);
        close(sockfd);
    }
    if (connfd >= 0) {
        shutdown(connfd, SHUT_RDWR);
        close(connfd);
    }
    status = false; // Update the status
}