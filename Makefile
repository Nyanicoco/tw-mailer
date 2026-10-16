all: twmailer-server twmailer-client

twmailer-server: server/Main.cpp
	g++ -std=c++17 server/Main.cpp -o twmailer-server

twmailer-client: client/Main.cpp
	g++ -std=c++17 client/Main.cpp -o twmailer-client

clean:
	rm -f twmailer-server twmailer-client
```[cite: 60]