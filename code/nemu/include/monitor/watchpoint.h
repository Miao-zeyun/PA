#ifndef __WATCHPOINT_H__
#define __WATCHPOINT_H__

#include "common.h"

typedef struct watchpoint {
  int NO;
  struct watchpoint *next;

  /* TODO: Add more members if necessary */
	char expr[32];
	int value;
} WP;

bool new_wp(char* exp);
bool free_wp(int no);
bool monitor_wppool_change();
void print_watchpoint_status();
#endif
