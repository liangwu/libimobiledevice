/*
 * ideviceinfo.c
 * Simple utility to show information about an attached device
 *
 * Copyright (c) 2010-2019 Nikias Bassen, All Rights Reserved.
 * Copyright (c) 2009 Martin Szulecki All Rights Reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define TOOL_NAME "ideviceinfo"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <getopt.h>
#ifndef WIN32
#include <signal.h>
#endif

#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/lockdown.h>
#include "common/utils.h"

#include <lusb0_usb.h>

#define FORMAT_KEY_VALUE 1
#define FORMAT_XML 2

#define VID_APPLE 0x5ac

static const char *domains[] = {
	"com.apple.disk_usage",
	"com.apple.disk_usage.factory",
	"com.apple.mobile.battery",
/* FIXME: For some reason lockdownd segfaults on this, works sometimes though
	"com.apple.mobile.debug",. */
	"com.apple.iqagent",
	"com.apple.purplebuddy",
	"com.apple.PurpleBuddy",
	"com.apple.mobile.chaperone",
	"com.apple.mobile.third_party_termination",
	"com.apple.mobile.lockdownd",
	"com.apple.mobile.lockdown_cache",
	"com.apple.xcode.developerdomain",
	"com.apple.international",
	"com.apple.mobile.data_sync",
	"com.apple.mobile.tethered_sync",
	"com.apple.mobile.mobile_application_usage",
	"com.apple.mobile.backup",
	"com.apple.mobile.nikita",
	"com.apple.mobile.restriction",
	"com.apple.mobile.user_preferences",
	"com.apple.mobile.sync_data_class",
	"com.apple.mobile.software_behavior",
	"com.apple.mobile.iTunes.SQLMusicLibraryPostProcessCommands",
	"com.apple.mobile.iTunes.accessories",
	"com.apple.mobile.internal", /**< iOS 4.0+ */
	"com.apple.mobile.wireless_lockdown", /**< iOS 4.0+ */
	"com.apple.fairplay",
	"com.apple.iTunes",
	"com.apple.mobile.iTunes.store",
	"com.apple.mobile.iTunes",
	NULL
};

static int is_domain_known(const char *domain)
{
	int i = 0;
	while (domains[i] != NULL) {
		if (strstr(domain, domains[i++])) {
			return 1;
		}
	}
	return 0;
}

static void find_driver(int pid, const char* udid) {
	if (!udid) {
		printf("FALSE");
		return;
	}
	//printf("find_driver pid:%d udid:%s \n", pid, udid);

	usb_init();

	const struct usb_version* version = usb_get_version();
	if (version->driver.major == -1) {
		printf("FALSE");
		return;
	}

	usb_find_busses();
	usb_find_devices();

	struct usb_bus *bus;
	struct usb_device *dev;

	bus = usb_get_busses();

	for (bus; bus; bus = bus->next)
	{
		for (dev = bus->devices; dev; dev = dev->next)
		{
			if (dev->descriptor.idVendor != VID_APPLE
				|| dev->descriptor.idProduct != pid)
			{
				continue;
			}

			usb_dev_handle *handle = usb_open(dev);

			if (handle) {
				boolean result = FALSE;
				char dev_serial[100];
				int ret = usb_get_string_simple(handle, dev->descriptor.iSerialNumber, dev_serial, 100);
				if (ret) {
					//printf("find_driver dev_serial:%s \n", dev_serial);
					if (strcmp(udid, dev_serial) == 0) {
						result = TRUE;
					}
				}
				usb_close(handle);

				if (result) {
					printf("TRUE");
					return;
				}
			}
		}
	}
	
	printf("FALSE");
}

static void print_usage(int argc, char **argv, int is_error)
{
	int i = 0;
	char *name = NULL;
	name = strrchr(argv[0], '/');
	fprintf(is_error ? stderr : stdout, "Usage: %s [OPTIONS]\n", (name ? name + 1: argv[0]));
	fprintf(is_error ? stderr : stdout,
		"\n" \
		"Show information about a connected device.\n" \
		"\n" \
		"OPTIONS:\n" \
		"  -u, --udid UDID    target specific device by UDID\n" \
		"  -n, --network      connect to network device\n" \
		"  -s, --simple       use a simple connection to avoid auto-pairing with the device\n" \
		"  -q, --domain NAME  set domain of query to NAME. Default: None\n" \
		"  -k, --key NAME     only query key specified by NAME. Default: All keys.\n" \
		"  -x, --xml          output information as xml plist instead of key/value pairs\n" \
		"  -h, --help         prints usage information\n" \
		"  -d, --debug        enable communication debugging\n" \
		"  -v, --version      prints version information\n" \
		"  -a, --assistive \n" \
		"  -r, --reset \n" \
		"  -g, --get \n" \
		"  -f, --find driver \n" \
		"\n"
	);
	fprintf(is_error ? stderr : stdout, "Known domains are:\n\n");
	while (domains[i] != NULL) {
		fprintf(is_error ? stderr : stdout, "  %s\n", domains[i++]);
	}
	fprintf(is_error ? stderr : stdout,
		"\n" \
		"Homepage:    <" PACKAGE_URL ">\n"
		"Bug Reports: <" PACKAGE_BUGREPORT ">\n"
	);
}

