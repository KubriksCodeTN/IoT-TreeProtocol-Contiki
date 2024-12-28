#!/usr/bin/env python3

from __future__ import division

import re
import sys
import os.path
import argparse
from datetime import datetime
import math

nodes = []

def parse_file(log_file, testbed=False):
    # Print some basic information for the user
    print(f"Logfile: {log_file}")
    print(f"{'Cooja simulation' if not testbed else 'Testbed experiment'}\n")

    # Regular expressions
    if testbed:
        # Regex for testbed experiments
        testbed_record_pattern = r"\[(?P<time>.{23})\] INFO:firefly\.(?P<self_id>\d+): \d+\.firefly < b"
        regex_dc = re.compile(r"{}'Energest: (?P<cnt>\d+) (?P<cpu>\d+) "
                              r"(?P<lpm>\d+) (?P<tx>\d+) (?P<rx>\d+)'".format(testbed_record_pattern))
    else:
        # Regular expressions for COOJA
        record_pattern = r"(?P<time>[\w:.]+)\s+ID:(?P<self_id>\d+)\s+"
        regex_dc = re.compile(r"{}Energest: (?P<cnt>\d+) (?P<cpu>\d+) "
                              r"(?P<lpm>\d+) (?P<tx>\d+) (?P<rx>\d+)".format(record_pattern))

    # Check if any node resets
    num_resets = 0

    data = {}

    # Parse log file and add data to CSV files
    with open(log_file, 'r') as f:
        for line in f:
            # Energest Duty Cycle
            m = regex_dc.match(line)
            if m:
                d = m.groupdict()
                if testbed:
                    ts = datetime.strptime(d["time"], '%Y-%m-%d %H:%M:%S,%f')
                    ts = ts.timestamp()
                else:
                    ts = d["time"]


                d['self_id'] = int(d['self_id'])
                d['cpu'] = int(d['cpu'])
                d['lpm'] = int(d['lpm'])
                d['tx'] =  int(d['tx'])
                d['rx'] =  int(d['rx'])

                if data.get(d['self_id']) is None:
                    data[d['self_id']] = {
                        'self_id': d['self_id'],
                        'cpu': 0,
                        'lpm': 0,
                        'tx': 0,
                        'rx': 0,
                    }

                if int(d['cnt']) >= 2:
                    data[d['self_id']]['cpu'] += d['cpu']
                    data[d['self_id']]['lpm'] += d['lpm']
                    data[d['self_id']]['tx'] += d['tx']
                    data[d['self_id']]['rx'] += d['rx']

    # Analyse and print the data
    dc_lst = []

    ordered_keys = sorted(data.keys())

    for nid in ordered_keys:
        v = data[nid]

        total_time = v['cpu'] + v['lpm']
        total_radio = v['tx'] + v['rx']

        dc = 100 * total_radio / total_time
        dc_lst.append(dc)

        print("Node {}:  Duty Cycle: {:.3f}%".format(nid, dc))

    dc_mean = sum(dc_lst)/len(dc_lst)
    dc_min = min(dc_lst)
    dc_max = max(dc_lst)
    dc_std = math.sqrt(sum([(v - dc_mean)**2 for v in dc_lst])/len(dc_lst))

    print("\n----- Duty Cycle Overall Statistics -----\n")
    print("Average Duty Cycle: {:.3f}%\nStandard Deviation: {:.3f}\n"
          "Minimum: {:.3f}%\nMaximum: {:.3f}%\n".format(dc_mean,
                                                        dc_std, dc_min,
                                                        dc_max))


    if num_resets > 0:
        print("----- WARNING -----")
        print("{} nodes reset during the simulation".format(num_resets))
        print("") # To separate clearly from the following set of prints

def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('logfile', action="store", type=str,
                        help="data collection logfile to be parsed and analyzed.")
    parser.add_argument('-t', '--testbed', action='store_true',
                        help="flag for testbed experiments")
    return parser.parse_args()


if __name__ == '__main__':

    args = parse_args()
    print(args)

    if not args.logfile:
        print("Log file needs to be specified as 1st positional argument.")
    if not os.path.exists(args.logfile):
        print("The logfile argument {} does not exist.".format(args.logfile))
        sys.exit(1)
    if not os.path.isfile(args.logfile):
        print("The logfile argument {} is not a file.".format(args.logfile))
        sys.exit(1)

    # Parse log file, create CSV files, and print some stats
    parse_file(args.logfile, testbed=args.testbed)

