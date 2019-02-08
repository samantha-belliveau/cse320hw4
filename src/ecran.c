/*
 * Ecran: A program that (if properly completed) supports the multiplexing
 * of multiple virtual terminal sessions onto a single physical terminal.
 */

#include <unistd.h>
#include <stdlib.h>
#include <ncurses.h>
#include <sys/signal.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>

#include "ecran.h"

static void initialize();
static void curses_init(void);
static void curses_fini(void);
static void finalize(void);
static void set_status(char *status);
void child_handler(int sig);
void alarm_handler(int sig);
static void terminate(void);

WINDOW *status_window;
int sigchldFlag = 0;
int stderrfd = 0;
char *current_status = NULL;
int status_len = 0;
int numCols = 0;
int alarmFlag = 1;
int num_sessions = 1;
int flag = 0;
int otherflag = 0;

int main(int argc, char *argv[]) {

    signal(SIGCHLD, child_handler);
    signal(SIGALRM, alarm_handler);

    for (int i = 1; i < argc; i++){
        if (argv[i][0] == '-'){

            if (argv[i][1] == 'o' && !argv[i][2]){

                // close fd before exiting
                char *fileName = argv[i+1];
                stderrfd = creat(fileName, 0777);
                i++;
                dup2(stderrfd, 2);
                continue;
            }
        }
        else{
            flag = 1;
            otherflag = 1;
            curses_init();

            char **args = argv + i;

            if (session_init(args[0], args) == (void *)-1){
                terminate();
            }
            else{
                break;
            }
        }
    }

    if (flag == 0){
        initialize();

    }

    mainloop();
    // NOT REACHED
}

/*
 * Initialize the program and launch a single session to run the
 * default shell.
 */
static void initialize(int argc, char *argvT[]) {

    //curses_init();
    char *path = getenv("SHELL");
    if(path == NULL)
	path = "/bin/bash";

    curses_init();
    char *argv[2] = { " (ecran session)", NULL };
    if (session_init(path, argv) == (void *)-1){
        terminate();
    }


}

/*
 * Cleanly terminate the program.  All existing sessions should be killed,
 * all pty file descriptors should be closed, all memory should be deallocated,
 * and the original screen contents should be restored before terminating
 * normally.  Note that the current implementation only handles restoring
 * the original screen contents and terminating normally; the rest is left
 * to be done.
 */
static void finalize(void) {
    //fprintf(stderr, "ERRRRRR\n");
    // REST TO BE FILLED IN
    for (int i = 0; i < 10; i++){
        if (session_get(i) == NULL){
            continue;
        }
        session_kill(session_get(i));
    }

    // TO BE FILLED IN
    for (int i = 0; i < 10; i++){
        if (session_get(i) == NULL){
            continue;
        }
        else{
            session_fini(session_get(i));
        }
    }

    free(current_status);

    if (stderrfd != 0){
        close(stderrfd);

    }
    curses_fini();
    delwin(status_window);
    delwin(main_screen);
    exit(EXIT_SUCCESS);
}

static void terminate(void){
    for (int i = 0; i < 10; i++){
        if (session_get(i) == NULL){
            continue;
        }
        session_kill(session_get(i));
    }

    // TO BE FILLED IN
    for (int i = 0; i < 10; i++){
        if (session_get(i) == NULL){
            continue;
        }
        else{
            session_fini(session_get(i));
        }
    }

    free(current_status);

    if (stderrfd != 0){
        close(stderrfd);

    }
    curses_fini();
    delwin(status_window);
    delwin(main_screen);
    exit(EXIT_FAILURE);
}

/*
 * Helper method to initialize the screen for use with curses processing.
 * You can find documentation of the "ncurses" package at:
 * https://invisible-island.net/ncurses/man/ncurses.3x.html
 */
static void curses_init(void) {
    initscr();
    raw();                       // Don't generate signals, and make typein
                                 // immediately available.
    noecho();                    // Don't echo -- let the pty handle it.
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);


    main_screen = newwin(w.ws_row - 1, w.ws_col, 0, 0);
    status_window = newwin(1, w.ws_col, w.ws_row-1, 0);
    numCols = w.ws_col;
    current_status = calloc(numCols, sizeof(char));

    nodelay(main_screen, TRUE);  // Set non-blocking I/O on input.
    wclear(main_screen);      // Clear the screen.

    refresh();                   // Make changes visible.
    alarm(1);
}

static void set_status(char * status){
    alarmFlag = 1;
    int i = 0;
    for (i = 0; *status; status++){
        current_status[i] = *status;
        i++;
    }

    status_len = i;

}

static void set_time(char *time){
    wclear(status_window);

    waddstr(status_window, time);


    wrefresh(status_window);
}

/*
 * Helper method to finalize curses processing and restore the original
 * screen contents.
 */
void curses_fini(void) {
    endwin();
}

