#ifndef SMASH_COMMAND_H_
#define SMASH_COMMAND_H_

#include <vector>
#include <string>
#include <list>

#define COMMAND_ARGS_MAX_LENGTH (200)
#define COMMAND_MAX_ARGS (20)
#define HISTORY_MAX_RECORDS (50)


class Command {
protected:
    std::string cmd_line;
    char** args;
    int args_len;
    char* exec;
 public:
  Command(const char* line, char** args, int args_len, char* exec);
  virtual ~Command() {cleanup();} 
  virtual void execute() = 0;
  virtual void cleanup();
  std::string getCmdLine() const {
    return cmd_line;
  }
  char* getExec() const {
    return exec;
  }
  void setCmdLine(std::string newCmdline) { //just to cover up some extreme cases. use with care...
    cmd_line = newCmdline;
  }
};

class JobsList;
class SmallShell;

class BuiltInCommand : public Command {
 public:
  BuiltInCommand(const char* cmd_line, char** args, int args_len, char* exec) : Command(cmd_line, args, args_len, exec) {};
  virtual ~BuiltInCommand() {}
  virtual void execute() = 0;
};

class ExternalCommand : public Command {
  bool bg;
 public:
  ExternalCommand(const char* cmd_line, char** args, int args_len, char* exec, bool bg) :
   Command(cmd_line, args, args_len, exec), bg(bg) {
   }
  virtual ~ExternalCommand() = default;
  void execute() override;
};

class PipeCommand : public Command {
  bool bg;
 public:
  PipeCommand(const char* cmd_line, char** args, int args_len, char* exec, bool bg);
  virtual ~PipeCommand() = default;
  void execute() override;
};

class RedirectionCommand : public Command {
  std::string path;
  bool bg;
 public:
  RedirectionCommand(const char *cmd_line, char** args, int args_lae, char* exec, bool bg);
  virtual ~RedirectionCommand() {}
  void execute() override;
};

class CopyCommand : public Command {
    bool bg;
public:
    CopyCommand(const char *cmd_line, char** args, int args_lae, char* exec, bool bg);
    virtual ~CopyCommand() {}
    void execute() override;
};

class ChangePromptCommand : public BuiltInCommand {
  public:
  ChangePromptCommand(const char* cmd_line, char** args, int args_len, char* exec) :
   BuiltInCommand(cmd_line, args, args_len, exec) {}
  virtual ~ChangePromptCommand() {}
  void execute() override;
};


class ChangeDirCommand : public BuiltInCommand {
  char **oldPwd_;
  public:
  ChangeDirCommand(const char* cmd_line, char** args, int args_len, char* exec, char** oldPwd);
  virtual ~ChangeDirCommand() {}
  void execute() override;
};

class GetCurrDirCommand : public BuiltInCommand {
  public:
  GetCurrDirCommand(const char* cmd_line, char** args, int args_len, char* exec) : BuiltInCommand(cmd_line, args, args_len, exec) {}
  virtual ~GetCurrDirCommand() {}
  void execute() override;
};

class ShowPidCommand : public BuiltInCommand {
 public:
  ShowPidCommand(const char* cmd_line, char** args, int args_len, char* exec) : BuiltInCommand(cmd_line, args, args_len, exec) {}
  virtual ~ShowPidCommand() {}
  void execute() override;
};

class QuitCommand : public BuiltInCommand {
  JobsList *job_list;
  public:
  QuitCommand(const char* cmd_line, char** args, int args_len, char* exec, JobsList* jobs): BuiltInCommand(cmd_line, args, args_len,exec),job_list(jobs){}
  virtual ~QuitCommand() {}
  void execute() override;
};

class JobsList {
 public:

  class JobEntry {
  public:
   Command* cmd;
   pid_t pid;
   int jobId;
   bool isStopped;
   time_t elapsed;
    JobEntry(Command* cmd, pid_t pid, bool isStopped, int jobId) : cmd(cmd), pid(pid), jobId(jobId), isStopped(isStopped) {
      elapsed = time(NULL);
      if (elapsed == -1) {
        perror("smash error: time failed");
      }
    }

    ~JobEntry() {
      delete cmd;
    }
  };

  typedef JobsList::JobEntry JobEntry;
  std::list<JobEntry*> jobs;
 public:
  JobsList() = default;
  ~JobsList();
  void addJob(Command* cmd, pid_t pid, bool isStopped = false);
  void printJobsList();
  void killAllJobs();
  void removeFinishedJobs();
  JobEntry * getJobById(int jobId) const; //returns nullptr if not found.
  void removeJobById(int jobId);
  JobEntry *getLastJob(int* lastJobId) const; //returns nullptr and sets lastJobId = -1 if not found.
  JobEntry *getLastStoppedJob(int *jobId) const; //same.
  int checkIfStopped(int jobId, bool* res) const; //returns 0 if success, -1 otherwise (i.e., jobId does not exist).
  int removeStopMark(int jobId); // same.
  int addStopMark(int jobId);
  int addExistingJob(JobEntry* job); //The goal is to add a job that was taken out from the JobsList, and now wants to return (i.e, by ctrl+z)
};


