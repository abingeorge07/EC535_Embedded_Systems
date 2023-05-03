# EC535 Spring 2023 Project: Arm of the Future
By Abin George and Justin Sadler

## Decription
This project aims to develop a system for controlling robotic arm servos directly from the Linux kernel. The primary focus will be on providing a high level of control over individual servo motors, allowing for precise movements of the robotic arm. The system will make use of a notification chain mechanism to receive input from a USB keyboard and control PWM signals. In addition to the kernel-level control, a user-space program was used for graphics on the LCD display. The resulting system will provide a powerful and flexible platform for controlling robotic arms from within the Linux operating system, with potential applications in areas such as industrial automation, research, and education.


The code was run on a Beaglebone Black. 

The arm directory contains the source code and Makefile for the linux kernel module. 

[Watch our demo here](https://youtu.be/9GUjCSS79KA)
## Compiling

```
cd arm
make
cd ../rasterwindow
make
```
Plug in a USB keyboard to the BeagleBone Black. 

Move ```arm.ko``` into the BeagleBone black and run:

```
insmod arm.ko
```

## Instructions

- Push the Up, Down, Left, and Right arrow keys to move the arm
- Push G/H (without shift or caps lock)
- Once the arm is in the desired position, define a sequence of moves using 1,2,3,4 keys (The Top Row of numbers) to define the current position in a sequence. 
- Hit Enter to begin the sequence
- Hit ESC to stop the sequence and start again!

