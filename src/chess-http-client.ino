#include <HttpClient.h>
#include <frozen.h>
#include "Serial2/Serial2.h"
#include "serial_spark.h"
#include "chess-client.h"

/*
 * chess-http-client
 * Author: Austin Payne
 * Date: 10/01/2017
 */
SYSTEM_MODE(SEMI_AUTOMATIC); // don't connect to Spark cloud until Particle.connect()

#define RES_SIZE     1024
#define HEADERS_SIZE 16
#define SERVER_RETRY 1000 // ms

// http client variables
HttpClient http;
char response_buffer[RES_SIZE];
http_header_t headers[HEADERS_SIZE];
http_response_t response = {response_buffer, RES_SIZE, headers, HEADERS_SIZE};
http_header_t default_headers[] = {
    { "Content-Type", "application/json" },
    { "Accept", "application/json" },
    { "Cache-Control", "no-cache"},
    { NULL } // always terminate headers with NULL
};
// logging
SerialLogHandler logHandler(LOG_LEVEL_INFO, {{"app", LOG_LEVEL_ALL}, {"app.m0", LOG_LEVEL_ALL}});
// timer to check for commands from android

// serial buffers
char console_serial_buffer[SERIAL_BUFF_SIZE]; // android
int console_save_i;

// global gid, pid
char gid[48] = {0};
char pid[48] = {0};
// global flags
char player_type = HUMAN;
bool player_turn = false;
bool waiting_for_user = false;
int mode = 0;

/*
 *  posts new game to server
 */
int create_new_game(const char *player_type, const char *opponent_type) {
    char body[64];
    // create json:
    // {"player_type":"ai"|"human", "opponent_type": "ai"|"human"}
    struct json_out out = JSON_OUT_BUF(body, sizeof(body));
    json_printf(&out, "{player_type:%Q, opponent_type:%Q}", player_type, opponent_type);
    http_request_t request = {SERVER, PORT, "/game", body};
    http.post(request, response, default_headers);
    return response.status;
}

/*
 *  joins game specified by gid
 */
int join_game(const char *gid, int player_type) {
    char path[64], body[64];
    // create json:
    // {"player_type":"ai"|"human"}
    const char *ptype = player_type == AI ? "ai" : "human";
    struct json_out out = JSON_OUT_BUF(body, sizeof(body));
    json_printf(&out, "{player_type:%Q}", ptype);

    snprintf(path, 64, "/game/%s/join", gid);
    http_request_t request = {SERVER, PORT, path, body};
    http.post(request, response, default_headers);
    return response.status;
}

/*
 *  this should probably not be used as it could return
 *  a very large array of games (making it difficult to predict
 *  the size of buffer needed to hold the respone/json object)
 */
int get_games() {
    http_request_t request = {SERVER, PORT, "/games"};
    http.get(request, response, default_headers);
    return response.status;
}

/*
 *  gets first game needing opponent (iterator interface)
 */
int get_first_game() {
    // can use iter interface to get first game
    http_request_t request = {SERVER, PORT, "/games/needing-opponent/0"};
    http.get(request, response, default_headers);
    return response.status;
}

/*
 *  gets the current* players bestmove, according to stockfish game engine
 *  fails if it is not player_id's turn
 */
int get_bestmove(char *game_id, char *player_id) {
    char path[128]; // sha1 len = 40 chars
    snprintf(path, 128, "/game/%s/player/%s/bestmove", game_id, player_id);
    http_request_t request = {SERVER, PORT, path};
    http.get(request, response, default_headers);
    return response.status;
}

/*
 *  gets the last move that was made (by other player)
 */
int get_last_move(char *game_id) {
    char path[64]; // sha1 len = 40 chars
    snprintf(path, 64, "/game/%s/last-move", game_id);
    http_request_t request = {SERVER, PORT, path};
    http.get(request, response, default_headers);
    return response.status;
}

/*
 *  checks if it is this players turn
 */
