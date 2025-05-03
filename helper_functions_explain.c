// functions explanation for me:

// dup2(int oldfd, int newfd): 
// Duplicates a file descriptor.
// It makes newfd refer to the same open file description as oldfd, 
// closing newfd first if it's already open.
// Often used for redirecting input/output, e.g., making stdout point to a file.
// *** dup2(fd, STDOUT_FILENO) - Redirect standard output to whatever fd refers to. 
// (I believe this is the usage we need here)\

// execvp(char* file, char* const argv[]):
// replaces the current process with a new process. the params are the program name and its arguments.
// argv[0] should be the name of the program

//getpid() - returns the process ID of the calling process.

// open(const char *pathname, int flags, mode_t mode):
// Opens a file and returns a file descriptor.

//pipe(int pipefd[2])
// Creates a pipe (a unidirectional data channel).
// pipefd[0] is for reading, pipefd[1] is for writing.
// Often used for inter-process communication.

// sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
// Sets up a signal handler for a given signal.
// More reliable than signal() for handling asynchronous signals like SIGCHLD.

// SIGCHLD
// A signal sent to a parent process when one of its child processes terminates.
// Typically handled to avoid zombie processes.
// Example use in sigaction()
//sigaction(SIGCHLD, &sa, NULL);

// wait(int *wstatus)
// Waits for any child process to terminate.
// Returns the PID of the terminated child.
// wstatus can be NULL or used to inspect exit status.

// waitpid(pid_t pid, int *wstatus, int options)
// More flexible version of wait().
// Can wait for a specific child process.
// options like WNOHANG make it non-blocking.