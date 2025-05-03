#define _GNU_SOURCE
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

#define MAX_PIPES 9
#define MAX_COMMANDS (MAX_PIPES + 1)

void find_and_remove_zombies(int signum) {
  while (waitpid(-1, NULL, WNOHANG) > 0) {
    // This loop will remove all terminated child processes.
    // In the moment that there is no child process to remove, the loop will stop
    // and execution will continue.
  };
}

// returns 1 if waitpid was successful or if it failed with ECHILD or EINTR, 
// returns 0 if it failed with any other error
int waitpid_which_allows_echild_eintr_errors(pid_t pid, int *status, int options) {
  if (waitpid(pid, status, options) == -1) {
      if (errno == ECHILD || errno == EINTR) {
          return 1;
      } else {
          perror("error in waitpid");
          return 0;
      }
  }
  return 1;
}

void execute_command(char** arglist, int is_background) {
  if (is_background != 0) {
    signal(SIGINT, SIG_IGN); 
  } else {
    signal(SIGINT, SIG_DFL);
  }

  execvp(arglist[0], arglist); 
  perror("error in execute_command execvp"); // This row and the row below only run if execvp fails
  exit(1);
}

int setup_and_execute_pipeline(char** commands[], int num_commands) {
  int pipes[MAX_PIPES][2];
  pid_t pids[MAX_COMMANDS];
  
  // Create all the necessary pipes
  for (int i = 0; i < num_commands - 1; i++) {
    if (pipe(pipes[i]) == -1) { // This creates a pipe with two file descriptors and stores them in pipes[i]
      perror("error in setup_and_execute_pipeline pipe creation");
      return 0;
    }
  }
  
  // Create child processes for each command
  for (int i = 0; i < num_commands; i++) {
    pids[i] = fork();
    
    if (pids[i] < 0) {
      perror("error in setup_and_execute_pipeline fork");
      return 0;
    } else if (pids[i] == 0) {
      // Child process

      // Set up stdin from previous pipe if not the first command
      if (i > 0) {
        dup2(pipes[i-1][0], STDIN_FILENO);
      }
      
      // Set up stdout to next pipe if not the last command
      if (i < num_commands - 1) {
        dup2(pipes[i][1], STDOUT_FILENO);
      }
      
      // Close all pipe file descriptors (will not close for other processes)
      // No need for them because we already redirected the stdin and stdout
      for (int j = 0; j < num_commands - 1; j++) {
        close(pipes[j][0]);
        close(pipes[j][1]);
      }
      
      // Execute the command
      execute_command(commands[i], 0);
    }
  }

  // Parent process
  // Close all pipe file descriptors
  for (int i = 0; i < num_commands - 1; i++) {
    close(pipes[i][0]);
    close(pipes[i][1]);
  }
  
  // Wait for all child processes to finish
  for (int i = 0; i < num_commands; i++) {
    if (waitpid_which_allows_echild_eintr_errors(pids[i], NULL, 0) == 0) {
      return 0;
    }
  }

  return 1;
}

int execute_background_command(char** arglist, int background_pos) {
  pid_t pid = fork();
  
  if (pid == 0) { 
    // Child process

    arglist[background_pos] = NULL; 
    execute_command(arglist, 1); 
  } else if (pid > 0) { 
    // Parent process

    // Do not wait for the child process to finish
    return 1;
  } else {
    perror("error in background option fork exec");
    return 0;
  }
  
  return 1; // This line is never reached, but included for compiler warnings
}

int execute_command_with_pipes(char** arglist, int pipe_positions[], int num_pipes) {
  char** commands[MAX_COMMANDS];
  
  // We will save a pointer to each command in the commands array 
  // The location where each command starts is determined by the pipe positions 
  // We will also put NULL at the end of each command (will replace the pipe symbol)

  // Prepare the first command (from start to first pipe)
  commands[0] = arglist;
  arglist[pipe_positions[0]] = NULL;
  
  // Prepare middle commands 
  for (int i = 1; i < num_pipes; i++) {
    commands[i] = &arglist[pipe_positions[i-1] + 1];
    arglist[pipe_positions[i]] = NULL;
  }
  
  // Prepare the last command (after the last pipe)
  commands[num_pipes] = &arglist[pipe_positions[num_pipes-1] + 1];
  
  // Execute all piped commands
  return setup_and_execute_pipeline(commands, num_pipes + 1);
}

