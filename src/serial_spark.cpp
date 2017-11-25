#ifdef SPARK
#include <Particle.h>
#include <frozen.h>
#include <serial.h>
#include "serial_spark.h"
#include "chess-client.h"

void init_serial(void) {
    Serial.begin(9600);
    Serial1.begin(9600);
}

int do_new_game(char *params) {
    Log.info("starting new game");
    /*char body[64];

    int ptype = atoi(params);
    const char *player_type = ptype == AI ? "ai" : "human";
    struct json_out out = JSON_OUT_BUF(body, sizeof(body));

    json_printf(&out, "{player_type:%Q}", ptype);
    http_request_t request = {SERVER, PORT, "/game", body};
    http.post(request, response, default_headers);

    if (http.ok(response.status)) {
        set_gid_pid(response.body);*/
        SEND_CMD(CMD_NEW_GAME);
        while (wait_for_board(CMD_STATUS) != STATUS_OKAY){
            Log.error("board failed to start new game, trying again...");
            SEND_CMD(CMD_NEW_GAME);
        }
        Log.info("board calibrated and ready for new game");


    /*}
    return response.status;*/
    return 0;
}

/*
 *  wait for '0' from board indicating "ok"
 *  returns -1 if timeout, non-zero if fail, 0 if ok
 */
int wait_for_board(int expected) {
    unsigned int timeout = millis() + BOARD_TIMEOUT; // timeout after 10s
    while (!Serial1.available()) {
        if (millis() > timeout) {
            Log.error("timed out waiting for board");
            return -1;
        }
    }

    // (austin) WAR: with some delay, Serial1.available() returns the proper
    // number of bytes the board sent. Without the delay, Serial.available()
    // only returns 1 byte and fails...
    delay(10);

    char rx_buffer[SERIAL_BUFF_SIZE];
    int cmd;
    int *cmd_ret;
    timeout = millis() + BOARD_TIMEOUT;
    while (millis() < timeout) {
        if (Serial1.available()) {
            char c = Serial1.read();
            cmd = rx_serial_command_r(c, rx_buffer, SERIAL_BUFF_SIZE, cmd_ret);
            if (cmd == -1) {
                // keep rx'ing bytes
            } else if (cmd == expected) {
                if (*cmd_ret < 0) { // param parsing error
                    Log.error("command %d failed to parse parameters, cmd_ret=%d", *cmd_ret);
                    return -1;
                }
                return *cmd_ret;
            } else if (cmd == -2) {
                Log.error("serial failed to rx command with error %d", cmd);
                return -1;
            }
            // else rx'd different command
        }
    }

    Log.error("timed out waiting for board");
    return -1;
}

int do_end_turn(char *params) {
    Log.trace("Serial end turn cmd not implemented");
    return -1;
}

/*
 *  validates that move is of the form:
 *      [a-h][1-8][a-h][1-8]
 */
bool valid_move(const char *move) {
    if (move && strlen(move) == 4) {
        unsigned char src_x, src_y, dst_x, dst_y;
        src_x = move[0]-'a';
	    src_y = move[1]-'1';
	    dst_x = move[2]-'a';
	    dst_y = move[3]-'1';
	    return (COORD_VALID(src_x) && COORD_VALID(src_y) && COORD_VALID(dst_x) && COORD_VALID(dst_y));
    }

    return false;
}

int do_move_piece(char *params) {
    if (mode == 1) { // direct control
        Log.info("sending %s", params);
        SEND_CMD_P(CMD_MOVE_PIECE, "%s", params); // forward to M0
        while (wait_for_board(CMD_STATUS) != STATUS_OKAY) {
            Log.error("board failed to move piece, trying again...");
            SEND_CMD_P(CMD_MOVE_PIECE, "%s", params);
        }
        Log.info("board moved piece");
        return 0;
    } else {
        if (valid_move(params)) {
            if (http.ok(post_move(gid, pid, params))) {
                int err = 0;
                json_scanf(response.body, strlen(response.body), "{err_code:%d}", &err);
                if (err = 99) { // invalid move
                    char undo_move[5];
                    undo_move[0] = params[2];
                    undo_move[1] = params[3];
                    undo_move[2] = params[0];
                    undo_move[3] = params[1];
                    undo_move[0] = '\0';
                    SEND_CMD_P(CMD_MOVE_PIECE, "%.4s", params);
                    return err;
                }
                return 0;
            }
            Log.error("Failed to post move");
            return -1;
        }
    }
}

int do_promote(char *params) {
    Log.trace("Serial promote cmd not implemented");
    return -1;
}

int do_calibrate(char *params) {
    Log.trace("Calibrating");
    SEND_CMD(CMD_CALIBRATE);
    return 0;
}

int do_end_game(char *params) {
    Log.trace("Serial end game cmd not implemented");
    return -1;
}

int do_send_log(char *params) {
    #define MAX_PARAMS 1
    char *p_arr[MAX_PARAMS];
    int num_params = parse_params(params, p_arr, MAX_PARAMS);
    if (num_params != 1)
        return -1;
    Logger m0_log("app.m0");
    int lvl = atoi(p_arr[0]);
    switch (lvl) {
        case LVL_TRACE:
            m0_log.trace("%s", params+2);
            break;
        case LVL_INFO:
            m0_log.info("%s", params+2);
            break;
        case LVL_WARN:
            m0_log.warn("%s", params+2);
            break;
        case LVL_ERR:
            m0_log.error("%s", params+2);
            break;
    }
    return 0;
}

int do_scan_wifi(char *params) {
    Log.trace("Serial scan wifi cmd not implemented");
    return -1;
}

int do_set_wifi(char *params) {
    Log.trace("Serial set wifi cmd not implemented");
    return -1;
}
#endif
