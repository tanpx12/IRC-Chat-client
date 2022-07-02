#include <iostream>
#include <cstdlib>
#include <malloc.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <pthread.h>
#include <dirent.h>
#include <fcntl.h>
#include <netdb.h>
#include <algorithm>
#include <sstream>
#include <vector>
#include <list>
#include <map>
#define NUM_IRC_CMDS 26
#define INVALID_SOCKET -1

typedef void *(*ThreadFunction)(void * /*param*/);
typedef void *ThreadReturn;
typedef pthread_t ThreadId;

using namespace std;



struct Thread
{
    Thread() : _threadID(0){};
    ThreadId _threadID;
    bool Start(ThreadFunction callback, void *param)
    {
        if (pthread_create(&_threadID, NULL, *callback, param) == 0)
        {
            return true;
        }
        return false;
    }
};


vector<string> split(string const &text, char sep)
{
    vector<string> tokens;
    size_t start = 0, end = 0;
    while ((end = text.find(sep, start)) != string::npos)
    {
        tokens.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    tokens.push_back(text.substr(start));
    return tokens;
}


struct IRCCommandPrefix
{

    string prefix;
    string nick;
    string user;
    string host;

    void Parse(string data)
    {
        if (data == "")
        {
            return;
        }
        prefix = data.substr(1, data.find(" ") - 1);
        vector<string> tokens;

        if (prefix.find("@") != string::npos)
        {
            tokens = split(prefix, '@');
            nick = tokens.at(0);
            user = tokens.at(1);
        }
        if (nick != "" && nick.find("!") != string::npos)
        {
            tokens = split(nick, '!');
            nick = tokens.at(0);
            user = tokens.at(1);
        }
    };
};

struct IRCMessage
{
    IRCMessage();
    IRCMessage(string cmd, IRCCommandPrefix p, vector<string> params) : command(cmd), prefix(p), parameters(params){};

    string command;
    IRCCommandPrefix prefix;
    vector<string> parameters;
};

struct IRCCommandHook
{
    IRCCommandHook() : function(NULL){};
    string command;
    void (*function)(IRCMessage);
};

struct IRCCommandHandler
{
    string command;
    void (*handler)(IRCMessage);
};

struct CommandEntry
{
    int argCount;
    void (*handler)(string);
};


//========== GLOBAL VARIABLE ===================

map<string, CommandEntry> _command;
bool _connected, _running;
string _user, _nick;
int _socket;
list<IRCCommandHook> _hooks;



bool SendData(char const* data)
{
    if (_connected)
        if (send(_socket, data, strlen(data), 0) == -1)
            return false;
    return true;
}

bool SendIRC(string data)
{
    data.append("\n");
    return SendData(data.c_str());
}


void Disconnect()
{
    close(_socket);
    _connected = false;
}


bool AddCommand(string name, int argCount, void (*handler)(string))
{
    CommandEntry entry;
    entry.argCount = argCount;
    entry.handler = handler;
    transform(name.begin(), name.end(), name.begin(), towlower);
    _command.insert(pair<string, CommandEntry>(name, entry));
    return true;
}

void ParseCommand(string command)
{
    if (_command.empty())
    {
        cout << "No commands available" << endl;
        return;
    }
    if (command[0] == '/')
        command = command.substr(1);

    string name = command.substr(0, command.find(" "));
    string args = command.substr(command.find(" ") + 1);

    int argCount = count(args.begin(), args.end(), ' ');

    transform(name.begin(), name.end(), name.begin(), towlower);

    map<string, CommandEntry>::const_iterator itr = _command.find(name);

    if (itr == _command.end())
    {
        cout << "Command not found." << endl;
        return;
    }

    if (++argCount < itr->second.argCount)
    {
        cout << "Insuficient arguments" << endl;
        return;
    }

    (*(itr->second.handler))(args);
}

void msgCommand(string arguments)
{
    string to = arguments.substr(0, arguments.find(" "));
    string text = arguments.substr(arguments.find(" ") + 1);

    cout << "To " + to + " :" + text << endl;
    SendIRC("PRIVMSG" + to + " :" + text);
}

void joinCommand(string channel)
{
    if (channel[0] != '#')
        channel = "#" + channel;
    SendIRC("JOIN" + channel);
}

void partCommand(string channel)
{
    if (channel[0] != '#')
        channel = "#" + channel;
    SendIRC("PART" + channel);
}

void ctcpCommand(string arguments)
{
    string to = arguments.substr(0, arguments.find(" "));
    string text = arguments.substr(arguments.find(" ") + 1);

    transform(text.begin(), text.end(), text.begin(), towupper);

    SendIRC("PRIVMSG" + to + " :\001" + text + "\001");
}

ThreadReturn inputThread(void *inp)
{
    string command;
    AddCommand("msg", 1, &msgCommand);
    AddCommand("join", 1, &joinCommand);
    AddCommand("part", 1, &partCommand);
    AddCommand("ctcp", 1, &ctcpCommand);

    while (true)
    {
        getline(cin, command);
        if (command == "")
            continue;
        if (command[0] == '/')
            ParseCommand(command);
        else
            SendIRC(command);
        if (command == "quit")
            break;
    }

    pthread_exit(NULL);
}

void HandleCTCP(IRCMessage message)
{
    string to = message.parameters.at(0);
    string text = message.parameters.at(message.parameters.size() - 1);

    text = text.substr(1, text.size() - 2);

    cout << "[" + message.prefix.nick << " request CTCP " << text << "]" << endl;
    if (to == _nick)
    {
        if (text == "VERSION")
        {
            SendIRC("NOTICE" + message.prefix.nick + " :\001IRC Chat Client by tanpx");
            return;
        }

        SendIRC("NOTICE" + message.prefix.nick + " :\001ERRMSG" + text + " :Not implemented\001");
    }
}

void HandlePrivMsg(IRCMessage message)
{
    string to = message.parameters.at(0);
    string text = message.parameters.at(message.parameters.size() - 1);

    if (text[0] = '\001')
    {
        HandleCTCP(message);
        return;
    }
    if (to[0] == '#')
        cout << "From " + message.prefix.nick << " @ " + to + ": " << text << endl;

    else
        cout << "From " + message.prefix.nick << ": " << text << endl;
}

void HandleNotice(IRCMessage message)
{
    string from = message.prefix.nick != "" ? message.prefix.nick : message.prefix.prefix;
    string text;

    if (!message.parameters.empty())
        text = message.parameters.at(message.parameters.size() - 1);
    if (!text.empty())
    {
        text = text.substr(1, text.size() - 2);
        if (text.find(" ") == string::npos)
        {
            cout << "[Invalid " << text << " reply from " << from << "]" << endl;
            return;
        }
        string ctcp = text.substr(0, text.find(" "));
        cout << "[" << from << " " << ctcp << " reply]" << text.substr(text.find(" ") + 1) << endl;
    }
    else
        cout << "-" << from << "- " << text << endl;
}

void HandleChannelJoinPart(IRCMessage message)
{
    string channel = message.parameters.at(0);
    string action = message.command == "JOIN" ? "joins" : "leaves";
    cout << message.prefix.nick << " " << action << " " << channel << endl;
}

void HandleUserNickChange(IRCMessage message)
{
    string newNick = message.parameters.at(0);
    cout << message.prefix.nick << " changed his nick to " << newNick << endl;
}

void HandleUserQuit(IRCMessage message)
{
    string text = message.parameters.at(0);
    cout << message.prefix.nick << " quits (" << text << ")" << endl;
}

void HandleChannelNamesList(IRCMessage message)
{
    string channel = message.parameters.at(2);
    string nicks = message.parameters.at(3);
    cout << "People on " << channel << ":" << endl
         << nicks << endl;
}

void HandleNicknameInUse(IRCMessage message)
{
    cout << message.parameters.at(1) << " " << message.parameters.at(2) << endl;
}

void HandleServerMessage(IRCMessage message)
{
    if (message.parameters.empty())
    {
        return;
    }
    vector<string>::const_iterator itr = message.parameters.begin();
    ++itr;
    for (; itr != message.parameters.end(); ++itr)
    {
        cout << *itr << " ";
    }
    cout << endl;
}

IRCCommandHandler ircCommandTable[NUM_IRC_CMDS] =
    {
        {"PRIVMSG", &HandlePrivMsg},
        {"NOTICE", &HandleNotice},
        {"JOIN", &HandleChannelJoinPart},
        {"PART", &HandleChannelJoinPart},
        {"NICK", &HandleUserNickChange},
        {"QUIT", &HandleUserQuit},
        {"353", &HandleChannelNamesList},
        {"433", &HandleNicknameInUse},
        {"001", &HandleServerMessage},
        {"002", &HandleServerMessage},
        {"003", &HandleServerMessage},
        {"004", &HandleServerMessage},
        {"005", &HandleServerMessage},
        {"250", &HandleServerMessage},
        {"251", &HandleServerMessage},
        {"252", &HandleServerMessage},
        {"253", &HandleServerMessage},
        {"254", &HandleServerMessage},
        {"255", &HandleServerMessage},
        {"265", &HandleServerMessage},
        {"266", &HandleServerMessage},
        {"366", &HandleServerMessage},
        {"372", &HandleServerMessage},
        {"375", &HandleServerMessage},
        {"376", &HandleServerMessage},
        {"439", &HandleServerMessage},
};

int GetCommandHandler(string command)
{
    for (int i = 0; i < NUM_IRC_CMDS; i++)
    {
        if (ircCommandTable[i].command == command)
            return i;
    }
    return NUM_IRC_CMDS;
}

void CallHook(string command, IRCMessage message)
{
    if (_hooks.empty())
        return;
    for (list<IRCCommandHook>::const_iterator itr = _hooks.begin(); itr != _hooks.end(); ++itr)
    {
        if (itr->command == command)
        {
            (*(itr->function))(message);
            break;
        }
    }
}


void signalHandler(int sig)
{
    _running = false;
}

bool InitSocket()
{
    if ((_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) == INVALID_SOCKET)
    {
        cout << "Invalid error" << endl;
        return false;
    }

    int on = 1;
    if (setsockopt(_socket, SOL_SOCKET, SO_REUSEADDR, (char const *)&on, sizeof(on)) == -1)
    {
        cout << "Invalid socket" << endl;
    }

    fcntl(_socket, F_SETFL, O_NONBLOCK);
    fcntl(_socket, F_SETFL, O_ASYNC);
    return true;
}

bool Connect(char const *host, int port)
{
    hostent *he;
    if (!(he = gethostbyname(host)))
    {
        cout << "Could not resolve host" << host << endl;
        return false;
    }
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr = *((const in_addr *)he->h_addr_list[0]);
    memset(&(addr.sin_zero), '\0', 8);

    if (connect(_socket, (sockaddr *)&addr, sizeof(addr)) == -1)
    {
        cout << "Could not connect to" << host << endl;
        close(_socket);
        return false;
    }
    _connected = true;
    return true;
}

bool Login(string nick, string user, string password = string())
{
    _nick = nick;
    _user = user;
    if (SendIRC("HELLO"))
    {
        if (!password.empty() && !SendIRC("PASS "+password))
            return false;
        if (SendIRC("NICK " + nick))
            if (SendIRC("USER " + user + " 8 * :Cpp IRC Client"))
                return true;
    }

    return false;
}

void Parse(string data)
{
    string original(data);
    IRCCommandPrefix cmdPrefix;

    if (data.substr(0, 1) == ":")
    {
        cmdPrefix.Parse(data);
        data = data.substr(data.find(" ") + 1);
    }
    string command = data.substr(0, data.find(" "));
    transform(command.begin(), command.end(), command.begin(), towupper);
    if (data.find(" ") != string::npos)
        data = data.substr(data.find(" ") + 1);
    else
        data = "";

    vector<string> parameters;

    if (data != "")
    {
        if (data.substr(0, 1) == ":")
            parameters.push_back(data.substr(1));
        else
        {
            size_t pos1 = 0, pos2;
            while ((pos2 = data.find(" ", pos1)) != string::npos)
            {
                parameters.push_back(data.substr(pos1, pos2 - pos1));
                pos1 = pos2 + 1;
                if (data.substr(pos1, 1) == ":")
                {
                    parameters.push_back(data.substr(pos1 + 1));
                    break;
                }
            }
            if (parameters.empty())
                parameters.push_back(data);
        }
    }

    if (command == "ERROR")
    {
        cout << data << endl;
        cout << original << endl;
        Disconnect();
        return;
    }

    if (command == "PING")
    {
        cout << "Ping? Pong!" << endl;
        SendIRC("PONG : " + parameters.at(0));
        return;
    }

    IRCMessage ircMessage(command, cmdPrefix, parameters);

    int commandIndex = GetCommandHandler(command);
    if (commandIndex < NUM_IRC_CMDS)
    {
        IRCCommandHandler &cmdHandler = ircCommandTable[commandIndex];
        cmdHandler.handler(ircMessage);
    }
    CallHook(command, ircMessage);
}
void ReceiveData()
{
    char buffer[4096];
    string buff, line;
    memset(buffer, 0, 4096);

    int bytes = recv(_socket, buffer, 4095, 0);
    if (bytes > 0)
    {
        buff = string(buffer);
    }
    else
    {
        Disconnect();
        buff = "";
    }
    istringstream iss(buff);
    while (getline(iss, line))
    {
        if (line.find("\r") != string::npos)
            line = line.substr(0, line.size() - 1);
        Parse(line);
    }
}

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        cout << "Insuficient parameters: host port [nick] [user]" << endl;
        return 1;
    }
    char* host = argv[1];
    int port = atoi(argv[2]);
    string nick("MyIRCClient");
    string user("IRCClient");
    if (argc >= 4)
    {
        nick = argv[3];
    }

    if (argc >= 5)
    {
        user = argv[4];
    }

    Thread thread;
    thread.Start(&inputThread, NULL);
    if (InitSocket())
    {
        cout << "Socket initialized. Connecting ... " << endl;
        if (Connect(host, port))
        {
            cout << "Connected. Loggin in ..." << endl;
            if (Login(nick, user))
            {
                cout << "Logged." << endl;
                _running = true;
                signal(SIGINT, signalHandler);

                while (_connected && _running){
                    // cout << "Receiving data" << endl;
                    ReceiveData();
                }
            }
            if (_connected)
                Disconnect();
            cout << "Disconnected" << endl;
        }
    }
}
