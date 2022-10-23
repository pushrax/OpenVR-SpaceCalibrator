#!/usr/bin/env python3
import os, sys
import subprocess
import re
import argparse

#vrpathreg = "/home/zack/.local/share/Steam/steamapps/common/SteamVR/bin/vrpathreg.sh"

def GetDrivers(vrpathreg):
    proc = subprocess.Popen([vrpathreg], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, encoding='utf-8')
    (stdout, _) = proc.communicate()

    lineno = 0
    externalReg = re.compile('.*External Drivers:.*')
    lines = stdout.split('\n')
    for line in lines:
        lineno += 1
        if externalReg.match(line):
            break

    lines = [ x.strip().split(':')[1].strip() for x in lines[lineno:] if len(x) > 0]
    return lines

def RemoveDriver(vrpathreg, driver):
    cmd = [vrpathreg, 'removedriver', driver]
    print(cmd)
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, encoding='utf-8')
    (stdout, _) = proc.communicate()

def AddDriver(vrpathreg, driver):
    cmd = [vrpathreg, 'adddriver', driver]
    print(cmd)
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, encoding='utf-8')
    (stdout, _) = proc.communicate()

def main():
    parser = argparse.ArgumentParser()

    parser.add_argument('--toInstall', help="Driver to install (folder path)", required=True)
    parser.add_argument('--vrpathreg', help="Path to vrpathreg.sh", required=True)

    args = parser.parse_args()

    fail = False
    if not os.path.exists(args.toInstall):
        print("Driver path [{args.toInstall}] does not exist")
        fail = True

    if not os.path.exists(args.vrpathreg):
        print("Path to vrpathreg.sh [{args.toInstall}] does not exist")
        fail = True

    if fail: 
        return

    drivers = GetDrivers(args.vrpathreg)

    spaceCalReg = re.compile('.*01spacecalibrator.*')

    for driver in drivers:
        RemoveDriver(args.vrpathreg, driver)

    AddDriver(args.vrpathreg, args.toInstall)

    for driver in drivers[::-1]:
        if spaceCalReg.match(driver):
            continue

        AddDriver(args.vrpathreg, driver)

    pass


if __name__ == "__main__":
    main()