/*
 * Function to read and process a command from the terminal.
 * This function is called from mainloop(), which arranges for non-blocking
 * I/O to be disabled before calling and restored upon return.
 * So within this function terminal input can be collected one character
 * at a time by calling getch(), which will block until input is available.
 * Note that while blocked in getch(), no updates to virtual screens can
 * occur.
 */
void do_command() {

    // Quit command: terminates the program cleanly
    char c = wgetch(main_screen);
    if(c == 'q'){
        finalize();
    }
    else if (c == 'n'){
        char *path = getenv("SHELL");
        if(path == NULL)
        path = "/bin/bash";
        char *argv[2] = { " (ecran session)", NULL };
        if (session_init(path, argv) == NULL){
            // no more sessions can be made
            //should print to the status
            set_status("Cannot create another session, 10 already running");
        }
        else{
            set_status("New session created");
            num_sessions++;
        }
        //wclear(main_screen);
    }
    else if(c >= '0' && c <= '9'){
        SESSION *session = session_get(c - '0');
        if (session != NULL){
            if (session_get(c-'0') != session_getfg()){
                session_setfg(session_get(c - '0'));
            }
            char *message = calloc(sizeof(char), 20);
            sprintf(message, "Switched to session %c", c);
            set_status(message);
            free(message);

        }
        else{
            set_status("Session does not exist");
            flash();
        }
    }
    else if (c == 'k'){
         c = wgetch(main_screen);
         if (c >= '0' && c <= '9'){
            if (session_get(c - '0') == NULL){
                set_status("Session does not exist");
                flash();
            }
            else {
                if (session_kill(session_get(c - '0')) == -1){
                    if (otherflag == 1){
                        otherflag = 0;
                        num_sessions--;
                        session_fini(session_get(0));

                        if (session_getfg() == NULL){
                            finalize();
                        }
                        char *message = calloc(sizeof(char), 20);
                        sprintf(message, "Killed session %c", c);
                        set_status(message);
                        free(message);
                    }
                    else{
                        set_status("Error: could not kill session.");
                    }
                }
                else{
                    char *message = calloc(sizeof(char), 20);
                    sprintf(message, "Killed session %c", c);
                    set_status(message);
                    free(message);
                }
            }

         }
         else{
            set_status("Invalid session number");
            flash();
         }
    }


    else{
        set_status("Invalid control sequence");
        flash();

    }
	// OTHER COMMANDS TO BE IMPLEMENTED

}

/*
 * Function called from mainloop(), whose purpose is do any other processing
 * that has to be taken care of.  An example of such processing would be
 * to deal with sessions that have terminated.
 */
void do_other_processing() {

    sigset_t mask_all, prev_all;
    sigfillset(&mask_all);

    // block signals so that if a child terminates after this loop exits but before the flag is set, we don't loose that
    sigprocmask(SIG_BLOCK, &mask_all, &prev_all); // block signals since this is accessing shared data

    if (alarmFlag){
        int numspaces = numCols - (status_len + 12);

        int i = status_len;

        time_t result = time(NULL);
        char *time = NULL;
        if(result != -1)
            time = ctime(&result);
        char *pos = strchr(time, ':');
        time = pos-2;

        for (i = status_len; i < numspaces + status_len; i++){
                current_status[i] = ' ';
        }
        i = status_len + numspaces;
        if (num_sessions == 10){
            current_status[i] = '(';
            current_status[i+1] = '1';
            current_status[i+2] = '0';
        }
        else{
            current_status[i] = ' ';
            current_status[i+1] = '(';
            current_status[i+2] = num_sessions + '0';
        }
        current_status[i+3] = ')';
        current_status[i+4] = *time;
        current_status[i+5] = *(time+1);
        current_status[i+6] = *(time+2);
        current_status[i+7] = *(time+3);
        current_status[i+8] = *(time+4);
        current_status[i+9] = *(time+5);
        current_status[i+10] = *(time+6);
        current_status[i+11] = *(time+7);

        set_time(current_status);

        alarmFlag = 0;
        alarm(1);

    }
    if (sigchldFlag){
        SESSION *session;
        int pid = 0;


        // CODE IS REAPING SESSION BEFORE SHOWING IT!!!!!!
        // DO NOT ALLOW FOR THIS TO RUN IF IT IS THE PROCESS CREATED BY THE COMMAND EXECUTION
        while((pid = waitpid(-1, NULL, WNOHANG)) != -1){
            if (pid == 0){
                break;
            }
            if (flag == 1){
                flag = 0;
                continue;
            }
            for (int i = 0; i < 10; i++){
                if ((session = session_get(i)) == NULL){
                    continue;
                }
                if (session->pid == pid){
                    num_sessions--;
                    session_fini(session_get(i));

                    if (session_getfg() == NULL){
                        finalize();
                    }
                    break;
                }
            }
        }
        sigchldFlag = 0;


    }

    sigprocmask(SIG_SETMASK, &prev_all, NULL);

}

void alarm_handler(int sig){
    alarmFlag = 1;
}

void child_handler(int sig){
    sigchldFlag = 1;
}


