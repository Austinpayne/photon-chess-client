#ifndef __SERIAL_H_
#define __SERIAL_H_

#include "Particle.h"

#define CMD_STATUS     0x0
#define CMD_NEW_GAME   0x1
#define CMD_END_TURN   0x2
#define CMD_MOVE_PIECE 0x3
#define CMD_PROMOTE    0x4
#define CMD_CALIBRATE  0x5
#define CMD_END_GAME   0x6
#define SCAN_WIFI      0x7
#define SET_WIFI       0x8

#define OK 0

typedef int (*cmd_f)(char *);

int do_status(char *params);
int do_new_game(char *params);
int do_end_turn(char *params);
int do_promote(char *params);
int do_end_game(char *params);
int do_scan_wifi(char *params);
int do_set_wifi(char *params);
int do_serial_command(char *cmd_str);
void rx_serial_command();
void send_cmd(unsigned char cmd, const char *params);
void send_move(const char *move);
void direct_control();

#endif /* __SERIAL_H_ */