int check_turn(const char *game_id, const char *player_id) {
    char path[128]; // sha1 len = 40 chars
    snprintf(path, 128, "/game/%s/player/%s/turn", game_id, player_id);
    http_request_t request = {SERVER, PORT, path};
    http.get(request, response, default_headers);

    if (http.ok(response.status)) {
        return get_json_boolean(response.body, "{turn:%B}");
    } else {
        LOG_ERR("could not get turn, response.status=%d", response.status);
        return -1;
    }
}

/*
 *  check if game is over
 */
int game_is_over(const char *game_id) {
    char path[64]; // sha1 len = 40 chars
    snprintf(path, 64, "/game/%s/game-over", game_id);
    http_request_t request = {SERVER, PORT, path};
    http.get(request, response, default_headers);

    if (http.ok(response.status)) {
        return get_json_boolean(response.body, "{game_over:%B}");
    } else {
        LOG_ERR("could not check if game is over, response.status=%d", response.status);
        return -1;
    }
}

/*
 *  get result of game (*, "1/2-1/2", "0-1", or "1-0")
 */
int get_game_result(const char *game_id) {
    char path[64]; // sha1 len = 40 chars
    snprintf(path, 64, "/game/%s/result", game_id);
    http_request_t request = {SERVER, PORT, path};
    http.get(request, response, default_headers);
    return response.status;
}

/*
 *  posts move to server
 */
int post_move(const char *game_id, const char *player_id, const char *move) {
    char path[128], body[64];
    // create json:
    // {"move:""}
    struct json_out out = JSON_OUT_BUF(body, sizeof(body));
    json_printf(&out, "{move:%Q}", move);

    LOG_INFO("posting move with body: %s", body);

    snprintf(path, 128, "/game/%s/player/%s/move", game_id, player_id);
    http_request_t request = {SERVER, PORT, path, body};
    http.post(request, response, default_headers);
    return response.status;
}

/*
 *  sends move to stm32 via serial
 */
void send_move(const char *move, const char *flags, const char *extra) {
    LOG_INFO("move valid, sending %.4s", move);
    SEND_CMD_START(CMD_MOVE_PIECE, "%.4s", move);

    if (strlen(flags) != 0) {
        LOG_INFO("flags %.4s", flags);
        SEND_CMD_PARAM("%.4s", flags);
    }
    if (extra) {
        LOG_INFO("extra %s", extra);
        SEND_CMD_PARAM("%s", extra);
    }

    SEND_CMD_END();
}

/*
 *  if mvoe is valid, send it to stm32
 */
int move_piece(const char *move, const char *flags, const char *extra) {
    if (valid_move(move)) {
        send_move(move, flags, extra);
        while (wait_for_board(CMD_STATUS) != STATUS_OKAY) {
            LOG_ERR("board failed to move piece, trying again");
            send_move(move, flags, extra);
        }
        player_turn = false;
        LOG_INFO("board moved piece");
        return 0;
    }
    LOG_ERR("move invalid");
    return -1;
}

/*
 *  parses move json, sets appropriate flags, then issues move_piece to stm32
 */
int move(const char *color, const char *move, const char *flags) {
    int ret;
    char *extra = NULL;
    char serial_flags[4]; // max 2 flags (pc)
    bool set_color = false;
    if (!move) {
        LOG_ERR("couldn't get move!");
        return -1;
    }
    if (flags) {
        int i = 0;
        if (strchr(flags, 'c')) { // capture
            serial_flags[i++] = 'c';
            set_color = true;
        }
        // only one of the following flags can be set at a time
        // but p and c can be set together (pawn capture into final rank)
        if (strchr(flags, 'k') || strchr(flags, 'q')) { // castling
            extra = get_json_str(response.body, "{extra_move:%Q}");
            serial_flags[i++] = 'k';
        } else if (strchr(flags, 'e')) { // en passant
            extra = get_json_str(response.body, "{en_passant:%Q}");
            serial_flags[i++] = 'e';
            set_color = true;
        } else if (strchr(flags, 'p')) { // promote
            extra = get_json_str(response.body, "{promotion:%Q}");
            serial_flags[i++] = 'p';
            set_color = true;
        }

        // need color for capture or promotion
        if (set_color && color) {
            serial_flags[i++] = *color;
        }

        serial_flags[i] = '\0';
    }
    LOG_INFO("updating board with move %s", move);
    ret = move_piece(move, serial_flags, extra);
    if (extra) free(extra);
    return ret;
}

