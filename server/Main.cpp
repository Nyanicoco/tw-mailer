#include <iostream>
#include <sys/socket.h>
#include <bits/stdc++.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <sys/stat.h>
#include <fstream>
#include <filesystem>
#include <arpa/inet.h>
#include <cerrno>
#include <ldap.h>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

//constants for LDAP and login limiting
const char* LDAP_HOST = "ldap.technikum-wien.at";
const int LDAP_PORT = 389;
const char* LDAP_BASE_DN = "dc=technikum-wien, dc=at";
const int MAX_FAILED_ATTEMPTS = 3;
const int BLACKLIST_DURATION_SEC = 60;

//shared state protected - aginst client threads 
std::mutex g_auth_mutex;
std::mutex g_spool_mutex;

// maps client ip, username to number of failed login attempts
std::map<std::pair<std::string, std::string>, int> g_failed_attempts;

//maps client ip to unix timestamp when the 60 sec ban expired
std::map<std::string, std::time_t> g_blacklisted_ips;


//Socket read / write ---------------------------------------------

//to reads single line ending with "\n" from socket
//uses per-connection buffer
bool read_line(int fd, std::string& buffer, std::string& out_line){
    out_line.clear();
    
    while (true) {
        size_t newline_pos = buffer.find('\n');
        if (newline_pos != std::string::npos) {
            out_line = buffer.substr(0, newline_pos);
            buffer.erase(0, newline_pos + 1);

            // Remove '\r' if client sends CRLF ("\r\n")
            if (!out_line.empty() && out_line.back() == '\r'){
                out_line.pop_back();
            }
            return true;
        }

        char temp[1024];
        ssize_t n = read(fd, temp, sizeof(temp));
        if (n < 0) {
            if (errno == EINTR) {
                continue; // interrupted by signal, try again
            }
            return false;
        }
        if (n == 0) {
            // Client closed the connection
            if (!buffer.empty()){
                out_line = buffer;
                buffer.clear();
                return true;
            }
            return false;
        }
        buffer.append(temp, n);
    }
}

bool write_all(int fd, const std::string& msg){
    size_t sent_total = 0;
    while(sent_total < msg.size()){
        ssize_t sent = write(fd, msg.data() + sent_total, msg.size() - sent_total);
        if (sent < 0){
            if (errno == EINTR){
                continue;
            }
            return false;
        }
        if(sent == 0){
            return false;
        }
        sent_total += static_cast<size_t>(sent);
    }
    return true;
}

//blacklist & LDAP auth ---------------------------------------------------------



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