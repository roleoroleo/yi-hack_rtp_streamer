#!/bin/bash

SCRIPT_DIR=$(cd `dirname $0` && pwd)
cd $SCRIPT_DIR

rm -rf fdk-aac
rm -rf live
rm -f *.tar.gz
