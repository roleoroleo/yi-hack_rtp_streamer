/*
 * Copyright (c) 2024 roleo.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>

extern int debug;

int DEVICE_NUM = 0x70;

int open_cpld() {
    int fd;
    if (access("/dev/cpld_periph", F_OK) == 0) {
        fd = open("/dev/cpld_periph", O_RDWR);
    } else {
        fd = -1;
    }

    return fd;
}

void close_cpld(int fd) {
    close(fd);
}

void run_io(int fd, int n) {
    ioctl(fd, _IOC(0, 0x70, n, 0x00), 0);
}

int speaker(int on)
{
    int num = -1;

    if (on == 1) {
        num = 16;
    } else if (on == 0) {
        num = 17;
    }

    if (num == -1) {
        return -1;
    }

    int fd = open_cpld();
    if (fd < 0) {
        fprintf(stderr, "Error: cannot open /dev/cpld_periph");
        return -2;
    }

    if (debug) fprintf(stderr, "Running ioctl: num %d\n", num);
    run_io(fd, num);

    close_cpld(fd);

    return 0;
}
