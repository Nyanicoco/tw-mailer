CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread
LDFLAGS = -pthread -lldap -llber


all: twmailer-server twmailer-client

twmailer-server: server/Main.cpp server/net_io.cpp server/auth.cpp server/spool.cpp server/net_io.h server/auth.h server/spool.h
	$(CXX) $(FLAGS) server/Main.cpp server/net_io.cpp server/auth.cpp server/spool.cpp -o twmailer-server $(LDAP_LIBS)

	# Client mit Main.cpp und MailerClient.cpp kompilieren
twmailer-client: client/Main.cpp client/MailerClient.cpp client/MailerClient.h
	$(CXX) $(CXXFLAGS) client/Main.cpp client/MailerClient.cpp -o twmailer-client


clean:
	rm -f twmailer-server twmailer-client


# Alles neu kompilieren
rebuild: clean all