/*
 *  clears global pid and pid
 */
int clear_gid_pid() {
    waiting_for_user = false;
    player_turn = false;
    memset(gid, 0, sizeof(gid));
    memset(pid, 0, sizeof(pid));
    player_type = HUMAN;
    return 0;
}

/*
 *  sets global pid and pid
 *  pid and gid are sha1 (40 byte) hashes
 */
int set_gid_pid(char *gid_pid_json, const char *fmt) {
    char *joined_game_id = NULL;
    char *player_id = NULL;
    json_scanf(gid_pid_json, strlen(gid_pid_json), fmt, &joined_game_id, &player_id);
    if (joined_game_id && player_id) {
        LOG_TRACE("got gid: %s  and pid:%s from %s", joined_game_id, player_id, gid_pid_json);
        strcpy(gid, joined_game_id);
        strcpy(pid, player_id);
        LOG_INFO("game id: %s", gid);
        LOG_INFO("your player id: %s", pid);
        free(joined_game_id);
        free(player_id);
        return 0;
    }
    LOG_ERR("could not set gid and pid");
    return -1;
}

/*
 *  gets a single json string  given fmt of type
 *      {key:%Q}
 *  WARNING: the returned str is malloc'd by json_scanf and must be freed by caller!
 */
char *get_json_str(char *json, const char *fmt) {
    char *str = NULL;
    json_scanf(json, strlen(json), fmt, &str);
    LOG_TRACE("got json string %s from %s", str, json);
    return str;
}

/*
 *  gets a single json boolean (int) given fmt of type
 *      {key:%B}
 */
int get_json_boolean(char *json, const char *fmt) {
    int b = 0;
    json_scanf(json, strlen(json), fmt, &b);
    LOG_TRACE("got json boolean %d from %s", b, json);
    return b;
}

/*
 *  joins first available game. If not game is available,
 *  continues checking server until one is available.
 */
void join_first_available_game() {
    while(1) {
        LOG_INFO("attempting to join first available game...");
        // get first available game
        if (http.ok(get_first_game())) {
            char *game_id = get_json_str(response.body, "{id:%Q}");
            LOG_INFO("joining game %s", game_id);
            // join game
            if (game_id && http.ok(join_game(game_id, player_type))) {
                LOG_INFO("joined game!");
                set_gid_pid(response.body, "{id:%Q, player2:{id:%Q}}");
                SEND_CMD(CMD_NEW_GAME);
                while (wait_for_board(CMD_STATUS) != STATUS_OKAY) {
                    LOG_ERR("board failed to start new game, trying again...");
                    SEND_CMD(CMD_NEW_GAME);
                }
                LOG_INFO("board calibrated and ready for new game");
                if (game_id) free(game_id);
                return;
            }
            if (game_id) free(game_id);
        } else {
            LOG_ERR("could not get first available game, response.status=%d", response.status);
        }

        delay(SERVER_RETRY);
    }
}

/*
 *  joins first available game. If not game is available,
 *  continues checking server until one is available.
 */
void print_game_result(const char *game_id) {
    if (http.ok(get_game_result(game_id))) {
        LOG_INFO("%s", response.body);
    }
    // clear gid and pid so client can start another game
    LOG_INFO("game is over, please join another game");
}

/*
 *  main game loop:
 *  1. checks if it is current players turn
 *      a. if it is, updates board with last move made by other players
 *      b. if player_type is AI, gets bestmove from server and makes that move
 *      c. if player_type is HUMAN, waits for end turn from android
 */
