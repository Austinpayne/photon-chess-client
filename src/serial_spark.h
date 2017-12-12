#ifndef __SERIAL_SPARK_H_
#define __SERIAL_SPARK_H_

#include <serial.h>

#ifdef MAIN_SERIAL
#undef MAIN_SERIAL
#endif
#define MAIN_SERIAL Serial2

#define COORD_VALID(c) ((c) < 8)
#define BOARD_TIMEOUT 30000 // ms
#define QUICK_TIMEOUT 5000 // ms
#define SERIAL_BUFF_SIZE 128

void init_serial(void);
int wait_for_board(int expected, unsigned int tout);
bool valid_move(const char *move);

#endif // __SERIAL_SPARK_H_
