all: twmailer-server twmailer-client

twmailer-server: server/Main.cpp
	g++ -std=c++17 server/Main.cpp -o twmailer-server

twmailer-client: client/Main.cpp
	g++ -std=c++17 client/Main.cpp -o twmailer-client

	# Client mit Main.cpp und MailerClient.cpp kompilieren
twmailer-client: client/Main.cpp client/MailerClient.cpp client/MailerClient.h
	$(CXX) $(CXXFLAGS) client/Main.cpp client/MailerClient.cpp -o twmailer-client


clean:
	rm -f twmailer-server twmailer-client
```[cite: 60]

# Alles neu kompilieren
rebuild: clean all