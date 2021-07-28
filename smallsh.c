#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdbool.h>
#include <signal.h>

/* 
These resources were helpful to me in completing this assignment: 
- CS 344 Module 4 and 5 Explorations
- Brewster's 3.1 Video about Processes (https://www.youtube.com/watch?v=1R9h-H2UnLs&t=2699s)
- Brewster's 3.3 Video about Signals (https://www.youtube.com/watch?v=VwS3dx3uyiQ&t=3184s) 
- CodeVault's Video about Handling Signals (https://www.youtube.com/watch?v=jF-1eFhyz1U&list=PLfqABt5AS4FkW5mOn2Tn9ZZLLDwA3kZUY&index=18)
*/

#define MAX_COMMAND_LINE 2048
#define MAX_ARGS 512
#define MAX_PATH_LENGTH 4096

int exit_status = 0;

// Initialize the command struct
struct command
{
    char command[MAX_COMMAND_LINE];
    char *args[MAX_ARGS];
    char input_file[256];
    char output_file[256];
    bool background;
};

// This function will strip whitespace (including newline characters) from the beginning and end of a string
char *stripSpaces(char *string)
{
    // Set up a pointer to the end of the string
    char *end;
    end = string + strlen(string) - 1;

    // Shift forward the start of the string until a non-space character is reached
    while (string < end && isspace(string[0]) != 0)
        string++;

    // Shift back the end of the string until a non-space character is reached
    while (string <= end && isspace(end[0]) != 0)
    {
        end--;
    }

    // Add a null character after the new end of string and return it
    end[1] = '\0';
    return string;
}

// This function will take in a string and expand all instances of "$$" to the smallsh PID
char *varExpand(char *string)
{
    // Get the smallsh pid and convert to a string
    int smallsh_pid = getpid();
    char smallsh_pid_str[20];
    sprintf(smallsh_pid_str, "%d", smallsh_pid);

    // Create a new empty string - to build the expanded one
    char *expanded_cl;
    expanded_cl = calloc(MAX_COMMAND_LINE, sizeof(char));

    // Set up two pointers to the original string (these will move as we process the string)
    char *left;
    left = string;
    char *right;
    right = string + 1;

    // String is too short to need any variable expansion
    if (strlen(string) <= 1)
    {
        return string;
    }
    else
    {
        // Traverse the original string, looking for instances of "$$"
        while (right - string <= strlen(string))
        {
            // Found one
            if (*(right - 1) == '$' && *right == '$')
            {
                strncat(expanded_cl, left, right - left - 1);
                strcat(expanded_cl, smallsh_pid_str);
                left = right + 1;
                right++;
                right++;
            }
            // Keep going
            else
            {
                right++;
            }
        }
    }
    // Cat over any remaining characters to the new string
    strncat(expanded_cl, left, right - left - 1);

    // Clean up variables
    memset(string, '\0', MAX_COMMAND_LINE);
    strcpy(string, expanded_cl);
    free(expanded_cl);

    return string;
}

struct command *splitUpCommand(char *expanded_command_line)
{
    struct command *currCommand = calloc(1, sizeof(struct command));
    char *left = expanded_command_line;
    char *right = expanded_command_line + 1;
    int cl_length = strlen(expanded_command_line);
    int arg_index = 0;

    // Check if '&' at end of command line, if so set background to true
    if ((*(left + cl_length - 1) == '&') && (*(left + cl_length - 2) == ' '))
    {
        currCommand->background = true;
        *(left + cl_length - 2) = '\0';
        *(left + cl_length - 1) = '\0';
        cl_length--;
        cl_length--;
    }
    // Otherwise, set background to false
    else
    {
        currCommand->background = false;
    }

