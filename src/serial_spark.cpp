#ifdef SPARK
#include "Particle.h"
#include "serial.h"

#define COORD_VALID(c) ((c) < 8)

int do_end_turn(char *params) {
    Log.trace("Serial end turn cmd not implemented");
    return -1;
}

/*
 *  validates that move is of the form:
 *      [a-h][1-8][a-h][1-8]
 */
bool valid_move(char *move) {
    if (move && strlen(move) == 4) {
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

// only for testing, captures a move command from usb serial
// and forwards it to motor driver
int do_move_piece(char *params) {
    Log.trace("Got move: %s", params);
    if (valid_move(params)) {
        Serial.println("move valid, sending");
        SEND_MOVE(params);
        return 0;
    }

    Serial.println("move invalid");
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
