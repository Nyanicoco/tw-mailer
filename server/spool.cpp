// server/spool.cpp
#include "spool.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

namespace fs = std::filesystem;

namespace{
std::mutex g_spool_mutex;

//collects numeric id.txt message IDs in ascending order.
//caller must hold g_spool_mutex.
std::vector<int> get_user_message_ids_locked(const std::string& user_dir){
    std::vector<int> ids;
    if(!fs::exists(user_dir) || !fs::is_directory(user_dir)){
        return ids;
    }

    for(const auto& entry : fs::directory_iterator(user_dir)){
        if (!entry.is_regular_file()) {
            continue;
        }
        std::string filename = entry.path().filename().string();
        if (filename.size() <= 4 || filename.substr(filename.size() - 4) != ".txt"){
            continue;
        }

        std::string number_part = filename.substr(0, filename.size() - 4);
        bool all_digits = !number_part.empty();
        for (char c : number_part) {
            if (!std::isdigit(static_cast<unsigned char>(c))){
                all_digits = false;
                break;
            }
        }
        if (all_digits) {
            ids.push_back(std::stoi(number_part));
        }
    }

    std::sort(ids.begin(), ids.end());
    return ids;
}
} // namespace

bool spool_send_message(const std::string& spool_dir, const std::string& sender,
                        const std::string& receiver, const std::string& subject,
                        const std::string& body) {
    if(sender.empty() || receiver.empty() || subject.size() > 80){
        return false;
    }

    std::lock_guard<std::mutex> lock(g_spool_mutex);
    std::string user_dir = spool_dir + "/" + receiver;
    fs::create_directories(user_dir);

    std::vector<int> ids = get_user_message_ids_locked(user_dir);
    int next_id = ids.empty() ? 1 : (ids.back() + 1);

    std::string file_path = user_dir + "/" + std::to_string(next_id) + ".txt";
    std::ofstream out_file(file_path);
    if(!out_file.is_open()){
        return false;
    }

    out_file << sender << "\n";
    out_file << receiver << "\n";
    out_file << subject << "\n";
    out_file << body;
    out_file.close();

    return out_file.good();
}

std::string spool_list_messages(const std::string& spool_dir, const std::string& username){
    std::lock_guard<std::mutex> lock(g_spool_mutex);
    std::string user_dir = spool_dir + "/" + username;
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

    return std::to_string(ids.size()) + "\n" + list_lines;
}

bool spool_read_message(const std::string& spool_dir, const std::string& username,
                        int msg_num, std::string& out_response){
    if(msg_num <= 0){
        return false;
    }

    std::lock_guard<std::mutex> lock(g_spool_mutex);
    std::string user_dir = spool_dir + "/" + username;
    std::vector<int> ids = get_user_message_ids_locked(user_dir);

    if (static_cast<size_t>(msg_num) > ids.size()){
        return false;
    }

    int actual_id = ids[static_cast<size_t>(msg_num - 1)];
    std::string file_path = user_dir + "/" + std::to_string(actual_id) + ".txt";
    std::ifstream in_file(file_path);
    if(!in_file.is_open()){
        return false;
    }

    std::string file_content, line;
    while(std::getline(in_file, line)){
        file_content += line + "\n";
    }

    out_response = "OK\n" + file_content + ".\n";
    return true;
}

bool spool_delete_message(const std::string& spool_dir, const std::string& username,
                          int msg_num){
    if(msg_num <= 0){
        return false;
    }

    std::lock_guard<std::mutex> lock(g_spool_mutex);
    std::string user_dir = spool_dir + "/" + username;
    std::vector<int> ids = get_user_message_ids_locked(user_dir);

    if(static_cast<size_t>(msg_num) > ids.size()){
        return false;
    }

    int actual_id = ids[static_cast<size_t>(msg_num - 1)];
    std::string file_path = user_dir + "/" + std::to_string(actual_id) + ".txt";
    return fs::remove(file_path);
}