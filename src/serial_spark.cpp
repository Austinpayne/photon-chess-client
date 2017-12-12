#ifdef SPARK
#include <Particle.h>
#include <frozen.h>
#include "serial_spark.h"
#include "chess-client.h"

char stm_serial_buffer[SERIAL_BUFF_SIZE];
int stm_save_i = 0;

void init_serial(void) {
    MAIN_SERIAL.begin(9600);
    Serial1.begin(9600);
}

/*
 *  param0 (player_type): ['h'|'a']?
 *  param1 (opponent_type): ['h'|'a']?
 *  default game (i.e. no params): player = HUMAN, opponent = AI
 */
int do_new_game(char *params) {
    LOG_INFO("starting new game");
    int num_params;
    char *p_arr[2];
	num_params = parse_params(params, p_arr, 2);

    player_type = HUMAN;
    char opponent_type = AI;
    switch(num_params) {
        case 2:
            opponent_type = (*(p_arr[1]) == HUMAN) ? HUMAN : AI;
        /* FALLTHROUGH */
        case 1:
            player_type = (*(p_arr[0]) == AI) ? AI : HUMAN;
            break;
    }

    SEND_CMD(CMD_NEW_GAME);
    LOG_TRACE("sent new game commnd");
    while (wait_for_board(CMD_STATUS, QUICK_TIMEOUT) != STATUS_OKAY) {
        LOG_ERR("board failed to start new game, trying again...");
        SEND_CMD(CMD_NEW_GAME);
    }
    LOG_INFO("board calibrated and ready for new game %c vs %c", player_type, opponent_type);

    if (http.ok(create_new_game(player_type == HUMAN ? "human" : "ai", opponent_type == HUMAN ? "human" : "ai"))) {
        set_gid_pid(response.body, "{id:%Q, player1:{id:%Q}}");
        return 0;
    }

    return -1;
}

/*
 *  wait for '0' from board indicating "ok"
 *  returns -1 if timeout, non-zero if fail, 0 if ok
 */
int wait_for_board(int expected, unsigned int tout) {
    unsigned int timeout = millis() + tout; // timeout after 10s
    while (!Serial1.available()) {
        if (millis() > timeout) {
            LOG_ERR("timed out waiting for board");
            return -1;
        }
    }

    // (austin) WAR: with some delay, Serial1.available() returns the proper
    // number of bytes the board sent. Without the delay, MAIN_SERIAL.available()
    // only returns 1 byte and fails...
    delay(10);

    int cmd;
    int cmd_ret;
    timeout = millis() + tout;
    while (millis() < timeout) {
        if (Serial1.available()) {
            char c = Serial1.read();
            cmd = rx_serial_command_r(c, stm_serial_buffer, &stm_save_i, SERIAL_BUFF_SIZE, &cmd_ret);
            if (cmd == CONTINUE) {
                // keep rx'ing bytes
            } else if (cmd == expected) {
                if (cmd_ret < 0) { // param parsing error
                    LOG_ERR("command %d failed to parse parameters, cmd_ret=%d", cmd_ret);
                    return -1;
                }
                LOG_TRACE("got %d bytes from stm32", cmd_ret);
                return cmd_ret;
            } else if (cmd == FAIL) {
                LOG_ERR("serial failed to rx command with error %d", cmd);
                return -1;
            }
            // else rx'd different command
        }
    }

    return -1;
}

int do_end_turn(char *params) {
    LOG_INFO("end turn, getting move from hall array");
    // just forward to M0
    SEND_CMD(CMD_END_TURN);

    int status = wait_for_board(CMD_MOVE_PIECE, QUICK_TIMEOUT);
    if (status == 99 || status == -1) { // try again
        LOG_ERR("computing move failed, try again");
        SEND_ANDROID_CMD(1);
        return 0;
    }

    player_turn = false;
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
        LOG_INFO("sending %s", params);
        SEND_CMD_P(CMD_MOVE_PIECE, "%s", params); // forward to M0
        while (1) {
            int status = wait_for_board(CMD_STATUS, BOARD_TIMEOUT);
            LOG_TRACE("status=%d", status);
            if (status == STATUS_OKAY) {
                break;
            } else if (status == STATUS_FAIL) {
                LOG_ERR("move invalid");
                return -1;
            } else {
                LOG_ERR("board failed to move piece, trying again...");
                SEND_CMD_P(CMD_MOVE_PIECE, "%s", params);
            }
        }
        LOG_INFO("board moved piece");
        return 0;
    } else {
        if (valid_move(params)) {
            if (http.ok(post_move(gid, pid, params))) {
                LOG_INFO("Move posted okay: %s", response.body);
                int err = 0;
                json_scanf(response.body, strlen(response.body), "{err_code:%d}", &err);
                if (err == 99) {
                    return err;
                }
                return 0;
            }
            LOG_ERR("Failed to post move %s", params);
            return -1;
        } else {
            LOG_ERR("Invalid move format %s", params);
            return 99;
        }
    }
}

int do_promote(char *params) {
    LOG_TRACE("Serial promote cmd not implemented");
    return -1;
}

int do_calibrate(char *params) {
    LOG_TRACE("Calibrating");
    SEND_CMD(CMD_CALIBRATE);
    return 0;
}

int do_end_game(char *params) {
    clear_gid_pid();
    // send end game to server
    return 0;
}

int do_send_log(char *params) {
    #define MAX_PARAMS 1
    char *p_arr[MAX_PARAMS];
    int num_params = parse_params(params, p_arr, MAX_PARAMS);
    if (num_params != 1)
        return -1;
    int lvl = atoi(p_arr[0]);
    switch (lvl) {
        case LVL_TRACE:
            MAIN_SERIAL.printf("[m0 trace] %s", params+2); MAIN_SERIAL.printf("\n");
            break;
        case LVL_INFO:
            MAIN_SERIAL.printf("[m0 info] %s", params+2); MAIN_SERIAL.printf("\n");
            break;
        case LVL_WARN:
            MAIN_SERIAL.printf("[m0 warn] %s", params+2); MAIN_SERIAL.printf("\n");
            break;
        case LVL_ERR:
            MAIN_SERIAL.printf("[m0 error] %s", params+2); MAIN_SERIAL.printf("\n");
            break;
    }
    return 0;
}

// debug commands for photon and stm32
int do_debug_cmd(char *params) {
    if (params)
        SEND_CMD_P(CMD_DEBUG, "%s", params);
    else // default capture
        SEND_CMD(CMD_DEBUG);
    return 0;
}

int do_retry(char *params) {
    SEND_CMD(CMD_RETRY);
    if (wait_for_board(CMD_STATUS, QUICK_TIMEOUT) != STATUS_OKAY) {
        LOG_ERR("retry failed");
    }
    return 0;
}

/*
 *  param0: ['c'|'k']
 */
int do_capture_castle(char *params) {
    // just forward to M0
    LOG_TRACE("capture castle, params=%s", params);
    if (params)
        SEND_CMD_P(CMD_CAPTURE_CASTLE, "%s", params);
    else // default capture
        SEND_CMD_P(CMD_CAPTURE_CASTLE, "%s", "c");

    if (wait_for_board(CMD_STATUS, QUICK_TIMEOUT) != STATUS_OKAY) {
        LOG_ERR("capture/castle failed");
    }
    return 0;
}

/*
 *  resets stm32 then photon
 */
int do_reset(char *params) {
    SEND_CMD(CMD_RESET);
    delay(500);
    System.reset();
    return 0;
}

int do_user_turn(char *params) {
    return -1;
}
#endif