int main(int argc, char *argv[])
{
	lockdownd_client_t client = NULL;
	lockdownd_error_t ldret = LOCKDOWN_E_UNKNOWN_ERROR;
	idevice_t device = NULL;
	idevice_error_t ret = IDEVICE_E_UNKNOWN_ERROR;
	int simple = 0;
	int format = FORMAT_KEY_VALUE;
	const char* udid = NULL;
	int use_network = 0;
	int assistive_func = 0;
	int assistive_enable = 0;
	const char *domain = NULL;
	const char *key = NULL;
	char *xml_doc = NULL;
	uint32_t xml_length;
	plist_t node = NULL;

	int c = 0;
	const struct option longopts[] = {
		{ "debug", no_argument, NULL, 'd' },
		{ "help", no_argument, NULL, 'h' },
		{ "udid", required_argument, NULL, 'u' },
		{ "network", no_argument, NULL, 'n' },
		{ "domain", required_argument, NULL, 'q' },
		{ "key", required_argument, NULL, 'k' },
		{ "simple", no_argument, NULL, 's' },
		{ "xml", no_argument, NULL, 'x' },
		{ "version", no_argument, NULL, 'v' },
		{ "assistive", no_argument, NULL, 'a' },
		{ "reset", no_argument, NULL, 'r' },
		{ "get", no_argument, NULL, 'g' },
		{ "find", required_argument, NULL, 'f' },
		{ NULL, 0, NULL, 0}
	};

#ifndef WIN32
	signal(SIGPIPE, SIG_IGN);
#endif

	while ((c = getopt_long(argc, argv, "dhu:nq:k:sxvargf:", longopts, NULL)) != -1) {
		switch (c) {
		case 'd':
			idevice_set_debug_level(1);
			break;
		case 'u':
			if (!*optarg) {
				fprintf(stderr, "ERROR: UDID must not be empty!\n");
				print_usage(argc, argv, 1);
				return 2;
			}
			udid = optarg;
			break;
		case 'n':
			use_network = 1;
			break;
		case 'q':
			if (!*optarg) {
				fprintf(stderr, "ERROR: 'domain' must not be empty!\n");
				print_usage(argc, argv, 1);
				return 2;
			}
			domain = optarg;
			break;
		case 'k':
			if (!*optarg) {
				fprintf(stderr, "ERROR: 'key' must not be empty!\n");
				print_usage(argc, argv, 1);
				return 2;
			}
			key = optarg;
			break;
		case 'x':
			format = FORMAT_XML;
			break;
		case 's':
			simple = 1;
			break;
		case 'h':
			print_usage(argc, argv, 0);
			return 0;
		case 'v':
			printf("%s %s\n", TOOL_NAME, PACKAGE_VERSION);
			return 0;
		case 'a':
			assistive_func = 1;
			assistive_enable = 1;
			break;
		case 'r':
			assistive_func = 1;
			assistive_enable = 0;
			break;
		case 'g':
			assistive_func = 1;
			assistive_enable = 10;
			break;
		case 'f':
			if (!*optarg) {
				return 0;
			}
			find_driver(atoi(optarg), udid);
			return 0;
		default:
			print_usage(argc, argv, 1);
			return 2;
		}
	}

	argc -= optind;
	argv += optind;

	ret = idevice_new_with_options(&device, udid, (use_network) ? IDEVICE_LOOKUP_NETWORK : IDEVICE_LOOKUP_USBMUX);
	if (ret != IDEVICE_E_SUCCESS) {
		if (udid) {
			printf("ERROR: Device %s not found!\n", udid);
		} else {
			printf("ERROR: No device found!\n");
		}
		return -1;
	}

	if (assistive_func == 1) {
		ldret = lockdownd_client_new_with_handshake(device, &client, "oa");
		if (ldret != LOCKDOWN_E_SUCCESS) {
			printf("ERROR: Could not connect to lockdownd: %s (%d)\n", lockdownd_strerror(ldret), ldret);
			idevice_free(device);
			return -1;
		}

		if (assistive_enable == 10) {
			ldret = lockdownd_get_value(client, "com.apple.Accessibility", "AssistiveTouchEnabledByiTunes", &node);
			if (ldret == LOCKDOWN_E_SUCCESS) {
				if (node) {
					plist_print_to_stream(node, stdout);
					plist_free(node);
					node = NULL;
				}
			}
		}
		else {
			node = plist_new_bool(assistive_enable != 0);
			ldret = lockdownd_set_value(client, "com.apple.Accessibility", "AssistiveTouchEnabledByiTunes", node);
			if (ldret == LOCKDOWN_E_SUCCESS) {
				printf("1");
			}
		}

		lockdownd_client_free(client);
		idevice_free(device);

		return 0;
	}

	if (LOCKDOWN_E_SUCCESS != (ldret = simple ?
			lockdownd_client_new(device, &client, TOOL_NAME):
			lockdownd_client_new_with_handshake(device, &client, TOOL_NAME))) {
		fprintf(stderr, "ERROR: Could not connect to lockdownd: %s (%d)\n", lockdownd_strerror(ldret), ldret);
		idevice_free(device);
		return -1;
	}

	/* run query and output information */
	if(lockdownd_get_value(client, domain, key, &node) == LOCKDOWN_E_SUCCESS) {
		if (node) {
			switch (format) {
			case FORMAT_XML:
				plist_to_xml(node, &xml_doc, &xml_length);
				printf("%s", xml_doc);
				free(xml_doc);
				break;
			case FORMAT_KEY_VALUE:
				plist_print_to_stream(node, stdout);
				break;
			default:
				if (key != NULL)
					plist_print_to_stream(node, stdout);
			break;
			}
			plist_free(node);
			node = NULL;
		}
	}

	lockdownd_client_free(client);
	idevice_free(device);

	return 0;
}

