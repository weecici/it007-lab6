#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define PROMPT "it007sh>"
#define MAX_INPUT 512
#define HISTORY_SIZE 3
#define MAX_ARGS 100
#define CMD_ERR "Command not found"
#define TOK " (<;>)|"
#define CASES ";<>|"
#define NO_MATCH_CASE 10000

struct termios original;
int subprocessID = -1;

void enableRawMode()
{
    tcgetattr(STDIN_FILENO, &original); // Save original terminal attributes
    struct termios raw = original;
    raw.c_lflag &= ~(ICANON | ECHO); // Disable canonical mode and echo
    raw.c_cc[VMIN] = 1;              // Minimum number of bytes for read
    raw.c_cc[VTIME] = 0;             // No timeout for read
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void updateCurrentInput(char *currentInput, char history[HISTORY_SIZE][MAX_INPUT], const int historyIndex)
{
    strcpy(currentInput, history[historyIndex]);
    printf("\r\033[K%s %s", PROMPT, currentInput);
    fflush(stdout);
}

// void handleArrowUp(char *currentInput, char history[HISTORY_SIZE][MAX_INPUT], int *historyIndex)
// {
//     if (*historyIndex > 0)
//         updateCurrentInput(currentInput, history, --(*historyIndex));
// }
// void handleArrowDown(char *currentInput, char history[HISTORY_SIZE][MAX_INPUT], int *historyIndex,
//                      const int historyCount)
// {
//     if (*historyIndex < historyCount + 1)
//         updateCurrentInput(currentInput, history, ++(*historyIndex));
// }

void handleSigint()
{
    if (subprocessID > 0)
        kill(subprocessID, SIGINT);
}

void handleSigquit()
{
    handleSigint();
    printf("\n%s Goodbye! Exiting...\n", PROMPT);
    exit(0);
}

void execCmd(char *args[MAX_ARGS])
{
    execvp(args[0], args);
    printf("%s: %s\n", CMD_ERR, args[0]);
    exit(0);
}

void execSingleCmd(char *args[MAX_ARGS])
{
    if (!args[0])
        return;

    if (!strcmp(args[0], "exit"))
        handleSigquit();

    subprocessID = fork();
    if (subprocessID == 0)
        execCmd(args);
    wait(NULL);
}

void execRedirectedOutCmd(char *args[MAX_ARGS], char *file)
{
    subprocessID = fork();
    if (subprocessID == 0)
    {
        int file_desc = open(file, O_CREAT | O_TRUNC | O_WRONLY);
        dup2(file_desc, 1);
        close(file_desc);

        execCmd(args);
    }
    wait(NULL);
}

void handlePipelineCmd(int fd1, int fd2, char *args[MAX_ARGS])
{
    subprocessID = fork();
    if (subprocessID == 0)
    {
        dup2(fd1, fd2);
        execCmd(args);
    }
    close(fd1);
    wait(NULL);
}

int main()
{
    char currentInput[MAX_INPUT] = {0};
    char history[HISTORY_SIZE][MAX_INPUT];
    int historyCount = 0;
    int historyIndex = 0;
    int inputLength = 0; // Length of the current input

    enableRawMode();

    printf("it007sh> ");

    signal(SIGINT, handleSigint);
    signal(SIGQUIT, handleSigquit);

    while (1)
    {
        char c = getchar();

        if (c == '\033')
        {
            if (getchar() == '[')
            {
                switch (getchar())
                {
                case 'A':
                    if (historyIndex > 0)
                        updateCurrentInput(currentInput, history, --historyIndex);
                    break;
                case 'B':
                    if (historyIndex < historyCount + 1)
                        updateCurrentInput(currentInput, history, ++historyIndex);
                    break;
                }
                inputLength = strlen(currentInput);
            }
        }
        else if (c == '\n')
        {
            if (inputLength > 0)
            {
                if (historyCount < HISTORY_SIZE)
                    strcpy(history[historyCount++], currentInput);
                else
                {
                    for (int i = 1; i < HISTORY_SIZE; i++)
                        strcpy(history[i - 1], history[i]);
                    strcpy(history[HISTORY_SIZE - 1], currentInput);
                }
                historyIndex = historyCount;
            }

            printf("\n");

            // Tokenizing command
            int argIndex = 0, caseIndex = NO_MATCH_CASE;
            char *args1[MAX_ARGS], *args2[MAX_ARGS];
            char specialCase = ' ';
            args1[0] = args2[0] = NULL;

            // Find case <, >, |, ;
            for (int i = 0; i < strlen(currentInput); ++i)
            {
                if (strchr(CASES, currentInput[i]))
                {
                    caseIndex = i;
                    specialCase = currentInput[caseIndex];
                    break;
                }
            }

            char *token = strtok(currentInput, TOK);

            // Tokenizing first part of cmd to args1
            while (token)
            {
                if ((token - currentInput) / sizeof(char) > caseIndex)
                    break;
                args1[argIndex++] = token;
                token = strtok(NULL, TOK);
            }
            args1[argIndex] = NULL;

            // Tokenizing 2nd part of cmd to args2
            argIndex = 0;
            while (token)
            {
                args2[argIndex++] = token;
                token = strtok(NULL, TOK);
            }
            args2[argIndex] = NULL;

            // Done tokenizing

            if (specialCase == '>')
                execRedirectedOutCmd(args1, args2[0]);
            else if (specialCase == '<')
                execRedirectedOutCmd(args2, args1[0]);
            else if (specialCase == '|')
            {
                int fd[2];
                pipe(fd);
                handlePipelineCmd(fd[1], STDOUT_FILENO, args1); // redirect output of stdout to write end
                handlePipelineCmd(fd[0], STDIN_FILENO, args2);  // redirect input of read end to stdin
            }
            else if (specialCase == ';')
            {
                execSingleCmd(args1);
                execSingleCmd(args2);
            }
            else
                execSingleCmd(args1);

            memset(currentInput, 0, MAX_INPUT);
            inputLength = 0;

            printf("%s ", PROMPT);
            fflush(stdout);
        }
        else
        {
            if (c == 127 && inputLength > 0) // Backspace key
                currentInput[--inputLength] = '\0';

            else if (inputLength + 1 < MAX_INPUT) // Regular char
            {
                currentInput[inputLength++] = c;
                currentInput[inputLength] = '\0';
            }
            printf("\r\033[K%s %s", PROMPT, currentInput);
        }
    }

    return 0;
}
