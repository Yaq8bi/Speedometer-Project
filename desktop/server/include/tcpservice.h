#ifndef TCPCOM_H
#define TCPCOM_H

#include "comservice.h"
#include <thread>
#include <unistd.h>
#include <sys/socket.h>

    
class TCPService : public COMService
{
    int sockfd;
    std::atomic<bool> server_window_closed{false};
    std::thread trd{&TCPService::run, this};

    /**
     * @brief Override of base class run function
     * 
     */
    void run(void) override;

public:
    /**
     * @brief Constructor for TCPService object
     * 
     */
    TCPService() = default;

    /**
     * @brief Destructor for TCPService object
     * 
     */
    ~TCPService()
    {
        server_window_closed = true;
        shutdown(sockfd, SHUT_RDWR);
        close(sockfd);
        trd.join();
    }
};

#endif
