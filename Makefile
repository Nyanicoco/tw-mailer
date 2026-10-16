CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra

all: twmailer-server twmailer-client

twmailer-server: server/Main.cpp
	$(CXX) $(CXXFLAGS) server/Main.cpp -o twmailer-server

	# Client mit Main.cpp und MailerClient.cpp kompilieren
twmailer-client: client/Main.cpp client/MailerClient.cpp client/MailerClient.h
	$(CXX) $(CXXFLAGS) client/Main.cpp client/MailerClient.cpp -o twmailer-client


clean:
	rm -f twmailer-server twmailer-client


# Alles neu kompilieren
rebuild: clean all