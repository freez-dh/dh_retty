#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/un.h>
#include <sys/ioctl.h>

static const char* app_name;

static void on_signal(int signal_num)
{
	struct termios tio;
	if (tcgetattr(0, &tio)) {
		fprintf(stderr, "%s: tcgetattr(): %s\n", app_name,
				strerror(errno));
		return; 
	}
	pid_t child_pid = fork();
	if (child_pid == 0)
	{
		if (setpgid(0, 0) < 0)
		{
			fprintf(stderr, "%s: child setpgid(): %s\n", app_name,
					strerror(errno));
			return;
		}
		sleep(-1);
	}
	while (1)
	{
		if (setpgid(0, child_pid) < 0)
		{
			printf("retrying set child pgid\n");
		}
		else
		{
			break;
		}
		sleep(1);
	}
	if (setsid() < 0)
	{
		fprintf(stderr, "%s: setsid(): %s\n", app_name,
				strerror(errno));
		return;
	}
	kill(child_pid, 9);
	wait(NULL);

	char buf[50];
	snprintf(buf, sizeof(buf), "/tmp/dh_repty_%d", getpid());
	FILE* fp = fopen(buf, "r");
	if (fp == NULL)
	{
		fprintf(stderr, "%s: fopen(%s): %s\n", app_name, buf,
				strerror(errno));
		return;
	}
	fscanf(fp, "%s", buf);
	fclose(fp);

	close(0);
	close(1);
	close(2);

	open(buf, O_RDONLY);
	open(buf, O_WRONLY);
	open(buf, O_WRONLY);

	if (ioctl(0, TIOCSCTTY, 1) < 0)
	{
		fprintf(stderr, "%s: ioctl_set_ctty(): %s\n", app_name,
				strerror(errno));
		return;
	}

	tcsetattr(0, TCSANOW, &tio);

	printf("Set terminal done, enjoy it!\n");

	while (1)
	{
		// printf("in loop\n");
		int c = getchar();
		printf("receive:%c\n", c);
	}

}

int main(int argc, char** argv)
{
	app_name = argv[0];
	signal(10, on_signal);
	printf("pid:%d\n", getpid());
	char buf[512];
	while (1)
	{
		sleep(1);
	}
	return 0;
}