void move_loop() {
    LOG_INFO("checking turn...");
    int my_turn = check_turn(gid, pid);
    if (my_turn > 0) {
        // "show" other players move on board
        char *fmt = "{move:%Q, flags:%Q, color:%Q}";
        char *mv, *flags, *color;
        if (!player_turn && http.ok(get_last_move(gid))) {
            char serial_flags[3];
            json_scanf(response.body, strlen(response.body), fmt, &mv, &flags, &color);
            move(color, mv, flags);
            if (mv)    free(mv);
            if (flags) free(flags);
            if (color) free(color);
            SEND_ANDROID_CMD(CMD_USER_TURN);
        }
        player_turn = true;
        LOG_INFO("IT IS YOUR TURN! GO, GO, GO!");
        // get best move
        if (player_type == AI && http.ok(get_bestmove(gid, pid))) {
            json_scanf(response.body, strlen(response.body), fmt, &mv, &flags, &color);
            if (move(color, mv, flags) == 0)
                post_move(gid, pid, mv);
            if (mv)    free(mv);
            if (flags) free(flags);
            if (color) free(color);
        } else if (player_type == HUMAN) {
            // wait for player_turn flag
            waiting_for_user = true;
        }
    } else if (my_turn == 0) {
        LOG_INFO("not your turn, waiting...");
    }
}

/*
 *  set direct control at bootup
 */
int set_mode() {
    int seconds = 10;
    unsigned int timeout = millis() + seconds*1000; // timeout after 10s
    while (!MAIN_SERIAL.available()) {
        LOG_INFO("Press any key for direct board control %d", seconds);
        delay(1000); // wait
        if (millis() > timeout)
            return 0;
        seconds -= 1;
    }
    char c = MAIN_SERIAL.read();
    return 1;
}

/*
 *  check for serial commands from android and stm32
 */
void check_serial() {
    int cmd;
    while (MAIN_SERIAL.available()) { // read commands from android
        char c = MAIN_SERIAL.read();
        if (mode == 1)
            MAIN_SERIAL.print(c);
        if (c != 127) // backspace
            cmd = rx_serial_command_r(c, console_serial_buffer, &console_save_i, SERIAL_BUFF_SIZE, NULL);
    }
}

void setup() {
    init_serial();
    WiFi.on(); // needed when in semi-automatic mode
    WiFi.connect();
    mode = set_mode(); // send 1 for direct control
    waitFor(WiFi.ready, 10000);
    if (!WiFi.ready()) {
        LOG_ERR("could not connect to wifi!");
        LOG_ERR("stored networks:");
        LOG_ERR("ssid\tsecurity\tcipher");
        WiFiAccessPoint ap[5];
        int found = WiFi.getCredentials(ap, 5);
        for (int i = 0; i < found; i++) {
            MAIN_SERIAL.print(ap[i].ssid); MAIN_SERIAL.print("\t");
            MAIN_SERIAL.print(ap[i].security); MAIN_SERIAL.print("\t");
            MAIN_SERIAL.println(ap[i].cipher);
        }
    } else {
        LOG_INFO("system ready");
        IPAddress ip = WiFi.localIP();
        LOG_INFO("IP: %u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
        ip = WiFi.subnetMask();
        LOG_INFO("Subnet: %u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
        ip = WiFi.gatewayIP();
        LOG_INFO("Gateway: %u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    }

    if (mode == 1) {
        LOG_INFO("Starting direct control");
    } else {
        LOG_INFO("Starting game mode");
    }

    while (MAIN_SERIAL.read() >= 0); // flush any data in read buffers
    while (Serial1.read() >= 0);
}

void loop() {
    if (mode == 1) {
        // direct control
        check_serial();
        return;
    }

    if (strlen(gid) == 0 || strlen(pid) == 0) {
        // waiting to start new game
        check_serial();
        return;
    }
    // main game loop
    // game joined, make best move while game is not over and it is your turn
    else if (game_is_over(gid)) {
        print_game_result(gid);
        clear_gid_pid();
    } else if (waiting_for_user) {
        check_serial();
    } else {
        move_loop();
    }

    delay(SERVER_RETRY);
}
