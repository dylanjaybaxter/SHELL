/*
CPE 357 Lab07
Author: Dylan Baxter (dybaxter)
File: mush2.c
Description: This file contains a the main functionality for a limited shell
*/

/*Includes*/
/*IO*/
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>

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
#define DEBUG 1
#define PARENT 1
#define CHILD 0
#define READ_END 0
#define WRITE_END 1

/*Prototypes*/
void handler(int signal);

int main(int argc, char const *argv[]) {
    /*Define Variables*/
    int fdin = 0;
    int fdout = 1;
    int readFromFile = 0;
    FILE* fptr;
    char* line;
    char pwd[PATH_MAX];

    /*Pipeline vars*/
    pipeline pipeln;
    clstage curStage;
    int postpipe[2];
    int prepipe[2];
    int stage = 0;

    /*Forking Vars*/
    int forkVal;
    int numProc;
    int childStat;
    int pid;
    sigset_t procMask;

    /*Setup interrupt handler OR Block signal*/
    struct sigaction sa;
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    sigemptyset(&procMask);
    sigaddset(&procMask, SIGINT);
    /*sigprocmask(SIG_BLOCK, &procMask, NULL);*/

    if(DEBUG){
        printf("Args: %d", argc);
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
        printf("Usage error");
        exit(EXIT_FAILURE);
    }

    /*Print the marker*/
    if(NULL == getcwd(pwd, PATH_MAX)){
        perror("PWD");
        exit(EXIT_FAILURE);
    }
    if(!readFromFile){
        printf("/%s:8-P ", pwd);
    }

    /*Read fd line by line until EOF(^D)*/
    while((line = readLongString(fptr)) != NULL){
        /*Parse line to get command info*/
        if(NULL == (pipeln = (pipeline)crack_pipeline(line))){
            if(DEBUG){
                printf("INVALID COMMAND\n");
            }
        }
        /*Free line*/
        free(line);
        else{
            if(pipeln->length == 0){
                printf("What the hell\n -David Lynch\n");
            }
            if(DEBUG){
                print_pipeline(stdout, pipeln);
            }

            /*Check for cd and run if present*/
            if((pipeln->length == 1) &&
            !(strcmp(pipeln->stage->argv[0],"cd\0"))){
                if(DEBUG){
                    printf("cd detected...\n");
                }
                if(-1 == chdir(pipeln->stage->argv[1])){
                    perror(pipeln->stage->argv[1]);
                }
                if(NULL == getcwd(pwd, PATH_MAX)){
                    perror("PWD");
                    exit(EXIT_FAILURE);
                }
            }
            /*Fork child processes to create pipelnline*/
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
                            exit(EXIT_FAILURE);
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
                        exit(EXIT_FAILURE);
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

                /*If exit as child*/
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
                /*If exit as parent*/
                else if(forkVal == PARENT){
                    /*Wait for all children to exit*/
                    if(DEBUG){
                        printf("Waiting for %d procs\n", numProc);
                    }
                    while(numProc > 0){
                        if(-1 == (pid = wait(&childStat))){
                            perror("Wait failed");
                            exit(EXIT_FAILURE);
                        }else{
                            if(DEBUG){
                                if(childStat){
                                    printf(
                                    "Process %d exited with an error value.\n",
                                    pid);
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
        /*Re-print the marker*/
        if(!readFromFile){
            printf("/%s:8-P ", pwd);
        }
        fflush(stdout);
        /*Free line*/
        free_pipeline(pipeln);
    }
    yylex_destroy();
    return 0;
}

void handler(int signal){
    if(DEBUG){
        printf("\n");
    }
}