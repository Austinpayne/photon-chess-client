#include <HttpClient.h>
#include "frozen.h"

#define TESTING

/*
 * chess-http-client
 * Author: Austin Payne
 * Date: 10/01/2017
 */
SYSTEM_MODE(SEMI_AUTOMATIC); // don't connect to Spark cloud until Particle.connect()
SYSTEM_THREAD(ENABLED); // run spark procs and application procs in parrallel

//#define SERVER "172.16.93.61" // home
#define SERVER "192.168.33.14"  // uknowthatsright
#define PORT   3000
#define RES_SIZE     1024
#define HEADERS_SIZE 16
#define COORD_VALID(c) ((c) < 8)
#define AI 0
#define HUMAN 1
#define PLAYER_TYPE AI // TODO: should be selectable

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
SerialLogHandler logHandler(LOG_LEVEL_INFO, {{"app", LOG_LEVEL_ALL}});

/*
 *  validates that move is of the form:
 *      [a-h][1-8][a-h][1-8]
 */
bool valid_move(uint8_t *move, size_t size) {
    if (size == 4) {
        unsigned char src_x, src_y, dst_x, dst_y;
        src_x = move[0]-'a';
	    src_y = move[1]-'1';
	    dst_x = move[2]-'a';
	    dst_y = move[3]-'1';
	    return (COORD_VALID(src_x) && COORD_VALID(src_y) && COORD_VALID(dst_x) && COORD_VALID(dst_y));
	    return false;
    }

    return false;
}

int join_game(const char *gid, const char *player_type) {
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

int move_piece(char *move) {
    Serial1.printf("%.4s", move);
    return wait_for_board();
}

int move(char *move) {
    // print 4 characters to chess robot
    if (PLAYER_TYPE == AI)
        Serial1.printf("%.4s", move);
    if (wait_for_board() == 0) {
        post_move(gid, pid, move);
        return response.status;
    }
    Log.error("could not make move");
    return -1;
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
 *  wait for '0' from board indicating "ok"
 *  returns -1 if timeout, non-zero if fail, 0 if ok
 */
int wait_for_board() {
    #ifdef TESTING
    return 0;
    #endif
    unsigned int timeout = millis() + 10000; // timeout after 10s
    while (!Serial1.available()) {
        Log.trace("waiting for board...");
        delay(200); // wait
        if (timeout > millis()) {
            Log.error("timed out waiting for board");
            return -1;
        }
    }
    return Serial1.read();
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

void init_serial(void) {
    Serial.begin(9600);
    Serial1.begin(9600);
}

void setup() {
    init_serial();
    WiFi.on(); // needed when in semi-automatic mode
    WiFi.connect();
    waitFor(WiFi.ready, 20000);
    if (!WiFi.ready()) {
        Log.error("could not connect to wifi!");
        Log.error("stored networks:");
        Log.error("ssid\tsecurity\tcipher\t");
        WiFiAccessPoint ap[5];
        int found = WiFi.getCredentials(ap, 5);
        for (int i = 0; i < found; i++) {
            Log.error("%s\t%s\t%s", ap[i].ssid, ap[i].security, ap[i].cipher);
        }
    } else {
        Log.info("system ready");
    }
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
        }
        if (game_id) free(game_id);
    } else {
        Log.error("could not get first available game, response.status=%d", response.status);
    }
}

int game_is_over(const char *game_id) {
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
            if (http.ok(get_last_move(gid))) {
                char *last_move = get_json_str(response.body, "{last_move:%Q}");
                Log.info("updating board with last move...");
                if (move_piece(last_move) != 0) {
                    Log.error("bot could not move piece!");
                    if (last_move) free(last_move);
                    return;
                }
                Log.info("piece moved!");
                if (last_move) free(last_move);
            }
            Log.info("IT IS YOUR TURN! GO, GO, GO!");
            // get best move
            if (http.ok(get_bestmove(gid, pid))) {
                char *bestmove = get_json_str(response.body, "{bestmove:%Q}");
                Log.info("making bestmove");
                move(bestmove);
                if (bestmove) free(bestmove);
            }
        } else {
            // make sure board state is same, if not move board
            Log.info("not your turn, waiting...");
        }
    } else {
        Log.error("could not get turn, response.status=%d", response.status);
    }
}

void loop() {
    // have not joined game yet, join first available
    if (strlen(gid) == 0 || strlen(pid) == 0) {
        join_first_available_game();
    }
    // game joined, make best move while game is not over and it is your turn
    else {
        if (game_is_over(gid)) {
            memset(gid, 0, sizeof(gid));
            memset(pid, 0, sizeof(pid));
            return;
        }
        make_best_move();
    }

    delay(1000); // check every 1 seconds
}
