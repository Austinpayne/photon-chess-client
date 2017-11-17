#ifndef __CHESS_CLIENT_H_
#define __CHESS_CLIENT_H_
#include <HttpClient.h>

// for direct control
extern int mode;
extern HttpClient http;
extern http_response_t response;
extern char gid[48];
extern char pid[48];

int post_move(const char *game_id, const char *player_id, const char *move);

#endif // __CHESS_CLIENT_H_
