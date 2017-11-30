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
    int num_params;
    char *p_arr[2] = {NULL};

	num_params = parse_params(params, p_arr, 2);

    SEND_CMD(CMD_NEW_GAME);
    while (wait_for_board(CMD_STATUS) != STATUS_OKAY) {
        Log.error("board failed to start new game, trying again...");
        SEND_CMD(CMD_NEW_GAME);
    }
    Log.info("board calibrated and ready for new game");

    if (num_params > 1) {
        player_type = *(p_arr[0]) == AI ? AI : HUMAN;
        if (player_type == HUMAN) {
            const char *opponent_type = *(p_arr[1]) == AI ? "ai" : "human";

            if (http.ok(create_new_game("human", opponent_type))) {
                set_gid_pid(response.body, "{id:%Q, player1:{id:%Q}}");
                return 0;
            }
            Log.error("failed to post new game");
        } else if (player_type == AI) {
            join_first_available_game();
            return 0;
        }
    }
    return -1;
}

/*
 *  wait for '0' from board indicating "ok"
 *  returns -1 if timeout, non-zero if fail, 0 if ok
 */
int wait_for_board(int expected) {
    #define DONE(r) do {ret = (r); goto out;} while(0)
    int ret = -1;
    waiting_for_board = true;
    unsigned int timeout = millis() + BOARD_TIMEOUT; // timeout after 10s
    while (!Serial1.available()) {
        if (millis() > timeout) {
            Log.error("timed out waiting for board");
            DONE(-1);
        }
    }

    // (austin) WAR: with some delay, Serial1.available() returns the proper
    // number of bytes the board sent. Without the delay, Serial.available()
    // only returns 1 byte and fails...
    delay(10);

    char rx_buffer[SERIAL_BUFF_SIZE];
    int cmd;
    int cmd_ret;
    timeout = millis() + BOARD_TIMEOUT;
    while (millis() < timeout) {
        if (Serial1.available()) {
            char c = Serial1.read();
            cmd = rx_serial_command_r(c, rx_buffer, SERIAL_BUFF_SIZE, &cmd_ret);
            if (cmd == CONTINUE) {
                // keep rx'ing bytes
            } else if (cmd == expected) {
                if (cmd_ret < 0) { // param parsing error
                    Log.error("command %d failed to parse parameters, cmd_ret=%d", cmd_ret);
                    DONE(-1);
                }
                DONE(cmd_ret);
            } else if (cmd == FAIL) {
                Log.error("serial failed to rx command with error %d", cmd);
                DONE(-1);
            }
            // else rx'd different command
        }
    }

out:
    waiting_for_board = false;
    return ret;
}

int do_end_turn(char *params) {
    // just forward to M0
    if (params)
        SEND_CMD_P(CMD_END_TURN, "%s", params);
    else
        SEND_CMD(CMD_END_TURN);
    return 0;
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
                player_turn = false;
                return 0;
            }
            Log.error("Failed to post move");
            return -1;
        }
        return 99;
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
    clear_gid_pid();
    return 0;
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

// params:
//      c = capture
//      k = castle
int do_capture_castle(char *params) {
    // just forward to M0
    if (params)
        SEND_CMD_P(CMD_CAPTURE_CASTLE, "%s", params);
    else
        SEND_CMD(CMD_CAPTURE_CASTLE);
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
