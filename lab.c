#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

// Simplifed xv6 shell.

#define MAXARGS 10

// All commands have at least a type. Have looked at the type, the code
// typically casts the *cmd to some specific cmd type.
struct cmd {
  int type;          //  ' ' (exec), ; for sequential, & for parallel
};

struct execcmd {
  int type;              // ' '
  char *argv[MAXARGS];   // arguments to the command to be exec-ed
};
// definitions for parallel command object
struct parcmd {
  int type;            // &
  struct cmd *left;    // the command to be run (e.g., an execcmd)
  struct cmd *right;   // the input/output file
};

// definitions for sequential command object
struct seqcmd {
  int type;          // ;
  struct cmd *left;  // first command to be run
  struct cmd *right; // second command to be run
};

int fork1(void);  // Fork but exits on failure.
struct cmd *parsecmd(char*);

// Execute cmd.  Never returns.
void
runcmd(struct cmd *cmd)
{
  int r;
  pid_t pid;
  struct execcmd *ecmd;
  struct seqcmd *scmd;
  struct parcmd *pcmd;

  if(cmd == 0) {
    exit(0);
  }
  
  switch(cmd->type){
  default:
    fprintf(stderr, "unknown runcmd\n");
    exit(-1);

  // decision branches for different command function ( ; or & )
  case ' ':
    ecmd = (struct execcmd*)cmd;
    if(ecmd->argv[0] == 0)
      exit(0);
    
    // modified this section to execute given command and wait for the command to finish
    pid = fork1();
    if(pid==0) {
      if(execvp(ecmd->argv[0], ecmd->argv) < 0) {
        fprintf(stderr, "Unknown command... exec failed\n");
      }
    }
    waitpid(pid, &r, 0);
    break;

  // modified this section to fork and have child execute the left side of the separator
  // parent waits for child to finish executing then executes right side of separator
  case ';':
    scmd = (struct seqcmd*)cmd;
    pid = fork1();
    if(pid==0) {
      runcmd(scmd->left);
    }
    waitpid(pid, &r, 0);
    runcmd(scmd->right);
    break;

  // modified this section to fork and both left side and right side of the operator run in parallel
  // but function waits for both to finish execution before retrieving the next command
  case '&':
    pcmd = (struct parcmd*)cmd;
    pid = fork1();
    if (pid==0) {
        runcmd(pcmd->left);
    }
    runcmd(pcmd->right);
    waitpid(pid, &r, 0);
    break;
  }    
  exit(0);
}

int
getcmd(char *buf, int nbuf)
{
  
  if (isatty(fileno(stdin)))
    // modified this line to adjust command prompt
    fprintf(stdout, "$CS450 ");
  memset(buf, 0, nbuf);
  fgets(buf, nbuf, stdin);
  if(buf[0] == 0) // EOF
    return -1;
  return 0;
}

int
main(void)
{
  static char buf[100];
  int r;
  pid_t pid;

  // Read and run input commands.
  while(getcmd(buf, sizeof(buf)) >= 0){
    if(buf[0] == 'c' && buf[1] == 'd' && buf[2] == ' '){
      // Clumsy but will have to do for now.
      // Chdir has no effect on the parent if run in the child.
      buf[strlen(buf)-1] = 0;  // chop \n
      if(chdir(buf+3) < 0)
        fprintf(stderr, "cannot cd %s\n", buf+3);
      continue;
    }
    pid = fork1();
    if(pid == 0) {
      runcmd(parsecmd(buf));
    }
    //changed to waitpid() so that it can handle multiple nested processes finishing to completion
    waitpid(pid, &r, 0);
  }
  exit(0);
}

int
fork1(void)
{
  int pid;
  
  pid = fork();
  if(pid == -1)
    perror("fork");
  return pid;
}

struct cmd*
execcmd(void)
{
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = ' ';
  return (struct cmd*)cmd;
}

// modified to parse the command string into the parallel command struct
struct cmd*
parallel_cmd(struct cmd *left, struct cmd *right)
{
  struct parcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = '&';
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

// modified to parse the command string into the sequential command struct
// only differs to parallel_cmd by type
struct cmd*
sequential_cmd(struct cmd *left, struct cmd *right)
{
  struct seqcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = ';';
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

// Parsing

char whitespace[] = " \t\r\n\v";
char symbols[] = "&;";

int
gettoken(char **ps, char *es, char **q, char **eq)
{
  char *s;
  int ret;
  
  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  if(q)
    *q = s;
  ret = *s;
  switch(*s){
  case 0:
    break;
  
  // modified switch cases to recognize the new sensitive characters
  case ';':
  case '&':
    s++;
    break;
  default:
    ret = 'a';
    while(s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
      s++;
    break;
  }
  if(eq)
    *eq = s;
  
  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return ret;
}

int
peek(char **ps, char *es, char *toks)
{
  char *s;
  
  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return *s && strchr(toks, *s);
}

struct cmd *parseline(char**, char*);
struct cmd *parseexec(char**, char*);

// make a copy of the characters in the input buffer, starting from s through es.
// null-terminate the copy to make it a string.
char 
*mkcopy(char *s, char *es)
{
  int n = es - s;
  char *c = malloc(n+1);
  assert(c);
  strncpy(c, s, n);
  c[n] = 0;
  return c;
}

struct cmd*
parsecmd(char *s)
{
  char *es;
  struct cmd *cmd;

  es = s + strlen(s);
  cmd = parseline(&s, es);
  peek(&s, es, "");
  if(s != es){
    fprintf(stderr, "leftovers: %s\n", s);
    exit(-1);
  }
  return cmd;
}

// modified this method to split commands by ';' and '&' characters instead of '|' '<' and '>'
struct cmd*
parseline(char **ps, char *es)
{
  struct cmd *cmd;
  cmd = parseexec(ps, es);
  if (peek(ps, es, ";")){
    gettoken(ps, es, 0, 0);
    cmd = sequential_cmd(cmd, parseline(ps, es));
  }
  else if (peek(ps, es, "&")) {
    gettoken(ps, es, 0, 0);
    if (*ps == es || **ps==';') {
      fprintf(stdout,"Cannot terminate command with \"&\"\n");
      exit(0);
    }
    cmd = parallel_cmd(cmd, parseline(ps, es));
  }
  return cmd;
}


struct cmd*
parseexec(char **ps, char *es)
{
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;
  
  ret = execcmd();
  cmd = (struct execcmd*)ret;

  argc = 0;
  while(!peek(ps, es, "&;")) {
    if((tok=gettoken(ps, es, &q, &eq)) == 0)
      break;
    if(tok != 'a') {
      fprintf(stderr, "syntax error\n");
      exit(-1);
    }
    cmd->argv[argc] = mkcopy(q, eq);
    argc++;
    if(argc >= MAXARGS) {
      fprintf(stderr, "too many args\n");
      exit(-1);
    }
  }
  cmd->argv[argc] = 0; // null terminate the arguments string
  return ret;
}