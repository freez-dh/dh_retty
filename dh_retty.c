#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <pty.h>  /* for openpty and forkpty */
#include <utmp.h> /* for login_tty */
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <errno.h>

static int proxyfdm, proxyfd;
static const char* app_name;
static pid_t target_pid;
static struct termios orig_tio;
static int running;

void read_from_fd(int fd, char* buf, size_t buf_size, size_t* read_size)
{
	size_t n;
	n = read(fd, buf, buf_size - 1);
	if (n == 0)
	{
		*read_size = 0;
	}
	else if(n < 0)
	{
		perror("read");
		*read_size = 0;
	}
	*read_size = n;
	buf[*read_size] = 0;
}

int set_raw_terminal(int fd)
{
	struct termios tio;
	if (tcgetattr(fd, &tio)) {
		fprintf(stderr, "%s: tcgetattr(): %s\n", app_name,
				strerror(errno));
		return -1;
	}
	cfmakeraw(&tio);
	if (tcsetattr(fd, TCSANOW, &tio)) {
		fprintf(stderr, "%s: tcsetattr(): %s\n", app_name,
				strerror(errno));
		return -1;
	}
	return 0;
}

int write_pty_fd()
{
	char buf[50];
	snprintf(buf, sizeof(buf), "/tmp/dh_repty_%d", target_pid);
	FILE* fp = fopen(buf, "w");
	if (fp == NULL)
	{
		fprintf(stderr, "%s: fopen(%s): %s\n", app_name, buf,
				strerror(errno));
		return -1;
	}
	snprintf(buf, sizeof(buf), "/proc/%d/fd/%d",
			getpid(), proxyfd);
	char link_buf[50];
	if (readlink(buf, link_buf, sizeof(link_buf)) < 0)
	{
		fprintf(stderr, "%s: readlink(%s): %s\n", app_name, buf,
				strerror(errno));
		return -1;
	}
	fprintf(fp, "%s\n", link_buf);
	fprintf(fp, "%d\n", getpid());
	fclose(fp);
	return 0;
}

int start_new_pty()
{
	struct winsize ws;
	if (0 > tcgetattr(0, &orig_tio)) {
		fprintf(stderr, "%s: tcgetattr(): %s\n", app_name,
				strerror(errno));
		return -1;
	}
	if (0 > ioctl(0, TIOCGWINSZ, &ws)) {
		fprintf(stderr, "%s: ioctl(TIOCGWINSZ): %s\n", app_name,
				strerror(errno));
		return -1;
	}

	if (0 > openpty(&proxyfdm, &proxyfd, NULL, &orig_tio, &ws)) {
		fprintf(stderr, "%s: open_tty: %s\n", app_name,
				strerror(errno));
		return -1;
	}
	if (write_pty_fd() < 0)
	{
		return -1;
	}
	close(proxyfd);
	return 0;
}

int restore_can_terminal(int fd)
{
	if (tcsetattr(fd, TCSANOW, &orig_tio) < 0)
	{
		fprintf(stderr, "%s: restore_terminal: %s\n", app_name,
				strerror(errno));
		return -1;
	}
	return 0;
}

void do_pty_proxy()
{
	if (set_raw_terminal(0) < 0)
	{
		return;
	}
	struct pollfd fds[3];
	char buff[512];
	size_t read_size;
	char to0[5120], to1[5120], to2[5120];
	to0[0] = to1[0] = to2[0] = '\0';
	int fds_cnt;
	while (1)
	{
		fds[0].fd = proxyfdm;
		fds[0].events = POLLIN;
		fds[0].revents = 0;
		fds[1].fd = 0;
		fds[1].events = POLLIN;
		fds[1].revents = 0;
		fds[2].fd = 1;
		fds[2].events = POLLIN;
		fds[2].revents = 0;
		
		if (strlen(to0) != 0)
		{
			fds[0].events |= POLLOUT;
		}
		if (strlen(to2) != 0)
		{
			fds[2].events |= POLLOUT;
		}
		fds_cnt = poll(fds, 3, -1);

		if (fds_cnt < 0)
		{
			fprintf(stderr, "%s: poll() %s\n", app_name,
					strerror(errno));
			return;
		}

		if (fds[0].revents & POLLHUP) {
			break;
		}

		if (fds[0].revents & POLLIN) {
			read_from_fd(fds[0].fd,
						buff, sizeof(buff), &read_size);
			if (read_size != 0)
			{
				strcat(to2, buff);
			}
			// printf("Write from 0 to 2:%s\n", to2);
		}
		if ((fds[1].revents & POLLIN)) {
			read_from_fd(fds[1].fd,
						buff, sizeof(buff), &read_size);
			if (read_size != 0)
			{
				strcat(to0, buff);
			}
			// printf("Write from 0 to 0:%s\n", to0);
		}
		if (strlen(to0) != 0 && (fds[0].revents & POLLOUT)) {
			write(fds[0].fd, to0, strlen(to0));
			memset(to0, 0, sizeof(to0));
		}
		if (strlen(to2) && (fds[2].revents & POLLOUT)) {
			write(fds[2].fd, to2, strlen(to2));
			memset(to2, 0, sizeof(to0));
		}
	}
	restore_can_terminal(0);
}

void on_signal(int signum)
{
	running = 0;
	do_pty_proxy();
}

int main(int argc, char** argv)
{
	app_name = argv[0];
	if (argc != 2)
	{
		printf("usage:%s pid\n", app_name);
		return 0;
	}
	target_pid = atoi(argv[1]);
	if (kill(target_pid, 0) != 0)
	{
		fprintf(stderr, "%s: %d not exist\n", app_name, target_pid);
		return -1;
	}

	if (start_new_pty() != 0)
	{
		return -1;
	}

	signal(10, on_signal);
	kill(target_pid, 10);

	running = 1;
	while (running)
	{
		sleep(1);
	}

	printf("Done terminal proxy!\n");

	return 0;
}


