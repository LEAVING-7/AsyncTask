import os;
import sys;
import subprocess;
import time;
import random;

# Path: RunProcessManyTimes.py
if __name__ == "__main__":
    if len(sys.argv) < 3:
        print ("Usage: python RunManyTimes.py <command> <number of times>")
        sys.exit(1)
    command = sys.argv[1]
    times = int(sys.argv[2])
    for i in range(times):
        print("Running %s for the %d time" % (command, i))
        subprocess.call(command, shell=True)
    print("Done")