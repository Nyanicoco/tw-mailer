#include "MailerClient.h"


#include <cstdio>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <cerrno>


namespace {

const size_t MAX_USERNAME_LENGTH = 8;
const size_t MAX_SUBJECT_LENGTH = 80;


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

bool readPassword(const std::string& prompt, std::string& password) {
    std::cout << prompt << std::flush;

    termios oldSettings{};
    bool echoDisabled = false;

    // Prüfen, ob stdin ein Terminal ist
    if (isatty(STDIN_FILENO)) {
        if (tcgetattr(STDIN_FILENO, &oldSettings) == -1) {
            return false;
        }

        termios newSettings = oldSettings;

        // Passworteingabe unsichtbar machen
        newSettings.c_lflag &= ~ECHO;

        if (tcsetattr(STDIN_FILENO, TCSANOW, &newSettings) == -1) {
            return false;
        }

        echoDisabled = true;
    }

    // Passwort einlesen
    bool ok = static_cast<bool>(std::getline(std::cin, password));

    // Alte Einstellungen wiederherstellen
    if (echoDisabled) {
        if (tcsetattr(STDIN_FILENO, TCSANOW, &oldSettings) == -1) {
            return false;
        }

        std::cout << '\n';
    }

    return ok;
}


// read und checks the username
bool readUsername(const std::string& prompt, std::string& name) {
    if (!readInput(prompt, name)) {
        return false;
    }
    
    return true;
}

// only reads and checks the message number
bool readMessageNumber(std::string& number) {
    if (!readInput("message number: ", number)) {
        return false;
    }
    if (!isValidNumber(number)) {
        std::cerr << "Invalid message number: only numbers allowed\n";
        return false;
    }
    return true;
}

}

MailerClient::MailerClient(int socketFd) 
: m_socketFd(socketFd),
  m_loggedIn(false),
  m_connected(true) {}

// shows the Dashboard

void MailerClient::showMenu() {
    std::cout << "\n--- TW-Mailer ---\n";

    // Show login status
    if (m_loggedIn) {
        std::cout << "Status: logged in\n";
    } else {
        std::cout << "Status: not logged in\n";
    }

    // Show menu options
    std::cout << "1  Login\n"
              << "2  Send message\n"
              << "3  List messages\n"
              << "4  Read message\n"
              << "5  Delete message\n"
              << "6  Quit\n";
}

// reads the Commands for the correct function
void MailerClient::run() {
    std::string choice;

    while (true) {
        showMenu();

        std::cout << "Choose option (1 to 6): ";

        if (!std::getline(std::cin, choice)) {
            sendAll("QUIT\n");
            m_loggedIn = false;
            std::cout << "\nInput ended. Disconnected from server.\n";
            return;
        }

        if (choice.length() != 1) {
            std::cerr << "Invalid option. Choose 1 to 6.\n";
            continue;
        }

        switch (choice[0]) {
            case '1':
                login();
                break;

            case '2':
                sendMessage();
                break;

            case '3':
                listMessages();
                break;

            case '4':
                readMessage();
                break;

            case '5':
                deleteMessage();
                break;

            case '6':
                // Send QUIT. No QUIT response expected.
                sendAll("QUIT\n");
                m_loggedIn = false;
                std::cout << "Disconnected from server.\n";
                return;

            default:
                std::cerr << "Invalid option. Choose 1 to 6.\n";
                break;
        }

         // Check connection after each command
    if (!m_connected) {
        std::cerr << "Connection lost. Client exits.\n";
        m_loggedIn = false;
        return;
    }
    }
}


void MailerClient::login() {
    std::string username;
    std::string password;

    // 1. Read username
    if (!readInput("Username: ", username)) {
        return;
    }

    // 2. Read password
    if (!readPassword("Password: ", password)) {
    std::cerr << "Password input failed.\n";
    return;
}

    // 3. Check empty input
    if (username.empty() || password.empty()) {
        std::cerr << "Username and password must not be empty.\n";
        return;
    }

    // 4. Build LOGIN request
    std::string request =
        "LOGIN\n" + username + "\n" + password + "\n";

    // 5. Send request
    if (!sendAll(request)) {
        return;
    }

    // 6. Read server response
    std::string response;
    if (!readLine(response)) {
        return;
    }

    // 7. Check login result
    if (response == "OK") {
        m_loggedIn = true;
        std::cout << "Login successful.\n";
    } else {
        m_loggedIn = false;
        std::cerr << "Login failed (" << response << ").\n";
    }
}


