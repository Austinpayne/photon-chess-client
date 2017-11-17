#include <HttpClient.h>
#include <frozen.h>
#include <serial.h>
#include "serial_spark.h"
#include "chess-client.h"

//#define TESTING

/*
 * chess-http-client
 * Author: Austin Payne
 * Date: 10/01/2017
 */
SYSTEM_MODE(SEMI_AUTOMATIC); // don't connect to Spark cloud until Particle.connect()
SYSTEM_THREAD(ENABLED); // run spark procs and application procs in parrallel

#define SERVER "174.52.149.107"
#define PORT   30300
#define RES_SIZE     1024
#define HEADERS_SIZE 16
#define AI 0
#define HUMAN 1
#define PLAYER_TYPE AI // TODO: should be selectable
#define SERVER_RETRY 1000 // ms

HttpClient http;
char response_buffer[RES_SIZE];
http_header_t headers[HEADERS_SIZE];
http_response_t response = {response_buffer, RES_SIZE, headers, HEADERS_SIZE};
char gid[48] = {0};
char pid[48] = {0};
http_header_t default_headers[] = {
    { "Content-Type", "application/json" },
    { "Accept", "application/json" },
    { "Cache-Control", "no-cache"},
    { NULL } // always terminate headers with NULL
};
SerialLogHandler logHandler(LOG_LEVEL_INFO, {{"app", LOG_LEVEL_ALL}, {"app.m0", LOG_LEVEL_ALL}});
char console_serial_buffer[SERIAL_BUFF_SIZE];
char stm_serial_buffer[SERIAL_BUFF_SIZE];

int mode = 0;

