# IRC-Chat-client
Final project for IT4062E - Network Programming

This project is built for the Linux environment

Compile : `g++ client.cpp -o ircclient -lpthread`
Connect to IRC Server: ./ircclient host port 
Example : ./ircclient fiery.swiftirc.net 8080

Command:
- /join #channel : connect to a channel 
- /msg #channel <msg> : send a msg to a channel 
- /msg nickname <msg>: send a <msg> to a user
- /part #channel : leave channel
- /nick <newNick> : change nickname to newNick
- /ping and /pong : test presence of a connection.
