#ifndef TCPSERVICE_H
#define TCPSERVICE_H

#include "comservice.h"
#include <thread>
#include <atomic>
#include <unistd.h>
#include <sys/socket.h>

class TCPClient : public COMService
{
    
private:

    int sockfd;

    // The bool to signal the thread to stop.
    std::atomic<bool> client_window_closed{false};
    std::thread trd{&TCPClient::run, this};

    // The main function for the client logic.
    void run(void) override;
    
    public:

    // The constructor, starts the client thread
    TCPClient() = default;

    // The destructor to handle the deletion.
    ~TCPClient()
    {
        client_window_closed = true;

        // Shut down the socket.
        shutdown(sockfd, SHUT_RDWR);

        // If socket file descriptor value is 0 or above, it means that the socket exits.
        if (sockfd >= 0)
        {
            close(sockfd);
        }
    }
};

#endif // TCPSERVICE_H