    // Traverse the command line, saving pointers to different parts and adding null terminators where needed
    while (right - expanded_command_line <= cl_length)
    {
        // Store the command in the struct
        if (left == expanded_command_line && (*right == ' ' || *right == '\0'))
        {
            *right = '\0';
            strcpy(currCommand->command, left);
            currCommand->args[arg_index] = left;
            arg_index++;
            left = right + 1;
        }
        // Store the input file name in the struct
        else if (*left == '<' && *right == ' ')
        {
            *left = '\0';
            left++;
            *left = '\0';
            left++;
            right++;
            while (*right != ' ' && *right != '\0')
            {
                right++;
            }
            *right = '\0';
            strcpy(currCommand->input_file, left);
            left = right + 1;
        }
        // Store the output file name in the struct
        else if (*left == '>' && *right == ' ')
        {
            *left = '\0';
            left++;
            *left = '\0';
            left++;
            right++;
            while (*right != ' ' && *right != '\0')
            {
                right++;
            }
            *right = '\0';
            strcpy(currCommand->output_file, left);
            left = right + 1;
        }
        // Store the args in the struct
        else if (*right == ' ' || *right == '\0')
        {
            *(left - 1) = '\0';
            *right = '\0';
            currCommand->args[arg_index] = left;
            left = right + 1;
            arg_index++;
        }
        right++;
    }
    currCommand->args[arg_index] = NULL;
    return currCommand;
}

// This function implements the cd command
// An absolute or relative path is okay
// If no arg was given, it will set the current working directory to the one specified in the HOME env var
int changeWorkingDir(char *dirPath)
{
    char absolute_path[MAX_PATH_LENGTH];
    char *cwd;

    // No arg was specified
    if (dirPath == NULL)
    {
        // Get the HOME path from the environment

        strcpy(absolute_path, getenv("HOME"));
    }
    // Arg was specified
    else
    {
        realpath(dirPath, absolute_path);
    }
    // Change cwd
    if (chdir(absolute_path) == 0)
    {
        return 0;
    }
    else
    {
        printf("That directory was not found.\n");
        return -1;
    }
}

