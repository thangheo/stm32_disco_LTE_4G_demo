#!/usr/bin/bash
st-info --probe
st-flash --flash=1024k write `pwd`/build/stm32_discov.bin 0x08000000
