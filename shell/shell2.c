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
#include <glob.h>
#include <fcntl.h>

typedef struct jobs {
	char cmd[255];
	pid_t *pidList;
	int pidcnt;
	struct jobs *next;
} Jobs;

Jobs* appendList(Jobs* head, char *cmd, pid_t* pidList, int pidcnt) {
		Jobs* newNode = malloc(sizeof(Jobs));
		strcpy(newNode->cmd,cmd);
		newNode->pidList = pidList;
		newNode->next = NULL;
		newNode->pidcnt = pidcnt;
		// Find the last node
		if(head == NULL) {
			return newNode;
		}
		else {
		Jobs* temp;
		for(temp = head;temp->next != NULL;temp=temp->next);
		temp->next = newNode;
		return head;
		}
}

void printList(Jobs* head) {
	if(head == NULL) {
		printf("No Suspended Jobs\n");
		return;
	}
	else {
		Jobs* temp;
		int i = 1;
		for(temp=head;temp != NULL;temp=temp->next,i++) {
			printf("[%d] %s\n",i,temp->cmd);
		}
	}
}

Jobs* deleteNode(Jobs* head,int id) {
	if(head == NULL) {
		return NULL;
	}
	else if (id == 1) {
		Jobs* newHead = head->next;
		//free(head->pidList);
		//free(head);
		return newHead;
	}
	else {
		Jobs* temp = head;
		int i;
		//find node before node id
		for(i = 1;i != id-1;i++,temp=temp->next);
		if(temp->next != NULL) {
				//Jobs* delPtr = temp->next;
				temp->next = temp->next->next;
				//free(delPtr->pidList);
				//free(delPtr);
				return head;
		}
		else { // impossible to enter but necessary for compile
			fprintf(stderr, "deleteNode error \n");
			return NULL;
		}
	}
}

Jobs* jobList = NULL;
int suspendedJob = 0;

void wakechildren(Jobs* head, int id) {
	Jobs* temp = head;
	int i;
	//find node id
	for(i = 1;i != id;i++,temp=temp->next);
  for(i = 0; i < temp->pidcnt;i++) {
    kill(temp->pidList[i],SIGCONT);
		//printf("Pid wake up: %d\n", temp->pidList[i]);
	}
	//printf("Job wake up: %s\n", temp->cmd);
}

void waitchildren(Jobs* head, int id) {
  Jobs* temp = head;
	int i;
  int status;
	//find node id
	for(i = 1;i != id;i++,temp=temp->next);
	pid_t *pids = malloc(sizeof(pid_t) * temp->pidcnt);
	for(i = 0; i < temp->pidcnt;i++) {
		pids[i] = temp->pidList[i];
	}
	for(i = 0; i < temp->pidcnt;i++) {
    waitpid(temp->pidList[i],&status,WUNTRACED);
    if (WIFSTOPPED(status)) {
			//jobList = appendList(jobList,temp->cmd,pids,temp->pidcnt);
			//suspendedJob++;
			printf("\n");
      //printf("They are sleeping again\n");
      break;
    }
		else {
			jobList = deleteNode(jobList,id);
			suspendedJob--;
			break;
		}
  }
}

void wildcard(char** token, int tokencnt) {
	int i;
	glob_t globbuf;
	globbuf.gl_offs = 1;
	int flags = GLOB_DOOFFS | GLOB_NOCHECK;
	for (i = 1; i < tokencnt; i++) {
		glob(token[i], flags, NULL, &globbuf);
		flags = GLOB_DOOFFS | GLOB_NOCHECK | GLOB_APPEND;
	}
	globbuf.gl_pathv[0] = *token;
	execvp(*globbuf.gl_pathv, globbuf.gl_pathv);
	globfree(&globbuf);
}


