#!/bin/sh
echo -n pwm > /sys/devices/platform/ocp/ocp:P9_14_pinmux/state
echo 0 > /sys/class/pwm/pwmchip4/export
echo 20000000 > /sys/class/pwm/pwm-4:0/period
