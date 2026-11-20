#include "auth.h"
#include "net_io.h"
#include "spool.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
// parses a 1-based message number - returns -1 on invalid input
int parse_msg_number(const std::string& s) {
    if (s.empty()) {
        return -1;
    }
    try {
        size_t pos = 0;
        int num = std::stoi(s, &pos);
        if (pos != s.size() || num <= 0) {
            return -1;
        }
        return num;
    } catch(...){
        return -1;
    }
}

void handle_client(int client_fd, std::string client_ip,
                   std::string spool_dir, std::string blacklist_file){
    std::string command;
    std::string read_buffer;
    std::string current_user;
    bool logged_in = false;

    while(read_line(client_fd, read_buffer, command)){
        if (command == "QUIT") {
            break;
        }

        if(command == "LOGIN"){
            std::string user, password;
            if (!read_line(client_fd, read_buffer, user) ||
                !read_line(client_fd, read_buffer, password)) {
                break;
            }

            if(is_ip_blacklisted(client_ip, blacklist_file)){
                write_all(client_fd, "ERR\n");
                continue;
            }

            if(check_ldap_login(user, password)){
                reset_failed_logins(client_ip, user);
                logged_in = true;
                current_user = user;
                write_all(client_fd, "OK\n");
            } else {
                record_failed_login(client_ip, user, blacklist_file);
                logged_in = false;
                current_user.clear();
                write_all(client_fd, "ERR\n");
            }
            continue;
        }

        // outside LOGIN and QUIT needs a login session
        if(!logged_in){
            write_all(client_fd, "ERR\n");
            continue;
        }

        if(command == "SEND"){
            // sender comes from the auth session, not from client input
            std::string receiver, subject, line, message;
            if(!read_line(client_fd, read_buffer, receiver) ||
                !read_line(client_fd, read_buffer, subject)){
                break;
            }

            while(read_line(client_fd, read_buffer, line)){
                if(line == "."){
                    break;
                }
                message += line + "\n";
            }

            if(spool_send_message(spool_dir, current_user, receiver, subject, message)){
                write_all(client_fd, "OK\n");
            } else {
                write_all(client_fd, "ERR\n");
            }
        } else if(command == "LIST"){
            write_all(client_fd, spool_list_messages(spool_dir, current_user));
        } else if(command == "READ"){
            std::string msg_num_str;
            if(!read_line(client_fd, read_buffer, msg_num_str)){
                break;
            }

            int msg_num = parse_msg_number(msg_num_str);
            std::string response;
            if(spool_read_message(spool_dir, current_user, msg_num, response)){
                write_all(client_fd, response);
            } else {
                write_all(client_fd, "ERR\n");
            }
        } else if(command == "DEL"){
            std::string msg_num_str;
            if(!read_line(client_fd, read_buffer, msg_num_str)){
                break;
            }

            int msg_num = parse_msg_number(msg_num_str);
            if (spool_delete_message(spool_dir, current_user, msg_num)) {
                write_all(client_fd, "OK\n");
            } else {
                write_all(client_fd, "ERR\n");
            }
        } else {
            write_all(client_fd, "ERR\n");
        }
    }

    close(client_fd);
    std::cout << "Client disconnected (" << client_ip << ", fd " << client_fd << ")\n";
}
} // namespace

int main(int argc, char* argv[]) {
    if(argc < 3){
        std::cerr << "Usage: ./twmailer-server <port> <mail-spool-directoryname>\n";
        return EXIT_FAILURE;
    }

    int port = std::stoi(argv[1]);
    std::string spool_dir = argv[2];
    std::string blacklist_file = spool_dir + "/blacklist.txt";

    //create server socket 
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket failed");
        return EXIT_FAILURE;
    }

    // allow immediate reuse of the port after restart
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    if(bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        perror("bind error");
        close(server_fd);
        return EXIT_FAILURE;
    }

    if(listen(server_fd, 10) < 0){
        perror("listen error");
        close(server_fd);
        return EXIT_FAILURE;
    }

    fs::create_directories(spool_dir);
    load_blacklist(blacklist_file);

    std::cout << "Server listening on port " << port
              << " | Spool directory: " << spool_dir << "\n";

    while (true) {
        sockaddr_in cli_addr{};
        socklen_t client_len = sizeof(cli_addr);

        int client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&cli_addr), &client_len);
        if(client_fd < 0){
            if (errno == EINTR) {
                continue;
            }
            perror("accept error");
            continue;
        }

        char ip_str[INET_ADDRSTRLEN] = "unknown";
        inet_ntop(AF_INET, &cli_addr.sin_addr, ip_str, sizeof(ip_str));
        std::string client_ip(ip_str);

        std::cout << "Connection established from " << client_ip << " (fd " << client_fd << ")\n";

        //spawn a detached thread so multiple clients can be handled concurrently
        std::thread client_thread(handle_client, client_fd, client_ip, spool_dir, blacklist_file);
        client_thread.detach();
    }
}