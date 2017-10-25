#include "serial.h"

#define SERIAL_BUFF_SIZE 64

// serial buffers
char rx_buffer[SERIAL_BUFF_SIZE];
int irx = 0;

// -1 == cmd failed, 0 == ok, 1 == error, etc.
int do_status(char *params) {
    char *p;
    int status;
    if ((p = strtok(params, ",")) != NULL) {
        status = atoi(p);
        Log.trace("Got serial status: %d", status);
        return status;
    }
    return -1;
}

int do_new_game(char *params) {
    Log.trace("Serial new game cmd not implemented");
    return -1;
}

int do_end_turn(char *params) {
    Log.trace("Serial end turn cmd not implemented");
    return -1;
}

int do_promote(char *params) {
    Log.trace("Serial promote cmd not implemented");
    return -1;
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

cmd_f cmds[] = {
    &do_status,
    &do_new_game,
    &do_end_turn,
    NULL, // move piece not supported on photon
    &do_promote,
    NULL, // calibrate not supported on photon
    &do_end_game,
    &do_scan_wifi,
    &do_set_wifi
};

int do_serial_command(char *cmd_str) {
    #define CMD_BITWIDTH 4

    char cmd[CMD_BITWIDTH] = {0};
    char params[] = {0};
    int len = strlen(cmd_str);

    if (len < CMD_BITWIDTH) {
        Log.error("commands must be at least %d bits", CMD_BITWIDTH);
        return -1;
    }

    strncpy(cmd, cmd_str, CMD_BITWIDTH);

    int c = atoi(cmd);
    int ret = -1;
    if (c < sizeof(cmds) && cmds[c]) {
        ret = cmds[c](cmd_str+CMD_BITWIDTH);
    } else {
        Log.warn("Serial cmd %d not implemented", c);
    }

    return ret;
}

void rx_serial_command() {
    if (irx < SERIAL_BUFF_SIZE) {
        char c = Serial1.read();
        if (c == '\n') {
            rx_buffer[irx] = '\0';
            if (do_serial_command(rx_buffer) == 0) {
                Log.trace("Serial cmd complete");
            } else {
                Log.error("Serial cmd failed");
            }
            memset(rx_buffer, 0, SERIAL_BUFF_SIZE);
            irx = 0;
        } else {
            rx_buffer[irx] = c;
            irx++;
        }
    }
}

void send_cmd(unsigned char cmd, const char *params) {
    Serial1.printlnf("%d %s", cmd, params);
}

void send_move(const char *move) {
    Serial1.printlnf("%d %.4s", CMD_MOVE_PIECE, move);
}
