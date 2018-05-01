/*
 * Copyright (C) 2013 Paul Kocialkowski <contact@paulk.fr>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <sys/types.h>
#include <linux/ioctl.h>
#include <linux/input.h>

#include <hardware/sensors.h>
#include <hardware/hardware.h>

#define LOG_TAG "orientationd"
#include <utils/Log.h>

#include "orientationd.h"

float smb380_convert(int value)
{
	return value * (GRAVITY_EARTH / 256.0f);
}

int smb380_get_data(struct orientationd_handlers *handlers,
	struct orientationd_data *data)
{
	struct input_event input_event;
	int input_fd;
	int rc;

	if (handlers == NULL || data == NULL)
		return -EINVAL;

	input_fd = handlers->poll_fd;
	if (input_fd < 0)
		return -1;

	do {
		rc = read(input_fd, &input_event, sizeof(input_event));
		if (rc < (int) sizeof(input_event))
			break;

		if (input_event.type == EV_REL) {
			switch (input_event.code) {
				case REL_X:
					data->acceleration.x = smb380_convert(input_event.value);
					break;
				case REL_Y:
					data->acceleration.y = smb380_convert(input_event.value);
					break;
				case REL_Z:
					data->acceleration.z = smb380_convert(input_event.value);
					break;
				default:
					continue;
			}
		}
	} while (input_event.type != EV_SYN);

	return 0;
}

struct orientationd_handlers smb380 = {
	.input_name = "accelerometer_sensor",
	.handle = SENSOR_TYPE_ACCELEROMETER,
	.poll_fd = -1,
	.get_data = smb380_get_data,
};
