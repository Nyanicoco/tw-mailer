#include "MailerClient.h"


#include <cstdio>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>


namespace {

const size_t MAX_USERNAME_LENGTH = 8;
const size_t MAX_SUBJECT_LENGTH = 80;

// Checks: 1 to 8 Zeichen, only a-z and number 0-9
bool isValidUsername(const std::string& name) {
    if (name.empty() || name.size() > MAX_USERNAME_LENGTH) {
        return false;
    }
    for (char c : name) {
        bool isLower = (c >= 'a' && c <= 'z');
        bool isDigit = (c >= '0' && c <= '9');
        if (!isLower && !isDigit) {
            return false;
        }
    }
    return true;
}

// Checks : min. one number only number
bool isValidNumber(const std::string& text) {
    if (text.empty()) {
        return false;
    }
    for (char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    return true;
}

// reads only one line if false EOF (Strg+D)
bool readInput(const std::string& prompt, std::string& input) {
    std::cout << prompt;
    if (!std::getline(std::cin, input)) {
        std::cerr << "\nEnter\n";
        return false;
    }
    return true;
}

// read und checks the username
bool readUsername(const std::string& prompt, std::string& name) {
    if (!readInput(prompt, name)) {
        return false;
    }
    if (!isValidUsername(name)) {
        std::cerr << "Invalid username: max. 8 char, only a-z and 0-9\n";
        return false;
    }
    return true;
}

// only reads and checks the message number
bool readMessageNumber(std::string& number) {
    if (!readInput("Nachrichtennummer: ", number)) {
        return false;
    }
    if (!isValidNumber(number)) {
        std::cerr << "Invalid message number: only numbers allowed\n";
        return false;
    }
    return true;
}

}

MailerClient::MailerClient(int socketFd) : m_socketFd(socketFd) {}

// shows the Dashboard
void MailerClient::showMenu() {
    std::cout << "\n--- TW-Mailer ---\n"
              << "1  Send message\n"
              << "2  List message\n"
              << "3  Read message\n"
              << "4  Delete message\n"
              << "5  Quit\n";
}

// reads the Commands for the correct function
void MailerClient::run() {
    std::string choice;

    while (true) {
        showMenu();

        if (!readInput("Option: ", choice)) {
            sendAll("QUIT\n"); //  EOF quits correct
            return;
        }

        // Only one char is valid
        if (choice.size() != 1) {
            std::cerr << "Option invalid: please enter 1 to 5 \n";
            continue;
        }

        switch (choice[0]) {
            case '1':
                sendMessage();
                break;
            case '2':
                listMessages();
                break;
            case '3':
                readMessage();
                break;
            case '4':
                deleteMessage();
                break;
            case '5':
                sendAll("QUIT\n"); // Server answers with QUIT
                std::cout << "logged off.\n";
                return;
            default:
                std::cerr << "Invalid option: please enter 1 to 5 \n";
                break;
        }
    }
}

// sends all Bytes, even if send() only get one part
bool MailerClient::sendAll(const std::string& data) {
    size_t totalSent = 0;

    while (totalSent < data.size()) {
        // MSG_NOSIGNAL prevent crash with SIGPIPE,
        // falls der Server die Verbindung geschlossen hat
        ssize_t bytesSent = send(
            m_socketFd,
            data.c_str() + totalSent,
            data.size() - totalSent,
            MSG_NOSIGNAL
        );

        if (bytesSent <= 0) {
    if (bytesSent == -1) {
        perror("send failed");
    } else {
        std::cerr << "no data sended\n";
    }
    return false;
}

        totalSent += static_cast<size_t>(bytesSent);
    }

    return true;
}

// Reads only one line till ('\n') from the Server
bool MailerClient::readLine(std::string& line) {
    while (true) {
        size_t pos = m_readBuffer.find('\n');
        if (pos != std::string::npos) {
            line = m_readBuffer.substr(0, pos);
            m_readBuffer.erase(0, pos + 1);
            return true;
        }

        char buffer[1024];
        ssize_t bytesReceived = recv(m_socketFd, buffer, sizeof(buffer), 0);

        if (bytesReceived == -1) {
            perror("recv failed");
            return false;
        }
        if (bytesReceived == 0) {
            std::cerr << "Server ends the connection\n";
            return false;
        }

        m_readBuffer.append(buffer, static_cast<size_t>(bytesReceived));
    }
}

// SEND\n<Sender>\n<Client>\n<subject>\n<message...>\n.\n
void MailerClient::sendMessage() {
    std::string sender;
    std::string receiver;
    std::string subject;

    if (!readUsername("Sender: ", sender)) {
        return;
    }
    if (!readUsername("receiver: ", receiver)) {
        return;
    }
    if (!readInput("subject (max. 80 char): ", subject)) {
        return;
    }
    if (subject.empty() || subject.size() > MAX_SUBJECT_LENGTH) {
        std::cerr << "invalid subject 1 to 80 Zeichen allowed\n";
        return;
    }

    std::string request = "SEND\n" + sender + "\n" + receiver + "\n" + subject + "\n";

    std::cout << "Enter the Mail. One Line with only \".\" end the input:\n";

    std::string line;
    while (true) {
        if (!std::getline(std::cin, line)) {
            std::cerr << "Input failed message can not be sent\n";
            return;
        }
        request += line + "\n";
        if (line == ".") {
            break;
        }
    }

    if (!sendAll(request)) {
        return;
    }

    std::string response;
    if (!readLine(response)) {
        return;
    }

    if (response == "OK") {
        std::cout << "Message sent.\n";
    } else {
        std::cerr << "Server reports an error: " << response << "\n";
    }
}

// LIST\n<username>\n
void MailerClient::listMessages() {
    std::string username;
    if (!readUsername("Username: ", username)) {
        return;
    }

    if (!sendAll("LIST\n" + username + "\n")) {
        return;
    }

    // First Line: count the message
    std::string countLine;
    if (!readLine(countLine)) {
        return;
    }

    int count = 0;
    try {
        count = std::stoi(countLine);
    } catch (...) {
        std::cerr << "Invalid answer from the server: " << countLine << "\n";
        return;
    }

    std::cout << "message count" << count << "\n";

    // each message 
    for (int i = 0; i < count; ++i) {
        std::string subject;
        if (!readLine(subject)) {
            return;
        }
        std::cout << (i + 1) << ": " << subject << "\n";
    }
}

// READ\n<username>\n<message number>\n
void MailerClient::readMessage() {
    std::string username;
    std::string number;

    if (!readUsername("username: ", username)) {
        return;
    }
    if (!readMessageNumber(number)) {
        return;
    }

    if (!sendAll("READ\n" + username + "\n" + number + "\n")) {
        return;
    }

    std::string response;
    if (!readLine(response)) {
        return;
    }

    if (response != "OK") {
        std::cerr << "Message not found (" << response << ")\n";
        return;
    }

    // server did not get the message ".\n"
    std::string line;
    while (readLine(line)) {
        if (line == ".") {
            break;
        }
        std::cout << line << "\n";
    }
}

// DEL\n<username>\n<messagenumber>\n
void MailerClient::deleteMessage() {
    std::string username;
    std::string number;

    if (!readUsername("username: ", username)) {
        return;
    }
    if (!readMessageNumber(number)) {
        return;
    }

    if (!sendAll("DEL\n" + username + "\n" + number + "\n")) {
        return;
    }

    std::string response;
    if (!readLine(response)) {
        return;
    }

    if (response == "OK") {
        std::cout << "Message deleted.\n";
    } else {
        std::cerr << "Deletion failed (" << response << ")\n";
    }
}