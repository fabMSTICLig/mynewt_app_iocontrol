#!/bin/bash

INPUT=keys.txt
DEVEUIBASE=d1d1e800000000
if  [ -z $1 ] || [ -z $2 ]
then
    echo "Usage : genconfig.sh [start] [numkeys]"
    exit 1
fi
COUNT=$1

> $INPUT
for (( c=1; c<=$2; c++ ))
do  
  hexdump -vn16 -e'4/4 "%08X" 1 "\n"' /dev/urandom >> $INPUT
done

while read -r line
do
  APPKEY=$line
  DEVEUI=$DEVEUIBASE$(printf '%02x\n' $COUNT)
  echo "flash $DEVEUI $APPKEY"
  ./flash_config.sh $DEVEUI $APPKEY
  COUNT=$((COUNT+1))
done < "$INPUT"
