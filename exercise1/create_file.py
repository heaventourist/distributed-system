#!/usr/bin/python
import argparse
import sys

parser = argparse.ArgumentParser(description='Generate a file of a specified size.')
parser.add_argument('size', type=int, help='the size in Megabytes (rounded down to the nearest 10M)')
parser.add_argument('path', help='the path of the target file')
args = parser.parse_args()
size = args.size / 10
path = args.path

s = "abcdefghij" * (1024*1024)

with open(path, 'w') as fout:
    for i in range(0, size):
        fout.write(s)