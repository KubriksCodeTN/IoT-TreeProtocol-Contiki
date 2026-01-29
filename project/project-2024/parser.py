#!/usr/bin/env python3.7

from __future__ import division

import re
import sys
import os.path

def parse_file(log_file):
    # Create CSV output files
    fpath = os.path.dirname(log_file)
    fpath = fpath if fpath else '.'
    print(f"Creating log files in {fpath}")
    frecv = open(os.path.join(fpath, "recv.csv"), 'w')
    fsent = open(os.path.join(fpath, "sent.csv"), 'w')

    # Write CSV headers
    frecv.write("rts,src,dest,seqn,hops\n")
    fsent.write("sts,src,dest,seqn\n")

    # Regular expressions
    if args.testbed:
        start_record_pattern = r"\[[0-9\-]+ (?P<time>[0-9,:]+)\] INFO:firefly.(?P<self_id>\d+): \d+.firefly < b'"
        end_record_pattern = "'"
    else:
        start_record_pattern = r"(?P<time>[\w:.]+)\s+ID:(?P<self_id>\d+)\s+"
        end_record_pattern = ""

    regex_node = re.compile(start_record_pattern + r"App: I am (normal node|sink) (?P<src1>\w+):(?P<src2>\w+)" + end_record_pattern)
    regex_recv = re.compile(start_record_pattern + r"App: Recv from (?P<src1>\w+):(?P<src2>\w+) seqn (?P<seqn>\d+) hops (?P<hops>\d+)" + end_record_pattern)
    regex_sent = re.compile(start_record_pattern + r"App: Send seqn (?P<seqn>\d+) to (?P<dest1>\w+):(?P<dest2>\w+)" + end_record_pattern)

    # Node list and dictionaries for later processing
    nodes = {}
    nid_sent = {}

    # Parse log file and add data to CSV files
    with open(log_file, 'r') as f:
        for line in f:
            line = line.rstrip()

            # Node boot
            m = regex_node.match(line)
            if m:
                # Get dictionary with data
                d = m.groupdict()
                node_id = int(d["self_id"])
                addr = int(d['src1']+d['src2'], 16)

                nodes.setdefault(addr, (node_id, 0))
                nodes[addr] = (node_id, nodes[addr][1]+1)

                # Save data in the nodes list
                if nodes[addr][1] > 1:
                    print("WARNING: node {} reset during the simulation.".format(node_id))

                # Continue with the following line
                continue


    #print('Nodes are:')
    #for k, v in nodes.items():
    #    print('\t{:02x}:{:02x}\t{: 3}'.format(int(k/256), k%256, v[0]))

    with open(log_file, 'r') as f:
        for line in f:
            line = line.rstrip()

            # RECV 
            m = regex_recv.match(line)
            if m:
                # Get dictionary with data
                d = m.groupdict()
                ts = d["time"]
                if ':' in ts:
                    lts = ts.split(':')
                    ts = 1e6 * (float(lts[0]) * 60 + float(lts[1])) # In microseconds
                src = nodes[int(d["src1"]+d['src2'], 16)][0] # convert to decimal

                dest = int(d["self_id"])
                seqn = int(d["seqn"])
                hops = int(d["hops"])

                # Write to CSV file
                frecv.write("{},{},{},{},{}\n".format(ts, src, dest, seqn, hops))

                # Continue with the following line
                continue

            # SENT
            m = regex_sent.match(line)
            if m:
                d = m.groupdict()
                ts = d["time"]
                if ':' in ts:
                    lts = ts.split(':')
                    ts = 1e6 * (float(lts[0]) * 60 + float(lts[1])) # In microseconds
                src = int(d["self_id"])
                dest = nodes[int(d["dest1"]+d['dest2'], 16)][0] # convert to decimal
                seqn = int(d["seqn"])

                # Write to CSV file
                fsent.write("{},{},{},{}\n".format(ts, src, dest, seqn))

                if src not in nid_sent.keys():
                    nid_sent[src] = 0
                nid_sent[src] = nid_sent[src] + 1
            #else:
            #    print('"{}" has no match'.format(line))


    # Analyze dictionaries and print some stats
    # Overall number of packets sent / received

    resets = {k: v[1]-1 for k,v in nodes.items() if v[1] > 1}
    if len(resets) > 0:
        print("----- WARNING -----")
        print("Nodes reset during the simulation")
        print(resets)
        print("") # To separate clearly from the following set of prints

    # Nodes that did not manage to send data
    fails = []
    for node_id in [v[0] for k,v in nodes.items()]:
        if node_id not in nid_sent.keys():
            fails.append(node_id)
    if fails:
        print("----- Data Collection WARNING -----")
        for node_id in fails:
            print("Warning: node {} did not send any data.".format(node_id))
        print("") # To separate clearly from the following set of prints


if __name__ == '__main__':
    import argparse

    parser = argparse.ArgumentParser(prog='LogParser')
    parser.add_argument('filepath', type=str, help='Path of the .log file')

    parser.add_argument('--testbed', dest='testbed', default=False, action='store_true',  help='Parse as a testbed log')
    parser.add_argument('--cooja',   dest='testbed', default=False, action='store_false', help='Parse as a cooja log')

    args = parser.parse_args()
    print(args)

    # Get the log file to parse and check that it exists
    if not os.path.isfile(args.filepath) or not os.path.exists(args.filepath):
        print("Error: No such file ({}).".format(args.filepath))
        sys.exit(1)

    # Parse log file, create CSV files, and print some stats
    parse_file(args.filepath)
