// server/auth.cpp
#include "auth.h"

#include <ctime>
#include <fstream>
#include <iostream>
#include <ldap.h>
#include <map>
#include <mutex>
#include <utility>

namespace {
const char* TW_LDAP_HOST = "ldap.technikum-wien.at";
const int TW_LDAP_PORT = 389;
const char* TW_LDAP_BASE_DN = "dc=technikum-wien,dc=at";
const int MAX_FAILED_ATTEMPTS = 3;
const int BLACKLIST_DURATION_SEC = 60;

//shared state protected by mutex for multi threaded access
std::mutex g_auth_mutex;
std::map<std::pair<std::string, std::string>, int> g_failed_attempts;
std::map<std::string, std::time_t> g_blacklisted_ips;

// caller must hold g_auth_mutex
void save_blacklist_locked(const std::string& blacklist_file){
    std::ofstream out(blacklist_file, std::ios::trunc);
    if (!out.is_open()){
        std::cerr << "Could not write blacklist file: " << blacklist_file << "\n";
        return;
    }

    for(const auto& pair : g_blacklisted_ips){
        out << pair.first << " " << pair.second << "\n";
    }
}
} // namespace

void load_blacklist(const std::string& blacklist_file){
    std::lock_guard<std::mutex> lock(g_auth_mutex);
    g_blacklisted_ips.clear();

    std::ifstream in(blacklist_file);
    if(!in.is_open()){
        return; // file might not exist first
    }

    std::string ip;
    std::time_t expires_at = 0;
    std::time_t now = std::time(nullptr);

    while(in >> ip >> expires_at){
        if(expires_at > now){
            g_blacklisted_ips[ip] = expires_at;
        }
    }
}

bool is_ip_blacklisted(const std::string& ip, const std::string& blacklist_file){
    std::lock_guard<std::mutex> lock(g_auth_mutex);
    std::time_t now = std::time(nullptr);
    bool changed = false;

    for(auto it = g_blacklisted_ips.begin(); it != g_blacklisted_ips.end(); ){
        if(it->second <= now){
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

void record_failed_login(const std::string& ip, const std::string& username,
                         const std::string& blacklist_file) {
    std::lock_guard<std::mutex> lock(g_auth_mutex);
    auto key = std::make_pair(ip, username);
    g_failed_attempts[key] += 1;

    if(g_failed_attempts[key] >= MAX_FAILED_ATTEMPTS){
        g_blacklisted_ips[ip] = std::time(nullptr) + BLACKLIST_DURATION_SEC;
        g_failed_attempts[key] = 0;
        save_blacklist_locked(blacklist_file);
    }
}

void reset_failed_logins(const std::string& ip, const std::string& username){
    std::lock_guard<std::mutex> lock(g_auth_mutex);
    g_failed_attempts.erase(std::make_pair(ip, username));
}

bool check_ldap_login(const std::string& username, const std::string& password){
    if(username.empty() || password.empty()){
        return false;
    }

    std::string uri = "ldap://" + std::string(TW_LDAP_HOST) + ":" + std::to_string(LDAP_PORT);
    LDAP* ld = nullptr;

    if(ldap_initialize(&ld, uri.c_str()) != LDAP_SUCCESS || ld == nullptr){
        return false;
    }

    int protocol_version = LDAP_VERSION3;
    ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &protocol_version);

    // a search finds a matching entry dn, use that instead
    std::string user_dn = "uid=" + username + "," + TW_LDAP_BASE_DN;

    LDAPMessage* search_result = nullptr;
    std::string filter = "(uid=" + username + ")";
    char* attrs[] = {nullptr};

    if(ldap_search_ext_s(ld, TW_LDAP_BASE_DN, LDAP_SCOPE_SUBTREE, filter.c_str(),
                          attrs, 0, nullptr, nullptr, nullptr, 0, &search_result) == LDAP_SUCCESS){
        LDAPMessage* entry = ldap_first_entry(ld, search_result);
        if (entry != nullptr){
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