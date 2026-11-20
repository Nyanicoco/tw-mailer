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

//saves blacklisted ips to disk to keep the blacklist
void save_blacklist_locked(const std::string& blacklist_file){
    std::ofstream out(blacklist_file, std::ios::trunc);
    if(!out.is_open()){
        std::cerr << "Could not write blacklist file: " << blacklist_file << "\n";
        return;
    }
    for(const auto& pair : g_blacklisted_ips){
        out << pair.first << " " << pair.second << "\n";
    }
}

void load_blacklist(const std::string& blacklist_file){
    std::lock_guard<std::mutex> lock(g_auth_mutex);
    g_blacklisted_ips.clear();

    std::ifstream in(blacklist_file);
    if (!in.is_open()) {
        return; //file might not exist on first run
    }

    std::string ip;
    std::time_t expires_at = 0;
    std::time_t now = std::time(nullptr);

    while (in >> ip >> expires_at){
        if (expires_at > now) {
            g_blacklisted_ips[ip] = expires_at;
        }
    }
}

//checks if ip is blacklisted, removes expired bans
bool is_ip_blacklisted(const std::string& ip, const std::string& blacklist_file){
    std::lock_guard<std::mutex> lock(g_auth_mutex);
    std::time_t now = std::time(nullptr);
    bool changed = false;

    for(auto it = g_blacklisted_ips.begin(); it != g_blacklisted_ips.end();){
        if (it->second <= now){
            it = g_blacklisted_ips.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }

    if(changed){
        save_blacklist_locked(blacklist_file);
    }
    return g_blacklisted_ips.find(ip) != g_blacklisted_ips.end();
}

//records a failed login for username, ip, after 3 failed attempts, blacklist ip for 60 secs
void record_failed_login(const std::string& ip, const std::string& username, const std::string& blacklist_file){
    std::lock_guard<std::mutex> lock(g_auth_mutex);

    auto key = std::make_pair(ip, username);
    g_failed_attempts[key] += 1;

    if(g_failed_attempts[key] >= MAX_FAILED_ATTEMPTS){
        g_blacklisted_ips[ip] = std::time(nullptr) + BLACKLIST_DURATION_SEC;
        g_failed_attempts[key] = 0;
        save_blacklist_locked(blacklist_file);
    }
}

// clears failure counter after login
void reset_failed_logins(const std::string& ip, const std::string& username){
    std::lock_guard<std::mutex> lock(g_auth_mutex);
    g_failed_attempts.erase(std::make_pair(ip, username));
}

//checks username and password against ldap server using openldap c-api
bool check_ldap_login(const std::string& username, const std::string& password){
    if (username.empty() || password.empty()){
        return false;
    }

    std::string uri = "ldap://" + std::string(LDAP_HOST) + ":" + std::to_string(LDAP_PORT);
    LDAP* ld = nullptr;

    if (ldap_initialize(&ld, uri.c_str()) != LDAP_SUCCESS || ld == nullptr){
        return false;
    }

    int protocol_version = LDAP_VERSION3;
    ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &protocol_version);

    // Default dn, if search finds a better dn, use that instead
    std::string user_dn = "uid=" + username + "," + LDAP_BASE_DN;

    LDAPMessage* search_result = nullptr;
    std::string filter = "(uid=" + username + ")";
    char* attrs[] = {nullptr};

    if (ldap_search_ext_s(ld, LDAP_BASE_DN, LDAP_SCOPE_SUBTREE, filter.c_str(),
                          attrs, 0, nullptr, nullptr, nullptr, 0, &search_result) == LDAP_SUCCESS){
        LDAPMessage* entry = ldap_first_entry(ld, search_result);
        if (entry != nullptr) {
            char* dn_ptr = ldap_get_dn(ld, entry);
            if (dn_ptr != nullptr){
                user_dn = dn_ptr;
                ldap_memfree(dn_ptr);
            }
        }
        ldap_msgfree(search_result);
    }

    BerValue cred;
    cred.bv_val = const_cast<char*>(password.data());
    cred.bv_len = static_cast<ber_len_t>(password.size());

    int rc = ldap_sasl_bind_s(ld, user_dn.c_str(), LDAP_SASL_SIMPLE, &cred,
                              nullptr, nullptr, nullptr);

    ldap_unbind_ext_s(ld, nullptr, nullptr);
    return rc == LDAP_SUCCESS;
}

//mail spool -------------------------------------------------------------------------

