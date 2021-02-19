dpatch
==========

`dpatch` is a minimalistic task runner / automation tool written in C. It aims to provide an easy-to-use interface with no frills attached for running, queuing and monitoring tasks both locally and remotely, and is meant to be extended with other Unix utilities.

A task runner should not require megabytes (or even gigabytes) of memory and complicated dependencies to simply delegate scripts and other processes.

## Installation

`dpatch` requires GCC as its build dependency.

Run `make` to create a bin/ folder and build `dpatch` into it.

Run `make install` to build and move `dpatch` to /usr/local/bin for global usage.

Run `make uninstall` to remove `dpatch` from /usr/local/bin.

## Usage

### Commands

For quick help or refresher on available commands, run `dpatch -h`.

`dpatch` Can run in two modes: agent and command.
```sh
# Example commands to run in command mode

# Sends a command to default port to set a workspace file to be used
dpatch set tests/workspace_test.ini

# Sends a command to port 8080 to run a task specified in active workspace with a variable
dpatch -p 8080 run do_stuff -e VAR1=1 

# Runs a task runner agent at port 8080
dpatch -p 8080

```

### Workspaces

`dpatch` uses customized INI-format files for describing tasks, and these files are called 'workspaces'. An example of a workspace file could be as follows:
```ini
[do_stuff]
# Reserved keyword for optionally waiting on existing task processes with given name
wait = looptest
# Reserved keyword for optionally changing working directory at the start of task
dir = tests
# Reserved keyword for task bash script
cmd = ls -lh
# Supports arbitrary entries to pass as environment variables
NODE_ENV = production

# Multi-lines are non-standardly supported by indentation (read as separate commands as it would in a bash file)
[chaintask1]
cmd = echo "This is the start of the Chain task 1"
      bin/dpatch task chaintask2

# Starting with empty into a multiline is also supported
[chaintask2]
cmd =
    echo "Chain task 2!!!"

# 'wait' on self means you can only run one instance at a time
[looptest]
wait = looptest
cmd =
    TIME=0
    while [ $TIME -le 5 ]; do
        echo "Test loop iterations: $TIME"
        TIME=$(( $TIME + 1 ))
        sleep 1s
    done
```
