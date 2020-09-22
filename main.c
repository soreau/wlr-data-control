/*
 * MIT License
 *
 * Copyright (c) 2020 Scott Moreau
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */


#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <wayland-client.h>
#include "wlr-data-control-unstable-v1.h"


static struct wl_display *display;
static struct wl_registry *registry;
static struct wl_seat *seat;
static struct zwlr_data_control_manager_v1 *manager;
static struct zwlr_data_control_device_v1 *device;
static struct zwlr_data_control_source_v1 *selection;
static struct zwlr_data_control_source_v1 *primary_selection;
static struct zwlr_data_control_offer_v1 *offer;
static const char *mime_type = "text/plain;charset=utf-8";
static char *cb_data;
static size_t cb_size;

static void registry_add(void *data, struct wl_registry *registry,
			 uint32_t id, const char *interface,
			 uint32_t version)
{
	if (strcmp(interface, wl_seat_interface.name) == 0) {
		seat = wl_registry_bind(registry, id, &wl_seat_interface, 7);
		return;
	}

	if (strcmp(interface, zwlr_data_control_manager_v1_interface.name) == 0) {
		manager = wl_registry_bind(registry, id,
				&zwlr_data_control_manager_v1_interface, 2);
		return;
	}
}

static void registry_remove(void *data, struct wl_registry *registry,
			    uint32_t id)
{
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_add,
	.global_remove = registry_remove,
};

// Called when selection is highlighted or copied by user
static void receive_data(void *data,
	struct zwlr_data_control_offer_v1 *zwlr_data_control_offer_v1)
{
	char buf[4096];
	FILE *mem_fp;
	char *mem_data;
	size_t mem_size = 0;
	int pipe_fd[2];
	int ret;

	if (pipe(pipe_fd) == -1) {
		fprintf(stderr, "pipe() failed: %m\n");
		return;
	}

	zwlr_data_control_offer_v1_receive(zwlr_data_control_offer_v1, mime_type, pipe_fd[1]);
	wl_display_flush(display);
	close(pipe_fd[1]);

	mem_fp = open_memstream(&mem_data, &mem_size);
	if (!mem_fp) {
		fprintf(stderr, "open_memstream() failed: %m\n");
		close(pipe_fd[0]);
		return;
	}

	fprintf(stderr, "calling read()\n");
	while ((ret = read(pipe_fd[0], &buf, sizeof(buf))) > 0)
		fwrite(&buf, 1, ret, mem_fp);

	fclose(mem_fp);
	fprintf(stderr, "read() complete: read %ld bytes\n", mem_size);

	printf("\n\"");
	if (mem_size)
		fwrite(mem_data, 1, mem_size, stdout);
	printf("\"\n\n");

	free(mem_data);
	close(pipe_fd[0]);
	zwlr_data_control_offer_v1_destroy(zwlr_data_control_offer_v1);
}

static void data_control_offer(void *data,
	struct zwlr_data_control_offer_v1 *zwlr_data_control_offer_v1,
	const char *type)
{
	if (offer)
		return;
	if (strcmp(type, mime_type)) {
		return;
	}

	offer = zwlr_data_control_offer_v1;
}

struct zwlr_data_control_offer_v1_listener data_control_offer_listener = {
	data_control_offer
};

static void data_control_device_offer(void *data,
	struct zwlr_data_control_device_v1 *zwlr_data_control_device_v1,
	struct zwlr_data_control_offer_v1 *id)
{
	if (!id)
		return;

	zwlr_data_control_offer_v1_add_listener(id, &data_control_offer_listener, data);
}

static void data_control_device_selection(void *data,
	struct zwlr_data_control_device_v1 *zwlr_data_control_device_v1,
	struct zwlr_data_control_offer_v1 *id)
{
	if (id && offer == id) {
		receive_data(data, id);
		offer = NULL;
	}
}

static void data_control_device_finished(void *data,
	struct zwlr_data_control_device_v1 *zwlr_data_control_device_v1)
{
	zwlr_data_control_device_v1_destroy(zwlr_data_control_device_v1);
}

static void data_control_device_primary_selection(void *data,
	struct zwlr_data_control_device_v1 *zwlr_data_control_device_v1,
	struct zwlr_data_control_offer_v1 *id)
{
	if (id && offer == id) {
		receive_data(data, id);
		offer = NULL;
	}
}

static struct zwlr_data_control_device_v1_listener data_control_device_listener = {
	.data_offer = data_control_device_offer,
	.selection = data_control_device_selection,
	.finished = data_control_device_finished,
	.primary_selection = data_control_device_primary_selection
};

// Called when user pastes a selection
static void
data_control_source_send(void *data,
	struct zwlr_data_control_source_v1 *zwlr_data_control_source_v1,
	const char *type,
	int32_t fd)
{
	int ret;

	if(!cb_data)
		return;

	fprintf(stderr, "calling write()\n");
	ret = write(fd, cb_data, cb_size);

	if (ret < (int)cb_size) {
		fprintf(stderr, "write() from clipboard incomplete\n");
		fprintf(stderr, "wrote %d of %ld bytes\n", ret, cb_size);
	} else {
		fprintf(stderr, "write() complete: wrote %d bytes\n", ret);
	}

	close(fd);
}

static void data_control_source_cancelled(void *data,
	struct zwlr_data_control_source_v1 *zwlr_data_control_source_v1)
{
	if (selection == zwlr_data_control_source_v1) {
		selection = NULL;
		printf("Destroying selection offer\n");
	}
	if (primary_selection == zwlr_data_control_source_v1) {
		printf("Destroying primary selection offer\n");
		primary_selection = NULL;
	}
	zwlr_data_control_source_v1_destroy(zwlr_data_control_source_v1);
}

struct zwlr_data_control_source_v1_listener data_control_source_listener = {
	.send = data_control_source_send,
	.cancelled = data_control_source_cancelled
};

int main()
{
	display = wl_display_connect(NULL);
	if (!display)
		return -1;

	registry = wl_display_get_registry(display);
	if (!registry)
		return -1;

	wl_registry_add_listener(registry, &registry_listener, NULL);

	wl_display_roundtrip(display);

	device = zwlr_data_control_manager_v1_get_data_device(manager, seat);
	zwlr_data_control_device_v1_add_listener(device, &data_control_device_listener, NULL);

	wl_display_roundtrip(display);

	cb_data = strdup("Test copy/paste string");
	if (!cb_data) {
		fprintf(stderr, "%m\n");
		return -1;
	}

	cb_size = strlen(cb_data);
	printf("Offer string: \"%s\"\n", cb_data);

	selection = zwlr_data_control_manager_v1_create_data_source(manager);
	zwlr_data_control_source_v1_add_listener(selection, &data_control_source_listener, NULL);
	zwlr_data_control_source_v1_offer(selection, mime_type);
	zwlr_data_control_device_v1_set_selection(device, selection);

	primary_selection = zwlr_data_control_manager_v1_create_data_source(manager);
	zwlr_data_control_source_v1_add_listener(primary_selection, &data_control_source_listener, NULL);
	zwlr_data_control_source_v1_offer(primary_selection, mime_type);
	zwlr_data_control_device_v1_set_primary_selection(device, primary_selection);

	while (wl_display_dispatch(display) >= 0);

	wl_display_disconnect(display);

	return 0;
}