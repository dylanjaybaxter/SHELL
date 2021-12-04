/*
CPE 357 Asgn6
Author: Dylan Baxter (dybaxter)
File: mush2.c
Description: This file contains a the main
functionality for a limited shell
*/

/*Includes*/
/*IO*/
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/types.h>

/*Signals*/
#include <signal.h>

/*Forking*/
#include <unistd.h>
#include <sys/wait.h>

/*other*/
#include <string.h>
#include <limits.h>
#include "mush.h"

/*Macros*/
#define DEBUG 0
#define CWD 0
#define PARENT 1
#define CHILD 0
#define READ_END 0
#define WRITE_END 1

/*Shell Colors*/
#define BLU "\x1b[36m"
#define GRN "\x1b[32m"
#define RST "\x1b[0m"

/*Prototypes*/
void handler(int sig);
int wrotePrompt = 0;

int main(int argc, char const *argv[]) {
    /*Define Variables*/
    int fdin = 0;
    int fdout = 1;
    int readFromFile = 0;
    FILE* fptr = NULL;
    char* line = NULL;
    char pwd[PATH_MAX];

    /*Pipeline vars*/
    pipeline pipeln = NULL;
    clstage curStage = NULL;
    int postpipe[2];
    int prepipe[2];
    int stage = 0;

    /*Forking Vars*/
    int forkVal = 1;
    int numProc = 0;
    int childStat = 0;
    int pid = 0;
    sigset_t procMask;

    /*Info*/
    char user[PATH_MAX] = {0};
    char computer[PATH_MAX] = {0};
    char* home;
    struct passwd* pswd;

    /*Set the signal handler*/
    signal(SIGINT, handler);

    /*Get Device and User Info*/
    if(-1 == gethostname(computer, PATH_MAX)){
        perror("Getting Host Name");
    }
    if(-1 == getlogin_r(user, PATH_MAX)){
        perror("Getting Host Name");
    }

    if(DEBUG){
        printf("Args: %d\n", argc);
    }

    /*If 1 arg*/
    if(argc == 1){
        /*Set fd to stdin*/
        fptr = stdin;
    }
    /*If 2 arg, open file*/
    else if(argc == 2){
        /*Open file*/
        if(NULL == (fptr = fopen(argv[1], "r"))){
            perror(argv[1]);
            exit(EXIT_FAILURE);
        }
        readFromFile = 1;
    }
    else{
        printf("Usage error: mush2 [input file(optional)]\n");
        exit(EXIT_FAILURE);
    }

    /*Print the marker*/
    if(NULL == getcwd(pwd, PATH_MAX)){
        perror("PWD");
        exit(EXIT_FAILURE);
    }
    if(!readFromFile && (isatty(0)&&isatty(1)) && CWD){
        printf(GRN"%s@%s" RST ":" BLU"~%s" RST " 8-P ",
        user, computer, pwd);
    }
    else if(!readFromFile && (isatty(0)&&isatty(1))){
        printf("8-P ");
    }

/*---------Read fd line by line until EOF(^D)---------------------*/
    while(((line = readLongString(fptr)) != NULL)){
        /*New line! No prompt!*/
        wrotePrompt = 0;
        /*Parse line to get command info*/
        if(DEBUG){
            printf("CREATING PIPELINE\n");
        }
        if(NULL == (pipeln = (pipeline)crack_pipeline(line))){
            if(DEBUG){
                printf("INVALID COMMAND\n");
            }
        }
        else{
            if(pipeln->length == 0){
                printf("What the hell\n -David Lynch\n");
            }
            if(DEBUG){
                print_pipeline(stdout, pipeln);
            }

/*-----------Check for cd and run if present-----------------------*/
            if((pipeln->length == 1) &&
            !(strcmp(pipeln->stage->argv[0],"cd\0"))){
                if(DEBUG){
                    printf("cd detected...\n");
                }
                /*Clear Home*/
                home = NULL;
                /*Check for cd*/
                if((pipeln->stage->argv[1] == NULL) ||
                !(strcmp(pipeln->stage->argv[1],"~\0"))){
                    /*Try finding home in PATH*/
                    if(NULL == (home = getenv("HOME"))){
                        /*Try finding home in pwd*/
                        if(NULL == (pswd = (struct passwd*)getpwuid(getuid()))){
                            /*Give up*/
                            fprintf(stderr,
                                "unable to determine home directory");
                        }else{
                            home = pswd->pw_dir;
                        }
                    }
                    if(DEBUG){
                        printf("HOME: %s\n",getenv("HOME"));
                    }
                    if(-1 == chdir(home)){
                        perror("chdir");
                    }
                }else{
                    if(-1 == chdir(pipeln->stage->argv[1])){
                        perror(pipeln->stage->argv[1]);
                    }
                }
                if(NULL == getcwd(pwd, PATH_MAX)){
                    perror("PWD");
                    exit(EXIT_FAILURE);
                }
            }
/*----------------------Set file descriptors, pipe------------------------*/
/*-------------------------and fork children------------------------------*/
            else{
                /*Set the current stage*/
                stage = 0;
                curStage = (clstage)&(pipeln->stage[stage]);
                while(stage<(pipeln->length)){
                    /*Set fdin*/
                    /*If input is null*/
                    if(curStage->inname == NULL){
                        /*If first stage, set to stdin*/
                        if(stage == 0){
                            if(DEBUG){
                                printf("Stage %d: fdin is stdin\n",stage);
                            }
                            fdin = 0;
                        }
                        /*If else set to pipe value*/
                        else{
                            fdin = prepipe[READ_END];
                            if(DEBUG){
                                printf("Stage %d: fdin is pipe(%d)\n",
                                stage, prepipe[READ_END]);
                            }
                        }
                    }/*If input is named, open file*/
                    else{
                        if(-1 == (fdin = open(curStage->inname,
                            O_RDONLY, 0666))){
                            perror(curStage->inname);
                            forkVal = -1;
                            break;
                        }
                        if(DEBUG){
                            printf("Stage %d: fdin is %s(%d)\n",
                            stage, curStage->inname, fdin);
                        }
                    }


                    /*If there is another stage, pipe*/
                    if(stage < ((pipeln->length)-1)){
                        if(-1 == pipe(postpipe)){
                            perror("Piping");
                            break;
                        }
                        if(DEBUG){
                            printf("Created pipe [%d %d]...\n",
                            postpipe[READ_END], postpipe[WRITE_END]);
                        }
                    }

                    /*Set fdout*/
                    if(curStage->outname == NULL){
                        /*If first stage, set to stdin*/
                        if(stage == ((pipeln->length)-1)){
                            fdout = 1;
                            if(DEBUG){
                                printf("Stage %d: fdout is stdout\n",
                                stage);
                            }
                        }
                        /*If else set to pipe value*/
                        else{
                            fdout = postpipe[WRITE_END];
                            if(DEBUG){
                                printf("Stage %d: fdout is postpipe(%d)\n",
                                stage, postpipe[WRITE_END]);
                            }
                        }
                    }/*If output is named, open file*/
                    else{
                        if(-1 == (fdout = open(curStage->outname,
                            O_WRONLY|O_CREAT|O_TRUNC, 0666))){
                            perror(curStage->outname);
                            if(stage > 0){
                                close(prepipe[WRITE_END]);
                                close(prepipe[READ_END]);
                            }
                            forkVal = -1;
                            break;
                        }
                        if(DEBUG){
                            printf("Stage %d: fdout is %s(%d)\n",
                            stage, curStage->outname, fdin);
                        }
                    }

                    /*Spawn and break child from loop*/
                    if(-1 == (forkVal = fork())){
                        perror("Fork Failed");
                        break;
                    }
                    if(forkVal == CHILD){
                        if(DEBUG){
                            printf("Child Created [%d %d]\n", fdin, fdout);
                        }
                        break;
                    }
                    /*Increment processes to wait for*/
                    numProc = numProc+1;

                    /*Move to next stage*/
                    stage = stage + 1;
                    curStage = (clstage)&(pipeln->stage[stage]);
                    if(DEBUG){
                        if(stage<(pipeln->length)){
                            printf("Should move to next stage\n");
                        }else{
                            printf("Last stage\n");
                        }
                    }
                    if(stage > 1){
                        if(DEBUG){
                            printf("Parent closing %d and %d\n",
                            prepipe[WRITE_END],
                            prepipe[READ_END]);
                        }
                        close(prepipe[WRITE_END]);
                        close(prepipe[READ_END]);
                    }
                    prepipe[WRITE_END] = postpipe[WRITE_END];
                    prepipe[READ_END] = postpipe[READ_END];
                }

/*----------------------If exit as Child------------------------------*/
                if(forkVal == CHILD){
                    /*Unblock SIGINT*/
                    sigprocmask(SIG_UNBLOCK, &procMask, NULL);

                    /*Hook up file descriptors*/
                    if(DEBUG){
                        printf("Assigning %d to %d\n", fdin, 0);
                        printf("Assigning %d to %d\n", fdout, 1);
                    }
                    dup2(fdin, 0);
                    dup2(fdout, 1);


                    /*Close pipes*/
                    if(stage > 0){
                        if(DEBUG){
                            printf("Closing %d %d\n",
                            prepipe[READ_END],prepipe[WRITE_END]);
                        }
                        close(prepipe[READ_END]);
                        close(prepipe[WRITE_END]);
                    }
                    if((stage > 0) &&(stage < (pipeln->length-1))){
                        if(DEBUG){
                            printf("Closing %d %d\n",
                            postpipe[READ_END],postpipe[WRITE_END]);
                        }
                        close(postpipe[READ_END]);
                        close(postpipe[WRITE_END]);
                    }

                    /*Execute order 66*/
                    if(-1 == execvp(curStage->argv[0], curStage->argv)){
                        perror(curStage->argv[0]);
                    }
                    return -1;
                }
/*----------------------If exit as PARENT------------------------------*/
                else if(forkVal){
                    /*Wait for all children to exit*/
                    if(DEBUG){
                        printf("Waiting for %d procs\n", numProc);
                    }
                    while(numProc > 0){
                        if(-1 == (pid = wait(&childStat))){
                            if(WSTOPSIG(childStat) == SIGINT ||
                            childStat == 0){
                                /*Keep Waiting*/
                            }else{
                                if(DEBUG){
                                    printf("stat %d is not sigint %d\n",
                                    childStat, SIGINT);
                                }
                                perror("Wait failed");
                                exit(EXIT_FAILURE);
                            }
                        }else{
                            if(DEBUG){
                                if(childStat){
                                    printf(
                                    "Process %d exited with an error %d.\n",
                                    pid, childStat);
                                }
                                else{
                                    printf("Process %d suceeded.\n", pid);
                                }
                            }
                            fflush(stdout);
                            numProc--;
                        }
                    }
                }
            }
        }
        if(DEBUG){
            printf("FREEING ELEMENTS\n");
        }
        /*Free pipeline*/
        free_pipeline(pipeln);
        /*Free line*/
        free(line);

        /*Re-print the marker*/
        if(!readFromFile && (isatty(0)&&isatty(1)) && CWD){
            printf(GRN"%s@%s" RST ":" BLU"~%s" RST " 8-P ",
            user, computer, pwd);
        }
        else if(!readFromFile && !wrotePrompt && (isatty(0)&&isatty(1))){
            printf("8-P ");
        }
        /*Flush output*/
        fflush(stdout);

    }

    /*Clean up some remaining memory*/
    yylex_destroy();

    /*Close input file and exit*/
    if(readFromFile){
        if(-1==fclose(fptr)){
            perror("fclose");
            exit(EXIT_FAILURE);
        }
    }else{
        if((isatty(0)&&isatty(1))){
            printf("\n");
        }
    }
    if(DEBUG){
        printf("\nExiting Normally");
    }

    return 0;
}

void handler(int sig){
    /*Reset signal handler*/
    signal(SIGINT, handler);
    /*Reprompt*/
    if((isatty(0)&&isatty(1))){
        printf("\n8-P ");
    }
    /*Signal that prompt has been written*/
    wrotePrompt = 1;
    /*Slush stdout*/
    fflush(stdout);
}