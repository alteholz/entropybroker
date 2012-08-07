#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include <libusb-1.0/libusb.h>
}

const char *server_type = "server_usb v" VERSION;
const char *pid_file = PID_DIR "/server_usb.pid";
char *password = NULL;

#include "error.h"
#include "utils.h"
#include "log.h"
#include "protocol.h"
#include "server_utils.h"
#include "auth.h"

void poke(libusb_device_handle *dev)
{
	unsigned char buffer[256];

	(void)libusb_get_descriptor(dev, 0, 0, buffer, sizeof buffer);
}

long int timed_poke(libusb_device_handle *dev)
{
	struct timespec t1, t2;

	clock_gettime(CLOCK_REALTIME, &t1);
	poke(dev);
	clock_gettime(CLOCK_REALTIME, &t2);

	long int dummy = t2.tv_sec - t1.tv_sec;

	return (dummy * 1000000000 + t2.tv_nsec) - t1.tv_nsec;
}

double gen_entropy_data(libusb_device_handle *dev)
{
	return double(timed_poke(dev));
}

void sig_handler(int sig)
{
	fprintf(stderr, "Exit due to signal %d\n", sig);
	unlink(pid_file);
	exit(0);
}

void help(void)
{
	printf("-i host   entropy_broker-host to connect to\n");
	printf("-o file   file to write entropy data to\n");
	printf("-S        show bps (mutual exclusive with -n)\n");
	printf("-l file   log to file 'file'\n");
	printf("-s        log to syslog\n");
	printf("-n        do not fork\n");
	printf("-P file   write pid to file\n");
	printf("-X file   read password from file\n");
}

int main(int argc, char *argv[])
{
	unsigned char bytes[1249];
	unsigned char byte = 0;
	int bits = 0;
	char *host = NULL;
	int port = 55225;
	int socket_fd = -1;
	int c;
	char do_not_fork = 0, log_console = 0, log_syslog = 0;
	char *log_logfile = NULL;
	char *bytes_file = NULL;
	char show_bps = 0;

	fprintf(stderr, "%s, (C) 2009-2012 by folkert@vanheusden.com\n", server_type);

	while((c = getopt(argc, argv, "hX:P:So:i:l:sn")) != -1)
	{
		switch(c)
		{
			case 'X':
				password = get_password_from_file(optarg);
				break;

			case 'P':
				pid_file = optarg;
				break;

			case 'S':
				show_bps = 1;
				break;

			case 'o':
				bytes_file = optarg;
				break;

			case 'i':
				host = optarg;
				break;

			case 's':
				log_syslog = 1;
				break;

			case 'l':
				log_logfile = optarg;
				break;

			case 'n':
				do_not_fork = 1;
				log_console = 1;
				break;

			case 'h':
				help();
				return 0;

			default:
				help();
				return 1;
		}
	}

	if (!password)
		error_exit("no password set");
	set_password(password);

	if (!host && !bytes_file && show_bps == 0)
		error_exit("no host to connect to/file to write to given");

	set_logging_parameters(log_console, log_logfile, log_syslog);

	if (!do_not_fork && !show_bps)
	{
		if (daemon(-1, -1) == -1)
			error_exit("fork failed");
	}

	write_pid(pid_file);

	signal(SIGPIPE, SIG_IGN);
	signal(SIGTERM, sig_handler);
	signal(SIGINT , sig_handler);
	signal(SIGQUIT, sig_handler);

	struct libusb_device **devs = NULL;
	libusb_device_handle **devhs;
	int index = 0, n = 0, use_n = 0;

	if (libusb_init(NULL) < 0)
		error_exit("cannot init libusb");

	if (libusb_get_device_list(NULL, &devs) < 0)
		error_exit("cannot retrieve usb devicelist");

	while(devs[n] != NULL) { n++; }

	dolog(LOG_INFO, "Found %d devices", n);

	devhs = (libusb_device_handle **)malloc(sizeof(libusb_device_handle *) * n);
	for(index=0; index<n; index++)
	{
		uint8_t bus_nr = libusb_get_bus_number(devs[index]);
		uint8_t dev_nr = libusb_get_device_address(devs[index]);
		int speed = libusb_get_device_speed(devs[index]);
		struct libusb_device_descriptor desc;
		libusb_get_device_descriptor(devs[index], &desc);

		dolog(LOG_INFO, "Opening device %d: %d/%d (%d) %04x:%04x", index, bus_nr, dev_nr, speed, desc.idVendor, desc.idProduct);

		if (desc.idVendor == 0x1d6b) // ignore
			continue;

		if (libusb_open(devs[index], &devhs[use_n++]) != 0)
			error_exit("error getting usb handle");
	}

	dolog(LOG_INFO, "Using %d devices", use_n);

	long int total_byte_cnt = 0;
	double cur_start_ts = get_ts();
	int dev_index = 0;
	for(;;)
	{
		// gather random data
		double t1 = gen_entropy_data(devhs[dev_index]), t2 = gen_entropy_data(devhs[dev_index]);

		if (++dev_index == use_n)
			dev_index = 0;

		if (t1 == t2)
			continue;

		byte <<= 1;
		if (t1 > t2)
			byte |= 1;

		if (++bits == 8)
		{
			bytes[index++] = byte;
			bits = 0;

			if (index == sizeof(bytes))
			{
				if (bytes_file)
				{
					emit_buffer_to_file(bytes_file, bytes, index);
				}
				if (host)
				{
					if (message_transmit_entropy_data(host, port, &socket_fd, password, server_type, bytes, index) == -1)
					{
						dolog(LOG_INFO, "connection closed");
						close(socket_fd);
						socket_fd = -1;
					}
				}

				index = 0; // skip header
			}

			if (show_bps)
			{
				double now_ts = get_ts();

				total_byte_cnt++;

				if ((now_ts - cur_start_ts) >= 1.0)
				{
					int diff_t = now_ts - cur_start_ts;

					printf("Number of bytes: %ld, avg/s: %f\n", total_byte_cnt, (double)total_byte_cnt / diff_t);

					cur_start_ts = now_ts;
					total_byte_cnt = 0;
				}
			}
		}
	}

	for(index=0; index<n; index++)
		libusb_close(devhs[index]);

	libusb_free_device_list(devs, 1);

	libusb_exit(NULL);

	free(devhs);

	unlink(pid_file);

	return 0;
}