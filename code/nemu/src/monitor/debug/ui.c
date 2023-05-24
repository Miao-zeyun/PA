#include "monitor/monitor.h"
#include "monitor/expr.h"
#include "monitor/watchpoint.h"
#include "nemu.h"

#include <stdlib.h>
#include <readline/readline.h>
#include <readline/history.h>

void cpu_exec(uint64_t);

/* We use the `readline' library to provide more flexibility to read from stdin. */
char* rl_gets() {
  static char *line_read = NULL;

  if (line_read) {
    free(line_read);
    line_read = NULL;
  }

  line_read = readline("(nemu) ");

  if (line_read && *line_read) {
    add_history(line_read);
  }

  return line_read;
}

static int cmd_c(char *args) {
  cpu_exec(-1);
  return 0;
}

static int cmd_q(char *args) {
  return -1;
}

static int cmd_help(char *args);
static int cmd_si(char *args);
static int cmd_info(char *args);
static int cmd_x(char *args);
static int cmd_p(char *args);
static int cmd_w(char *args);
static int cmd_d(char *args);

static struct {
  char *name;
  char *description;
  int (*handler) (char *);
} cmd_table [] = {
  { "help", "Display informations about all supported commands", cmd_help },
  { "c", "Continue the execution of the program", cmd_c },
  { "q", "Exit NEMU", cmd_q },
  { "si", "Single-step debug", cmd_si },
  { "info", "Print program status", cmd_info },
  { "x", "Scan memory", cmd_x },
  { "p", "Evaluate expression", cmd_p },
  { "w", "Set watchpoint", cmd_w },
  { "d", "Delete watchpoint", cmd_d },

  /* TODO: Add more commands */

};

#define NR_CMD (sizeof(cmd_table) / sizeof(cmd_table[0]))

static int cmd_help(char *args) {
  /* extract the first argument */
  char *arg = strtok(NULL, " ");
  int i;

  if (arg == NULL) {
    /* no argument given */
    for (i = 0; i < NR_CMD; i ++) {
      printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
    }
  }
  else {
    for (i = 0; i < NR_CMD; i ++) {
      if (strcmp(arg, cmd_table[i].name) == 0) {
        printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
        return 0;
      }
    }
    printf("Unknown command '%s'\n", arg);
  }
  return 0;
}

static int cmd_si(char *args) 
{
  int step;
  if (args == NULL) 
    cpu_exec(1);
  else
  {
    step = atoi(args);
    cpu_exec(step);
  }
  return 0;
}

static void print_register_status() 
{
  int i,j;
  for(i = 0,j = 4; i < 4; i++,j++)
  {
    printf("  %s  0x%08x | %s  0x%04x | %s  0x%02x | %s  0x%02x\n",
            regsl[i],reg_l(i),regsw[i],reg_w(i),regsb[i],reg_b(i),regsb[j],reg_b(j));
  }
  for(; i < 8; i++)
  {
    printf("  %s  0x%08x | %s  0x%04x\n",regsl[i],reg_l(i),regsw[i],reg_w(i));
  }
  printf("  eip  0x%08x\n",cpu.eip);
}

static int cmd_info(char *args) 
{
  if (args == NULL)
    return 0;
  switch(*args)
  {
    case 'r':
      print_register_status();
      break;
    case 'w':
    	print_watchpoint_status();
      break;
    default:
      return 0;
  };
  return 0;
}

static int cmd_x(char *args) 
{
  char *arg = strtok(NULL, " ");
  if (arg == NULL)
    return 0;
  int num;
  sscanf(arg,"%d",&num);
  char *exp = args+strlen(arg)+1;
  bool success;
  uint32_t addr=expr(exp,&success);
  for(int i=0;i<num*4;i++)
  {
    if(i%4==0)
      printf("0x%x:  0x%02x",addr+i,vaddr_read(addr+i,1));
    else if(i%4==3)
      printf("  0x%02x\n",vaddr_read(addr+i,1));
    else
      printf("  0x%02x",vaddr_read(addr+i,1));
  }
  return 0;
}

static int cmd_p(char *args)
{
  if (args == NULL)
    return 0;
  bool success;
  uint32_t result=expr(args,&success);
  printf("exp result:  0x%08x  %d\n",result,result);
  return 0;
}

static int cmd_w(char *args)
{
  if (args == NULL)
    return 0;
  bool new;
  new=new_wp(args);
  if(new==false)
  	printf("Failed to new watchpoint\n");
  return 0;
}

static int cmd_d(char *args)
{
  if (args == NULL)
    return 0;
  int num;
  bool delete;
  sscanf(args,"%d",&num);
  delete=free_wp(num);
  if(delete==true)
  	printf("Successfully delete watchpoint %d\n",num);
  else
  	printf("Failed to delete watchpoint %d\n",num);
  return 0;
}

void ui_mainloop(int is_batch_mode) {
  if (is_batch_mode) {
    cmd_c(NULL);
    return;
  }

  while (1) {
    char *str = rl_gets();
    char *str_end = str + strlen(str);

    /* extract the first token as the command */
    char *cmd = strtok(str, " ");
    if (cmd == NULL) { continue; }

    /* treat the remaining string as the arguments,
     * which may need further parsing
     */
    char *args = cmd + strlen(cmd) + 1;
    if (args >= str_end) {
      args = NULL;
    }

#ifdef HAS_IOE
    extern void sdl_clear_event_queue(void);
    sdl_clear_event_queue();
#endif

    int i;
    for (i = 0; i < NR_CMD; i ++) {
      if (strcmp(cmd, cmd_table[i].name) == 0) {
        if (cmd_table[i].handler(args) < 0) { return; }
        break;
      }
    }

    if (i == NR_CMD) { printf("Unknown command '%s'\n", cmd); }
  }
}