int join_game(const char *gid, int player_type) {
    char path[64], body[64];
    // create json:
    // {"player_type":"ai"|"human", "opponent_type": "ai"|"human"}
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
 *  gets first game needing opponent_type
 */
int get_first_game() {
    // can use iter interface to get first game
    http_request_t request = {SERVER, PORT, "/games/needing-opponent/0"};
    http.get(request, response, default_headers);
    return response.status;
}

int get_bestmove(char *game_id, char *player_id) {
    char path[128]; // sha1 len = 40 chars
    snprintf(path, 128, "/game/%s/player/%s/bestmove", game_id, player_id);
    http_request_t request = {SERVER, PORT, path};
    http.get(request, response, default_headers);
    return response.status;
}

int get_last_move(char *game_id) {
    char path[64]; // sha1 len = 40 chars
    snprintf(path, 64, "/game/%s/last-move", game_id);
    http_request_t request = {SERVER, PORT, path};
    http.get(request, response, default_headers);
    return response.status;
}

int get_turn(const char *game_id, const char *player_id) {
    char path[128]; // sha1 len = 40 chars
    snprintf(path, 128, "/game/%s/player/%s/turn", game_id, player_id);
    http_request_t request = {SERVER, PORT, path};
    http.get(request, response, default_headers);
    return response.status;
}

int get_game_over(const char *game_id) {
    char path[64]; // sha1 len = 40 chars
    snprintf(path, 64, "/game/%s/game-over", game_id);
    http_request_t request = {SERVER, PORT, path};
    http.get(request, response, default_headers);
    return response.status;
}

int get_game_result(const char *game_id) {
    char path[64]; // sha1 len = 40 chars
    snprintf(path, 64, "/game/%s/result", game_id);
    http_request_t request = {SERVER, PORT, path};
    http.get(request, response, default_headers);
    return response.status;
}

int post_move(const char *game_id, const char *player_id, const char *move) {
    char path[128], body[64];
    // create json:
    // {"move:""}
    struct json_out out = JSON_OUT_BUF(body, sizeof(body));
    json_printf(&out, "{move:%Q}", move);

    Log.info("posting move with body: %s", body);

    snprintf(path, 128, "/game/%s/player/%s/move", game_id, player_id);
    http_request_t request = {SERVER, PORT, path, body};
    http.post(request, response, default_headers);
    return response.status;
}

int move_piece(const char *color, const char *move, const char *flags, const char *extra) {
    char fmt[32] = "%.4s";
    if (valid_move(move)) {
        Log.info("move valid, sending %.4s", move);
        SEND_CMD_START(CMD_MOVE_PIECE, "%.4s", move);

        if (flags && color) {
            SEND_CMD_PARAM("%.2s%.1s", flags, color);
        } else if (flags && !color) {
            SEND_CMD_PARAM("%.2s", flags);
        } else if (!flags && color) {
            SEND_CMD_PARAM("%.1s", color);
        }
        if (extra) {
            SEND_CMD_PARAM("%.4s", extra);
        }

        SEND_CMD_END();

        int expected = CMD_STATUS;
        if (wait_for_board(&expected) == 0) {
            Log.info("board moved piece");
            return 0;
        }
        Log.error("board failed to move piece");
        return -1;
    }
    Log.error("move invalid");
    return -1;
}

int move(const char *color, const char *move, const char *flags) {
    int ret;
    char *extra;
    char serial_flags[3]; // max 2 flags (pc)
    if (!move) {
        Log.error("couldn't get move!");
        return -1;
    }
    if (flags) {
        int i = 0;
        if (strchr(flags, 'c')) { // capture
            serial_flags[i++] = 'c';
        }

        // only one of the following flags can be set at a time
        // but p and c can be set together (pawn capture into final rank)
        if (strchr(flags, 'k') || strchr(flags, 'q')) { // castling
            extra = get_json_str(response.body, "{extra_move:%Q}");
            serial_flags[i++] = 'k';
        } else if (strchr(flags, 'e')) { // en passant
            extra = get_json_str(response.body, "{en_passant:%Q}");
            serial_flags[i++] = 'e';
        } else if (strchr(flags, 'p')) { // promote
            extra = get_json_str(response.body, "{promotion:%Q}");
            serial_flags[i++] = 'p';
        }

        serial_flags[i] = '\0';
    }
    Log.info("updating board with move %s", move);
    ret = move_piece(color, move, flags, extra);
    if (extra) free(extra);
    return ret;
}

int clear_gid_pid() {
    memset(gid, 0, sizeof(gid));
    memset(pid, 0, sizeof(pid));
    return 0;
}

/*
 *  sets global pid and pid:
 */
int set_gid_pid(char *gid_pid_json) {
    char *joined_game_id = NULL;
    char *player2_id = NULL;
    json_scanf(gid_pid_json, strlen(gid_pid_json), "{id:%Q, player2:{id:%Q}}", &joined_game_id, &player2_id);
    if (joined_game_id && player2_id) {
        Log.trace("got gid: %s  and pid:%s from %s", joined_game_id, player2_id, gid_pid_json);
        strcpy(gid, joined_game_id);
        strcpy(pid, player2_id);
        Log.info("game id: %s", gid);
        Log.info("your player id: %s", pid);
        free(joined_game_id);
        free(player2_id);
        return 0;
    }
    Log.error("could not set gid and pid");
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
    Log.trace("got json string %s from %s", str, json);
    return str;
}

/*
 *  gets a single json boolean (int) given fmt of type
 *      {key:%B}
 */
int get_json_boolean(char *json, const char *fmt) {
    int b = 0;
    json_scanf(json, strlen(json), fmt, &b);
    Log.trace("got json boolean %d from %s", b, json);
    return b;
}

void join_first_available_game() {
    Log.info("attempting to join first available game...");
    // get first available game
    if (http.ok(get_first_game())) {
        char *game_id = get_json_str(response.body, "{id:%Q}");
        Log.info("joining game %s", game_id);
        // join game
        if (game_id && http.ok(join_game(game_id, PLAYER_TYPE))) {
            Log.info("joined game!");
            set_gid_pid(response.body);
            int ready = -1;
            while (ready != 0) {
                SEND_CMD(CMD_NEW_GAME);
                int expected = CMD_STATUS;
                ready = wait_for_board(&expected);
            }
            Log.info("board calibrated and ready for new game");
        }
        if (game_id) free(game_id);
    } else {
        Log.error("could not get first available game, response.status=%d", response.status);
    }
}

unsigned char game_is_over(const char *game_id) {
    if (http.ok(get_game_over(game_id))) {
        int game_over = get_json_boolean(response.body, "{game_over:%B}");
        if (game_over) {
            if (http.ok(get_game_result(game_id))) {
                Log.info("%s", response.body);
            }
            // clear gid and pid so client can start another game
            Log.info("game is over, please join another game");
            return 1;
        }
    }
    return 0;
}

void make_best_move() {
    Log.info("checking turn...");
    if (http.ok(get_turn(gid, pid))) {
        // parse turn
        int my_turn = get_json_boolean(response.body, "{turn:%B}");
        if (my_turn) {
            // "show" other players move on board
            char *fmt = "{move:%Q, flags%Q, color:%Q}";
            char *mv, *flags, *color;
            if (http.ok(get_last_move(gid))) {
                char serial_flags[3];
                json_scanf(response.body, strlen(response.body), fmt, &mv, &flags, &color);
                move(color, mv, flags);
                if (mv)    free(mv);
                if (flags) free(flags);
                if (color) free(color);
            }
            Log.info("IT IS YOUR TURN! GO, GO, GO!");
            // get best move
            if (PLAYER_TYPE == AI && http.ok(get_bestmove(gid, pid))) {
                json_scanf(response.body, strlen(response.body), fmt, &mv, &flags, &color);
                if (move(color, mv, flags) == 0)
                    post_move(gid, pid, mv);
                if (mv)    free(mv);
                if (flags) free(flags);
                if (color) free(color);
            } else if (PLAYER_TYPE == HUMAN) {
                // TODO: wait for move from board
                // also need to check move object returned from server
                // and undo any invalid moves
                int expected = CMD_MOVE_PIECE;
                wait_for_board(&expected);
                while (expected == 99) { // invalid move, try again
                    expected = CMD_MOVE_PIECE;
                    wait_for_board(&expected);
                }
            }
        } else {
            // make sure board state is same, if not move board
            Log.info("not your turn, waiting...");
        }
    } else {
        Log.error("could not get turn, response.status=%d", response.status);
    }
}

int set_mode() {
    unsigned int timeout = millis() + 10000; // timeout after 10s
    while (!Serial.available()) {
        Log.info("Press any key for direct board control");
        delay(1000); // wait
        if (millis() > timeout) {
            Log.info("Starting game mode");
            return 0;
        }
    }
    char c = Serial.read();
    return 1;
}

void direct_control() {
    while (Serial.available()) { // read commands from direct control
        char c = Serial.read();
        Serial.print(c);
        if (c != 127) // backspace
            rx_serial_command_r(c, console_serial_buffer, SERIAL_BUFF_SIZE, NULL);
    }
    while (Serial1.available()) { // read commands from stm32
        char c = Serial1.read();
        rx_serial_command_r(c, stm_serial_buffer, SERIAL_BUFF_SIZE, NULL);
    }
}

void setup() {
    init_serial();
    WiFi.on(); // needed when in semi-automatic mode
    WiFi.connect();
    mode = set_mode(); // send 1 for direct control
    waitFor(WiFi.ready, 10000);
    if (!WiFi.ready()) {
        Log.error("could not connect to wifi!");
        Log.error("stored networks:");
        Log.error("ssid\tsecurity\tcipher");
        WiFiAccessPoint ap[5];
        int found = WiFi.getCredentials(ap, 5);
        for (int i = 0; i < found; i++) {
            Serial.print(ap[i].ssid); Serial.print("\t");
            Serial.print(ap[i].security); Serial.print("\t");
            Serial.println(ap[i].cipher);
        }
    } else {
        Log.info("system ready");
        Serial.println(WiFi.localIP());
        Serial.println(WiFi.subnetMask());
        Serial.println(WiFi.gatewayIP());
    }

    if (mode == 1) {
        Log.info("Starting direct control");
    }

    while (Serial.read() >= 0); // flush any data in read buffer
    while (Serial1.read() >= 0);
}

void loop() {
    if (mode == 1) {
        direct_control();
        return;
    }
    // have not joined game yet, join first available
    if (strlen(gid) == 0 || strlen(pid) == 0) {
        join_first_available_game();
    }
    // game joined, make best move while game is not over and it is your turn
    else {
        if (game_is_over(gid)) {
            clear_gid_pid();
            return;
        }
        make_best_move();
    }

    delay(SERVER_RETRY);
}
