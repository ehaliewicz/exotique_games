#include "SDL_net.h"

// server listens to inputs
// builds up a queue of up to 10 inputs per second


// a command is 2 bytes

// 187 commands :)))))

typedef struct {
    int num_commands;
    action commands[40];
} packet;


#define NUM_PLAYERS 4

char* server_address = NULL;
static TCPsocket serv_sock = NULL;
SDLNet_SocketSet socket_set;
int num_socks_in_set = 0;
int num_connected_players = 0;

typedef struct {
    int is_host;
    //int player_num;
    int used;
    TCPSocket sock;
    //u8 name[256+1]; // whatever
} client_info[NUM_PLAYERS];

void init_client_info_for_host() {
    game_data[num_connected_players].player_type = HUMAN;
    client_info[num_connected_players].is_host = 1;
    client_info[num_connected_players++].used = 1;
}

void server_get_connection() {

    if(num_connected_players == NUM_PLAYERS) {
        LOG("NETWORK", "Already have enough clients");
        return;
    }

    TCPsocket new_sock = SDLNet_TCP_Accept(serv_sock);
    if(new_sock == NULL) {
        return;
    }

    disable_nagle(new_sock);



    client_info[num_connected_players].sock = new_sock;
    client_info[num_connected_players].peer = *SDLNet_TCP_GetPeerAddress(new_sock);

    game_data.player_types[num_connected_players++] = NETWORK_HUMAN;

    int added_sock = SDLNet_TCP_AddSocket(socket_set, client_info[which].sock);
    if(added_sock == -1) {
        LOG("NETWORK", "Error adding socket to set");
        exit(1);
    }
    num_socks_in_set = added_sock;
    LOG("NETWORK", "Client connected at %i %i", client_info[which].peer.host, client_info[which].peer.port);
}

static struct {
    int active;
    int player_num;
    TCPsocket sock;
    IPaddress peer; // host and port used to connect to server
    u16 listen_port; // port this client is listening on
    Uint8 name[256+1];
} client_info[NUM_PLAYERS];



