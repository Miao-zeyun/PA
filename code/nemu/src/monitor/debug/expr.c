#include "nemu.h"

/* We use the POSIX regex functions to process regular expressions.
 * Type 'man regex' for more information about POSIX regex functions.
 */
#include <sys/types.h>
#include <regex.h>

enum {
  TK_NOTYPE = 256, TK_HEXNUM, TK_NUM, TK_REG, TK_LP, TK_RP,
  TK_EQ, TK_NOTEQ, TK_AND, TK_OR,
  TK_NEG, TK_DEREF

  /* TODO: Add more token types */

};

static struct rule {
  char *regex;
  int token_type;
} rules[] = {

  /* TODO: Add more rules.
   * Pay attention to the precedence level of different rules.
   */

  {" +", TK_NOTYPE},                // spaces
  {"0x[0-9a-f]+",TK_HEXNUM},        // hexnumber
  {"[0-9]+",TK_NUM},                // number
  {"\\$[a-z]{2,3}",TK_REG},         // register
  {"\\(", TK_LP},                   // '('
  {"\\)", TK_RP},                   // ')'
  {"\\+", '+'},                     // plus
  {"-", '-'},                       // minus
  {"\\*", '*'},                     // multiply
  {"\\/", '/'},                     // divide
  {"==", TK_EQ},                    // equal
  {"!=", TK_NOTEQ},                 // not equal
  {"&&", TK_AND},                   // and
  {"\\|\\|", TK_OR},                // or
  {"!", '!'},                       // not
};

#define NR_REGEX (sizeof(rules) / sizeof(rules[0]) )

static regex_t re[NR_REGEX];

/* Rules are used for many times.
 * Therefore we compile them only once before any usage.
 */
void init_regex() {
  int i;
  char error_msg[128];
  int ret;

  for (i = 0; i < NR_REGEX; i ++) {
    ret = regcomp(&re[i], rules[i].regex, REG_EXTENDED);
    if (ret != 0) {
      regerror(ret, &re[i], error_msg, 128);
      panic("regex compilation failed: %s\n%s", error_msg, rules[i].regex);
    }
  }
}

typedef struct token {
  int type;
  char str[32];
} Token;

Token tokens[32];
int nr_token;

static bool make_token(char *e) {
  int position = 0;
  int i;
  regmatch_t pmatch;

  nr_token = 0;

  while (e[position] != '\0') {
    /* Try all rules one by one. */
    for (i = 0; i < NR_REGEX; i ++) {
      if (regexec(&re[i], e + position, 1, &pmatch, 0) == 0 && pmatch.rm_so == 0) {
        char *substr_start = e + position;
        int substr_len = pmatch.rm_eo;

        //Log("match rules[%d] = \"%s\" at position %d with len %d: %.*s",
        //    i, rules[i].regex, position, substr_len, substr_len, substr_start);
        position += substr_len;

        /* TODO: Now a new token is recognized with rules[i]. Add codes
         * to record the token in the array `tokens'. For certain types
         * of tokens, some extra actions should be performed.
         */

        switch (rules[i].token_type) {
          case TK_NOTYPE:
            break;
          case TK_HEXNUM:
            if(substr_len > 32)
            { 
              printf("The hexadecimal number is too long\n");
              assert(0);
            }
            else
            {
              tokens[nr_token].type=TK_HEXNUM;
              strncpy(tokens[nr_token].str,substr_start+2,substr_len-2);
              *(tokens[nr_token].str+substr_len-2)='\0';
              nr_token++;
            }
            break;
          case TK_NUM:
            if(substr_len > 32)
            { 
              printf("The number is too long\n");
              assert(0);
            }
            else
            {
              tokens[nr_token].type=TK_NUM;
              strncpy(tokens[nr_token].str,substr_start,substr_len);
              *(tokens[nr_token].str+substr_len)='\0';
              nr_token++;
            }
            break;
          case TK_REG:
            tokens[nr_token].type=TK_REG;
            strncpy(tokens[nr_token].str,substr_start+1,substr_len-1);
            *(tokens[nr_token].str+substr_len-1)='\0';
            nr_token++;
            break;
          case TK_LP:
            tokens[nr_token++].type=TK_LP;
            break;
          case TK_RP:
            tokens[nr_token++].type=TK_RP;
            break;
          case '+':
            tokens[nr_token++].type='+';
            break;
          case '-':
          {
            if(nr_token==0 || (tokens[nr_token-1].type!=TK_NUM && tokens[nr_token-1].type!=TK_HEXNUM && tokens[nr_token-1].type!=TK_RP))
            {
              tokens[nr_token++].type=TK_NEG;
              break;
            }
            else
            {
              tokens[nr_token++].type='-';
              break;
            }
          }
          case '*':
          {
            if(nr_token==0 || (tokens[nr_token-1].type!=TK_NUM && tokens[nr_token-1].type!=TK_HEXNUM && tokens[nr_token-1].type!=TK_RP))
            {
              tokens[nr_token++].type=TK_DEREF;
              break;
            }
            else
            {
              tokens[nr_token++].type='*';
              break;
            }
          }
          case '/':
            tokens[nr_token++].type='/';
            break;
          case TK_EQ:
            tokens[nr_token++].type=TK_EQ;
            break;
          case TK_NOTEQ:
            tokens[nr_token++].type=TK_NOTEQ;
            break;
          case TK_AND:
            tokens[nr_token++].type=TK_AND;
            break;
          case TK_OR:
            tokens[nr_token++].type=TK_OR;
            break;
          case '!':
            tokens[nr_token++].type='!';
            break;
          default:
            printf("The token type is wrong\n");
            assert(0);
            break;
        }

        break;
      }
    }

    if (i == NR_REGEX) {
      printf("no match at position %d\n%s\n%*.s^\n", position, e, position, "");
      return false;
    }
  }

  return true;
}

