/* 
 * tsh - A tiny shell program with job control
 * 
 * <###########################################################******************************************#################################################################
 * 									Name :- Raj Jakasaniya
 * 									  ID :- 201501408
 *							       Email Address :- raj.jakasaniya@gmail.com
 *								 DAIICT Mail :- 201501408@daiict.ac.in
 * ############################################################******************************************################################################################>
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdbool.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs); 
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid); 
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) 
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             /* print help message */
            usage();
	    break;
        case 'v':             /* emit additional diagnostic info */
            verbose = 1;
	    break;
        case 'p':             /* don't print a prompt */
            emit_prompt = 0;  /* handy for automatic testing */
	    break;
	default:
            usage();
	}
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {

	/* Read command line */
	if (emit_prompt) {
	    printf("%s", prompt);
	    fflush(stdout);
	}
	if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
	    app_error("fgets error");
	if (feof(stdin)) { /* End of file (ctrl-d) */
	    fflush(stdout);
	    exit(0);
	}

	/* Evaluate the command line */
	eval(cmdline);
	fflush(stdout);
	fflush(stdout);
    } 

    exit(0); /* control never reaches here */
}
  
/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
*/
void eval(char *cmdline) 
{
    char *argv[MAXARGS];
    pid_t pid;
    int bg=parseline(cmdline,argv);
    sigset_t s;
    sigemptyset(&s);
    sigaddset(&s, SIGCHLD);                                 					/* Add sigchild to the sigset to be blocked */
    if(bg!=-1){											/* Ignoring Blank Lines */
		if(!builtin_cmd(argv)){								/* Cheking if the command is builtin or not */				
                		sigprocmask(SIG_BLOCK, &s, 0);					/* Block the sigset s containing SIGCHLD */
				if((pid=fork())==0){
					/* Child */
					setpgid(0,0);			/* Making a Process Group with Child's Process ID and making child the leader of the group */
					sigprocmask(SIG_UNBLOCK, &s, 0);			/* Unblocking the sigset in child */
                    			execvp(argv[0],argv);
					printf("%s: Command not found\n",argv[0]);
					fflush(stdout);
					exit(0);
				}else{
					/* Parent */
					sigprocmask(SIG_UNBLOCK, &s, 0);                        /* Unblocking the sigset in parent */
                    			if(bg){
						addjob(jobs,pid,BG,cmdline);			/* Adding the job to the Background */
						printf("[%d] (%d) %s",pid2jid(pid),pid,cmdline);
						fflush(stdout);
					}else{
						addjob(jobs,pid,FG,cmdline);			/* Adding the job to the foreground */
						waitfg(pid);					/* Waiting for foreground process to finish */

					}
				}
		}
    }
    return;
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv) 
{
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
	buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
	buf++;
	delim = strchr(buf, '\'');
    }
    else {
	delim = strchr(buf, ' ');
    }

    while (delim) {
	argv[argc++] = buf;
	*delim = '\0';
	buf = delim + 1;
	while (*buf && (*buf == ' ')) /* ignore spaces */
	       buf++;

	if (*buf == '\'') {
	    buf++;
	    delim = strchr(buf, '\'');
	}
	else {
	    delim = strchr(buf, ' ');
	}
    }
    argv[argc] = NULL;
    
    if (argc == 0)  /* ignore blank line */
	return -1;

    /* should the job run in the background? */
    if ((bg = (*argv[argc-1] == '&')) != 0) {
	argv[--argc] = NULL;
    }
    return bg;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) 
{
    int i;
    if(strcmp(argv[0],"quit")==0){
	/* checking for Stopped Process before exiting If there are ST Process Printing ERROR Condition and returning 1 */
	for(i = 0; i < MAXJOBS; i++){	
		if(jobs[i].state==ST){
			printf("There are Stopped Jobs\n");					/* Printing ERROR if there are Stopped Process's */
			return 1;		
		}	
	}
    	exit(0);										/* If no ST process then exiting with 0 */
    }else if(strcmp(argv[0],"jobs")==0){
    	listjobs(jobs);										/* Listing all the Jobs */
	return 1;
    }else if(strcmp(argv[0],"fg")==0 ){							      /* if first argument is fg or bg calling do_bgfg function and returning 1 */
    	do_bgfg(argv);
	return 1;
    }else if(strcmp(argv[0],"bg")==0){
    	do_bgfg(argv);
	return 1;
    }else{
    	return 0;     										/* not a builtin command */
    }
    return 0;
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) 
{
    struct job_t *p;
    int a,cd=0,pid=0,flag=1;

    if(argv[1]==NULL){										/* Checking if the first argument is empty or not */
    	printf("%s command requires PID or %%jobid argument\n",argv[0]);
	fflush(stdout);
	return ;
    }
    

    if((strcmp(argv[0],"fg")==0 || strcmp(argv[0],"bg")==0) && strcmp(argv[1],"\0")==0){	/* Cheking if the Second argument is empty or not */
    	printf("%s command requires PID or %%jobid argument\n",argv[0]);
	fflush(stdout);
    }else{
	flag=1;
	for(a=0;a<strlen(argv[1]);a++){				/* Building the pid or jid using character in the argument and checking for any violations */
		pid=pid*10;
		if(a==0){									/* For the First Character */
			if((argv[1][a]=='%') || (argv[1][a]<='9' && argv[1][a]>='0')){
				if(argv[1][a]!='%'){
					pid=pid+(int)(argv[1][a]-48);
				}else{
					cd=1;							/* cd Used to Distinguish between jid (cd=1) and pid (cd=0) */ 
				}
			}else{									/* If the String contains any character other than a number than flag=1 */
				flag=0;
				break;
			}	
		}else{
			if((argv[1][a]<='9' && argv[1][a]>='0')){
	                 	pid=pid+(int)(argv[1][a]-48);
	        	}else{
				flag=0;
				break;
	        	}
		}	
	}
			
	if(!flag){
		if(argv[1][0]!='%' || (argv[1][0]<='1'&& argv[1][0]>='9')){
	/* if the First character of argument is other than % or a number than printing the Appropriate Message */
			printf("%s: argument must be pid or %%jobid\n",argv[0]);
			fflush(stdout);
		}
		else{
			printf("%s: No such job\n",argv[1]);			/* checking for any violations in the second argument */
			fflush(stdout);
		}	
	}else{
		if(cd==1){
		/* If the User Inputed a JID as Second Argument */
			p=getjobjid(jobs,pid);							/* Getting the job for jid provided from user using jobs table */
			if(p==NULL){								/* if there is no job with jid provided printing the error */
				printf("%s: No such job\n",argv[1]);
				fflush(stdout);
			}else{	
		/* if jid is correct changing the state accordingly and if the state is changed to FG then Waiting for the Process to be completed */
	/* If the State is ST i.e. Stopped then sending the SIGCONT Signal to the Whole Process Group (done by -(p->pid)) and changing the State as per the user input */
				if(p->state==ST){
					if(strcmp(argv[0],"bg")==0){
						p->state=BG;
						kill(-(p->pid),SIGCONT);
						printf("[%d] (%d) %s",pid,p->pid,p->cmdline);
						fflush(stdout);	
					}else{
						p->state=FG;
						kill(-(p->pid),SIGCONT);
						waitfg(p->pid);
					}	
				}else if(p->state==BG){
					if(strcmp(argv[0],"fg")==0){
						p->state=FG;
						waitfg(p->pid);
                                        }	
				}
							
			}
		}else{
		/* If the User Inputed a PID as Second Argument */	
			p=getjobpid(jobs,pid);							/* Getting the job for pid provided from user from jobs table */
			if(p==NULL){								/* if there is no job with pid provided printing the error */
				printf("(%s): No such process\n",argv[1]);
				fflush(stdout);
			}else{								
		/* if pid is correct changing the state accordingly and if the state is changed to FG then Waiting for the Process to be completed */
	/* If the State is ST i.e. Stopped then sending the SIGCONT Signal to the Whole Process Group (done by -(p->pid)) and changing the State as per the user input */
				if(p->state==ST){
					if(strcmp(argv[0],"bg")==0){
						p->state=BG;
						kill(-(p->pid),SIGCONT);
						printf("[%d] (%d) %s",pid,p->pid,p->cmdline);
						fflush(stdout);
					}else if(strcmp(argv[0],"fg")==0){
						p->state=FG;
						kill(-(p->pid),SIGCONT);
						waitfg(p->pid);
					}	
				}else if(p->state==BG){
					if(strcmp(argv[0],"fg")==0){
						p->state=FG;
						waitfg(p->pid);
                                        }	
				}
			}
		}
	}
    }	
    return;
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
	struct job_t *j;
	j=getjobpid(jobs,pid);									/* Getting the job from the jobs table using getjobpid function */
	while(j->state==FG){									/* Waiting for the process to change the state from the FG */
		sleep(1);
	}
	if(verbose){										/* For Debugging purposes */
		printf("waitfg: Process (%d) no longer the fg process\n",pid);
		fflush(stdout);
	}
    return;
}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) 
{
    int stat;
    pid_t cpid;
    struct job_t *j;
    if(verbose){										/* For Debugging purpose */
	printf("sigchild_handler: entering\n");
	fflush(stdout);
    }
    while((cpid = waitpid(-1, &stat, WNOHANG | WUNTRACED)) > 0){				/* Reaping every terminated or stopped child */
	    j=getjobpid(jobs,cpid);

	    if(WIFEXITED(stat)){								/* Deleting job from the jobs table of the child which exited normally */
		if(verbose){									/* For Debugging purpose */
			printf("sigchld_handler: Job [%d] (%d) deleted\n",j->jid,j->pid);
			fflush(stdout);			
			printf("sigchld_handler: Job [%d] (%d) terminates OK (status %d)\n",j->jid,j->pid,WEXITSTATUS(stat));
			fflush(stdout);
	    	}		
		deletejob(jobs, cpid);
		
	    }
	    else if(WIFSIGNALED(stat)){								
	/* Deleting the job from jobs table and printing appropriate message as job is terminated due to some Signal */
		if(verbose){									/* For Debugging purpose */
			printf("sigchld_handler: Job [%d] (%d) deleted\n",j->jid,j->pid);
			fflush(stdout);
		}
		printf("Job [%d] (%d) terminated by signal %d\n", pid2jid(cpid), cpid, WTERMSIG(stat));
		fflush(stdout);
		deletejob(jobs, cpid);
		
	    }
	    else if(WIFSTOPPED(stat)){
	/* Changing the State of job to ST because job is stopped by signal ( by the use of WUNTRACED ) */
		printf("Job [%d] (%d) stopped by signal %d\n", pid2jid(cpid), cpid, WSTOPSIG(stat));
		fflush(stdout);
		j->state=ST;
	    }
    }
	
    if(verbose){										/* for Debugging purposes */
	printf("sigchild_handler: exiting\n");
	fflush(stdout);
    }
    return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) 
{
    pid_t pid;
    pid=fgpid(jobs);										/* getting the PID of Foreground Process */
    if(verbose){										/* For Debugging purpose */
    	printf("sigint_handler: entering\n");
    	fflush(stdout);
    }
    if(pid!=0){
	if(verbose){										/* For Debugging purpose */
		printf("sigint_handler: Job (%d) killed\n",pid);
		fflush(stdout);
	}
    	kill(-(pid),SIGINT);						/* By using -pid We are sending SIGINT(2) to the whole Process Group with that PID in it */
    }
    if(verbose){										/* for Debugging purposes */
    	printf("sigint_handler: exiting\n");
    	fflush(stdout);
    }
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) 
{
    pid_t pid;
    struct job_t *j;
    pid=fgpid(jobs);										/* Getting the PID of the Foreground Process Using fgpid() */
    j=getjobpid(jobs,pid);							/* Getting the Job entry in jobs table for the Foreground Process using getjobpid */
    if(verbose){										/* for Debugging purposes */
    	printf("sigtstp_handler: entering\n");
    	fflush(stdout);
    }
    if(pid!=0){
	if(verbose){										/* for Debugging purposes */
		printf("sigtstp_handler: Job [%d] (%d) stopped\n",j->jid,pid);
		fflush(stdout);
	}	
	kill(-(j->pid),SIGTSTP);			/* By using -(j->pid) We are sending SIGTSTP(20) to the whole Process Group with the foreground jobs PID */
    }
    if(verbose){										/* for Debugging purposes */
    	printf("sigtstp_handler: exiting\n");
    	fflush(stdout);
    }
    return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) 
{
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid > max)
	    max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
    int i;
    
    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == 0) {
	    jobs[i].pid = pid;
	    jobs[i].state = state;
	    jobs[i].jid = nextjid++;
	    if (nextjid > MAXJOBS)
		nextjid = 1;
	    strcpy(jobs[i].cmdline, cmdline);
  	    if(verbose){
	        printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
	}
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == pid) {
	    clearjob(&jobs[i]);
	    nextjid = maxjid(jobs)+1;
	    return 1;
	}
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].state == FG)
	    return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid)
	    return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
    int i;

    if (jid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid == jid)
	    return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) 
{
    int i;
    
    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid != 0) {
	    printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
	    switch (jobs[i].state) {
		case BG: 
		    printf("Running ");
		    break;
		case FG: 
		    printf("Foreground ");
		    break;
		case ST: 
		    printf("Stopped ");
		    break;
	    default:
		    printf("listjobs: Internal error: job[%d].state=%d ", 
			   i, jobs[i].state);
	    }
	    printf("%s", jobs[i].cmdline);
	}
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) 
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) 
{
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
	unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) 
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}



