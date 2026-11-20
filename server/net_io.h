// server/net_io.h
#pragma once

#include <string>

// reads a single '\n'-terminated line from socket fd using buffer
bool read_line(int fd, std::string& buffer, std::string& out_line);

//sends the entire msg string over socket fd, handling partial writes.
bool write_all(int fd, const std::string& msg);