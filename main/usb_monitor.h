#pragma once

#include <stddef.h>

void usb_monitor_start();
void usb_monitor_set_maintenance(bool enabled);
void usb_monitor_copy_status(char *output, size_t output_size);
void usb_monitor_copy_reply(char *output, size_t output_size);
