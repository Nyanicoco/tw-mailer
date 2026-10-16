#include "MailerClient.h"


#include <cstdio>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>

#include "MailerClient.h"

#include <cstdio>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>

namespace {

const size_t MAX_USERNAME_LENGTH = 8;
const size_t MAX_SUBJECT_LENGTH = 80;

// Prüft: 1 bis 8 Zeichen, nur Kleinbuchstaben a-z und Ziffern 0-9
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

// Prüft: mindestens eine Ziffer, nur Ziffern
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

// Liest eine ganze Zeile von der Tastatur. Liefert false bei EOF (Strg+D)
bool readInput(const std::string& prompt, std::string& input) {
    std::cout << prompt;
    if (!std::getline(std::cin, input)) {
        std::cerr << "\nEingabe beendet\n";
        return false;
    }
    return true;
}

// Liest und prüft einen Benutzernamen
bool readUsername(const std::string& prompt, std::string& name) {
    if (!readInput(prompt, name)) {
        return false;
    }
    if (!isValidUsername(name)) {
        std::cerr << "Ungültiger Benutzername: max. 8 Zeichen, nur a-z und 0-9\n";
        return false;
    }
    return true;
}

// Liest und prüft eine Nachrichtennummer
bool readMessageNumber(std::string& number) {
    if (!readInput("Nachrichtennummer: ", number)) {
        return false;
    }
    if (!isValidNumber(number)) {
        std::cerr << "Ungültige Nachrichtennummer: nur Ziffern erlaubt\n";
        return false;
    }
    return true;
}

}

MailerClient::MailerClient(int socketFd) : m_socketFd(socketFd) {}

// Zeigt die verfügbaren Befehle
void MailerClient::showMenu() {
    std::cout << "\n--- TW-Mailer ---\n"
              << "1  Nachricht senden\n"
              << "2  Nachrichten auflisten\n"
              << "3  Nachricht lesen\n"
              << "4  Nachricht löschen\n"
              << "5  Beenden\n";
}

// Hauptschleife: liest Befehle und ruft die passende Funktion auf
void MailerClient::run() {
    std::string choice;

    while (true) {
        showMenu();

        if (!readInput("Auswahl: ", choice)) {
            sendAll("QUIT\n"); // bei EOF sauber abmelden
            return;
        }

        // Nur genau ein Zeichen ist gültig
        if (choice.size() != 1) {
            std::cerr << "Ungültige Auswahl: bitte 1 bis 5 eingeben\n";
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
                sendAll("QUIT\n"); // Server antwortet nicht auf QUIT
                std::cout << "Abgemeldet.\n";
                return;
            default:
                std::cerr << "Ungültige Auswahl: bitte 1 bis 5 eingeben\n";
                break;
        }
    }
}

// Sendet alle Bytes, auch wenn send() nur einen Teil überträgt
bool MailerClient::sendAll(const std::string& data) {
    size_t totalSent = 0;

    while (totalSent < data.size()) {
        // MSG_NOSIGNAL verhindert einen Absturz durch SIGPIPE,
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
        std::cerr << "Keine Daten gesendet\n";
    }
    return false;
}

        totalSent += static_cast<size_t>(bytesSent);
    }

    return true;
}

// Liest genau eine Zeile (bis '\n') vom Server
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
            std::cerr << "Server hat die Verbindung geschlossen\n";
            return false;
        }

        m_readBuffer.append(buffer, static_cast<size_t>(bytesReceived));
    }
}

// SEND\n<Sender>\n<Empfänger>\n<Betreff>\n<Nachricht...>\n.\n
void MailerClient::sendMessage() {
    std::string sender;
    std::string receiver;
    std::string subject;

    if (!readUsername("Sender: ", sender)) {
        return;
    }
    if (!readUsername("Empfänger: ", receiver)) {
        return;
    }
    if (!readInput("Betreff (max. 80 Zeichen): ", subject)) {
        return;
    }
    if (subject.empty() || subject.size() > MAX_SUBJECT_LENGTH) {
        std::cerr << "Ungültiger Betreff: 1 bis 80 Zeichen erlaubt\n";
        return;
    }

    std::string request = "SEND\n" + sender + "\n" + receiver + "\n" + subject + "\n";

    std::cout << "Nachricht eingeben. Eine Zeile mit nur \".\" beendet die Eingabe:\n";

    std::string line;
    while (true) {
        if (!std::getline(std::cin, line)) {
            std::cerr << "Eingabe abgebrochen, Nachricht nicht gesendet\n";
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
        std::cout << "Nachricht gesendet.\n";
    } else {
        std::cerr << "Server meldet Fehler: " << response << "\n";
    }
}

// LIST\n<Benutzername>\n
void MailerClient::listMessages() {
    std::string username;
    if (!readUsername("Benutzername: ", username)) {
        return;
    }

    if (!sendAll("LIST\n" + username + "\n")) {
        return;
    }

    // Erste Zeile: Anzahl der Nachrichten
    std::string countLine;
    if (!readLine(countLine)) {
        return;
    }

    int count = 0;
    try {
        count = std::stoi(countLine);
    } catch (...) {
        std::cerr << "Ungültige Antwort vom Server: " << countLine << "\n";
        return;
    }

    std::cout << "Anzahl Nachrichten: " << count << "\n";

    // Pro Nachricht eine Zeile mit dem Betreff
    for (int i = 0; i < count; ++i) {
        std::string subject;
        if (!readLine(subject)) {
            return;
        }
        std::cout << (i + 1) << ": " << subject << "\n";
    }
}

// READ\n<Benutzername>\n<Nachrichtennummer>\n
void MailerClient::readMessage() {
    std::string username;
    std::string number;

    if (!readUsername("Benutzername: ", username)) {
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
        std::cerr << "Nachricht nicht gefunden (" << response << ")\n";
        return;
    }

    // Annahme: der Server beendet die Nachricht mit ".\n"
    std::string line;
    while (readLine(line)) {
        if (line == ".") {
            break;
        }
        std::cout << line << "\n";
    }
}

// DEL\n<Benutzername>\n<Nachrichtennummer>\n
void MailerClient::deleteMessage() {
    std::string username;
    std::string number;

    if (!readUsername("Benutzername: ", username)) {
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
        std::cout << "Nachricht gelöscht.\n";
    } else {
        std::cerr << "Löschen fehlgeschlagen (" << response << ")\n";
    }
}