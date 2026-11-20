// server/spool.h
#pragma once

#include <string>

//stores a new message in receiver's spool folder using sender from the session.
bool spool_send_message(const std::string& spool_dir, const std::string& sender,
                        const std::string& receiver, const std::string& subject,
                        const std::string& body);

// Returns "<count>\n<subject 1>\n..." for username
std::string spool_list_messages(const std::string& spool_dir, const std::string& username);

// reads 1-based message msg_num for username and sets out_response 
bool spool_read_message(const std::string& spool_dir, const std::string& username,
                        int msg_num, std::string& out_response);

// deletes 1-based message msg_num for username
bool spool_delete_message(const std::string& spool_dir, const std::string& username,
                          int msg_num);