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
    while (wait_for_board(CMD_STATUS) != STATUS_OKAY) {
        LOG_ERR("board failed to start new game, trying again...");
        SEND_CMD(CMD_NEW_GAME);
    }
    LOG_INFO("board calibrated and ready for new game");

    if (player_type == HUMAN) {
        if (http.ok(create_new_game("human", opponent_type == HUMAN ? "human" : "ai"))) {
            set_gid_pid(response.body, "{id:%Q, player1:{id:%Q}}");
            return 0;
        }
        LOG_ERR("failed to post new game");
    } else {
        join_first_available_game();
        return 0;
    }

    return -1;
}

/*
 *  wait for '0' from board indicating "ok"
 *  returns -1 if timeout, non-zero if fail, 0 if ok
 */
int wait_for_board(int expected) {
    unsigned int timeout = millis() + BOARD_TIMEOUT; // timeout after 10s
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
    timeout = millis() + BOARD_TIMEOUT;
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
    // just forward to M0
    if (params)
        SEND_CMD_P(CMD_END_TURN, "%s", params);
    else
        SEND_CMD(CMD_END_TURN);

    while (wait_for_board(CMD_STATUS) != STATUS_OKAY) {
        if (params)
            SEND_CMD_P(CMD_END_TURN, "%s", params);
        else
            SEND_CMD(CMD_END_TURN);
    }

    waiting_for_user = false;
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
            int status = wait_for_board(CMD_STATUS);
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
            LOG_ERR("Failed to post move");
            return -1;
        }
        return 99;
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
    LOG_TRACE("Serial scan wifi cmd not implemented");
    return -1;
}

int do_set_wifi(char *params) {
    LOG_TRACE("Serial set wifi cmd not implemented");
    return -1;
}

/*
 *  param0: ['c'|'k']
 */
int do_capture_castle(char *params) {
    // just forward to M0
    if (params)
        SEND_CMD_P(CMD_CAPTURE_CASTLE, "%s", params);
    else // default capture
        SEND_CMD_P(CMD_CAPTURE_CASTLE, "%s", "c");
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
