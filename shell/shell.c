#define _GNU_SOURCE
#define CMD_MAX 255

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <limits.h>


int main(int argc, char *argv[]){


while (1) {

  int i, numToken = 0;
  char cwd[PATH_MAX+1];
  char cmd[CMD_MAX];
  char tmp[CMD_MAX][CMD_MAX];
  char **token;

  signal(SIGINT , SIG_IGN);
  signal(SIGTERM , SIG_IGN);
  signal(SIGQUIT , SIG_IGN);
  signal(SIGTSTP , SIG_IGN);

  if (getcwd(cwd,PATH_MAX+1) != NULL)
    printf("[3150 shell:%s]$ ", cwd);

  fgets(cmd,CMD_MAX,stdin);
  cmd[strlen(cmd)-1] = '\0';

  char *retval = strtok (cmd , " ");
  while(retval != NULL){
   strcpy(tmp[numToken], retval);
   numToken++;
   retval = strtok(NULL, " ");
  }

  if (numToken != 0) {

  token = (char **) malloc(sizeof(char *) * numToken);
  for (i = 0; i < numToken; i++) {
    token[i] = (char *) malloc(sizeof(char ) * strlen(tmp[i]));
    strcpy(token[i], tmp[i]);
  }
  token[i] = NULL;

  if (strcmp(*token, "cd") == 0) {
  if (i == 2){
      if(chdir(token[1]) == -1)
        fprintf(stderr, "%s: cannot change directory \n", token[1]);
  }
  else
      fprintf(stderr, "cd: wrong number of arguments \n");
  }

  else if (strcmp(*token, "exit") == 0) {
    if (i == 1)
      exit(0);
    else
      fprintf(stderr, "exit: wrong number of arguments \n");
  }

  else {

    pid_t child_pid;
    if (!(child_pid = fork())){

      signal(SIGINT , SIG_DFL);
      signal(SIGTERM , SIG_DFL);
      signal(SIGQUIT , SIG_DFL);
      signal(SIGTSTP , SIG_DFL);

      setenv("PATH", "/bin:/usr/bin:.", 1);
      execvp(*token,token);

      if (errno == ENOENT){
        fprintf(stderr, "%s: command not found \n", *token);
        exit(EXIT_FAILURE);
      }
      else{
        fprintf(stderr, "%s: unknown error \n", *token);
        exit(EXIT_FAILURE);
      }

    }
    else
      waitpid(child_pid, NULL, WUNTRACED);

  }

  }

}

  return 0;
}
