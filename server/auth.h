// server/auth.h
#pragma once

#include <string>

//loads active blacklisted ips from disk at startup
void load_blacklist(const std::string& blacklist_file);

// returns true if ip is currently blacklisted + removes expired bans
bool is_ip_blacklisted(const std::string& ip, const std::string& blacklist_file);

// increments failed login attempts for after 3 failures, blacklists ip for 60 seconds and saves to blacklist_file
void record_failed_login(const std::string& ip, const std::string& username,
                         const std::string& blacklist_file);

//resets failed login count for ip, username, after a successful login
void reset_failed_logins(const std::string& ip, const std::string& username);

//verifies credentials against technikum wien ldap using the openldap c api
bool check_ldap_login(const std::string& username, const std::string& password);