int main()
{
    char command_line[MAX_COMMAND_LINE];
    memset(command_line, '\0', MAX_COMMAND_LINE);

    char stripped_command_line[MAX_COMMAND_LINE];
    memset(stripped_command_line, '\0', MAX_COMMAND_LINE);

    char expanded_command_line[MAX_COMMAND_LINE];
    memset(expanded_command_line, '\0', MAX_COMMAND_LINE);

    pid_t spawnPid;
    int child_status;

    // SIG_INT Handler - causes smallsh and background processes to ignore CTRL-C signal
    struct sigaction sigint_struct;
    sigint_struct.sa_handler = SIG_IGN;
    sigint_struct.sa_flags = 0;
    sigaction(SIGINT, &sigint_struct, NULL);

    // Displays colon symbol as prompt, exits if the command line is "exit"
    while (strcmp(command_line, "exit") != 0)
    {
        // Show command prompt (colon)
        printf(": ");

        // Get command line, strip off any extra whitespace
        memset(command_line, '\0', MAX_COMMAND_LINE);
        memset(stripped_command_line, '\0', MAX_COMMAND_LINE);
        memset(expanded_command_line, '\0', MAX_COMMAND_LINE);
        fgets(command_line, MAX_COMMAND_LINE, stdin);
        strcpy(stripped_command_line, stripSpaces(command_line));

        // Check if comment
        if (strncmp(stripped_command_line, "#", 1) == 0)
        {
            continue;
        }
        // Check if blank line
        if (strlen(stripped_command_line) == 0)
        {
            continue;
        }

        // At this point we have an actual command to work with

        // Variable expansion
        strcpy(expanded_command_line, varExpand(stripped_command_line));

        // Split up the components of the command line, save in a command struct
        struct command *currCommand = splitUpCommand(expanded_command_line);

        // Handle exit built-in command
        if (strcmp(currCommand->command, "exit") == 0)
        {
            break;
        }

        // Handle status built-in command
        else if (strcmp(currCommand->command, "status") == 0)
        {
            printf("%d\n", exit_status);
        }

        // Handle cd built-in command
        else if (strcmp(currCommand->command, "cd") == 0)
        {
            changeWorkingDir(currCommand->args[1]);
        }
        else
        {
            // Handle a background process
            if (currCommand->background == true)
            {
                spawnPid = -5;
                child_status = -5;

                // Create the child process
                spawnPid = fork();
                if (spawnPid > 0)
                {
                    printf("New background process id is %d\n", spawnPid);
                    fflush(stdout);
                }

                // Problem with spawning of child process
                if (spawnPid == -1)
                {
                    exit(1);
                }
                // Spawn worked, have the child execute the command
                else if (spawnPid == 0)
                {
                    // Handle any I/O redirection here, before calling exec

                    // Input redirection
                    if (strlen(currCommand->input_file) > 0)
                    {
                        int inputFD = open(currCommand->input_file, O_RDONLY | O_CLOEXEC);
                        if (inputFD == -1)
                        {
                            exit(1);
                        }
                        int result = dup2(inputFD, 0);
                        if (result == -1)
                        {
                            exit(1);
                        }
                    }
                    // If no input file is specified, it will redirect to '/dev/null'
                    else
                    {
                        int inputFD = open("/dev/null", O_RDONLY);
                        dup2(inputFD, 0);
                    }

                    // Output redirection
                    if (strlen(currCommand->output_file) > 0)
                    {
                        int outputFD = open(currCommand->output_file, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0777);
                        if (outputFD == -1)
                        {
                            exit(1);
                        }
                        int result = dup2(outputFD, 1);
                        if (result == -1)
                        {
                            exit(1);
                        }
                    }
                    // If no output file specified, it will redirect to '/dev/null'
                    else
                    {
                        int outputFD = open("/dev/null", O_WRONLY);
                        dup2(outputFD, 0);
                    }

                    // Do exec function, this executes any valid non built-in command
                    execvp(currCommand->args[0], currCommand->args);
                    exit(1);
                }               // Parent process continues taking commands, will save child status when it terminates
                waitpid(spawnPid, &child_status, WNOHANG);
            }
            // Handle a foreground process
            else
            {
                spawnPid = -5;
                child_status = -5;

                // Create the child process
                spawnPid = fork();

                // Problem with spawning of child process
                if (spawnPid == -1)
                {
                    printf("Uh oh, there was a problem spawning this process!\n");
                    fflush(stdout);
                    exit_status = 1;
                    exit(1);
                }
                // Spawn worked, have the child execute the command
                else if (spawnPid == 0)
                {
                    // Restore SIGINT default behavior for foreground processes
                    sigint_struct.sa_handler = SIG_DFL;
                    sigaction(SIGINT, &sigint_struct, NULL);

                    // Handle any I/O redirection here, before calling exec

                    // Input redirection
                    if (strlen(currCommand->input_file) > 0)
                    {
                        int inputFD = open(currCommand->input_file, O_RDONLY | O_CLOEXEC);
                        if (inputFD == -1)
                        {
                            printf("Input file not found.\n");
                            fflush(stdout);
                            exit_status = 1;
                            exit(1);
                        }
                        int result = dup2(inputFD, 0);
                        if (result == -1)
                        {
                            printf("Input redirection failed.\n");
                            fflush(stdout);
                            exit_status = 1;
                            exit(1);
                        }
                    }

                    // Output redirection
                    if (strlen(currCommand->output_file) > 0)
                    {
                        int outputFD = open(currCommand->output_file, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0777);
                        if (outputFD == -1)
                        {
                            printf("Output file not found.\n");
                            fflush(stdout);
                            exit_status = 1;
                            exit(1);
                        }
                        int result = dup2(outputFD, 1);
                        if (result == -1)
                        {
                            printf("Output redirection failed.\n");
                            fflush(stdout);
                            exit_status = 1;
                            exit(1);
                        }
                    }

                    // Do exec function, this executes any valid non built-in command
                    execvp(currCommand->args[0], currCommand->args);
                    printf("Command not found. Try again.\n");
                    fflush(stdout);
                    exit_status = 1;
                    exit(1);
                }
                // Parent process waits, changes exit status to 1 if the child failed, 0 if all good
                waitpid(spawnPid, &child_status, 0);

                // If the child was terminated by a signal, print the signal number
                if (WIFSIGNALED(child_status))
                {
		            printf("Foreground process terminated by signal %d\n", WTERMSIG(child_status));
                    fflush(stdout);
	            }
                if (child_status != 0)
                {
                    exit_status = 1;
                }
                else
                {
                    exit_status = 0;
                }
            }
        }
        // Check for background processes which have terminated before showing the prompt again
        // If none have terminated, just keep prompting
        while ((spawnPid = waitpid(-1, &child_status, WNOHANG)) > 0)
        {
            // If the child was terminated by a signal, print the signal number
            if (WIFSIGNALED(child_status))
            {
		        printf("Background process terminated by signal %d\n", WTERMSIG(child_status));
                fflush(stdout);
	        } 
            else
            {
            printf("Background process %d terminated with an exit status of %d\n", spawnPid, child_status);
            fflush(stdout);
            }
        }
        free(currCommand);
    }
    exit(exit_status);
}

