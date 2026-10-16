#include <iostream>
#include <sys/socket.h>
#include <bits/stdc++.h>
#include <netinet/in.h>

// struct sockaddr_in {
//     sa_family_t sin_family;
//     in_port_t sin_port;
//     struct in_addr sin_addr;
// };
// struct in_addr {
//     u_int32_t s_addr;
// };
int main(int argc, char *argv[]) {
    std::cout << "Server, expects the port it should listen on\n";
    std::cout << "Total Argument passed: " << (argc - 1) << std::endl;
    std::cout << "Port is: " << (argv[1]) << std::endl;
    int port = std::stoi(argv[1]);
    int socket_fd;
    struct  sockaddr_in my_addr;
    
    socket_fd = socket(AF_INET, SOCK_STREAM, 0); 

    memset(&my_addr, 0, sizeof(my_addr));
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(port);
    my_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if(bind(socket_fd, (struct sockaddr *) &my_addr, sizeof(my_addr))){
        perror("bind error");
        exit(1);
    }

    return 0;
}