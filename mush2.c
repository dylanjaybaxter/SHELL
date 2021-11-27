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
#include "mush.h"

/*Macros*/
#define DEBUG 1
#define PARENT 1
#define CHILD 0

/*Prototypes*/
void handler(int signal);

int main(int argc, char const *argv[]) {
    /*Define Variables*/
    int fdin = 0;
    int fdout = 1;
    FILE* fptr;
    char* line;

    /*Pipeline vars*/
    pipeline pipeln;
    struct clstage *curStage;
    int postpipe[2];
    int prepipe[2];
    int stageOneFlag;

    /*Forking Vars*/
    int forkVal;
    int numProc;
    int childStat;
    int pid;

    /*Setup interrupt handler*/
    struct sigaction sa;
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

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
    }
    else{
        printf("Usage error");
        exit(EXIT_FAILURE);
    }

    /*Print the marker*/
    printf(":-P");
    /*Read fd line by line until EOF(^D)*/
    while((line = readLongString(fptr)) != NULL){
        /*Parse line to get command info*/
        pipeln = (pipeline)crack_pipeline(line);
        if(pipeln->length == 0){
            printf("What the hell\n -David Lynch\n");
        }
        if(DEBUG){
            print_pipeline(stdout, pipeln);
        }

        /*Check for cd and run if present*/
        if((pipeln->length == 1) && !(strcmp(pipeln->stage->argv[0],"cd\0"))){
            if(DEBUG){
                printf("cd detected...\n");
            }
            chdir(pipeln->stage->argv[1]);
        }
        /*Fork child processes to create pipelnline*/
        else{
            /*Set the current stage*/
            curStage = pipeln->stage;
            while(curStage != NULL){
                /*Set fdin*/
                /*If input is null*/
                if(curStage->inname == NULL){
                    /*If first stage, set to stdin*/
                    if(stageOneFlag){
                        fdin = 0;
                    }
                    /*If else set to pipe value*/
                    else{
                        fdin = prepipe[1];
                    }
                }/*If input is named, open file*/
                else{
                    if(-1 == (fdin = open(curStage->inname,
                        O_RDONLY, 0666))){
                        perror(curStage->inname);
                        exit(EXIT_FAILURE);
                    }
                }

                /*If there is another stage, pipe*/
                if(curStage->next != NULL){
                    if(DEBUG){
                        printf("Creating pipe...\n");
                    }
                    if(-1 == pipe(postpipe)){
                        perror("Piping");
                        exit(EXIT_FAILURE);
                    }
                }

                /*Set fdout*/
                if(curStage->outname == NULL){
                    /*If first stage, set to stdin*/
                    if(curStage->next == NULL){
                        fdout = 0;
                    }
                    /*If else set to pipe value*/
                    else{
                        fdout = postpipe[0];
                    }
                }/*If output is named, open file*/
                else{
                    if(-1 == (fdin = open(curStage->outname,
                        O_WRONLY|O_CREAT|O_TRUNC, 0666))){
                        perror(curStage->outname);
                        exit(EXIT_FAILURE);
                    }
                }

                /*Spawn and break child from loop*/
                if(-1 == (forkVal = fork())){
                    perror("Fork Failed");
                    exit(EXIT_FAILURE);
                }
                if(forkVal == CHILD){
                    if(DEBUG){
                        printf("Child Created...\n");
                    }
                    break;
                }
                /*Increment processes to wait for*/
                numProc = numProc+1;

                /*Move to next stage*/
                curStage = curStage->next;
                if(!stageOneFlag){
                    close(prepipe[0]);
                    close(prepipe[1]);
                }else{
                    stageOneFlag = 0;
                }
                prepipe[0] = postpipe[0];
                prepipe[1] = postpipe[1];
            }

            /*If exit as child*/
            if(forkVal == CHILD){
                /*Hook up file descriptors*/
                dup2(fdin, 0);
                dup2(fdout, 1);

                /*Close pipes*/
                close(prepipe[0]);
                close(prepipe[1]);
                close(postpipe[0]);
                close(postpipe[1]);

                /*Execute order 66*/
                if(-1 == execvp(curStage->argv[0], curStage->argv)){
                    perror("Execvp");
                    exit(EXIT_FAILURE);
                }
            }
            /*If exit as parent*/
            else{
                /*Close Remaining Pipe*/
                close(prepipe[0]);
                close(prepipe[1]);

                /*Wait for all children to exit*/
                while(numProc < 0){
                    if(-1 == (pid = wait(&childStat))){
                        perror("Wait failed");
                        exit(EXIT_FAILURE);
                    }
                    if(childStat){
                        printf("Process %d exited with an error value.\n", pid);
                    }
                    else{
                        printf("Process %d suceeded.\n", pid);
                    }
                    numProc--;
                }
                /*Re-print the marker*/
                printf(":-P");
                fflush(stdout);
            }

        }
    }
    yylex_destroy();
    return 0;
}

void handler(int signal){
    if(DEBUG){
        printf("Child process exiting\n");
    }
    exit(SIGINT);
}