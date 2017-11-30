#ifndef __CHESS_CLIENT_H_
#define __CHESS_CLIENT_H_
#include <HttpClient.h>

#define SERVER "174.52.149.107"
#define PORT   30300
#define AI 'a'
#define HUMAN 'h'

// for direct control
extern int mode;
extern HttpClient http;
extern http_response_t response;
extern char gid[48];
extern char pid[48];
extern char player_type;
extern bool player_turn;
extern bool waiting_for_board;

int post_move(const char *game_id, const char *player_id, const char *move);
int set_gid_pid(char *gid_pid_json, const char *fmt);
int create_new_game(const char *player_type, const char *opponent_type);
void join_first_available_game();
int clear_gid_pid();

#endif // __CHESS_CLIENT_H_
