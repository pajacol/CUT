#!/bin/bash
FILE=`readlink -f $1`
cd `dirname $0`
../bin/read_file_test $FILE | sha1sum -b | head -c 40
echo
sha1sum -b $FILE | head -c 40
echo
