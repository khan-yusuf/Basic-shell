/*
 * Yusuf Khan
 * A simple c-shell for linux
 */


#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <sys/wait.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

#define MAXCHARACTERS 2000 // max number of characters in a command line
#define MAXTOKENS 67 // max number of tokens

void defaultSignals();

typedef struct process{
    pid_t pid;
    char* cmd;
    char** parsedCmd;

    int bg;
    int stdinput;
    int stdoutput;
    int stderror;
} process_t;


typedef struct job{ /* GROUP OF PROCESSES */
    pid_t pgid; //group id

    char* cmd;
    char** parsedCmd;

    process_t* head; //head of LL of processes

    int state; //0 for running, 1 for stopped, 2 for done
    int bg; //1 if bg, 0 for fg
    int signal; //Given to all processes in group

} job_t;


int readInput(char* cmd) {
    char* in;
    in = readline("# ");

    /*if(!in){
        _exit(0); //^D to exit shell
    }*/
    if(strlen(in) > 0) {
        add_history(in);
        strcpy(cmd, in);
        return 1;
    }
    else {
        return 0;
    }
}


void parseSpace(char* cmd, char** parsedCmd, int* argc) {
    int i;
    for(i = 0; i < MAXTOKENS; i++){
        parsedCmd[i] = strsep(&cmd, " ");

        if(parsedCmd[i] == NULL){
            break;
        }
        if((strlen(parsedCmd[i]) == 0)){
            i--;
        }
    }
    *argc = i; //number of tokens in command
}


int parsePipe(char* cmd, char** pipedCmd) {
    for(int i = 0; i <= 1; i++) {
        pipedCmd[i] = strsep(&cmd, "|");

        if(pipedCmd[i] == NULL){ // returns 0 if no pipe found
            return 0;
        }
    }
    return 1; // returns 1 for pipe found
}


int parseCommand(char* cmd, char** parsedCmd, int* argc, char** parsedCmdPipe, int* argcpipe) {
    if(strcmp(cmd, "jobs") == 0){
        //display jobs prompt
        return 2;
    }
    if(!strcmp(cmd, "bg")){
        //send SIGCONT to recently stopped process
        return 2;
    }
    if(!strcmp(cmd, "fg")){
        //bring recent process to foreground
        return 2;
    }

    char* pipedCmd[2]; // holds left and right side of piped command
    int hasPipe = parsePipe(cmd, pipedCmd);

    if(hasPipe){
        parseSpace(pipedCmd[0], parsedCmd, argc); //cmd to the left of pipe
        parseSpace(pipedCmd[1], parsedCmdPipe, argcpipe); //cmd to the right of pipe
    }
    else{
        parseSpace(cmd, parsedCmd, argc);
    }
    return hasPipe;
}

void fileRedirection(char** parsedCmd, int* argc) {
    char outputFile[30], inputFile[30], errorFile[30];
    int in = 0, out = 0, err = 0;

    for(int i = 0; i < MAXTOKENS; i++){
        if(i > *argc - 1 || parsedCmd[i] == NULL){
            continue; //ignores NULL values in parsedCmd
        }
        else if(strcmp(parsedCmd[i], "<") == 0){ //input redirection
            parsedCmd[i] = NULL;
            strcpy(inputFile, parsedCmd[i + 1]);
            parsedCmd[i + 1] = NULL;
            in = 1;
        }
        else if(strcmp(parsedCmd[i], ">") == 0){ //output redirection
            parsedCmd[i] = NULL;
            strcpy(outputFile, parsedCmd[i + 1]);
            parsedCmd[i + 1] = NULL;
            out = 1;
        }
        else if(strcmp(parsedCmd[i], "2>") == 0){ //error redirection
            parsedCmd[i] = NULL;
            strcpy(errorFile, parsedCmd[i + 1]);
            parsedCmd[i + 1] = NULL;
            err = 1;
        }
        else if(strcmp(parsedCmd[i], "&") == 0){ //process is background
            parsedCmd[i] = NULL;
            //process -> background = true and terminal not take over
        }
    }

    if(in){
        int fd0 = open(inputFile, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
        if(fd0 < 0){
            perror(inputFile);
            exit(0);
        }
        dup2(fd0, 0);
        close(fd0);
    }
    if(out){
        int fd1 = creat(outputFile, 0644);
        dup2(fd1, 1);
        close(fd1);
    }
    if(err){
        int fd2 = creat(errorFile, 0644);
        dup2(fd2, 2);
        close(fd2);
    }
}


void executeCmd(char** parsedCmd, int* argc) {
    pid_t pid = fork();

    if(pid == 0){
        setpgid(getpid(), 0);
        tcsetpgrp(0, getpgid(getpid())); //gives control of terminal to child process
        defaultSignals();

        fileRedirection(parsedCmd, argc);

        execvp(parsedCmd[0], parsedCmd); // overlays child process
        exit(0);
    }
    else{
        wait(NULL); // while parent process waits
        return;
    }
}

void defaultSignals() {
    signal(SIGQUIT, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
}

void ignoreSignals() {
    signal (SIGQUIT, SIG_IGN);
    signal (SIGINT, SIG_IGN);
    signal (SIGTTOU, SIG_IGN);
    signal (SIGCHLD, SIG_IGN);
    signal (SIGTSTP, SIG_IGN);
    signal (SIGTTIN, SIG_IGN);
}


void executePipedCmd(char** parsedCmd, int* argc, char** parsedCmdPipe, int* argcpipe) {
    int pipefd[2];
    pipe(pipefd);

    if(fork() == 0){
        close(pipefd[0]); //closes read
        dup2(pipefd[1], 1);
        fileRedirection(parsedCmd, argc);
        execvp(parsedCmd[0], parsedCmd); //left command
    }
    if(fork() == 0){
        close(pipefd[1]); //closes write
        dup2(pipefd[0], 0);
        fileRedirection(parsedCmdPipe, argcpipe);
        execvp(parsedCmdPipe[0], parsedCmdPipe); //right command
    }
    close(pipefd[0]);
    close(pipefd[1]);
    wait(NULL); //waits for both child processes to finish before continuing to parent
    wait(NULL);
}


int main(void) {
    char cmd[MAXCHARACTERS];
    char* parsedCmd[MAXTOKENS];
    char* parsedCmdPipe[MAXTOKENS];
    int argc = 0, argcpipe = 0;

    ignoreSignals();

    while(1) {
        if(!readInput(cmd)) { //keeps polling for non-zero length input
            continue;
        }

        int execFlag = parseCommand(cmd, parsedCmd, &argc, parsedCmdPipe, &argcpipe);

        if(execFlag == 0){ //regular command
            executeCmd(parsedCmd, &argc);
        }
        else if(execFlag == 1){ //contains pipe
            executePipedCmd(parsedCmd, &argc, parsedCmdPipe, &argcpipe);
        }
        else if(execFlag == 2){
            //jobs control
        }
    }
}