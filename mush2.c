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

/*Prototypes*/
void handler(int signal);

int main(int argc, char const *argv[]) {
    /*Define Variables*/
    int fdin = 0;
    int fdout = 1;
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
    if(NULL == getcwd(pwd, PATH_MAX)){
        perror("PWD");
        exit(EXIT_FAILURE);
    }
    printf("%s:-P ", pwd);

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
            if(-1 == chdir(pipeln->stage->argv[1])){
                perror("chdir");
                exit(EXIT_FAILURE);
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
                        fdin = prepipe[1];
                        if(DEBUG){
                            printf("Stage %d: fdin is pipe(%d)\n",
                            stage, prepipe[1]);
                        }
                    }
                }/*If input is named, open file*/
                else{
                    if(-1 == (fdin = open(curStage->inname,
                        O_RDONLY, 0666))){
                        perror(curStage->inname);
                        exit(EXIT_FAILURE);
                    }
                    if(DEBUG){
                        printf("Stage %d: fdin is %s\n",
                        stage, curStage->inname);
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
                        postpipe[0], postpipe[1]);
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
                        fdout = postpipe[0];
                        if(DEBUG){
                            printf("Stage %d: fdin is postpipe(%d)\n",
                            stage, postpipe[0]);
                        }
                    }
                }/*If output is named, open file*/
                else{
                    if(-1 == (fdin = open(curStage->outname,
                        O_WRONLY|O_CREAT|O_TRUNC, 0666))){
                        perror(curStage->outname);
                        exit(EXIT_FAILURE);
                    }
                    if(DEBUG){
                        printf("Stage %d: fdin is %s\n",
                        stage, curStage->outname);
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
                if(curStage != NULL){
                    printf("Should move to next stage\n");
                }else{
                    printf("Last stage\n");
                }
                if(stage > 0){
                    close(prepipe[0]);
                    close(prepipe[1]);
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
                }
                return -1;
            }
            /*If exit as parent*/
            else{
                /*Close Remaining Pipe*/
                close(prepipe[0]);
                close(prepipe[1]);

                /*Wait for all children to exit*/
                if(DEBUG){
                    printf("Waiting for %d procs\n", numProc);
                }
                while(numProc > 0){
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
                    fflush(stdout);
                    numProc--;
                }
                /*Re-print the marker*/
                printf("%s:-P ", pwd);
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