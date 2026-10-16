#include <iostream>
#include <sys/socket.h>
#include <bits/stdc++.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <sys/stat.h>

//handles individual client communication
void handle_client(int client_fd, const std::string &spoolDir){
    std::cout << "Communicating with client on socket descriptor: " << client_fd << "\n";
}

int main(int argc, char *argv[]) {
    if(argc < 3){
        std::cerr << "Usage: ./twmailer-server <port> <mail-spool-directoryname>\n";
        exit(EXIT_FAILURE);
    }

    int port = std::stoi(argv[1]);
    std::string spoolDir = argv[2];

    //create server socket, ipv4, tcp
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd < 0){
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    //allow immediate reuse of the port
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    //binding socket to port
    if(bind(server_fd, (struct sockaddr *) &address, sizeof(address))){
        perror("bind error");
        exit(EXIT_FAILURE);
    }

    //start listening for connections (backlog 5)
    if (listen(server_fd, 5) < 0){
        perror("listen error");
        exit(EXIT_FAILURE);
    }

    std::cout << "Server listening on port " << port << " | Spool directory: " << spoolDir << "\n";
    
    //iterative main loop
    while (true){
        struct sockaddr_in cli_addr;
        socklen_t client_len = sizeof(cli_addr);

        //block & wait for client to connect
        int client_fd = accept(server_fd, (struct  sockaddr *)&cli_addr, &client_len);
        if (client_fd < 0)
        {
            perror("accept error");
            continue;
        }
        std::cout << "Connection established with client. \n";
        
        //handle client session
        handle_client(client_fd, spoolDir);

        //close client socket after disconnect
        close(client_fd);
        std::cout << "Client connection closed. \n";
    }
    
    //close listening socket
    close(server_fd);
    
    

    return 0;
}