/*
 * Copyright (c) 2020 Jiri Svoboda
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** @addtogroup uidemo
 * @{
 */
/** @file User interface demo
 */

#include <gfx/bitmap.h>
#include <gfx/coord.h>
#include <io/pixelmap.h>
#include <stdio.h>
#include <str.h>
#include <ui/entry.h>
#include <ui/fixed.h>
#include <ui/image.h>
#include <ui/label.h>
#include <ui/pbutton.h>
#include <ui/resource.h>
#include <ui/ui.h>
#include <ui/window.h>
#include "uidemo.h"

static errno_t bitmap_moire(gfx_bitmap_t *, gfx_coord_t, gfx_coord_t);

static void wnd_close(ui_window_t *, void *);

static ui_window_cb_t window_cb = {
	.close = wnd_close
};

static void pb_clicked(ui_pbutton_t *, void *);

static ui_pbutton_cb_t pbutton_cb = {
	.clicked = pb_clicked
};

/** Window close button was clicked.
 *
 * @param window Window
 * @param arg Argument (demo)
 */
static void wnd_close(ui_window_t *window, void *arg)
{
	ui_demo_t *demo = (ui_demo_t *) arg;

	ui_quit(demo->ui);
}

/** Push button was clicked.
 *
 * @param pbutton Push button
 * @param arg Argument (demo)
 */
static void pb_clicked(ui_pbutton_t *pbutton, void *arg)
{
	ui_demo_t *demo = (ui_demo_t *) arg;
	errno_t rc;

	if (pbutton == demo->pb1) {
		rc = ui_entry_set_text(demo->entry, "OK pressed");
		if (rc != EOK)
			printf("Error changing entry text.\n");
		(void) ui_entry_paint(demo->entry);
	} else {
		rc = ui_entry_set_text(demo->entry, "Cancel pressed");
		if (rc != EOK)
			printf("Error changing entry text.\n");
		(void) ui_entry_paint(demo->entry);
	}
}

