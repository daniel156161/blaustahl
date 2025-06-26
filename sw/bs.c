/*
 * Blaustahl Utility
 * Copyright (c) 2024 Lone Dynamics Corporation. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>

#include <libusb-1.0/libusb.h>

#define USB_MFG_ID 0x16c0
#define USB_DEV_ID 0x05e1

#define BS_FRAM_SIZE 8192

#define BS_CMD_NOP 0x00
#define BS_CMD_WRITE_BYTE 0x21
#define BS_CMD_READ_BYTE 0x31

void bsCmd(uint8_t cmd, uint8_t arg1, uint8_t arg2, uint8_t arg3);

void fram_write_byte(uint16_t addr, uint8_t d);
uint8_t fram_read_byte(uint16_t addr);

struct libusb_device_handle *usb_dh = NULL;

#define DELAY() usleep(1000);

void show_usage(char **argv);

void show_usage(char **argv) {
   printf("usage: %s [-harwv] [-a <bus> <addr>] <image.bin>\n" \
      " -h\tdisplay help\n" \
      " -r\tread from FRAM to <image.bin>\n" \
      " -w\twrite <image.bin> to FRAM\n" \
      " -v\tverify <image.bin> with FRAM\n" \
      " -a\tusb bus and address are specified as first arguments\n" \
      " -d\tdebug mode\n",
      argv[0]);
}

#define MODE_NONE 0
#define MODE_READ 1
#define MODE_WRITE 2
#define MODE_VERIFY 3

#define OPTION_NONE 0
#define OPTION_ADDR 1

int debug = 0;

int main(int argc, char *argv[]) {

   int opt;
	int mode = MODE_NONE;
	int options = 0;

   while ((opt = getopt(argc, argv, "harwvd")) != -1) {
      switch (opt) {
         case 'h': show_usage(argv); return(0); break;
         case 'r': mode = MODE_READ; break;
         case 'w': mode = MODE_WRITE; break;
         case 'v': mode = MODE_VERIFY; break;
         case 'a': options |= OPTION_ADDR; break;
         case 'd': debug = 1; break;
      }
   }

	int usb_bus = -1;
	int usb_addr = -1;

	if ((options & OPTION_ADDR) == OPTION_ADDR) {
		
		usb_bus = (uint32_t)strtol(argv[optind], NULL, 10);
		usb_addr = (uint32_t)strtol(argv[optind + 1], NULL, 10);

		optind += 2;

	}

   if ((mode == MODE_READ || mode == MODE_WRITE) && optind >= argc) {
      show_usage(argv);
      return(1);
   }

	if (libusb_init(NULL) < 0) {
		fprintf(stderr, "usb init error\n");
		exit(1);
	}

	libusb_device **list = NULL;
	ssize_t count = 0;

	count = libusb_get_device_list(NULL, &list);

	printf("devices found: \n");

	for (size_t idx = 0; idx < count; ++idx) {

		libusb_device *dev = list[idx];
		struct libusb_device_descriptor desc = {0};

		int rc = libusb_get_device_descriptor(dev, &desc);
		if (rc != 0) continue;

		int bus = libusb_get_bus_number(dev);
		int addr = libusb_get_device_address(dev);

		if (desc.idVendor == USB_MFG_ID && desc.idProduct == USB_DEV_ID) {

			printf(" vendor %04x id %04x serial %i bus %i addr %i\n",
				desc.idVendor, desc.idProduct, desc.iSerialNumber, bus, addr);

			if ((usb_bus == -1 && usb_addr == -1) ||
					(usb_bus == bus && usb_addr == addr)) {

				if (usb_dh == NULL) {
					printf("using bus %i addr %i\n", bus, addr);
					int err = libusb_open(dev, &usb_dh);
                                        if (err != LIBUSB_SUCCESS)
                                        {
                                                fprintf(stderr, "usb device open error: '%s'\n", libusb_strerror(err));
                                                exit(1);
                                        }
				}

			}
		}

	}

	if (count == 0) printf("none.\n");

	libusb_free_device_list(list, count);

	if (!usb_dh) {
		fprintf(stderr, "usb device error\n");
		exit(1);
	}

	char *buf;
	uint32_t len;

	FILE *fp;

	if (mode == MODE_WRITE || mode == MODE_VERIFY) {

		fp = fopen(argv[optind], "r");

		if (fp == NULL) {
			fprintf(stderr, "unable to open file: %s\n", argv[optind]);
			exit(1);
		}

		fseek(fp, 0L, SEEK_END);
		len = ftell(fp);
		rewind(fp);

		printf("file size: %lu\n", (unsigned long)len);

		buf = (char *)malloc(len);

		fread(buf, 1, len, fp);
		fclose(fp);

	}

	if (mode == MODE_WRITE) {

		printf("writing %i bytes to FRAM ...\n", len);

		for (int i = 0; i < len; i++) {
			fram_write_byte(i, buf[i]);
		}
		printf("done writing.\n");
		free(buf);

	} else if (mode == MODE_READ) {

		printf("reading %i bytes from FRAM to %s ...\n", BS_FRAM_SIZE,
			argv[optind]);

		char fbuf[1];
		fp = fopen(argv[optind], "w");

		for (int i = 0; i < BS_FRAM_SIZE; i++) {
			uint8_t d = fram_read_byte(i);
			fbuf[0] = d;
			fwrite(fbuf, 1, 1, fp);
		}
		printf("done reading.\n");

		fclose(fp);

	} else if (mode == MODE_VERIFY) {

		printf("verifying %i bytes from FRAM with %s ...\n", BS_FRAM_SIZE,
			argv[optind]);

		int mismatches = 0;

		for (int i = 0; i < BS_FRAM_SIZE; i++) {
			uint8_t d = fram_read_byte(i);
			if (d != (uint8_t)buf[i]) {
				printf("mismatch at address 0x%.4x\r\n", i);
				printf(" %.2x != %.2x\r\n", d, buf[i]);
				++mismatches;
			}
		}
		printf("%i mismatches.\n", mismatches);
		free(buf);

	}

	libusb_exit(NULL);

	return 0;

}

void bsCmd(uint8_t cmd, uint8_t arg1, uint8_t arg2, uint8_t arg3) {
   int actual;
	uint8_t buf[64];
	bzero(buf, 64);
	buf[0] = cmd;
	buf[1] = arg1;
	buf[2] = arg2;
	buf[3] = arg3;
	if (debug)
		printf("send cmd [%.2x %.2x %.2x %.2x]\n", cmd, arg1, arg2, arg3);
   libusb_bulk_transfer(usb_dh, (1 | LIBUSB_ENDPOINT_OUT), buf, 64,
      &actual, 0);
}

void fram_write_byte(uint16_t addr, uint8_t d) {
	bsCmd(BS_CMD_WRITE_BYTE, (addr >> 8) & 0xff, addr & 0xff, d);
}

uint8_t fram_read_byte(uint16_t addr) {
   int actual;
	uint8_t buf[64];
	bsCmd(BS_CMD_READ_BYTE, (addr >> 8) & 0xff, addr & 0xff, 0);
	again:
   libusb_bulk_transfer(usb_dh, (2 | LIBUSB_ENDPOINT_IN), buf, 64,
      &actual, 0);
	if (debug)
		printf("read %i bytes [%.2x]\n", actual, buf[0]);
	if (actual == 0) goto again;
	return buf[0];
}