class JobsCommand : public BuiltInCommand {
  JobsList* jobs;
 public:
  JobsCommand(const char* cmd_line, char** args, int args_len, char* exec, JobsList* jobs) : BuiltInCommand(cmd_line, args, args_len, exec), jobs(jobs) {}
  virtual ~JobsCommand() {}
  void execute() override;
};

class TimeoutList {
 public:
  struct TimeoutEntry {
    Command* cmd;
    pid_t pid;
    time_t timestamp;
    int duration;
    time_t time_to_kill;
    TimeoutEntry(Command* cmd, pid_t pid, int duration) : cmd(cmd), pid(pid), duration(duration) {
      timestamp = time(NULL);
      if (timestamp == -1) {
        perror("smash error: time failed");
      }
      time_to_kill = timestamp + duration;
    }

    int timeToLive() const {
      return difftime(time_to_kill, time(NULL));
    }

    ~TimeoutEntry() {}
  };
  int minTimeout = -1;
  typedef TimeoutList::TimeoutEntry ToEntry;
  std::list<ToEntry*> timeouts;
 public:
  TimeoutList() = default;
  ~TimeoutList();
  void addTimeout(Command* cmd, pid_t pid, int duration);
  void handleAlarms();
  int findMinTimeout() const;
  void removeByPid(pid_t);
};


class TimeoutCommand : public Command {
  TimeoutList* timeouts;
  bool bg;
 public:
  TimeoutCommand(const char* cmd_line, char** args, int args_len, char* exec, TimeoutList* timeouts, bool bg) 
    : Command(cmd_line, args, args_len, exec), timeouts(timeouts), bg(bg) {}
  virtual ~TimeoutCommand() {}
  void execute() override;
};


class ForegroundCommand : public BuiltInCommand {
 JobsList* jobs;
 public:
  ForegroundCommand(const char* cmd_line, char** args, int args_len, char* exec, JobsList* jobs) : BuiltInCommand(cmd_line, args, args_len, exec), jobs(jobs) {}
  virtual ~ForegroundCommand() {}
  void execute() override;
};

class BackgroundCommand : public BuiltInCommand {
 JobsList* jobs;
 public:
  BackgroundCommand(const char* cmd_line, char** args, int args_len, char* exec, JobsList* jobs) : BuiltInCommand(cmd_line, args, args_len, exec), jobs(jobs) {}
  virtual ~BackgroundCommand() {}
  void execute() override;
};


class KillCommand : public BuiltInCommand {
  JobsList *job_list;
  public:
  KillCommand(const char* cmd_line, char** args, int args_len, char* exec, JobsList* jobs) : BuiltInCommand(cmd_line, args, args_len,exec),job_list(jobs) {}
  virtual ~KillCommand() {}
  void execute() override;
};


class LsDirectoryCommand : public BuiltInCommand { // ls
 // TODO: Add your data members
 public:
  LsDirectoryCommand(const char* cmd_line, char** args, int args_len, char* exec): BuiltInCommand(cmd_line,args,args_len,exec){}
  virtual ~LsDirectoryCommand() {}
  void execute() override;
};


typedef JobsList::JobEntry JobEntry;
typedef TimeoutList::TimeoutEntry ToEntry;

class SmallShell {
 private:
  std::string prompt_name = "smash";
  const char* old_pwd = NULL;
  JobsList jobs;
  TimeoutList timeouts;
  pid_t fg_pid = -1;
  pid_t second_fg_pid = -1; // For pipes.

  /* For timeouts */
  int duration = -1;
  Command* toTimeout = nullptr;
  int stdout_fd = -1; 
  
  SmallShell();
 public:
  Command *CreateCommand(const char* cmd_line);
  SmallShell(SmallShell const&)      = delete; // disable copy ctor
  void operator=(SmallShell const&)  = delete; // disable = operator
  static SmallShell& getInstance() // make SmallShell singleton
  {
    static SmallShell instance; // Guaranteed to be destroyed.
    // Instantiated on first use.
    return instance;
  }
  ~SmallShell();
  void executeCommand(const char* cmd_line);

  void changePromptName(const char* new_name);

  std::string getPromptName();

  void addJob(Command* cmd, pid_t pid, bool isStopped = false);
  void addJob(JobEntry* job, bool isStopped = false);

  void removeJobs() {
    jobs.removeFinishedJobs();
  }

  void setForegroundProcess(pid_t fg);
  pid_t getForegroundPid() const;
  void setPipedForegroundProcess (pid_t pid) {
    second_fg_pid = pid;
  }
  pid_t getPipedForegroundPid() const {
    return second_fg_pid;
  }
  void cleanup();
  void handleAlarms();
  void addTimeout(Command* cmd, pid_t pid, int duration) {
    timeouts.addTimeout(cmd, pid, duration);
  }
  void setTimeout(Command* timeout, int dur) {
    toTimeout = timeout;
    duration = dur;
  }
  bool isTimedout(int* dur_p, Command** timeout) const {
    *timeout = toTimeout;
    *dur_p = duration;
    return toTimeout != nullptr;
  }
  void setStdout(int fd) {
    stdout_fd = fd;
  }
  int getStdout() const {
    return stdout_fd;
  }

  void removeTimeout(pid_t pid) {
    timeouts.removeByPid(pid);
  }
};

#endif //SMASH_COMMAND_H_