// sends all Bytes, even if send() only get one part
bool MailerClient::sendAll(const std::string& data) {
    size_t totalSent = 0;

    while (totalSent < data.size()) {
        // MSG_NOSIGNAL prevent crash with SIGPIPE,
        ssize_t bytesSent = send(
            m_socketFd,
            data.c_str() + totalSent,
            data.size() - totalSent,
            MSG_NOSIGNAL
        );

       if (bytesSent == -1) {

    // Interrupted system call: retry
    if (errno == EINTR) {
        continue;
    }

    perror("send failed");
    m_connected = false;
    return false;
}

    if (bytesSent == 0) {
        std::cerr << "Connection closed while sending.\n";
        m_connected = false;
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

            // Interrupted system call: retry
            if (errno == EINTR) {
                continue;
            }

            perror("recv failed");
            m_connected = false;
            return false;
        }

        if (bytesReceived == 0) {
            std::cerr << "Server ends the connection\n";
            m_connected = false;
            return false;
        }

        m_readBuffer.append(
            buffer,
            static_cast<size_t>(bytesReceived)
        );

    }
}


 // SEND\n<receiver>\n<subject>\n<text>\n.\n
void MailerClient::sendMessage() {
    std::string receiver;
    std::string subject;

    // 1. Read receiver
    if (!readUsername("receiver: ", receiver)) {
        return;
    }

    // 2. Read subject
    if (!readInput("subject (max. 80 char): ", subject)) {
        return;
    }

    if (subject.empty() || subject.size() > MAX_SUBJECT_LENGTH) {
        std::cerr << "Invalid subject. 1 to 80 characters allowed.\n";
        return;
    }

    // 3. Build SEND request without sender
    std::string request = "SEND\n" + receiver + "\n" + subject + "\n";

    // 4. Read message body
    std::cout << "Enter the Mail. One line with only \".\" ends the input:\n";

    std::string line;
    while (true) {
        if (!std::getline(std::cin, line)) {
            std::cerr << "Input failed. Message cannot be sent.\n";
            return;
        }

        request += line + "\n";

        if (line == ".") {
            break;
        }
    }

    // 5. Send request
    if (!sendAll(request)) {
        return;
    }

    // 6. Read server response
    std::string response;
    if (!readLine(response)) {
        return;
    }

    // 7. Check response
    if (response == "OK") {
        std::cout << "Message sent.\n";
    } else {
        std::cerr << "SEND failed (" << response
                  << "). Not logged in or message rejected.\n";
    }
}


// LIST\n<username>\n

void MailerClient::listMessages() {

    // 1. Send LIST request to server
    if (!sendAll("LIST\n")) {
        return;
    }

    // 2. Read message count from server
    std::string countLine;
    if (!readLine(countLine)) {
        return;
    }

    // Server reports an error
    if (countLine == "ERR") {
        std::cerr << "Server rejected LIST (not logged in?)\n";
        return;
    }


    // 3. Convert count to integer
    int count = 0;
    try {
        count = std::stoi(countLine);
    } catch (...) {
        std::cerr << "Invalid answer from the server: "
                  << countLine << "\n";
        return;
    }

    std::cout << "message count: " << count << "\n";

    // 4. Read and print each subject
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
    std::string number;

    // 1. Read message number
    if (!readMessageNumber(number)) {
        return;
    }

    // 2. Send READ request
    if (!sendAll("READ\n" + number + "\n")) {
        return;
    }

    // 3. Read server response
    std::string response;
    if (!readLine(response)) {
        return;
    }

    // 4. Check response
    if (response != "OK") {
        std::cerr << "READ failed ("
                  << response << "). Message not found or not logged in.\n";
        return;
    }

    // 5. Read message until "."
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
    std::string number;

    // 1. Read message number
    if (!readMessageNumber(number)) {
        return;
    }

    // 2. Send DEL request
    if (!sendAll("DEL\n" + number + "\n")) {
        return;
    }

    // 3. Read server response
    std::string response;
    if (!readLine(response)) {
        return;
    }

    // 4. Check response
    if (response == "OK") {
        std::cout << "Message deleted.\n";
    } else {
        std::cerr << "DEL failed ("
                  << response << "). Message not found or not logged in.\n";
    }
}
