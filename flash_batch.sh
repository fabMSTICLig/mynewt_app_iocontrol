#!/bin/bash

STM32CLI='/home/germain/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer.sh -c port=SWD freq=4000 -w '


INPUT=keys.txt
DEVEUIBASE=d1d1e800000000
MAINPROG=built/wbasev2_io_eu868_river_prod.hex

if  [ -z $1 ]
then
    echo "Usage : flashbatch.sh [start]"
    exit 1
fi

COUNT=$1

while read -u 7 -r line
do
  APPKEY=$line
  DEVEUI=$DEVEUIBASE$(printf '%02x\n' $COUNT)
  configfile="config_${DEVEUI}.hex"
  echo "Start Flashing ${configfile} ?"
  read -p "Enter to continue"
  ${STM32CLI} $MAINPROG

  ${STM32CLI} $configfile
 
  
  COUNT=$((COUNT+1))
done 7<<< "$(cat $INPUT)"
