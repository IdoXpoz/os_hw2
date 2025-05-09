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
  memset(&sa, 0, sizeof(sa)); 
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

  // make sure that there are no more than 9 pipes, if there are print an error message
  // and continue to the next command insertion
  if (num_pipes > MAX_PIPES) {
    perror("Too many pipes received");
    return 1;
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
// 1. Please explain the following functions:
// dup2(), execvp() (not to be confused with execv()!), getpid(), open(), pipe(), sigaction(), SIGCHLD, wait(), waitpid().
// Answer:
// # POSIX System Call Functions

// Let me explain these important POSIX system call functions commonly used in Unix-like operating systems for process and signal management:

// ## 1. dup2()
// ```c
// int dup2(int oldfd, int newfd);
// ```
// **Purpose**: Duplicates a file descriptor.
// - Makes `newfd` be a copy of `oldfd`, closing `newfd` first if it was open
// - After dup2(), both file descriptors refer to the same file and share file offset and flags
// - Often used in redirecting standard I/O in shell pipelines and command execution

// ## 2. execvp()
// ```c
// int execvp(const char *file, char *const argv[]);
// ```
// **Purpose**: Replaces the current process image with a new one.
// - The "p" in execvp() means it searches for the executable in the PATH environment variable
// - Unlike execv(), you don't need to provide the full path to the executable
// - The argv array passes command-line arguments to the new program
// - On success, execvp() never returns (process is replaced); returns -1 on error

// ## 3. getpid()
// ```c
// pid_t getpid(void);
// ```
// **Purpose**: Returns the process ID of the calling process.
// - Each process has a unique ID that identifies it in the system
// - Often used for logging, creating unique filenames, or identifying the current process

// ## 4. open()
// ```c
// int open(const char *pathname, int flags, mode_t mode);
// ```
// **Purpose**: Opens a file or creates one if it doesn't exist.
// - Returns a file descriptor (a small non-negative integer)
// - `flags` control how the file is opened (O_RDONLY, O_WRONLY, O_RDWR, etc.)
// - `mode` specifies permissions when creating a file
// - Used as the first step in file I/O operations

// ## 5. pipe()
// ```c
// int pipe(int pipefd[2]);
// ```
// **Purpose**: Creates an unidirectional data channel for interprocess communication.
// - Creates two file descriptors:
//   - `pipefd[0]` for reading from the pipe
//   - `pipefd[1]` for writing to the pipe
// - Data written to the write end can be read from the read end
// - Essential for creating pipelines between processes

// ## 6. sigaction()
// ```c
// int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
// ```
// **Purpose**: Examines or changes the action taken on receipt of a specific signal.
// - More flexible alternative to signal()
// - Allows for more precise control of signal handling
// - Can specify flags to control signal behavior
// - Can be used to block additional signals during handler execution

// ## 7. SIGCHLD
// **Purpose**: A signal sent to the parent process when a child process terminates.
// - Not a function but a signal constant (typically value 17)
// - Allows parent processes to be notified when child processes exit
// - Can be caught and handled with signal() or sigaction()
// - By default, this signal is ignored

// ## 8. wait()
// ```c
// pid_t wait(int *wstatus);
// ```
// **Purpose**: Suspends execution of the calling process until one of its children terminates.
// - Returns the PID of the terminated child
// - `wstatus` stores information about how the child terminated
// - Can be used with macros like WIFEXITED() to check exit status

// ## 9. waitpid()
// ```c
// pid_t waitpid(pid_t pid, int *wstatus, int options);
// ```
// **Purpose**: Waits for a specific child process to change state.
// - More flexible than wait()
// - Can wait for a specific child (specified by pid)
// - Can be non-blocking with WNOHANG option
// - Can wait for stopped or continued children with additional options

// These functions are the building blocks for implementing process creation, interprocess communication, and signal handling in Unix/Linux systems, particularly useful when building shells, daemons, and other system-level programs.



// 2. What does it mean to close a pipe?
// # Closing a Pipe

// When you close a pipe in a Unix/Linux system, you're essentially terminating one end of a communication channel between processes. Let me explain what this means in detail:

// ## What Happens When You Close a Pipe

// When you close a pipe (by calling `close()` on one of the pipe file descriptors):

// 1. **Resource Release**: The system releases the file descriptor, making it available for reuse by other operations.

// 2. **End-of-File Signaling**: If you close the write end of a pipe:
//    - Any process reading from the pipe will receive an EOF (End-of-File) after reading all previously written data
//    - `read()` calls will return 0 once all data has been consumed

// 3. **Broken Pipe Signaling**: If you close the read end of a pipe:
//    - Any process that attempts to write to the pipe will receive a SIGPIPE signal
//    - `write()` calls will fail with EPIPE error (broken pipe)

// ## Importance in Parent-Child Process Communication

// Closing unused pipe ends is critical for proper process communication:

// ```c
// int pipefd[2];
// pipe(pipefd);

// if (fork() == 0) {
//     /* Child process */
//     close(pipefd[1]);  // Child doesn't need write end
//     // Read from pipefd[0]...
//     close(pipefd[0]);
//     exit(0);
// } else {
//     /* Parent process */
//     close(pipefd[0]);  // Parent doesn't need read end
//     // Write to pipefd[1]...
//     close(pipefd[1]);
//     wait(NULL);
// }
// ```

// ## Why Closing Pipes Is Essential

// 1. **Preventing Resource Leaks**: Each pipe end consumes a file descriptor, a limited resource in Unix/Linux systems.

// 2. **Ensuring Proper EOF Detection**: If the write end is never closed, readers will hang waiting for more data that will never come.

// 3. **Preventing Deadlocks**: If both processes keep both ends open, situations can arise where they wait for each other indefinitely.

// 4. **Performance**: Keeping unnecessary file descriptors open wastes system resources.

// By properly closing the unused ends of pipes, you ensure clean and predictable communication between processes in your system.



// 3. If I close a pipe in 1 process, will it close in the other process?
// Answer: 
// # Pipe Closure Between Processes

// No, closing a pipe in one process does not close it in another process. Here's why:

// ## Independent File Descriptors

// When you create a pipe and then fork a process, both parent and child have their own separate copies of the file descriptors:

// - Each process has its own file descriptor table
// - When you close a pipe end in one process, it only closes that specific file descriptor in that process's table
// - The corresponding file descriptor in the other process remains open and valid

// ## Example Scenario

// ```c
// int pipefd[2];
// pipe(pipefd);  // Create pipe with read end pipefd[0], write end pipefd[1]

// if (fork() == 0) {
//     // Child process
//     close(pipefd[1]);  // Close write end in child
//     // pipefd[0] is still open in child
//     // Parent still has both pipefd[0] and pipefd[1] open
//     // ...
// } else {
//     // Parent process
//     // Both ends still open in parent
//     // ...
// }
// ```

// ## Kernel Reference Counting

// The kernel maintains reference counts for pipe resources:
// - Each open file descriptor increments the reference count
// - Each close decrements the reference count
// - The pipe is only truly deallocated when all references are closed

// ## Implications

// 1. **Both processes must properly close their unused ends** for the pipe to function correctly
// 2. **EOF is only received** by a reader when all write ends across all processes are closed
// 3. **SIGPIPE is only generated** when writing to a pipe whose read ends are all closed

// This independent file descriptor behavior is what allows pipes to work as interprocess communication channels - each process can manage its own access to the shared pipe resource.



// 4. What are ECHILD and EINTR errors in waitpid?
// Answer:
// # ECHILD and EINTR Errors in waitpid()

// When using `waitpid()`, two common error conditions you might encounter are ECHILD and EINTR. Here's what they mean and how to handle them:

// ## ECHILD Error

// **Error Code**: `ECHILD` (typically defined as 10 in most systems)

// **When It Occurs**:
// - When the calling process has no children to wait for
// - When the specified child PID doesn't exist
// - When the specified child isn't a child of the calling process
// - When the child was already waited for (collected)

// **Example Scenario**:
// ```c
// pid_t pid = waitpid(-1, &status, 0);
// if (pid == -1) {
//     if (errno == ECHILD) {
//         printf("No children to wait for\n");
//     }
// }
// ```

// **Common Mistakes Leading to ECHILD**:
// - Calling `waitpid()` multiple times for the same child
// - Race conditions where another thread already waited for the child
// - Using incorrect PID values

// ## EINTR Error

// **Error Code**: `EINTR` (typically defined as 4 in most systems)

// **When It Occurs**:
// - When the `waitpid()` call is interrupted by a signal before any child changed state
// - Only happens when `waitpid()` was blocking (i.e., not using WNOHANG)

// **Example Scenario**:
// ```c
// pid_t pid;
// do {
//     pid = waitpid(child_pid, &status, 0);
// } while (pid == -1 && errno == EINTR);

// if (pid == -1) {
//     // Handle other errors
// }
// ```

// **Proper Handling**:
// - For robust code, always retry the `waitpid()` call when EINTR is received
// - This pattern is common in system programming to handle signal interruptions

// ## Checking for These Errors

// ```c
// pid_t result = waitpid(pid, &status, options);
// if (result == -1) {
//     switch (errno) {
//         case ECHILD:
//             fprintf(stderr, "No child process to wait for\n");
//             break;
//         case EINTR:
//             fprintf(stderr, "Interrupted by signal\n");
//             // Typically you would retry the waitpid call here
//             break;
//         default:
//             fprintf(stderr, "Unknown error: %s\n", strerror(errno));
//     }
// }
// ```

// Understanding these errors is crucial for writing robust process management code that can handle edge cases and signal interactions correctly.



// 5. How does pipeline creation work (pipe function)? What is the input, what is the output and what is happening?
// Answer:
// # Pipeline Creation with the pipe() Function

// The `pipe()` function is a fundamental system call for creating unidirectional communication channels between processes. Let me break down how it works in detail:

// ## Function Signature

// ```c
// int pipe(int pipefd[2]);
// ```

// ## Input and Output

// ### Input:
// - `pipefd`: An array of two integers that will be filled with file descriptors

// ### Output:
// - Return value: 0 on success, -1 on failure
// - Side effect: Fills `pipefd` with two file descriptors:
//   - `pipefd[0]`: The read end of the pipe
//   - `pipefd[1]`: The write end of the pipe

// ## What Happens When pipe() Is Called

// 1. **Kernel Resource Allocation**:
//    - The kernel allocates a new pipe buffer (typically 65536 bytes on modern systems)
//    - Creates two file descriptors that reference this buffer

// 2. **File Descriptor Setup**:
//    - The read end (`pipefd[0]`) is configured for reading only
//    - The write end (`pipefd[1]`) is configured for writing only

// 3. **Data Flow Direction**:
//    - Data written to `pipefd[1]` can be read from `pipefd[0]`
//    - Data flows in one direction only (unidirectional)

// ## Example of Creating a Pipe

// ```c
// int pipefd[2];
// if (pipe(pipefd) == -1) {
//     perror("pipe");
//     exit(EXIT_FAILURE);
// }
// printf("Read end: %d, Write end: %d\n", pipefd[0], pipefd[1]);
// ```

// ## How Data Flows Through the Pipe

// ```
// Writer Process                         Reader Process
//   |                                      |
//   | write(pipefd[1], data, size)        | read(pipefd[0], buffer, size)
//   |     |                               |     ^
//   |     v                               |     |
//   |  +-----------------------------------+     |
//   +->| Kernel Pipe Buffer (FIFO Queue) |------+
//      +-----------------------------------+
// ```

// ## Internal Implementation Details

// 1. **Kernel Buffer**:
//    - The pipe is backed by a circular buffer in kernel memory
//    - Writes add data to the buffer; reads remove data from the buffer
//    - If the buffer is full, `write()` blocks until space is available
//    - If the buffer is empty, `read()` blocks until data is available

// 2. **Atomicity**:
//    - Writes up to PIPE_BUF bytes (typically 4096) are atomic
//    - This means they won't be interleaved with writes from other processes

// 3. **File Descriptor Properties**:
//    - Both descriptors can be used with `select()`, `poll()`, and `epoll()`
//    - Non-blocking I/O can be set with `fcntl()`

// ## Using Pipes for IPC (Interprocess Communication)

// The most common pattern is to fork after creating a pipe:

// ```c
// int pipefd[2];
// pipe(pipefd);

// pid_t pid = fork();
// if (pid == 0) {
//     /* Child process */
//     close(pipefd[1]);  // Close unused write end
    
//     char buffer[100];
//     read(pipefd[0], buffer, sizeof(buffer));
//     printf("Child received: %s\n", buffer);
    
//     close(pipefd[0]);
//     exit(0);
// } else {
//     /* Parent process */
//     close(pipefd[0]);  // Close unused read end
    
//     write(pipefd[1], "Hello from parent!", 18);
    
//     close(pipefd[1]);
//     wait(NULL);
// }
// ```

// Understanding how `pipe()` works is essential for implementing shell pipelines, producer-consumer patterns, and other forms of interprocess communication in Unix/Linux systems.

// 6. What do I need to initialize when using sigaction?
// Answer:
// When using sigaction, you typically need to initialize a struct sigaction before passing it to the sigaction() system call. Here's what you should set up:

// ✅ Fields to Initialize in struct sigaction
// c
// Copy
// Edit
// struct sigaction {
//     void     (*sa_handler)(int);      // or use sa_sigaction if using SA_SIGINFO
//     void     (*sa_sigaction)(int, siginfo_t *, void *);
//     sigset_t   sa_mask;               // Signals to block during handler execution
//     int        sa_flags;              // Behavior modifiers
//     void     (*sa_restorer)(void);    // (usually ignored; obsolete on most systems)
// };
// 🧱 Steps to Properly Initialize
// Set the handler:

// Use either sa_handler or sa_sigaction.

// sa_handler = SIG_IGN to ignore the signal.

// sa_handler = SIG_DFL for default behavior.

// sa_handler = your_handler_function for a custom function.

// If you want detailed info (e.g., siginfo_t), use sa_sigaction and set the flag SA_SIGINFO.

// Initialize sa_mask:

// This mask specifies which signals should be blocked during execution of the handler.

// Typically done using:

// c
// Copy
// Edit
// sigemptyset(&action.sa_mask);
// Optionally followed by sigaddset to add specific signals.

// Set sa_flags:

// Common flags:

// SA_RESTART: Restart certain system calls if interrupted.

// SA_SIGINFO: Use sa_sigaction instead of sa_handler.

// SA_NOCLDWAIT: Prevent zombie children for SIGCHLD.

// SA_NODEFER: Don’t block the signal being handled.

// (Ignore sa_restorer):

// You usually don’t need to touch this. It’s obsolete.

// ✅ Minimal Example
// c
// Copy
// Edit
// #include <signal.h>
// #include <stdio.h>
// #include <string.h>

// void handler(int sig) {
//     printf("Received signal %d\n", sig);
// }

// int main() {
//     struct sigaction action;

//     memset(&action, 0, sizeof(struct sigaction)); // Good practice
//     action.sa_handler = handler;                  // Set handler
//     sigemptyset(&action.sa_mask);                 // No extra signals blocked
//     action.sa_flags = 0;                          // No special flags

//     // Set the handler for SIGINT
//     if (sigaction(SIGINT, &action, NULL) == -1) {
//         perror("sigaction");
//         return 1;
//     }

//     // Wait for signal
//     pause();
//     return 0;
// }
// Would you like an example with SA_SIGINFO and sa_sigaction instead?






// 7. What is sigemptyset, when should it be used?
// Answer:
// sigemptyset is a function in C (defined in <signal.h>) used for working with signal sets, which are data structures representing collections of signals. These are often used in signal handling (e.g. with sigaction, sigprocmask, etc.) to control which signals are blocked or handled in certain ways.

// Function Signature
// c
// Copy
// Edit
// int sigemptyset(sigset_t *set);
// Purpose
// sigemptyset initializes the signal set pointed to by set to exclude all signals — it sets the signal set to be empty.

// Return Value
// Returns 0 on success, or -1 on failure (with errno set appropriately).

// When to Use sigemptyset
// You should use sigemptyset before adding specific signals to a set, especially when:

// You're preparing a signal set to use with functions like sigaddset, sigprocmask, or sigaction.

// You want to ensure the set starts empty (i.e., contains no signals) to avoid unintended behavior from uninitialized memory.