int main(int argc, char *argv[]){

while (1) {

  int i;
  int tokencnt = 0;
  int pipelinecnt = 0;
	int haswildcard;
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
	char wholecmd[CMD_MAX];
	strcpy(wholecmd, cmd);

  char *retval = strtok (cmd , " ");
  while(retval != NULL){
   strcpy(tmp[tokencnt], retval);
   tokencnt++;
   retval = strtok(NULL, " ");
  }

  if (tokencnt != 0) {

  token = (char **) malloc(sizeof(char *) * tokencnt);
  for (i = 0; i < tokencnt; i++) {
    token[i] = (char *) malloc(sizeof(char ) * strlen(tmp[i]));
    strcpy(token[i], tmp[i]);
    if (strcmp(token[i], "|") == 0)
      pipelinecnt++;
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
    if (i == 1) {
			if (suspendedJob > 0)
				fprintf(stderr, "There is at least one suspended job \n");
      else
				exit(0);
			}
    else
      fprintf(stderr, "exit: wrong number of arguments \n");
  }

  else if (strcmp(*token, "fg") == 0) {
    if (i == 2) {
			int id = atoi(token[1]);
      if (id > suspendedJob || id <= 0) {
        fprintf(stderr, "fg: no such job \n");
      }
      else {
				wakechildren(jobList,id);
				waitchildren(jobList,id);
				//jobList = deleteNode(jobList,id);
				//suspendedJob--;
			}
		}
    else
      fprintf(stderr, "fg: wrong number of arguments \n");
  }

  else if (strcmp(*token, "jobs") == 0) {
		if (i == 1)
			printList(jobList);
		else
			fprintf(stderr, "jobs: wrong number of arguments \n");
  }

  else { // not builtin cmd

    // without pipe
		if (pipelinecnt == 0) {

		pid_t child_pid;
		haswildcard = 0;
		// child
    if (!(child_pid = fork())){

      signal(SIGINT , SIG_DFL);
      signal(SIGTERM , SIG_DFL);
      signal(SIGQUIT , SIG_DFL);
      signal(SIGTSTP , SIG_DFL);
      setenv("PATH", "/bin:/usr/bin:.", 1);

			for (i = 0; i < tokencnt; i++) {
				if (strstr(token[i], "*") != NULL) {
					haswildcard = 1;
					wildcard(token, tokencnt);
					break;
				}
			}
			if (!haswildcard) {
				execvp(*token,token);
			}

			if (errno == ENOENT){
        fprintf(stderr, "%s: command not found \n", *token);
        exit(EXIT_FAILURE);
      }
      else{
        fprintf(stderr, "%s: unknown error \n", *token);
        exit(EXIT_FAILURE);
      }

    }
		// parent
    else {

			int status;
	    waitpid(child_pid,&status,WUNTRACED);
	    if(WIFSTOPPED(status)) {
				pid_t *pids = malloc(sizeof(pid_t));
				pids[0] = child_pid;
	      jobList = appendList(jobList,wholecmd,pids,1);
				suspendedJob++;
				printf("\n");
				//printf("It is sleeping\n");
	    }
		}
	}

	// with pipe
  else {

				int pipecnt = pipelinecnt + 1;
				int k;
				pid_t *child_pid = malloc(sizeof(pid_t) * pipecnt);
				int pipefd[2];

				// 2 pipe
				if (pipecnt == 2) {

          int p;
					int leftnum, rightnum;
					int lefthaswildcard = 0;
					int righthaswildcard = 0;

          for (p = 0; p < tokencnt; p++) {
            if (strcmp(token[p], "|") == 0)
              break;
          }
          char **left = (char **) malloc(sizeof(char *) * p);
					leftnum = p;
          for (i = 0; i < p; i++) {
            left[i] = (char *) malloc(sizeof(char ) * strlen(token[i]));
            strcpy(left[i], token[i]);
						if (strstr(left[i], "*") != NULL)
							lefthaswildcard = 1;
          }
          left[i] = NULL;
          p++;
          char **right = (char **) malloc(sizeof(char *) * (tokencnt - p));
					rightnum = (tokencnt - p);
          for (i = p; i < tokencnt; i++) {
            right[i-p] = (char *) malloc(sizeof(char ) * strlen(token[i]));
            strcpy(right[i-p], token[i]);
						if (strstr(right[i-p], "*") != NULL)
							righthaswildcard = 1;
          }
          right[i-p] = NULL;

          pipe(pipefd);
          if(!(child_pid[0] = fork())){
						signal(SIGINT , SIG_DFL);
			      signal(SIGTERM , SIG_DFL);
			      signal(SIGQUIT , SIG_DFL);
			      signal(SIGTSTP , SIG_DFL);
			      setenv("PATH", "/bin:/usr/bin:.", 1);

            dup2(pipefd[1],1);
						close(pipefd[0]);
            close(pipefd[1]);
						if (lefthaswildcard)
							wildcard(left, leftnum);
						else
            	execvp(*left, left);

						if (errno == ENOENT) {
			      	fprintf(stderr, "%s: command not found \n", *left);
							exit(EXIT_FAILURE);
			      }
			      else {
							fprintf(stderr, "%s: unknown error \n", *left);
			        exit(EXIT_FAILURE);
			      }
	        	for (i = 0; i < leftnum; i++)
	          	free(left[i]);
	        	free(left);

          } else {
						if (!(child_pid[1] = fork())) {
							signal(SIGINT , SIG_DFL);
				      signal(SIGTERM , SIG_DFL);
				      signal(SIGQUIT , SIG_DFL);
				      signal(SIGTSTP , SIG_DFL);
				      setenv("PATH", "/bin:/usr/bin:.", 1);

							dup2(pipefd[0],0);
							close(pipefd[0]);
	            close(pipefd[1]);
							if (righthaswildcard)
								wildcard(right, rightnum);
							else
	            execvp(*right, right);

							if (errno == ENOENT) {
				      	fprintf(stderr, "%s: command not found \n", *right);
								exit(EXIT_FAILURE);
				      }
				      else {
								fprintf(stderr, "%s: unknown error \n", *right);
				        exit(EXIT_FAILURE);
				      }

	          	for (i = 0; i < rightnum; i++)
	            	free(right[i]);
	          	free(right);
						}
						else {
							close(pipefd[0]);
					    close(pipefd[1]);
						}
          }
        }

				// more than 2 pipe
				else {

				int num;
				int p = 0;
				int fd_in = 0;

				// loop for every pipe
        for (k = 0; k < pipecnt; k++) {

					haswildcard = 0;
          num = 0; // no of token in pipecmd
          while (p < tokencnt) {
            if (strcmp(token[p], "|") == 0)
              break;
            p++;
            num++;
          }
          char **pipecmd = (char **) malloc(sizeof(char *) * num);
          for (i = 0; i < num; i++) {
            pipecmd[i] = (char *) malloc(sizeof(char) * strlen(token[p-num+i]));
            strcpy(pipecmd[i], token[p-num+i]);
            //printf("%d: %s\n", i, pipecmd[i]);
          }
          pipecmd[i] = NULL;
          p++;

          pipe(pipefd);
					// child
          if(!(child_pid[k]=fork())) {

						signal(SIGINT , SIG_DFL);
			      signal(SIGTERM , SIG_DFL);
			      signal(SIGQUIT , SIG_DFL);
			      signal(SIGTSTP , SIG_DFL);
			      setenv("PATH", "/bin:/usr/bin:.", 1);

						dup2(fd_in,0);
						if (k!=(pipecnt-1)) // left and mid
							dup2(pipefd[1],1);
						close(pipefd[0]);
						close(pipefd[1]);

						for (i = 0; i < num; i++) {
							if (strstr(pipecmd[i], "*") != NULL) {
								haswildcard = 1;
								wildcard(pipecmd, num);
								break;
							}
						}
						if (!haswildcard) {
							execvp(*pipecmd, pipecmd);
						}

						if (errno == ENOENT) {
			      	fprintf(stderr, "%s: command not found \n", *pipecmd);
							exit(EXIT_FAILURE);
			      }
			      else {
							fprintf(stderr, "%s: unknown error \n", *pipecmd);
			        exit(EXIT_FAILURE);
			      }

          	for (i = 0; i < num; i++)
            	free(pipecmd[i]);
          	free(pipecmd);
						}
						// child end
						// parent
						else {
							fd_in = pipefd[0];
							//close(pipefd[0]);
							close(pipefd[1]);
						}

				}
				// loop end
			}

				for (k = 0; k < pipecnt; k++) {
					int status;
					waitpid(child_pid[k],&status,WUNTRACED);
					if(WIFSTOPPED(status)) {
						jobList = appendList(jobList,wholecmd,child_pid,pipecnt);
						suspendedJob++;
						printf("\n");
						//printf("They are sleeping\n");
			      break;
					}
  			}
				//free(child_pid);
		}

		}

}

}

  return 0;
}
