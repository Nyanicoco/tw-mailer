#include <iostream>
#include <sys/socket.h>
#include <bits/stdc++.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <sys/stat.h>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

//to read a single line ending with "\n" from socket
bool read_line(int fd, std::string &line){
    line.clear();
    char c;
    while (true)
    {
        ssize_t n = read(fd, &c, 1);
        if(n < 0){
            if(errno ==EINTR) continue;
            return false;
        } else if(n == 0){
            return false; //connection closed by client
        }
        if(c == '\n'){
            break;
        }
        line+= c;
    }
    return true;   
}

//handles individual client communication
void handle_client(int client_fd, const std::string &spoolDir){
    std::string command;

    while(read_line(client_fd, command)){
        if (command == "QUIT"){break;}      
        else if(command == "SEND"){
            std::string sender, reciever, subject, line, message = "";
            read_line(client_fd, sender);
            read_line(client_fd, reciever);
            read_line(client_fd, subject);

            //read messages line until "."
            while (read_line(client_fd, line)){
                if(line == "."){break;}
                message += line + "\n";   
            }

            std::string userDir = spoolDir + "/" + reciever;
            fs::create_directories(userDir);

            int msgID = 1;
            for(const auto &entry : fs::directory_iterator(userDir)){
                if(entry.is_regular_file()){
                    msgID++;
                }
            }

            //save sender, reciever, subject, and message content into userDir/<msgID>.txt
            std::ofstream outFile(userDir + "/" + std::to_string(msgID) + ".txt");
            outFile << sender << "\n";
            outFile << reciever << "\n";
            outFile << subject << "\n";
            outFile << message;
            outFile.close();

            std::string response = "OK\n";
            write(client_fd, response.c_str(), response.size());
        }
        else if(command == "LIST"){
            std::string username;
            read_line(client_fd, username);

            //count no. of messages of the user, send the count and each message's index and subject
            std::string userDir = spoolDir + "/" + username;
            int count = 0;
            std::string listDetails = "";

            if(fs::exists(userDir) && fs::is_directory(userDir)){
                for(int i = 1; ; ++i){
                    std::string filePath = userDir + "/" + std::to_string(i) + ".txt";
                    if (!fs::exists(filePath)) break;

                    count++;
                    std::ifstream inFile(filePath);
                    std::string s, r, subj;
                    std::getline(inFile, s);
                    std::getline(inFile, r);
                    std::getline(inFile, subj);
            
                    listDetails += subj + "\n";
                }
            }

            std::string response = std::to_string(count) + "\n" + listDetails;
            write(client_fd, response.c_str(), response.size());

        } else if (command == "READ"){
            std::string username, msgNumber;

            read_line(client_fd, username);
            read_line(client_fd, msgNumber);

            std::string filePath = spoolDir + "/" + username + "/" + msgNumber + ".txt";
            if(fs::exists(filePath)){
                std::ifstream inFile(filePath);
                std::string fileContent = "";
                std::string line;
                while(std::getline(inFile, line)){
                    fileContent += line + "\n";
                }
                std::string response = "OK\n" + fileContent + ".\n";
                write(client_fd, response.c_str(), response.size());
            } else {
                std::string response = "ERR\n";
                write(client_fd, response.c_str(), response.size());
            }

        }else if (command == "DEL"){
            std::string username, msgNumber;
            read_line(client_fd, username);
            read_line(client_fd, msgNumber);

            std::string userDir = spoolDir + "/" + username;
            std::string filePath = userDir + "/" + msgNumber + ".txt";
            if (fs::exists(filePath)) {
                fs::remove(filePath);

                int deletedIdx = std::stoi(msgNumber);
                for (int i = deletedIdx + 1; ; ++i) {
                        std::string oldPath = userDir + "/" + std::to_string(i) + ".txt";
                        std::string newPath = userDir + "/" + std::to_string(i - 1) + ".txt";
                        if (fs::exists(oldPath)) {
                             fs::rename(oldPath, newPath);
                        } else {
                            break;
                        }
                    }
                std::string response = "OK\n";
                 write(client_fd, response.c_str(), response.size());
            } else {
                std::string response = "ERR\n";
                write(client_fd, response.c_str(), response.size());
            }
        }
    } 
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
    
    // creating mail spool dir
    mkdir(spoolDir.c_str(), 0777);

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