//collects all number.txt message ids for user in asc order
std::vector<int> get_user_message_ids_locked(const std::string& user_dir){
    std::vector<int> ids;
    if(!fs::exists(user_dir) || !fs::is_directory(user_dir)){
        return ids;
    }

    for(const auto& entry : fs::directory_iterator(user_dir)){
        if(!entry.is_regular_file()){
            continue;
        }
        std::string filename = entry.path().filename().string();
        if(filename.size() <= 4 || filename.substr(filename.size() - 4) != ".txt"){
            continue;
        }

        std::string number_part = filename.substr(0, filename.size() - 4);
        bool all_digits = !number_part.empty();
        for (char c : number_part)
        {
            if(!std::isdigit(static_cast<unsigned char>(c))){
                all_digits = false;
                break;
            }
        }
        if (all_digits){
            ids.push_back(std::stoi(number_part));
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

//client session ---------------------------------------------------



//handles individual client communication
void handle_client(int client_fd, std::string client_ip, std::string spool_dir, std::string blacklist_file){
    std::string command;
    std::string read_buffer;
    std::string current_user;
    bool logged_in = false;

    while(read_line(client_fd, read_buffer, command)){
        if (command == "QUIT"){break;}   
        
        if(command == "LOGIN"){
            std::string user, password;
            if(!read_line(client_fd, read_buffer, user) || !read_line(client_fd, read_buffer, password)){
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
        
        //everything outside login and quit requires an active login session
        if(!logged_in){
            write_all(client_fd, "ERR\n");
            continue;
        }

        if(command == "SEND"){

            std::string receiver, subject, line, message;
            if(!read_line(client_fd, read_buffer, receiver) || !read_line(client_fd, read_buffer, subject)){
                break;
            }

            //read messages line until "."
            while (read_line(client_fd, read_buffer, line)){
                if(line == "."){break;}
                message += line + "\n";   
            }

            if(receiver.empty() || subject.size() > 80){
                write_all(client_fd, "ERR\n");
                continue;
            }

            std::lock_guard<std::mutex> lock(g_spool_mutex);
            std::string user_dir = spool_dir + "/" + receiver;
            fs::create_directories(user_dir);

            std::vector<int> ids = get_user_message_ids_locked(user_dir);
            int next_id = ids.empty() ? 1 : (ids.back() + 1);

            std::string file_path = user_dir + "/" + std::to_string(next_id) + ".txt";
            std::ofstream out_file(file_path);
            if (!out_file.is_open()) {
                write_all(client_fd, "ERR\n");
                continue;
            }

            out_file << current_user << "\n";
            out_file << receiver << "\n";
            out_file << subject << "\n";
            out_file << message;
            out_file.close();

            write_all(client_fd, "OK\n");
        }

        else if(command == "LIST"){

            //count no. of messages of the user, send the count and each message's index and subject
            std::lock_guard<std::mutex> lock(g_spool_mutex);
            std::string user_dir = spool_dir + "/" + current_user;
            std::vector<int> ids = get_user_message_ids_locked(user_dir);

            std::string list_lines;

                for(int id : ids){
                    std::string file_path = user_dir + "/" + std::to_string(id) + ".txt";
                    std::ifstream in_file(file_path);
                    std::string sender, receiver, subj;
                    std::getline(in_file, sender);
                    std::getline(in_file, receiver);
                    std::getline(in_file, subj);
            
                    list_lines += subj + "\n";
                }
            

            std::string response = std::to_string(ids.size()) + "\n" + list_lines;
            write_all(client_fd, response);

        } else if (command == "READ"){
            std::string msg_num_str;
            if (!read_line(client_fd, read_buffer, msg_num_str)){
                break;
            }

            int msg_num = 0;
            try{
                msg_num = std::stoi(msg_num_str);
            } catch(...){
                msg_num = -1;
            }

            std::lock_guard<std::mutex> lock(g_spool_mutex);
            std::string user_dir = spool_dir + "/" + current_user;
            std::vector<int> ids = get_user_message_ids_locked(user_dir);

            if(msg_num < 1 || static_cast<size_t>(msg_num) > ids.size()){
                write_all(client_fd, "ERR\n");
                continue;
            }

            int actual_id = ids[static_cast<size_t>(msg_num - 1)];
            std::string file_path = user_dir + "/" + std::to_string(actual_id) + ".txt";
            std::ifstream in_file(file_path);
            if (!in_file.is_open()){
                write_all(client_fd, "ERR\n");
                continue;
            }

            std::string file_content, line;
            while (std::getline(in_file, line)){
                file_content += line + "\n";
            }

            std::string response = "OK\n" + file_content + ".\n";
            write_all(client_fd, response);

        }else if (command == "DEL"){
            std::string msg_num_str;

            if(!read_line(client_fd, read_buffer, msg_num_str)){
                break;
            }

            int msg_num = 0;
            try{
                msg_num = std::stoi(msg_num_str);
            } catch(...){
                msg_num = -1;
            }

            std::lock_guard<std::mutex> lock(g_spool_mutex);
            std::string user_dir = spool_dir + "/" + current_user;
            std::vector<int> ids = get_user_message_ids_locked(user_dir);

            if (msg_num < 1 || static_cast<size_t>(msg_num) > ids.size()) {
                write_all(client_fd, "ERR\n");
                continue;
            }
            int actual_id = ids[static_cast<size_t>(msg_num - 1)];
            std::string file_path = user_dir + "/" + std::to_string(actual_id) + ".txt";
            if (fs::remove(file_path)) {
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


// main ------------------------------------------------------------------

int main(int argc, char *argv[]) {
    if(argc < 3){
        std::cerr << "Usage: ./twmailer-server <port> <mail-spool-directoryname>\n";
        exit(EXIT_FAILURE);
    }

    int port = std::stoi(argv[1]);
    std::string spool_dir = argv[2];

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

    std::cout << "Server listening on port " << port << " | Spool directory: " << spool_dir << "\n";
    
    // creating mail spool dir
    mkdir(spool_dir.c_str(), 0777);

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
        handle_client(client_fd, spool_dir);

        //close client socket after disconnect
        close(client_fd);
        std::cout << "Client connection closed. \n";
    }
    
    //close listening socket
    close(server_fd);
    
    

    return 0;
}