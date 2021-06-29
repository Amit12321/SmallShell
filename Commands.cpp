#include <unistd.h>
#include <string.h>
#include <iostream>
#include <vector>
#include <sstream>
#include <sys/wait.h>
#include <iomanip>
#include "Commands.h"
#include <dirent.h>
#include <regex>
#include <fcntl.h>
#include <sys/stat.h>

using std::cout;
using std::endl;
using std::vector;
using std::list;
using std::string;
using std::stoi;
using std::invalid_argument;

const std::string WHITESPACE = " \n\r\t\f\v";

#if 0
#define FUNC_ENTRY()  \
  cerr << __PRETTY_FUNCTION__ << " --> " << endl;

#define FUNC_EXIT()  \
  cerr << __PRETTY_FUNCTION__ << " <-- " << endl;
#else
#define FUNC_ENTRY()
#define FUNC_EXIT()
#endif

#define DEBUG_PRINT cerr << "DEBUG: "

#define EXEC(path, arg) \
  execvp((path), (arg));

template <std::size_t N>
int execvp(const char* file, const char* const (&argv)[N]) {
  return execvp(file, const_cast<char* const*>(argv));
}

string _ltrim(const std::string& s)
{
  size_t start = s.find_first_not_of(WHITESPACE);
  return (start == std::string::npos) ? "" : s.substr(start);
}

