
#ifndef MAILERCLIENT_H
#define MAILERCLIENT_H

#include <string>

class MailerClient {
public:
    MailerClient(int socketFd); 
    void showMenu();
    void run();

      // Client-
    void login();
    void sendMessage();
    void listMessages();
    void readMessage();
    void deleteMessage();

    private:
    int m_socketFd; // saves the Socket Deskriptor
    bool m_loggedIn;
    bool m_connected;
    std::string m_readBuffer;
// function for the communication
    bool sendAll(const std::string& data);
    bool readLine(std::string& line);
    static bool isValidUsername(const std::string& name);    

};

#endif
