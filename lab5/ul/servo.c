#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>


int enablePWM(void){
	system("echo 1 > /sys/class/pwm/pwm-4:0/enable");	

	return 0;

}

int setDutyCycle(unsigned int pwm){
	char temp[80];
	sprintf(temp, "echo %u > /sys/class/pwm/pwm-4:0/duty_cycle\n", pwm);
	system(temp);
	return 0;
	
}


int main(int argc, char **argv) {
	char line[256];
    struct sigaction signal;
    int oflags;

	char buffer[256];
	unsigned int PWM;
	printf("What PWM do you want to set it too?\n");
	scanf("%u", &PWM);
	setDutyCycle(PWM);
	enablePWM(); 
	
	return 0;
}
