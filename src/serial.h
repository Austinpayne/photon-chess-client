#ifndef __SERIAL_H_
#define __SERIAL_H_

#include "Particle.h"

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

#endif /* __SERIAL_H_ */