int execute_input_redirection(char** arglist, int redirection_position) {
  pid_t pid = fork();
  
  if (pid == 0) { // Child process
    // Open the input file
    int fd = open(arglist[redirection_position + 1], O_RDONLY);
    if (fd == -1) {
      perror("error in execute_input_redirection open");
      exit(1);
    }
    
    // Redirect stdin to the file
    dup2(fd, STDIN_FILENO);
    close(fd);
    
    // Remove redirection symbols from arguments
    arglist[redirection_position] = NULL;
    
    // Execute the command
    execute_command(arglist, 0);
  } else if (pid > 0) { // Parent process
    // Wait for the child to complete
    return waitpid_which_allows_echild_eintr_errors(pid, NULL, 0);
  } else {
    perror("error in execute_input_redirection fork exec");
    return 0;
  }
  
  return 1; 
}

int execute_output_redirection(char** arglist, int redirection_position) {
  pid_t pid = fork();
  
  if (pid == 0) { // Child process
    // Open the output file, create if it doesn't exist, truncate if it does
    int fd = open(arglist[redirection_position + 1], O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd == -1) {
      perror("error in execute_output_redirection open");
      exit(1);
    }
    
    // Redirect stdout to the file
    dup2(fd, STDOUT_FILENO);
    close(fd);
    
    // Remove redirection symbols from arguments
    arglist[redirection_position] = NULL;
    
    // Execute the command
    execute_command(arglist, 0);
  } else if (pid > 0) { // Parent process
    // Wait for the child to complete
    return waitpid_which_allows_echild_eintr_errors(pid, NULL, 0);
  } else {
    perror("error in execute_output_redirection fork exec");
    return 0;
  }
  
  return 1; 
}

int execute_standard_command(char** arglist) {
  pid_t pid = fork();
  
  if (pid == 0) { // Child process
    execute_command(arglist, 0);
  } else if (pid > 0) { // Parent process
    return waitpid_which_allows_echild_eintr_errors(pid, NULL, 0);
  } else {
    perror("error in default option fork exec");
    return 0;
  }
  
  return 1; 
}

int prepare(void) {
  struct sigaction sa;
  sigemptyset(&sa.sa_mask); 
  sa.sa_flags = SA_RESTART; 
  sa.sa_handler = find_and_remove_zombies; 

  // Set up the signal handler for SIGCHLD that will remove zombie processes whenever 
  // a child process terminates
  if (sigaction(SIGCHLD, &sa, NULL) == -1) {
      perror("error in prepare SIGCHLD sigaction");
      return 1;
  }

  // Change the signal handler to ignore SIGINT so the shell doesn't terminate on Ctrl+C
  sa.sa_handler = SIG_IGN; 
  if (sigaction(SIGINT, &sa, NULL) == -1) {
      perror("error in prepare SIGINT sigaction");
      return 1;
  }

  return 0;
}

int process_arglist(int count, char** arglist) {
  int background = 0;
  int redirection_in_position = -1;
  int redirection_out_position = -1;
  int num_pipes = 0;
  int pipe_positions[MAX_PIPES];

  // Find special symbols and store their positions
  for (int i = 0; i < count; i++) {
    if (strcmp(arglist[i], "&") == 0) {
      background = i;
    }
    if (strcmp(arglist[i], "<") == 0) {
      redirection_in_position = i;
    }
    if (strcmp(arglist[i], ">") == 0) {
      redirection_out_position = i;
    }
    if (strcmp(arglist[i], "|") == 0) {
      pipe_positions[num_pipes++] = i;
    }
  }

  // Execute the appropriate command based on special symbols
  if (background) {
    return execute_background_command(arglist, background);
  }
  else if (num_pipes > 0) {
    return execute_command_with_pipes(arglist, pipe_positions, num_pipes);
  }
  else if (redirection_in_position != -1) {
    return execute_input_redirection(arglist, redirection_in_position);
  }
  else if (redirection_out_position != -1) {
    return execute_output_redirection(arglist, redirection_out_position);
  }
  else {
    return execute_standard_command(arglist);
  }
}

int finalize(void) {
  return 0;
}


//Questions to chatgpt:
// 1. Please explain the following functions: (WRITE ALL HELPER FUNCTIONS HERE)
// 2. What does it mean to close a pipe?
// 3. If I close a pipe in 1 process, will it close in the other process?
// 4. What are ECHILD and EINTR errors in waitpid?
// 5. How does pipeline creation work (pipe function)? What is the input, what is the output and what is happening?