/** Run UI demo on display server. */
static errno_t ui_demo(const char *display_spec)
{
	ui_t *ui = NULL;
	ui_wnd_params_t params;
	ui_window_t *window = NULL;
	ui_demo_t demo;
	gfx_rect_t rect;
	gfx_context_t *gc;
	ui_resource_t *ui_res;
	gfx_bitmap_params_t bparams;
	gfx_bitmap_t *bitmap;
	gfx_coord2_t off;
	errno_t rc;

	rc = ui_create(display_spec, &ui);
	if (rc != EOK) {
		printf("Error creating UI on display %s.\n", display_spec);
		return rc;
	}

	ui_wnd_params_init(&params);
	params.caption = "UI Demo";
	params.style |= ui_wds_resizable;
	params.rect.p0.x = 0;
	params.rect.p0.y = 0;
	params.rect.p1.x = 220;
	params.rect.p1.y = 180;

	memset((void *) &demo, 0, sizeof(demo));
	demo.ui = ui;

	rc = ui_window_create(ui, &params, &window);
	if (rc != EOK) {
		printf("Error creating window.\n");
		return rc;
	}

	ui_window_set_cb(window, &window_cb, (void *) &demo);
	demo.window = window;

	ui_res = ui_window_get_res(window);
	gc = ui_window_get_gc(window);

	rc = ui_fixed_create(&demo.fixed);
	if (rc != EOK) {
		printf("Error creating fixed layout.\n");
		return rc;
	}

	rc = ui_label_create(ui_res, "Text label", &demo.label);
	if (rc != EOK) {
		printf("Error creating label.\n");
		return rc;
	}

	rect.p0.x = 60;
	rect.p0.y = 37;
	rect.p1.x = 160;
	rect.p1.y = 50;
	ui_label_set_rect(demo.label, &rect);
	ui_label_set_halign(demo.label, gfx_halign_center);

	rc = ui_fixed_add(demo.fixed, ui_label_ctl(demo.label));
	if (rc != EOK) {
		printf("Error adding control to layout.\n");
		return rc;
	}

	rc = ui_pbutton_create(ui_res, "OK", &demo.pb1);
	if (rc != EOK) {
		printf("Error creating button.\n");
		return rc;
	}

	ui_pbutton_set_cb(demo.pb1, &pbutton_cb, (void *) &demo);

	rect.p0.x = 15;
	rect.p0.y = 70;
	rect.p1.x = 105;
	rect.p1.y = 98;
	ui_pbutton_set_rect(demo.pb1, &rect);

	ui_pbutton_set_default(demo.pb1, true);

	rc = ui_fixed_add(demo.fixed, ui_pbutton_ctl(demo.pb1));
	if (rc != EOK) {
		printf("Error adding control to layout.\n");
		return rc;
	}

	rc = ui_pbutton_create(ui_res, "Cancel", &demo.pb2);
	if (rc != EOK) {
		printf("Error creating button.\n");
		return rc;
	}

	ui_pbutton_set_cb(demo.pb2, &pbutton_cb, (void *) &demo);

	rect.p0.x = 115;
	rect.p0.y = 70;
	rect.p1.x = 205;
	rect.p1.y = 98;
	ui_pbutton_set_rect(demo.pb2, &rect);

	rc = ui_fixed_add(demo.fixed, ui_pbutton_ctl(demo.pb2));
	if (rc != EOK) {
		printf("Error adding control to layout.\n");
		return rc;
	}

	rc = ui_entry_create(ui_res, "", &demo.entry);
	if (rc != EOK) {
		printf("Error creating entry.\n");
		return rc;
	}

	rect.p0.x = 15;
	rect.p0.y = 110;
	rect.p1.x = 205;
	rect.p1.y = 135;
	ui_entry_set_rect(demo.entry, &rect);
	ui_entry_set_halign(demo.entry, gfx_halign_center);

	rc = ui_fixed_add(demo.fixed, ui_entry_ctl(demo.entry));
	if (rc != EOK) {
		printf("Error adding control to layout.\n");
		return rc;
	}

	gfx_bitmap_params_init(&bparams);
	bparams.rect.p0.x = 0;
	bparams.rect.p0.y = 0;
	bparams.rect.p1.x = 188;
	bparams.rect.p1.y = 24;

	rc = gfx_bitmap_create(gc, &bparams, NULL, &bitmap);
	if (rc != EOK)
		return rc;

	rc = bitmap_moire(bitmap, bparams.rect.p1.x, bparams.rect.p1.y);
	if (rc != EOK)
		return rc;

	rc = ui_image_create(ui_res, bitmap, &params.rect, &demo.image);
	if (rc != EOK) {
		printf("Error creating label.\n");
		return rc;
	}

	off.x = 15;
	off.y = 145;
	gfx_rect_translate(&off, &bparams.rect, &rect);

	/* Adjust for frame width (2 x 1 pixel) */
	rect.p1.x += 2;
	rect.p1.y += 2;
	ui_image_set_rect(demo.image, &rect);
	ui_image_set_flags(demo.image, ui_imgf_frame);

	rc = ui_fixed_add(demo.fixed, ui_image_ctl(demo.image));
	if (rc != EOK) {
		printf("Error adding control to layout.\n");
		return rc;
	}

	ui_window_add(window, ui_fixed_ctl(demo.fixed));

	rc = ui_window_paint(window);
	if (rc != EOK) {
		printf("Error painting window.\n");
		return rc;
	}

	ui_run(ui);

	ui_window_destroy(window);
	ui_destroy(ui);

	return EOK;
}

/** Fill bitmap with moire pattern.
 *
 * @param bitmap Bitmap
 * @param w Bitmap width
 * @param h Bitmap height
 * @return EOK on success or an error code
 */
static errno_t bitmap_moire(gfx_bitmap_t *bitmap, gfx_coord_t w, gfx_coord_t h)
{
	int i, j;
	int k;
	pixelmap_t pixelmap;
	gfx_bitmap_alloc_t alloc;
	errno_t rc;

	rc = gfx_bitmap_get_alloc(bitmap, &alloc);
	if (rc != EOK)
		return rc;

	/* In absence of anything else, use pixelmap */
	pixelmap.width = w;
	pixelmap.height = h;
	pixelmap.data = alloc.pixels;

	for (i = 0; i < w; i++) {
		for (j = 0; j < h; j++) {
			k = i * i + j * j;
			pixelmap_put_pixel(&pixelmap, i, j,
			    PIXEL(255, k, k, 255 - k));
		}
	}

	return EOK;
}

static void print_syntax(void)
{
	printf("Syntax: uidemo [-d <display-spec>]\n");
}

int main(int argc, char *argv[])
{
	const char *display_spec = UI_DISPLAY_DEFAULT;
	errno_t rc;
	int i;

	i = 1;
	while (i < argc && argv[i][0] == '-') {
		if (str_cmp(argv[i], "-d") == 0) {
			++i;
			if (i >= argc) {
				printf("Argument missing.\n");
				print_syntax();
				return 1;
			}

			display_spec = argv[i++];
		} else {
			printf("Invalid option '%s'.\n", argv[i]);
			print_syntax();
			return 1;
		}
	}

	if (i < argc) {
		print_syntax();
		return 1;
	}

	rc = ui_demo(display_spec);
	if (rc != EOK)
		return 1;

	return 0;
}

/** @}
 */
