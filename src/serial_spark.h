#ifndef __SERIAL_SPARK_H_
#define __SERIAL_SPARK_H_

#define COORD_VALID(c) ((c) < 8)
#define BOARD_TIMEOUT 20000 // ms
#define SERIAL_BUFF_SIZE 128

void init_serial(void);
int wait_for_board(int *expected);
bool valid_move(const char *move);

#endif // __SERIAL_SPARK_H_