/*
#include "SDL_net.h"

IPaddress server_ip;


#define MAKE_NUM(A, B, C, D)    (((A+B)<<8)|(C+D))

#define JONG_PORT MAKE_NUM('J','O','N','G')
#define JONG_PORT1 JONG_PORT+1
#define JONG_PORT2 JONG_PORT+2
#define JONG_PORT3 JONG_PORT+3
#define MAX_CLIENTS 3
static struct {
    int active;
    int player_num;
    TCPsocket sock;
    IPaddress peer; // host and port used to connect to server
    u16 listen_port; // port this client is listening on
    Uint8 name[256+1];
} client_info[MAX_CLIENTS];



typedef struct {
    int ready;
    SOCKET channel; // actual socket fd
    IPaddress remoteAddress;
    IPaddress localAddress;
    int sflag;
} internal_TCPsocket;

void disable_nagle(TCPsocket sock) {
    internal_TCPsocket *int_sock = (internal_TCPsocket*)sock;
    int opt = 1;
    int res = setsockopt(int_sock->channel, IPPROTO_TCP, TCP_NODELAY, (char*)&opt, sizeof(opt));
    if(res != 0) {
        int err = WSAGetLastError();
        LOG("NETWORK", "Error setting TCP_NODELAY on socket: %i", err);
        //exit(1);
    }
}

void server_get_connection() {
    TCPsocket new_sock;
    int which;

    new_sock = SDLNet_TCP_Accept(serv_sock);
    disable_nagle(new_sock);
    if(new_sock == NULL) {
        return;
    }

    // look for unconnected person slot
    for(which = 0; which < MAX_CLIENTS; which++) {
        if(!client_info[which].sock) {
            break;
        }
    }
    if(which == MAX_CLIENTS) {
        // another client is attempting to connect but we're already full
        LOG("NETWORK", "Already have enough clients");
        return;
    }

    client_info[which].sock = new_sock;
    client_info[which].peer = *SDLNet_TCP_GetPeerAddress(new_sock);
    client_info[which].player_num = -1;
    for(int i = 0; i < 4; i++) {
        if(game_data.player_types[i] == NETWORK_HUMAN) {
            int already_used = 0;
            for(int j = 0; j < MAX_CLIENTS; j++) {
                if(client_info[j].sock && client_info[j].player_num == i) {
                    already_used = 1;
                    break;
                }
            }
            if(already_used == 0) {
                client_info[which].player_num = i;
                break;
            }
        }
    }
    if(client_info[which].player_num == -1) {
        LOG("NETWORK", "All players are used even though we have a free network slot?");
        exit(1);
    }
    int added_sock = SDLNet_TCP_AddSocket(socket_set, client_info[which].sock);
    if(added_sock == -1) {
        LOG("NETWORK", "Error adding socket to set");
        exit(1);
    }
    num_socks_in_set = added_sock;
    LOG("NETWORK", "Client connected at %i %i", client_info[which].peer.host, client_info[which].peer.port);
}


void setup_host(int num_clients) {
    if(num_clients < 0 || num_clients > 3) {
        LOG("INIT", "Error: Please specify number of clients between 0 and 3, --num-clients [num]");
        exit(1);
    }
    
    game_data.player_types[0] = HUMAN;
    for(int i = 1; i <= num_clients; i++) {
        game_data.player_types[i] = NETWORK_HUMAN;
    }
    for(int i = num_clients+1; i < 4; i++) {
        game_data.player_types[i] = ai_select_order[i];
    }
    if(num_clients == 0) {
        return;  
    }
    socket_set = SDLNet_AllocSocketSet(num_clients+1);
    if(socket_set == NULL) {
        LOG("NETWORK", "Error: Couldn't create socket set: %s", SDLNet_GetError());
        exit(1);
    }

    SDLNet_ResolveHost(&server_ip, NULL, JONG_PORT);
    LOG("NETWORK", "Server IP: %x, %d", server_ip.host, SDLNet_Read16(&server_ip.port));
    serv_sock = SDLNet_TCP_Open(&server_ip);
    if ( serv_sock == NULL ) {
        LOG("NETWORK", "Error: Couldn't create server socket: %s",SDLNet_GetError());
        exit(1);
    }
    int added_sock = SDLNet_TCP_AddSocket(socket_set, serv_sock);
    if(added_sock == -1) {
        LOG("NETWORK", "Error adding socket to set");
        exit(1);
    }
    num_socks_in_set = added_sock;

    // wait until clients connect
    LOG("NETWORK", "Waiting for clients to connect to port %i", JONG_PORT);

    while(1) {
        SDLNet_CheckSockets(socket_set, ~0);
        if(SDLNet_SocketReady(serv_sock)) {
            server_get_connection();
        }
        int connected_clients = 0;
        for(int i = 0; i < MAX_CLIENTS; i++) {
            connected_clients += client_info[i].sock ? 1 : 0;
        }
        if(connected_clients == num_clients) {
             LOG("NETWORK", "All clients connected.");
            break;
        }
    }
    
    // remove server listen socket from socket set
    added_sock = SDLNet_TCP_DelSocket(socket_set, serv_sock);
    if(added_sock == -1) {
        LOG("NETWORK", "Error removing socket from set");
        exit(1);
    }
    num_socks_in_set = added_sock;
}


typedef enum __attribute__((packed)) {
    LISTEN_PORT = 1,
    RANDOM_SEED_AND_PLAYER_INFO = 3,
    INPUT_FROM_CLIENT = 5,
} msg_type;


IPaddress client_ip;
TCPsocket client_listen_sock;
u16 client_listen_port;
void setup_client(char* server_address) {
    socket_set = SDLNet_AllocSocketSet(4);
    if(socket_set == NULL) {
        LOG("NETWORK", "Error: Couldn't create socket set: %s", SDLNet_GetError());
        exit(1);
    }

    for(int listen_port = JONG_PORT+1; listen_port < JONG_PORT+100; listen_port++) {
        if(SDLNet_ResolveHost(&client_ip, NULL, listen_port) == 0) {
            client_listen_sock = SDLNet_TCP_Open(&client_ip);

            if(client_listen_sock != NULL) {
                LOG("NETWORK", "Listening on port %i:%i / %i", SDLNet_Read32(&client_ip.host), SDLNet_Read16(&client_ip.port), listen_port);
                client_listen_port = client_ip.port;
                break;
            }
        }
    }
    if(client_listen_sock == NULL) {
        LOG("NETWORK", "Error: Couldn't open listen socket");
        exit(1);
    }

    LOG("NETWORK", "Attempting to connect to server @ %s:%i", server_address, JONG_PORT);
    SDLNet_ResolveHost(&server_ip, server_address, JONG_PORT);
    if(server_ip.host == INADDR_NONE) {
        LOG("NETWORK", "Error: Couldn't resolve hostname");
        exit(1);
    }
    LOG("NETWORK", "Connecting to %s %i", server_address, JONG_PORT);
    while(1) {
        serv_sock = SDLNet_TCP_Open(&server_ip);
        if(serv_sock != NULL) {
            break;
        }
        LOG("NETWORK", "Failed, retrying");
        int counter = 1000000;
        while(counter) { counter--; }
    }
    disable_nagle(serv_sock);

    int added_sock = SDLNet_TCP_AddSocket(socket_set, serv_sock);
    if(added_sock == -1) {
        LOG("NETWORK", "Error adding socket to set");
        exit(1);
    }
    num_socks_in_set = added_sock;
}

typedef struct {
    int player_num;
    IPaddress ip; // address and port on which they are going to listen
} player_conn_info;

typedef struct {
    u32 seeds[4];
    u32 num_other_clients;
    player_conn_info clients_info[3];

    IPaddress your_ip; 
    int player_num;
} seed_and_player_info;

#define MAX_PACKET_SIZE (sizeof(seed_and_player_info)+1)

char data_buf[MAX_PACKET_SIZE];

void send_packet_to_socket(TCPsocket sock, msg_type type, u32 num_bytes_after_type, void* src_buf, const char* obj_type) {
    data_buf[0] = type;
    memcpy(data_buf+1, src_buf, num_bytes_after_type);
    int sent = SDLNet_TCP_Send(sock, &data_buf, num_bytes_after_type+1);
    if(sent < 0) {
        LOG("NETWORK", "Error: Disconnected while sending %s", obj_type);
        exit(1);
    }
    if(sent != (int)num_bytes_after_type+1) {
        LOG("NETWORK", "Error sending %s (of %i bytes), only sent %i bytes", obj_type, num_bytes_after_type+1, sent);
        exit(1);
    }
}

void receive_packet_from_socket(TCPsocket sock, msg_type type, u32 num_bytes_after_type, void* dst_buf, const char* obj_type) {
    int recvd = SDLNet_TCP_Recv(sock, data_buf, num_bytes_after_type+1);
    if(recvd < 0) {
        LOG("NETWORK", "Error: Disconnected while receiving %s :(", obj_type);
        exit(1);
    }
    if(recvd != (int)num_bytes_after_type+1) {
        LOG("NETWORK", "Error receiving %s (of %i bytes), only received %i bytes", obj_type, num_bytes_after_type+1, recvd);
        exit(1);
    }
    if(data_buf[0] != type) {
        LOG("NETWORK", "Got unexpected byte from client when waiting for %s: %i", obj_type, data_buf[0]);
        exit(1);
    }
    memcpy(dst_buf, data_buf+1, num_bytes_after_type);
}

typedef struct {
    u16 listen_port;
} listen_port_t;

void server_wait_for_listen_port_from_players() {
    
    for(int i = 0; i < MAX_CLIENTS; i++) {
        if(client_info[i].sock) {
            LOG("NETWORK", "Waiting for listen port from %i:%i", SDLNet_Read32(&client_info[i].peer.host), SDLNet_Read16(&client_info[i].peer.port));
            
            listen_port_t port;
            receive_packet_from_socket(client_info[i].sock, LISTEN_PORT, sizeof(listen_port_t), &port, "listen port");
            
            client_info[i].listen_port = port.listen_port;
            LOG("NETWORK", "Client is listening on %i", port.listen_port);
        }
    }
}

void client_send_listen_port_to_server() {
    listen_port_t dat; dat.listen_port = client_listen_port;
    send_packet_to_socket(serv_sock, LISTEN_PORT, sizeof(listen_port_t), &dat, "listen port");
}

void send_initial_state() {
    seed_and_player_info seed_info;

    int num_all_clients = 0;
    memcpy(seed_info.seeds, game_data.seeds, sizeof(u32)*4);

    for(int i = 0; i < MAX_CLIENTS; i++) {
        if(client_info[i].sock) {
            seed_info.clients_info[num_all_clients].ip = client_info[i].peer;
            seed_info.clients_info[num_all_clients].ip.port = client_info[i].listen_port;
            LOG("NETWORK", "Set listen port to %i", client_info[i].listen_port);
            seed_info.clients_info[num_all_clients++].player_num = client_info[i].player_num;
        }
    }
    seed_info.num_other_clients = num_all_clients;

    for(int i = 0; i < MAX_CLIENTS; i++) {
        if(client_info[i].sock) {
            seed_info.your_ip = client_info[i].peer;
            seed_info.your_ip.port = client_info[i].listen_port;
            seed_info.player_num = client_info[i].player_num;

            send_packet_to_socket(client_info[i].sock, RANDOM_SEED_AND_PLAYER_INFO, sizeof(seed_and_player_info), &seed_info, "initial state");
        }
    }
}

seed_and_player_info receive_initial_state() {
    LOG("GAME", "Waiting for initial state");

    seed_and_player_info start_info;    
    receive_packet_from_socket(serv_sock, RANDOM_SEED_AND_PLAYER_INFO, sizeof(seed_and_player_info), &start_info, "initial state");

    human_player = start_info.player_num;
    memcpy(&game_data.seeds, start_info.seeds, sizeof(u32)*4);

    return start_info;
}

SDLNet_SocketSet client_socket_set;
IPaddress client_to_client_ips[3];
TCPsocket client_to_client_sockets[3];

void setup_connections_to_other_clients(seed_and_player_info initial_state) {

    int our_index = -1;
    for(u32 i = 0; i < initial_state.num_other_clients; i++) {
        // sub-connections to wait for 
        
        if(initial_state.clients_info[i].player_num == initial_state.player_num) {
            // this is us!!
            our_index = i;
        }
    }

    int connections_to_open = our_index;

    int connections_to_listen_for = initial_state.num_other_clients-1 - our_index;

    // now setup player types
    game_data.player_types[0] = NETWORK_HUMAN;
    for(int i = 0; i < connections_to_open; i++) {
        game_data.player_types[i+1] = NETWORK_HUMAN;
    }
    game_data.player_types[1+connections_to_open] = HUMAN;
    
    for(int i = connections_to_open; i < connections_to_open+connections_to_listen_for; i++) {
        game_data.player_types[i+2] = NETWORK_HUMAN;
    }

    for(int i = connections_to_listen_for+connections_to_open+2; i < 4; i++) {
        game_data.player_types[i] = ai_select_order[i];;
    }

    LOG("NETWORK", "Connections to listen for %i, connections to open %i", connections_to_listen_for, connections_to_open);
    int connected_clients = 0;

    for(int i = initial_state.num_other_clients-1; i > our_index; i--) {
        // wait for connections from last client to next after our index
        while(1) {
            TCPsocket new_sock = SDLNet_TCP_Accept(client_listen_sock);
            if(new_sock == NULL) {
                // just try again
                continue;
            }
            disable_nagle(new_sock);

            IPaddress *ipptr = SDLNet_TCP_GetPeerAddress(new_sock);
            LOG("NETWORK", "Got p2p connection from %i:%i", SDLNet_Read32(&ipptr->host), SDLNet_Read16(&ipptr->port));
        

            client_info[connected_clients].sock = new_sock;
            int added_sock = SDLNet_TCP_AddSocket(socket_set, client_info[connected_clients].sock);
            if(added_sock == -1) {
                LOG("NETWORK", "Error adding socket to set");
                exit(1);
            }
            num_socks_in_set = added_sock;
            client_info[connected_clients++].player_num = initial_state.clients_info[i].player_num;
            break;
        }
    }
    for(int i = our_index-1; i >= 0; i--) {
        // open connections to clients from the one before us to 0
        IPaddress p2p_ip = initial_state.clients_info[i].ip;
        LOG("NETWORK", "Opening connection to %i:%i/%i", SDLNet_Read32(&p2p_ip.host), p2p_ip.port, SDLNet_Read16(&p2p_ip.port));

        TCPsocket p2p_sock = SDLNet_TCP_OpenClient(&p2p_ip);
        if(p2p_sock == NULL) {
            LOG("NETWORK", "Error: Couldn't open p2p socket to player %i: %s", initial_state.clients_info[i].player_num, SDLNet_GetError());
            exit(1);
        }
        disable_nagle(p2p_sock);

        LOG("NETWORK", "Opened p2p connection to player %i %i:%i", initial_state.clients_info[i].player_num, SDLNet_Read32(&p2p_ip.host), SDLNet_Read16(&p2p_ip.port));
        client_info[connected_clients].sock = p2p_sock;
        int added_sock = SDLNet_TCP_AddSocket(socket_set, client_info[connected_clients].sock);
        if(added_sock == -1) {
            LOG("NETWORK", "Error adding socket to set");
            exit(1);
        }
        num_socks_in_set = added_sock;
        client_info[connected_clients++].player_num = initial_state.clients_info[i].player_num;
    }

    // add server socket to client info
    client_info[connected_clients].player_num = 0;
    client_info[connected_clients].sock = serv_sock;
}

void client_send_input_to_all_clients() {
    player_action this_action;
    if(queue_size() == 0) {
        LOG("GAME", "BUG - Expected one entry (player input) in queue, but got 0");
        exit(1);
    } else {
        this_action = queue_peek();
    }

    for(int i = 0; i < MAX_CLIENTS; i++) {
        if(client_info[i].sock && client_info[i].player_num != human_player) {
            send_packet_to_socket(client_info[i].sock, INPUT_FROM_CLIENT, sizeof(player_action), &this_action, "action");
        }
    }
}

void client_wait_for_all_inputs() {
    player_action action;

    while(socket_set != NULL && SDLNet_CheckSockets(socket_set, 0)) {
        for(int i = 0; i < MAX_CLIENTS; i++) {
            if(client_info[i].sock && client_info[i].player_num != human_player) {
                if(!SDLNet_SocketReady(client_info[i].sock)) {
                    continue;
                }

                receive_packet_from_socket(client_info[i].sock, INPUT_FROM_CLIENT, sizeof(player_action), &action, "input");

                if(action.player_num == human_player){
                    LOG("NETWORK", "Error: Corrupted packet claims to be from this client");
                    exit(1);
                }
                LOG("NETWORK", "Got an action from player %i", action.player_num);
                queue_push(action);
            }
        }
    }
}
*/