bool check_parentheses(int p,int q)
{
  int p_number=0;
  if(tokens[p].type!=TK_LP || tokens[q].type!=TK_RP)
    return false;
  for(int i=p;i<=q;i++)
  {
    if(tokens[i].type==TK_LP)
      p_number++;
    else if(tokens[i].type==TK_RP)
      p_number--;
    if(i<q && p_number==0)
      return false;
  }
  if(p_number==0)
    return true;
  return false;
}

int find_dominant_operator(int p,int q)
{
  int p_number=0,poi=0,pri=5;
  for(int i=p;i<=q;i++)
  {
    if (tokens[i].type==TK_LP)
      p_number++;
    else if (tokens[i].type==TK_RP)
      p_number--;
    if(p_number==0)
    {
      if (pri>=0 && (tokens[i].type==TK_AND || tokens[i].type==TK_OR))
        {poi=i;pri=0;}
      else if (pri>=1 && (tokens[i].type==TK_EQ || tokens[i].type==TK_NOTEQ))
        {poi=i;pri=1;}
      else if (pri>=2 && (tokens[i].type=='+' || tokens[i].type=='-'))
        {poi=i;pri=2;}
      else if (pri>=3 && (tokens[i].type=='*' || tokens[i].type=='/'))
        {poi=i;pri=3;}
      else if (pri>=4 && (tokens[i].type==TK_DEREF || tokens[i].type==TK_NEG || tokens[i].type=='!'))
        {poi=i;pri=4;}
    }
  }
  return poi;
}

uint32_t eval(int p,int q)
{
  //printf("p=%d,q=%d\n",p,q);
  if(p>q)
  {
    printf("The expression is wrong(p>q in eval),p=%d,q=%d\n",p,q);
    assert(0);
  }
  else if(p==q)
  {
    int num;
    if(tokens[p].type==TK_NUM)
    {
      sscanf(tokens[p].str,"%d",&num);
      return num;
    }
    else if(tokens[p].type==TK_HEXNUM)
    {
      sscanf(tokens[p].str,"%x",&num);
      return num;
    }
    else if(tokens[p].type==TK_REG)
    {
      for(int i=0;i<8;i++)
      {
        if(strcmp(tokens[p].str,regsl[i])==0)
          return reg_l(i);
        if(strcmp(tokens[p].str,regsw[i])==0)
          return reg_w(i);
        if(strcmp(tokens[p].str,regsb[i])==0)
          return reg_b(i);
      }
      if(strcmp(tokens[p].str,"eip")==0)
        return cpu.eip;
      else
      {
        printf("The registers name is wrong\n");
        assert(0);
      }
    }
    else
    {
      printf("wrong in eval(p==q)\n");
      assert(0);
    }
  }
  else if(check_parentheses(p,q)==true)
  {
    return eval(p+1,q-1);
  }
  else
  {
    int op=find_dominant_operator(p,q);
    //printf("op=%d\n",op);
    if(tokens[op].type==TK_NEG || tokens[op].type==TK_DEREF || tokens[op].type=='!')
    {
      if(tokens[p].type==TK_NEG)
        return -eval(p+1,q);
      else if(tokens[p].type==TK_DEREF)
      {
        vaddr_t addr;
        addr=eval(p+1,q);
        return vaddr_read(addr,4);
      }
      else if(tokens[p].type=='!')
      {
        if(eval(p+1,q)!=0)
          return 0;
        else
          return 1;
      }
    }
    int val1=eval(p,op-1);
    int val2=eval(op+1,q);
    //printf("op.type=%d\n",tokens[op].type);
    switch (tokens[op].type)
    {
      case '+': return val1+val2;
      case '-': return val1-val2;
      case '*': return val1*val2;
      case '/': { if(val2==0)
                  {
                    printf("Can't divide 0\n");
                    assert(0);
                  }
                  else
                    return val1/val2;
                 }
      case TK_EQ: return val1 == val2;
      case TK_NOTEQ: return val1 != val2;
      case TK_AND: return val1 && val2;
      case TK_OR: return val1 || val2;
      default: assert(0);
    }
  }
}

uint32_t expr(char *e, bool *success) {
  if (!make_token(e)) {
    *success = false;
    return 0;
  }

  /* TODO: Insert codes to evaluate the expression. */
  *success = true;
  return eval(0,nr_token-1);
}
