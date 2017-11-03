#ifdef SPARK
#include "Particle.h"
#include "serial.h"
#include "serial_spark.h"

void init_serial(void) {
    Serial.begin(9600);
    Serial1.begin(9600);
}

/*
 *  wait for '0' from board indicating "ok"
 *  returns -1 if timeout, non-zero if fail, 0 if ok
 */
int wait_for_board() {
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
    int expected = CMD_STATUS;
    int status;
    while (Serial1.available()) {
        char c = Serial1.read();
        status = rx_serial_command_r(c, rx_buffer, SERIAL_BUFF_SIZE, &expected);
        if (status == 0) {
            if (expected == OKAY)
                return 0;
            break;
        }
    }

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

// only for testing, captures a move command from usb serial
// and forwards it to motor driver
int do_move_piece(char *params) {
    if (valid_move(params)) {
        Log.info("move valid, sending %.4s", params);
        SEND_MOVE(params);
        if (wait_for_board() == 0) {
            Log.info("board moved piece");
            return 0;
        }
        Log.error("board failed to move piece");
        return -1;
    }
    Log.error("move invalid");
    return -1;
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

int do_scan_wifi(char *params) {
    Log.trace("Serial scan wifi cmd not implemented");
    return -1;
}

int do_set_wifi(char *params) {
    Log.trace("Serial set wifi cmd not implemented");
    return -1;
}
#endif
