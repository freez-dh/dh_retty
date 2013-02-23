#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/un.h>
#include <sys/ioctl.h>

#define NEED_CTTY

static const char* app_name;

static void on_signal(int signal_num)
{
	int back_fd0, back_fd1, back_fd2;
	struct termios tio;
	if (tcgetattr(0, &tio)) {
		fprintf(stderr, "%s: tcgetattr(): %s\n", app_name,
				strerror(errno));
		return; 
	}
#ifdef NEED_CTTY
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
			printf("%s\n", strerror(errno));
		}
		else
			break;
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
#endif

	char buf[50];
	int from_pid;
	snprintf(buf, sizeof(buf), "/tmp/dh_repty_%d", getpid());
	FILE* fp = fopen(buf, "r");
	if (fp == NULL)
	{
		fprintf(stderr, "%s: fopen(%s): %s\n", app_name, buf,
				strerror(errno));
		return;
	}
	fscanf(fp, "%s", buf);
	fscanf(fp, "%d", &from_pid);
	fclose(fp);

	back_fd0 = dup(0);
	back_fd1 = dup(1);
	back_fd2 = dup(2);

	close(0);
	close(1);
	close(2);

	open(buf, O_RDONLY);
	open(buf, O_WRONLY);
	open(buf, O_WRONLY);

#ifdef NEED_CTTY
	if (ioctl(0, TIOCSCTTY, 1) < 0)
	{
		fprintf(stderr, "%s: ioctl_set_ctty(): %s\n", app_name,
				strerror(errno));
		return;
	}
#endif

	tcsetattr(0, TCSANOW, &tio);
	printf("from pid:%d\n", from_pid);
	kill(from_pid, 10);

	printf("Set terminal done, enjoy it!\n");

	while (1)
	{
		// printf("in loop\n");
		int c = getchar();
		if (c == 'q')
		{
			break;
		}
		printf("receive:%c\n", c);
	}

	close(0);
	close(1);
	close(2);
	
	dup2(back_fd0, 0);
	dup2(back_fd0, 1);
	dup2(back_fd0, 2);

	close(back_fd0);
	close(back_fd1);
	close(back_fd2);

	printf("back to normal process\n");
}

static int signal_nums[100];

void signal_hook(int signal_num)
{
	signal_nums[signal_num] = 1;
}

void check_signal()
{
	int i;
	for (i = 0; i < 100; ++i)
	{
		if (signal_nums[i])
		{
			signal_nums[i] = 0;
			if (i == 10)
			{
				on_signal(i);
			}
		}
	}
}

int main(int argc, char** argv)
{
	app_name = argv[0];
	printf("pid:%d\n", getpid());

	memset(signal_nums, 0, sizeof(signal_nums));
	signal(10, signal_hook);
	signal(SIGHUP, signal_hook);

	struct pollfd fds[0];
	char c;
	size_t r;
	while (1)
	{
		check_signal();
		fds[0].fd = 0;
		fds[0].events = POLLIN;
		fds[0].revents = 0;

		poll(fds, 1, 1);

		if (fds[0].revents & POLLIN) {
			r = read(fds[0].fd, &c, 1);
			if (r < 0)
			{
				printf("read from fd 0 failed:%s\n",
						strerror(errno));
				break;
			}
			printf("main process received:%c\n", c);
		}
	}
	return 0;
}