string _rtrim(const std::string& s)
{
  size_t end = s.find_last_not_of(WHITESPACE);
  return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

string _trim(const std::string& s)
{
  return _rtrim(_ltrim(s));
}

int _parseCommandLine(const char* cmd_line, char** args) {
  FUNC_ENTRY()
  int i = 0;
  std::istringstream iss(_trim(string(cmd_line)).c_str());
  for(std::string s; iss >> s; ) {
    args[i] = (char*)malloc(s.length()+1);
    memset(args[i], 0, s.length()+1);
    strcpy(args[i], s.c_str());
    args[++i] = NULL;
  }
  return i;

  FUNC_EXIT()
}

bool _isBackgroundComamnd(const char* cmd_line) {
  const string str(cmd_line);
  return str[str.find_last_not_of(WHITESPACE)] == '&';
}

void _removeBackgroundSign(char* cmd_line) {
  const string str(cmd_line);
  // find last character other than spaces
  unsigned int idx = str.find_last_not_of(WHITESPACE);
  // if all characters are spaces then return
  if (idx == string::npos) {
    return;
  }
  // if the command line does not end with & then return
  if (cmd_line[idx] != '&') {
    return;
  }
  // replace the & (background sign) with space and then remove all tailing spaces.
  cmd_line[idx] = ' ';

  // truncate the command line string up to the last non-space character
  cmd_line[str.find_last_not_of(WHITESPACE, idx) + 1] = 0;
}


Command::Command(const char* line, char** args, int args_len, char* exec) : 
  cmd_line(string(line)), args(args), args_len(args_len), exec(exec) {
  }

void Command::cleanup() {
  for (int i = 0; i < args_len; ++i) {
    if (args[i]) free(args[i]);
  }
  if (args) free(args);
  if (exec) free(exec);
}

/* JobsList + jobs command start */

void JobsList::addJob(Command* cmd, pid_t pid, bool isStopped) {
  removeFinishedJobs(); 
  int newId = jobs.empty() ? 1 : jobs.back()->jobId + 1;
  JobEntry* newJob = new JobEntry(cmd, pid, isStopped, newId);
  jobs.push_back(newJob);
}

void JobsList::printJobsList() {
  removeFinishedJobs();
  time_t now = time(NULL);
  if (now == -1) { //might fail according to man.
    perror("smash error: time failed");
  }
  for(JobEntry* job : jobs) {
    std::cout << "[" << job->jobId << "] " << job->cmd->getCmdLine() << " : " << 
      job->pid << " " << difftime(now, job->elapsed) << " secs";
    if (job->isStopped) {
      std::cout << " (stopped)";
    }
    std::cout << std::endl;
  }
}

void JobsList::killAllJobs() {
    cout << "smash: sending SIGKILL signal to " << jobs.size() << " jobs:" << endl;
    for (JobEntry* job : jobs) {
      if (kill(job->pid, SIGKILL) == -1) {
        perror("smash error: kill failed");
      } else {
      cout << job->pid << ": " << job->cmd->getCmdLine() << endl;
	  }
    } 
}

void JobsList::removeFinishedJobs() {
  pid_t w;
 
  auto current = jobs.begin();
  while (current != jobs.end())
  {
      int status;
      w = waitpid((*current)->pid, &status, WNOHANG | WUNTRACED | WCONTINUED); //WNOHANG = don't block, check immediately the status of pid.
      if (w == -1) {
        perror("smash error: waitpid failed");
        ++current;
      } else if ((WIFEXITED(status) || WIFSIGNALED(status)) && w > 0) { // remove finished process. (WIFSIGNALED means killed by sigkill)
        SmallShell::getInstance().removeTimeout((*current)->pid);
        delete *current;
        current = jobs.erase(current);  // "erase" returns an iterator, pointing to the next element in the list (after the erased one)
      } else if (WIFSTOPPED(status) && w > 0) {
        addStopMark((*current)->jobId);
        ++current;
      } else if (WIFCONTINUED(status) && w > 0) {
        removeStopMark((*current)->jobId);
        ++current;
      } else {
        ++current;
      }
  }
}

JobEntry * JobsList::getJobById(int jobId) const {
  for (auto current = jobs.begin(); current != jobs.end(); ++current) {
    if ((*current)->jobId == jobId) return *current;
  }
  return nullptr;
}

void JobsList::removeJobById(int jobId) {
  for (auto current = jobs.begin(); current != jobs.end(); ++current) {
    if ((*current)->jobId == jobId) {
      jobs.erase(current);
      return;
    }
  }
}

JobEntry* JobsList::getLastJob(int *lastJobId) const {
  if (jobs.empty()) {
    *lastJobId = -1;
    return nullptr;
  }
  *lastJobId = jobs.back()->jobId;
  return jobs.back();
}

JobEntry* JobsList::getLastStoppedJob(int* jobId) const {
  if (jobs.empty()) {
    *jobId = -1;
    return nullptr;
  }
  for (auto current = jobs.rbegin(); current != jobs.rend(); ++current) { //Using reverse iterator because we want the last stopped.
    if ((*current)->isStopped) {
      *jobId = (*current)->jobId;
      return *current;
    }
  }
  *jobId = -1;
  return nullptr;
}

int JobsList::checkIfStopped(int jobId, bool* res) const {
  JobEntry* job = getJobById(jobId);
  if (!job) {
    return -1;
  }
  *res = job->isStopped;
  return 0;
}

int JobsList::removeStopMark(int jobId) {
  JobEntry* job = getJobById(jobId);
  if (!job) {
    return -1;
  }
  job->isStopped = false;
  return 0;
}
int JobsList::addStopMark(int jobId) {
  JobEntry* job = getJobById(jobId);
  if (!job) {
    return -1;
  }
  job->isStopped = true;
  return 0;
}


int JobsList::addExistingJob(JobEntry* job) {
  /* Recieved a job to insert (i.e, job that was taken out from the jobs list and now wants back).
  We should add it in the right place w.r.t jobId. */
  if (jobs.empty() || jobs.back()->jobId < job->jobId) {
    /* The idea is that if the list is empty, so the job inserted should inserted first.
    Otherwise, we check if the last job in the list (which, according to our invaraiant, has the maximal id) has 
    a smaller jobId than the job inserted*/
    jobs.push_back(job);
    return 0;
  } else { // else, we will insert in the middle of the list
    for (auto it = jobs.begin(); it != jobs.end(); ++it) {
      if ((*it)->jobId > job->jobId) {
        jobs.insert(it, job); // "insert" inserts just *before* the iterator (first argument) given.
        return 0;
      }
    }
  }
  return -1; //Something went wrong (shouldn't reach here, if our code works well).
}

JobsList::~JobsList() {
  for(JobEntry* job : jobs) {
    delete job;
  }
}

void JobsCommand::execute() {
  jobs->printJobsList(); 
}

/* Jobs command end */

static void handleForeground(Command* cmd, pid_t pid) { 
  /*Helper function to handle foreground *processes*, that didn't run in the background before.*/
  SmallShell& smash = SmallShell::getInstance();
  smash.setForegroundProcess(pid);
  int status;
  int w = waitpid(pid, &status, WUNTRACED); // WUNTRACED = also return if a child has stopped. needed for ctrl+z.
  if (w == -1) {
    smash.setForegroundProcess(-1);
    perror("smash error: waitpid failed");
    return;
  }
  if (WIFSTOPPED(status)) {
    //Fg process was stopped. add to jobs list.
    smash.addJob(cmd, pid, true); // true means: add stopped mark.
  }
  else if (WIFEXITED(status)) {
    smash.removeTimeout(pid);
    delete cmd; 
  }
  smash.setForegroundProcess(-1); //No process is running in the foreground now.
}

static void handleForeground(JobEntry* job) {
  /* Used for fg command, where we want to foregroung a process that is currently stopped, or runs in the background.
  That means that we have already allocated a JobEntry for it, and it already has a unique JobId that should not be changed.
  So we have to use the same JobEntry which was already created, and add it again to the list using addExistingJob. */
  int status;
  SmallShell& smash = SmallShell::getInstance();
  smash.setForegroundProcess(job->pid);
  pid_t w = waitpid(job->pid, &status, WUNTRACED);
  if (w == -1) {
    smash.setForegroundProcess(-1);
    perror("smash error: waitpid failed");
    return;
  }
  if (WIFSTOPPED(status)) {
    //job was stopped (CTRL+Z)
    smash.addJob(job, true); // true = process is stopped.
  } else if (WIFEXITED(status)) {
    //job was killed (Ctrl+C)
    smash.removeTimeout(job->pid);
    delete job;
  }
  smash.setForegroundProcess(-1);
}

static void handleForeground(Command* cmd1, Command* cmd2, pid_t p1, pid_t p2) {
  /* Used for pipe command. */
  int status;
  SmallShell& smash = SmallShell::getInstance();
  smash.setForegroundProcess(p1); 
  smash.setPipedForegroundProcess(p2);
  pid_t w = waitpid(p1, &status, WUNTRACED); 
  if (w == -1) {
    smash.setForegroundProcess(-1); 
    smash.setPipedForegroundProcess(-1);
    perror("smash error: waitpid failed");
    return;
  } 
  if (WIFSTOPPED(status)) {
    //Fg process was stopped. add to jobs list.
    smash.addJob(cmd1, p1, true); // true means: add stopped mark.
  }
  else if (WIFEXITED(status)) {
    delete cmd1; 
  }
  smash.setForegroundProcess(-1); //Ended/stopped now.
  w = waitpid(p2, &status, WUNTRACED);
  if (w == -1) {
    smash.setForegroundProcess(-1); 
    smash.setPipedForegroundProcess(-1);
    perror("smash error: waitpid failed");
    return;
  }
  if (WIFSTOPPED(status)) {
    //Fg process was stopped. add to jobs list.
    smash.addJob(cmd2, p2, true); // true means: add stopped mark.
  }
  else if (WIFEXITED(status)) {
    delete cmd2; 
  }
  smash.setForegroundProcess(-1); 
  smash.setPipedForegroundProcess(-1);
}

/* fg commang start */
void ForegroundCommand::execute() {
  JobEntry* job;
  int jobId;
  if (args_len > 2) { // too many arguments
    std::cout << "smash error: fg: invalid arguments" << std::endl;
    return;
  }
  if (args[1] == NULL) { // No second argument, take last job from the list.
    job = jobs->getLastJob(&jobId);
    if (jobId == -1) {
      std::cout << "smash error: fg: jobs list is empty" << std::endl;
      return;
    }
  } else {
    jobId = atoi(args[1]); // atoi converts a const char* to a int.
    if (jobId == 0) { // when atoi can't make the conversion, it returns 0. Luckily, no job has id 0.
       std::cout << "smash error: fg: invalid arguments" << std::endl;
       return;
    }
    job = jobs->getJobById(jobId);
    if (!job) { // jobId is not exist. getJobById returns nullptr in that case.
      std::cout << "smash error: fg: job-id " << jobId << " does not exist" << std::endl;
      return;
    }
  }
  //Now job is actually a JobEntry* and jobId is its id.
  std::cout << job->cmd->getCmdLine() << " : " << job->pid << std::endl;
  pid_t pid = job->pid; 
  if (kill(pid, SIGCONT) == -1) {
    perror("smash error: kill failed");
    return;
  }
  jobs->removeJobById(jobId); //Can't fail, job does exist (returned by getJobById)
  handleForeground(job);

}
/* fg command end */

/* bg command start */
void BackgroundCommand::execute() {
  JobEntry* job;
  int jobId;
  if (args_len > 2) { // too many arguments
    std::cout << "smash error: bg: invalid arguments" << std::endl;
    return;
  }
  if (args[1] == NULL) { // No second argument, take last job from the list.
    job = jobs->getLastStoppedJob(&jobId);
    if (jobId == -1) {
      std::cout << "smash error: bg: there is no stopped jobs to resume" << std::endl;
      return;
    }
  } else {
    jobId = atoi(args[1]); // atoi converts a const char* to a int.
    if (jobId == 0) { // when atoi can't make the conversion, it returns 0. Luckily, no job has id 0.
       std::cout << "smash error: bg: invalid arguments" << std::endl;
       return;
    }
    job = jobs->getJobById(jobId);
    if (!job) { // jobId is not exist. getJobById returns nullptr in that case.
      std::cout << "smash error: bg: job-id " << jobId << " does not exist" << std::endl;
      return;
    }
    bool res;
    jobs->checkIfStopped(jobId, & res);
    if (!res) {
      std::cout << "smash error: bg: job-id " << jobId << " is already running in the background" << std::endl;
      return;
    }
  }
  std::cout << job->cmd->getCmdLine() << " : " << job->pid << std::endl;
  pid_t pid = job->pid;
  if (kill(pid, SIGCONT) == -1) {
    perror("smash error: kill failed");
    return;
  }
  jobs->removeStopMark(jobId);
}
/* bg command end */

/* chprompt command start */

void ChangePromptCommand::execute() {
  SmallShell::getInstance().changePromptName(args[1]);
}
/*chprompt command end */

/* ExternalCommand start */
void ExternalCommand::execute() { 
  pid_t pid = fork();
  if (pid < 0) {
    perror("smash error: fork failed");
    return;
  }
  else if (pid == 0) { //child
    setpgrp();
    const char* const bash_args[] = {"/bin/bash", "-c", exec, nullptr};
    EXEC("/bin/bash", bash_args); 
  }
  else {
    //parent
    SmallShell& smash = SmallShell::getInstance();
    int duration;
    Command* timeout;
    if (smash.isTimedout(&duration, &timeout)) {
      smash.addTimeout(timeout, pid, duration);
    }
    int stdout_fd = smash.getStdout(); //For redirection...
    if (stdout_fd != -1) { 
      dup2(stdout_fd, STDOUT_FILENO); //If stdout was overriden, return it.
    }
    if (bg) { //background command, don't wait, add to jobsList.
      smash.addJob(this, pid);
    } else { //foreground command, wait, change shell's state.
      handleForeground(this, pid);
    }
  }
}
/* ExternalCommand end */

/* Pipe command start */
PipeCommand::PipeCommand(const char *cmd_line, char** args, int args_len, char* exec, bool bg) : 
  Command(cmd_line, args, args_len, exec), bg(bg) {}

void PipeCommand::execute() {
  string cmd1, cmd2, cmd_str = exec; //exec has no background sign &
  size_t index = cmd_str.find_first_of("|");
  cmd1 = cmd_str.substr(0,index);
  bool err_flag = cmd_str.find_first_of("&") != string::npos;
  // check if we have & operator
  if(err_flag && cmd_str.find_first_of("&") == index+1){
      cmd2 = cmd_str.substr(index + 2);
  } else {
      err_flag = false; // The & is for background, because it is not right after |.
      cmd2 = cmd_str.substr(index + 1);
  }
  cmd2 = _trim(cmd2);
  cmd1 =_trim(cmd1);
  // get all the vars before we start to fork the proc
  SmallShell &my_shell = SmallShell::getInstance();
  if (bg) {
    cmd1.append("&");
    cmd2.append("&");
  }
  //Create commands.
  Command *command1 = my_shell.CreateCommand(cmd1.c_str());
  Command *command2 = my_shell.CreateCommand(cmd2.c_str());
  bool isCmd1Builtin = dynamic_cast<BuiltInCommand *>(command1) != nullptr;
  if (dynamic_cast<BuiltInCommand *>(command2) != nullptr) {// the command is built-in.
      command2->execute();
      delete command1;
      delete command2;
      return;
      /// need to check if we should handle the jobs here
  }
  pid_t p1, p2;
  // create 2 post array for file descriptors
  int fileD[2]; 
  if (pipe(fileD) == -1) {
    perror("smash error: pipe failed");
  }
  int stdIn = dup(STDIN_FILENO), stdOut = dup(STDOUT_FILENO), stdErr = dup(STDERR_FILENO);
  int to_close = err_flag ? STDERR_FILENO : STDOUT_FILENO; //Depends on | or |&
  int old_out_fd = err_flag ? stdErr : stdOut; //Relevant standard stream file-object.

  /* write side */
  close(to_close); //Closes stderr or stdout.
  dup(fileD[1]); // Copies write-side of the pipe to err/out.

  if(isCmd1Builtin) {// the command is built-in.
      command1->execute();
  }
  else { // we need to use an external command - fork
      p1 = fork();
      if (p1 < 0) {
        close(fileD[0]);
        close(fileD[1]);
        dup2(stdIn, 0);
        dup2(old_out_fd, to_close);
        close(stdOut);
        close(stdIn);
        close(stdErr);
        perror("smash error: fork failed");
        return;
      }
      if (p1 == 0) { // first child, writes to the pipe.
        setpgrp();
        close(fileD[0]); // close the read side of the pipe
        /// now we need to exec the cmd1 and let it write to the pipe
        // Should check if redirection and pipes are possible.
        const char * const bash_args[] = {"/bin/bash", "-c", const_cast<char *>(command1->getExec()), nullptr};
        EXEC("/bin/bash", bash_args);
      }
  }

  /* read side */
  dup2(stdOut, STDOUT_FILENO); // Change 1 back to the stdout we started with.
  dup2(stdErr, STDERR_FILENO);
  p2 = fork();
  if (p2 < 0) {
    close(fileD[0]);
    close(fileD[1]);
    dup2(stdIn, 0);
    dup2(old_out_fd, to_close);
    close(stdOut);
    close(stdIn);
    close(stdErr);
    perror("smash error: fork failed");
    return;
  }
  else if (p2 == 0) { // second child - reads from the pipe - change std in cmd2 - we dont use here builtIn Commands
    setpgrp();
    dup2(fileD[0], 0); // set the read side of the pipe as the stdin
    close(fileD[1]); // close the write side of the pipe
    // external command
    // no need to touch signal handlers, as execv will reset them for the child.
    const char * const bash_args[] = {"/bin/bash", "-c", const_cast<char *>(command2->getExec()), nullptr};
    EXEC("/bin/bash", bash_args);
  }
  /* back to the smash proc */
  close(fileD[0]);
  close(fileD[1]);

  dup2(stdIn, 0);
  dup2(old_out_fd, to_close);
  /* SmallShell process continued */
  
  if (bg) { //pipe runs in background. treat it as two seperate jobs.
    if (!isCmd1Builtin) {
      my_shell.addJob(command1, p1);
    }
    my_shell.addJob(command2, p2);
  } else {
    if (isCmd1Builtin) {// the first command is built in
      handleForeground(command2, p2);
    } else {
      handleForeground(command1, command2, p1, p2);
    }
  }
  close(stdOut);
  close(stdIn);
  close(stdErr);
}

/* Pipe command end */

/* Redirection command start */
RedirectionCommand::RedirectionCommand(const char *cmd_line, char **args, int args_len, char *exec, bool bg) : 
  Command(cmd_line, args, args_len, exec), bg(bg) {}

void RedirectionCommand::execute() {
    SmallShell& myShell = SmallShell::getInstance();
    string cmd_str = string(exec), cmd_1, cmd_2;
    int f_index = cmd_str.find_first_of(">");
    int s_index = cmd_str.find_last_of(">");

    cmd_1 = cmd_str.substr(0, f_index); // the command that we need te exec
    cmd_2 = cmd_str.substr(s_index + 1); // the destination of the output
    cmd_2 = _trim(cmd_2); //clear spaces from filename
    if(cmd_2 == "") {
        return;
    }
    /*save "older" stdout */
    int stdout_fd = dup(STDOUT_FILENO);
    if (stdout_fd == -1) { //not really necessary...
      perror("smash error: dup failed");
      return;
    }
    /* open files */
    
    close(STDOUT_FILENO);
    /// check if we need to create folders for the redirection
    size_t f_dir_index = cmd_2.find_first_of("/");
    string dir_Path = "";
    string temp_Path = cmd_2;
    struct stat st = {0};
    if (f_dir_index != string::npos) {
        size_t l_dir_index = cmd_2.find_last_of("/");
        while (f_dir_index != l_dir_index) {
            dir_Path.append(temp_Path.substr(0,f_dir_index));
            if(stat(dir_Path.c_str(),&st) == -1) {
                int dir_int = mkdir(dir_Path.c_str(), 0700);
                if (dir_int < 0) {
                    dup2(stdout_fd, STDOUT_FILENO); 
                    close(stdout_fd);
                    perror("smash error: mkdir failed");
                    return;
                }
            }
            dir_Path.append("/");
            temp_Path = temp_Path.substr(f_dir_index+1);
            f_dir_index = temp_Path.find_first_of("/");
            l_dir_index = temp_Path.find_last_of("/");
        }
        dir_Path.append(temp_Path.substr(0,f_dir_index));
        if(stat(dir_Path.c_str(),&st) == -1) {
            int dir_int = mkdir(dir_Path.c_str(), 0700);
            if (dir_int < 0) {
                dup2(stdout_fd, STDOUT_FILENO); 
                close(stdout_fd);
                perror("smash error: mkdir failed");
                return;
            }
        }
    }

    int fd;
    if (f_index == s_index) { // we have ">" and we need to overwrite
      fd = open(cmd_2.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    }
    else { // we have ">>" ane we need to append to the file
      fd = open(cmd_2.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0666);
    }
    if (fd == -1) { // open failed
      dup2(stdout_fd, STDOUT_FILENO); 
      close(stdout_fd);
      perror("smash error: open failed");
      return;
    }
    if (bg) {
      cmd_1.append("&"); // Make sure the command created is backgrounded.
    }
    
    Command* command = myShell.CreateCommand(cmd_1.c_str());
    command->setCmdLine(getCmdLine()); //Change command to be printed to the form: command > filename, instead of command.
    myShell.setStdout(stdout_fd);
    //Don't fork built-in commands, as we wish to get a good grade :)
    command->execute(); //So much simpler now...
    myShell.setStdout(-1);
    close(fd); //fd should be 1.
    dup2(stdout_fd, STDOUT_FILENO); 
    close(stdout_fd);
}
/* Redirection command end */

/* copy command start */
void makeCopy(int f_source, int f_destination) {
  std::vector<char> buffer(1024);
  int num, wrote;
  while (true) {
    num = read(f_source, buffer.data(), buffer.size());
    if (num == 0) break;

    auto write_amount = 0;
    auto need_to_write = num;
    while (write_amount < need_to_write) {
      wrote = write(f_destination, buffer.data() + write_amount, num - write_amount);
      if (wrote < 0) {
          perror("smash error: write failed");
          close(f_destination);
          close(f_source);
          return;
      }
      write_amount += wrote;
    }
  }
  close(f_destination);
  close(f_source);
}

//Helper functions to check if two descriptors refer to the exact same place in hard-disk.
bool same_file(int fd1, int fd2) {
  struct stat stat1, stat2;
  if (fstat(fd1, &stat1) < 0) {
    perror("smash error: fstat failed");
    return false;
  }
  if (fstat(fd2, &stat2) < 0) {
    perror("smash error: fstat failed");
    return false;
  }
  return (stat1.st_dev == stat2.st_dev) && (stat1.st_ino == stat2.st_ino);
}

CopyCommand::CopyCommand(const char *cmd_line, char **args, int args_len, char *exec, bool bg) :
        Command(cmd_line, args, args_len, exec), bg(bg) {}

void CopyCommand::execute() {
  if (args_len != 3) {
      std::cout << "smash error: cp: invalid arguments" << endl;
      return;
  }
  string source, destination;
  source = args[1];
  destination = args[2];
  int f_source = open(source.c_str(), O_RDONLY);
  if (f_source == -1) {
    perror("smash error: open failed");
    return;
  }
  int f_destination = open(destination.c_str(), O_WRONLY | O_CREAT, 0666);
  if (f_destination == -1) {
    close(f_source);
    perror("smash error: open failed");
    return;
  }
  if (!same_file(f_source, f_destination)) {
    /*If the files are exactly the same, close the old descriptor and open it again without O_TRUNC,
    as we don't want to delete a filed being copied, while already exists. */
    close(f_destination);
    f_destination = open(destination.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (f_destination == -1) {
      close(f_source);
      perror("smash error: open failed");
      return;
    }
  }
  SmallShell &myShell = SmallShell::getInstance();

  pid_t pid = fork();
  if (pid == 0) { //child proc
    setpgrp();
    signal(SIGINT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    makeCopy(f_source, f_destination);
    std::cout << "smash: " << source << " was copied to " << destination << endl;
    exit(0);
  } else if (pid < 0) {
    close(f_destination);
    close(f_source);
    perror("smash error: fork failed");
    return;
  } else { // parent
    close(f_destination);
    close(f_source);
    if (bg) { // background func
      myShell.addJob(this, pid);
    }
    else { // foreground func
      handleForeground(this, pid);
    }
  }
}
/*copy command end */

/* showpid start */
void ShowPidCommand::execute() {
  //no need to check for errors, according to man getpid() is always successful.
  cout << "smash pid is " << getpid() << endl;
}

/* showpid end */

/*cd command start */
ChangeDirCommand::ChangeDirCommand(const char* cmd_line, char** args, int args_len, char* exec, char** oldPwd) 
  : BuiltInCommand(cmd_line, args, args_len, exec), oldPwd_(oldPwd) {}

void ChangeDirCommand::execute() {
    if (args_len > 2) {
        cout << "smash error: cd: too many arguments" << endl;
        return;
    } // the argument error
    if (args_len < 2) {
        return;
        // need to check what we need to print
    }
    char *cwd = getcwd(NULL, 0);
    if (cwd == nullptr) {
        perror("smash error: getcwd failed");
        return;
    }
    // check if we need to change to the last dir
    int result;
    if (args[1][0] == '-') {
        if(*oldPwd_ == nullptr){
            cout << "smash error: cd: OLDPWD not set" << endl;
            return;
        }
        result = chdir(*oldPwd_);
    }
    else if (strcmp(args[1], "..") == 0) {
        string a = cwd;
        int pos = a.find_last_of("/");
        string buff = a.substr(0, pos);

        result = chdir(buff.c_str());
    }
    else {
        result = chdir(args[1]);
    }

    if(result == -1) {
        perror("smash error: chdir failed");
        return;
    }
    else {
        if(oldPwd_ != nullptr) free(*oldPwd_);
        *oldPwd_ = cwd;
    }
}
/* cd command end */

/* pwd command start */
void GetCurrDirCommand::execute() {
  char *path = getcwd(nullptr, 0);
  if (path == nullptr) {
      return;
  } 
  std::cout << path << std::endl;
  free(path); // getcwd allocate memory for the path we need to free it
}

/* ls command start */
void LsDirectoryCommand::execute() {
  char *cwd = getcwd(NULL, 0);
  if (!cwd) {
    perror("smash error: getcwd failed");
    return;
  }
  struct dirent **namelist;
  int i = 0, n;
  n = scandir(cwd, &namelist, 0, alphasort);
  if (n == -1) {
    perror("smash error: scandir failed");
    return;
  }
  while(i < n){
    if (strcmp(namelist[i]->d_name, "..") != 0 && strcmp(namelist[i]->d_name, ".")) {
      cout << namelist[i]->d_name << endl;
    }
    i++;
  }
}
/* ls command end */

/*kill command start */
void KillCommand::execute() {
  if (args_len != 3) {
    cout << "smash error: kill: invalid arguments" << endl;
    return;
  }
  if (!std::regex_match(args[1], std::regex("[(-|+)][0-9]+")) || !std::regex_match(args[2], std::regex("[(-|+)]?[0-9]+"))) {
      cout << "smash error: kill: invalid arguments" << endl;
      return;
  }
  int jobId = stoi(args[2]), sigNum = stoi(args[1]);

  JobEntry *thisJob = job_list->getJobById(jobId);
  if (!thisJob) {
      cout << "smash error: kill: job-id " << jobId << " does not exist" << endl;
      return;
  }
  sigNum = abs(sigNum);
  if (kill(thisJob->pid, sigNum) == -1) {
      perror("smash error: kill failed");
      return;
  }
  std::cout << "signal number " << sigNum << " was sent to pid " << thisJob->pid << std::endl;
}
/* kill command end*/

/* quit command start */

void QuitCommand::execute() {
  if (args_len >= 2 && strcmp(args[1], "kill") == 0) {
    SmallShell::getInstance().cleanup(); //Kills all processes and prints.
  }
  exit(0);
}

/* quit command end */

/* timeout command start */

TimeoutList::~TimeoutList() {
  for (ToEntry* to : timeouts) {
    delete to;
  }
}

int TimeoutList::findMinTimeout() const {
  if (timeouts.empty()) return 0;
  int min = timeouts.front()->timeToLive();
  for (ToEntry* to : timeouts) {
    int left = to->timeToLive();
    if (left < min) {
      min = left;
    }
  }
  return min;
}

void TimeoutList::removeByPid(pid_t pid) {
  auto current = timeouts.begin();
  while (current != timeouts.end()) {
    if ((*current)->pid == pid) { //Time to kill.
      delete *current;
      current = timeouts.erase(current);
      return;
    } else {
      ++current;
    }
  } 
}

void TimeoutList::addTimeout(Command* cmd, pid_t pid, int duration) {
  ToEntry* to = new ToEntry(cmd, pid, duration);
  timeouts.push_back(to);
}

void TimeoutList::handleAlarms() {
  SmallShell::getInstance().removeJobs();
  auto current = timeouts.begin();
  while (current != timeouts.end()) {
    time_t now = time(NULL);
    if (now == -1) {
      perror("smash error: time failed");
      return;
    }
    if (difftime(now, (*current)->timestamp) >= (*current)->duration) { //Time to kill.
      if (kill((*current)->pid, SIGKILL) == -1) {
        perror("smash error: kill failed");
      } else {
        std::cout << "smash: " << (*current)->cmd->getCmdLine() << " timed out!" << std::endl;
      }
      delete *current;
      current = timeouts.erase(current);
    } else {
      ++current;
    }
  }
  int next_timeout = findMinTimeout();
  if (next_timeout > 0) {
    alarm(next_timeout);
  }
}

void TimeoutCommand::execute() {
  SmallShell& smash = SmallShell::getInstance();
  string timeout_cmd_str = cmd_line;
  if (!args[1] || !args[2]) {
    //Invalid command. it is not mentioned in the hw what to do in this case, but at least avoid bugs...
    std::cout << "smash error: timeout: invalid arguments" << std::endl;
    return;
  }
  string dur = args[1]; 
  size_t index = timeout_cmd_str.find(dur);
  index += dur.size(); 
  string cmd = timeout_cmd_str.substr(index); // cmd to execute.
  cmd = _trim(cmd);
  int duration;
  try {
    duration = stoi(dur);
  } catch (invalid_argument& i) {
    std::cout << "smash error: timeout: invalid arguments" << std::endl;
    return;
  } 
  if (duration <= 0) {
    std::cout << "smash error: timeout: invalid arguments" << std::endl;
    return;
  }
  time_t before = timeouts->findMinTimeout();
  if (before == 0 || (before > 0 && duration < before)) {
    alarm(duration);
  }

  Command* command = smash.CreateCommand(cmd.c_str());
  command->setCmdLine(getCmdLine()); //Do we need to print "timeout X Y" in jobs list or just the "Y"? Who knows...?
  if (dynamic_cast<BuiltInCommand*>(command) != nullptr) {
    // What to do in a timeout <> <built-in>? for now just ignore it and execute regularly.
    command->execute();
    delete command;
    return;
  }
  /* Good case: external command! (What about pipes, redirection...?) */
  smash.setTimeout(this, duration);
  command->execute();
  smash.setTimeout(nullptr, -1);
}

/* SmallShell start */
SmallShell::SmallShell() {
  old_pwd = nullptr;
}

SmallShell::~SmallShell() {}

void SmallShell::addJob(Command* cmd, pid_t pid, bool isStopped) {
  jobs.addJob(cmd, pid, isStopped);
}

void SmallShell::addJob(JobEntry* job, bool isStopped) {
  job->isStopped = isStopped;
  job->elapsed = time(NULL); // reset timer.
  if (job->elapsed == -1) { //Shouldn't happen really. I hope...
      perror("smash error: time failed");
  }
  jobs.addExistingJob(job);
}

void SmallShell::setForegroundProcess(pid_t fg) {
  fg_pid = fg;
}

pid_t SmallShell::getForegroundPid() const {
  return fg_pid;
}

void SmallShell::cleanup() {
  jobs.killAllJobs();
}

void SmallShell::changePromptName(const char* new_name) {
  if (!new_name) {
    prompt_name = string("smash");
    return;
  }
  prompt_name = string(new_name);
}

string SmallShell::getPromptName() {
  return prompt_name;
}

void SmallShell::handleAlarms() {
  timeouts.handleAlarms();
}

/**
* Creates and returns a pointer to Command class which matches the given command line (cmd_line)
*/
Command * SmallShell::CreateCommand(const char* cmd_line) {
  bool background = false;
  if (_isBackgroundComamnd(cmd_line)) {
    background = true;
  }
  if (string(cmd_line).find_first_not_of(WHITESPACE) == string::npos) return nullptr;
  // remove bg sign...
  char** args = (char**)malloc(sizeof(char*) * (COMMAND_MAX_ARGS + 1));
  char* cmd_to_execute = (char*)malloc(strlen(cmd_line) + 1);
  strcpy(cmd_to_execute, cmd_line);
  _removeBackgroundSign(cmd_to_execute);
  int args_len = _parseCommandLine(cmd_to_execute, args);
  if (args_len <= 0) { // empty command (just pressed enter)
    return nullptr;
  }
  // first check for pipes / i-o redirection commands ...
  string cmd_str = string(cmd_line);
  if (strcmp(args[0], "timeout") == 0) { //Give timeout top priority. Important.
    return new TimeoutCommand(cmd_line, args, args_len, cmd_to_execute, &timeouts, background);
  } 
  if(cmd_str.find_first_of(">") != string::npos) { //redirection
    return new RedirectionCommand(cmd_line, args, args_len, cmd_to_execute, background);
  }
  else if (cmd_str.find_first_of("|") != string::npos) { //pipe
    return new PipeCommand(cmd_line, args, args_len, cmd_to_execute, background);
  }
  else if (strcmp(args[0], "chprompt") == 0) {
    return new ChangePromptCommand(cmd_line, args, args_len, cmd_to_execute);
  }
  else if (strcmp(args[0], "ls") == 0 && args[1] == NULL) {
   return new LsDirectoryCommand(cmd_line, args, args_len,cmd_to_execute);
  }
  else if (strcmp(args[0], "showpid") == 0) {
    return new ShowPidCommand(cmd_line, args, args_len, cmd_to_execute);
  }
  else if (strcmp(args[0], "pwd") == 0) {
    return new GetCurrDirCommand(cmd_line, args, args_len, cmd_to_execute);
  }
  else if (strcmp(args[0], "cp") == 0) {
    return new CopyCommand(cmd_line, args, args_len, cmd_to_execute, background);
  }
  else if (strcmp(args[0], "cd") == 0) {
    return new ChangeDirCommand(cmd_line, args, args_len, cmd_to_execute, (char**)&this->old_pwd);
  }
  else if (strcmp(args[0], "kill") == 0) {
    return new KillCommand(cmd_line, args, args_len, cmd_to_execute, &jobs);
  }
  else if (strcmp(args[0], "jobs") == 0) {
    return new JobsCommand(cmd_line, args, args_len, cmd_to_execute, &jobs); 
  }
  else if (strcmp(args[0], "fg") == 0) {
    return new ForegroundCommand(cmd_line, args, args_len, cmd_to_execute, &jobs); 
  }
  else if (strcmp(args[0], "bg") == 0) {
    return new BackgroundCommand(cmd_line, args, args_len, cmd_to_execute, &jobs);
  }
  else if (strcmp(args[0], "quit") == 0) {
    return new QuitCommand(cmd_line, args, args_len, cmd_to_execute, &jobs);
  }
  else { //External
    return new ExternalCommand(cmd_line, args, args_len, cmd_to_execute, background);
  }
  return nullptr;
}

void SmallShell::executeCommand(const char *cmd_line) {
  jobs.removeFinishedJobs();
  Command* cmd = CreateCommand(cmd_line);
  if (!cmd) { //"Empty" command
    return;
  }
  cmd->execute();
}

/* SmallShell end */