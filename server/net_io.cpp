// server/net_io.cpp
#include "net_io.h"

#include <cerrno>
#include <unistd.h>

bool read_line(int fd, std::string& buffer, std::string& out_line){
    out_line.clear();

    while(true){
        size_t newline_pos = buffer.find('\n');
        if (newline_pos != std::string::npos){
            out_line = buffer.substr(0, newline_pos);
            buffer.erase(0, newline_pos + 1);

            // strip '\r' if the client sends CRLF ("\r\n")
            if(!out_line.empty() && out_line.back() == '\r'){
                out_line.pop_back();
            }
            return true;
        }

        char temp[1024];
        ssize_t n = read(fd, temp, sizeof(temp));
        if(n < 0){
            if(errno == EINTR){
                continue; //interrupted by signal, try again
            }
            return false;
        }
        if(n == 0){
            //client closed connection - return leftover bytes
            if(!buffer.empty()){
                out_line = buffer;
                buffer.clear();
                return true;
            }
            return false;
        }
        buffer.append(temp, static_cast<size_t>(n));
    }
}

bool write_all(int fd, const std::string& msg){
    size_t sent_total = 0;
    while(sent_total < msg.size()){
        ssize_t sent = write(fd, msg.data() + sent_total, msg.size() - sent_total);
        if(sent < 0){
            if(errno == EINTR){
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