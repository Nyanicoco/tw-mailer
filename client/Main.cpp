
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "MailerClient.h"

#include <cstdio>   // für perror
#include <cstdint>  // für uint16_t

int main(int argc, char* argv[]) {

    // Check if the IP address and por were provided
    if (argc != 3) {
        std::cerr << "Usage: ./twmailer-client <ip> <port>\n";
        return 1;
    }

    //Read the server IP adress
    std::string ip = argv[1];
    int port;


    //Convert and validate the port number
    try {
        size_t pos;
        port = std::stoi(argv[2], &pos);

        if (pos != std::string(argv[2]).size()
            || port < 1 || port > 65535) {
            std::cerr << "Ungueltiger Port\n";
            return 1;
        }
    } catch (...) {
        std::cerr << "Port muss eine Zahl sein\n";
        return 1;
    }

    // create a TCP socket using IPv$
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);

    
    if (socket_fd == -1) {
        perror("socket failed");
        return 1;
    }

    
     // Configure the server address
    sockaddr_in server_addr{};

    // Use IPv4
    server_addr.sin_family = AF_INET;
    //Convert port to network byte order
    server_addr.sin_port = htons(port);

    //convert the IP adress from text to binary
    if (inet_pton(AF_INET, ip.c_str(),
                  &server_addr.sin_addr) != 1) {
        std::cerr << "Ungueltige IP-Adresse\n";
        close(socket_fd);
        return 1;
    }

    // Connect to the server

    if (connect(socket_fd,
                (struct sockaddr*)&server_addr,
                sizeof(server_addr)) == -1) {

        perror("connect failed");
        close(socket_fd);
        return 1;
    }

    std::cout << "Verbindung zum Server erfolgreich!\n";


    MailerClient client(socket_fd);
    client.run();

    // Close the connection

    close(socket_fd);

    return 0;
}
