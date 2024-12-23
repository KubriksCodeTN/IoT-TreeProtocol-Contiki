#!/usr/bin/env python3.7

from __future__ import division

import re
import sys
import time
import os.path
import pandas as pd


def compute_pdr(log_path, is_testbed):
    sdf = pd.read_csv(log_path + '/sent.csv')
    rdf = pd.read_csv(log_path + '/recv.csv')

    # Do not consider the last message seqn sent
    last_valid_seqn = sdf.seqn.max() - 1

    # Disregard messages sent/received with a higher seqn than the last valid one
    sdf = sdf[sdf.seqn <= last_valid_seqn]
    rdf = rdf[rdf.seqn <= last_valid_seqn]

    # Remove sent rows with src == dest
    sdf = sdf[sdf.src != sdf.dest]

    # Remove duplicates if any
    sdf.drop_duplicates(['src', 'dest', 'seqn'], keep = 'first', inplace = True)
    rdf.drop_duplicates(['src', 'dest', 'seqn'], keep = 'first', inplace = True)

    # Merge the dataframes
    mdf = pd.merge(sdf, rdf, on = ['src', 'dest', 'seqn'], how = 'left')

    # Create new df to store the results
    df = pd.DataFrame(columns = ['node', 'pdr', 'sent', 'lost'])

    print("***** PDR *****")
    # Iterate over the nodes
    nodes = sorted(sdf.src.unique())
    for node in nodes:
        rmdf = mdf[mdf.src == node]
        nsent = rmdf.sts.count()
        nlost = nsent - rmdf.rts.count()
        pdr = 100 * rmdf.rts.count() / float(len(rmdf.sts))
        print("Node: {} PDR: {:.2f}% SENT: {} LOST: {}".format(node, pdr, nsent, nlost))

        # Store the results in the DF
        idf = len(df.index)
        df.loc[idf] = [node, pdr, nsent, nlost]

    # Print network-wise statistics
    print("Overall PDR: {:.2f}% ({} LOST / {} SENT)".format(
        100 * mdf.rts.count() / float(len(mdf.sts)),
        mdf.sts.count() - mdf.rts.count(),
        mdf.sts.count()))

    # Compute latency in ms
    mdf = mdf.dropna()
    mdf['latency'] = (mdf.rts - mdf.sts) / 1e3

    # Print latency statistics
    if not is_testbed:
        print("\n***** Latency *****")
        print("Average: {:.2f} ms Stdev: {:.2f} ms Min: {:.2f} ms Max: {:.2f} ms".format(
            mdf.latency.mean(), mdf.latency.std(), mdf.latency.min(), mdf.latency.max()))


def compute_duty_cycle(log_path):
    log_file = os.path.join(log_path, 'test_dc.log')

    # Create new df to store the results
    df = pd.DataFrame(columns = ['node', 'dc'])

    # Regular expression for duty cycle log file
    regex_dc = re.compile("Sky_(?P<node_id>\d+) ON \d+ us (?P<dc1>\d+).(?P<dc2>\d+) %")

    print("\n***** Duty Cycle *****")
    with open(log_file, 'r') as f:
        for line in f:
            # Node boot
            m = regex_dc.match(line)
            if m:
                # Get dictionary with data
                d = m.groupdict()
                node_id = int(d["node_id"])
                dc = int(d["dc1"]) + (int(d["dc2"]) / 100.0)
                print("Node: {} Duty cycle: {:.2f}%".format(node_id, dc))
                # Store the results in the DF
                idf = len(df.index)
                df.loc[idf] = [node_id, dc]

    # Print network-wise statistics
    print("Overall Duty Cycle: {:.2f}% Stdev: {:.2f}% Min: {:.2f}% Max: {:.2f}%".format(
        df.dc.mean(), df.dc.std(), df.dc.min(), df.dc.max()))


if __name__ == '__main__':
    import argparse

    parser = argparse.ArgumentParser(prog='Analysis')
    parser.add_argument('dir_path', type=str, help='Path of the dir in which recv.csv and sent.csv are kept')

    parser.add_argument('--testbed', dest='testbed', default=False, action='store_true',  help='Parse as a testbed log')
    parser.add_argument('--cooja',   dest='testbed', default=False, action='store_false', help='Parse as a cooja log')

    args = parser.parse_args()
    print(args)

    # Get the log file to parse and check that it exists
    if not os.path.isdir(args.dir_path) or not os.path.exists(args.dir_path):
        print("Error: No such file ({}).".format(args.dir_path))
        sys.exit(1)

    # Compute node stats
    compute_pdr(args.dir_path, args.testbed)
    if not args.testbed:
        compute_duty_cycle(args.dir_path)
