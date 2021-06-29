#include <iostream>
#include <signal.h>
#include "signals.h"
#include "Commands.h"

using namespace std;

void ctrlZHandler(int sig_num) {
  SmallShell& smash = SmallShell::getInstance();
  cout << "smash: got ctrl-Z" << endl;
  pid_t pid = smash.getForegroundPid();
  if (pid != -1) {
    // Then there is a process running in the foreground.
    if (kill(pid, SIGSTOP) == -1) {
      perror("smash error: kill failed");
    } else {
      cout << "smash: process " << pid << " was stopped" << endl;
    }
  }
  pid_t pid2 = smash.getPipedForegroundPid();
  if (pid2 != -1) {
    // Then there is a process running in the foreground.
    if (kill(pid2, SIGSTOP) == -1) {
      perror("smash error: kill failed");
    } else {
      cout << "smash: process " << pid2 << " was stopped" << endl;
    }
  }
}

void ctrlCHandler(int sig_num) {
  SmallShell& smash = SmallShell::getInstance();
  cout << "smash: got ctrl-C" << endl;
  pid_t pid = smash.getForegroundPid();
  if (pid != -1) {
    // Then there is a process running in the foreground.
    if (kill(pid, SIGKILL) == -1) {
      perror("smash error: kill failed");
    } else {// Kill it.
      cout << "smash: process " << pid << " was killed" << endl;
      smash.removeTimeout(pid); 
    }
  }
  pid_t pid2 = smash.getPipedForegroundPid();
  if (pid2 != -1) { //The second command in the pipe
    if (kill(pid2, SIGKILL) == -1) {
      perror("smash error: kill failed");
    } else {// Kill it.
      cout << "smash: process " << pid2 << " was killed" << endl;
    }
  }
}

void alarmHandler(int sig_num) {
  cout << "smash: got an alarm" << endl;
  SmallShell& smash = SmallShell::getInstance();
  smash.handleAlarms();
}