#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libmng.h>
#include <unistd.h>
#include "fbsplash_mng.h"

static mng_handle mngh;
static struct fbsplash_mng_data mng_data;

extern mng_ptr fbsplash_mng_memalloc(mng_size_t len);
extern void fbsplash_mng_memfree(mng_ptr p, mng_size_t len);
extern mng_retcode mng_init_callbacks(mng_handle handle);

int mng_load(char *filename) {
	int fd;
	int len;
	char *data;
	struct stat sb;

	if ((fd = open(filename, O_RDONLY)) < 0) {
		perror("mng_load: open");
		return 0;
	}
	if (fstat(fd, &sb) == -1) {
		perror("mng_load: stat");
		goto close_fail;
	}
	mng_data.len = sb.st_size;

	if ((mng_data.data = malloc(mng_data.len)) == NULL) {
		fprintf(stderr, "mng_load: Unable to allocate memory for MNG file\n");
		goto close_fail;
	}

	len = 0;
	data = mng_data.data;
	while (len < mng_data.len) {
		int ret;
		ret = read(fd, data, 0x10000); /* read 64KB at a time */
		switch (ret) {
			case -1:
				perror("mng_load: read");
				goto close_fail;
			case 0:
				fprintf(stderr, "mng_load: Shorter file than expected!\n");
				goto close_fail;
		}
		data += ret;
		len += ret;
	}

	close(fd);

	return 1;

close_fail:
	close(fd);
	return 0;
}

int mng_init(char* filename, int bytes_pp) {
	if (bytes_pp < 3 || bytes_pp > 4) {
		fprintf(stderr, "%s: Invalid colour depth!", __FUNCTION__);
		return 0;
	}

	memset(&mng_data, 0, sizeof(mng_data));
	mng_data.canvas_bytes_pp = bytes_pp;

	if (!mng_load(filename))
		return 0;

	mngh = mng_initialize(&mng_data, fbsplash_mng_memalloc, fbsplash_mng_memfree,
			MNG_NULL);
	if (mngh == MNG_NULL)
		return 0;

	if (mng_init_callbacks(mngh)) {
		fprintf(stderr, "mng_init_callbacks failed!\n");
		goto cleanup_fail;
	}

	if (mng_set_suspensionmode(mngh, MNG_FALSE) != MNG_NOERROR) {
		fprintf(stderr, "mng_set_suspensionmode failed!\n");
		goto cleanup_fail;
	}

	return 1;

cleanup_fail:
	mng_cleanup(&mngh);
	return 0;
}

int mng_render_next() {
	mng_retcode ret;

	if ((ret = mng_read(mngh)) != MNG_NOERROR) {
		/* Do something about it. */
		fprintf(stderr, "mng_read: error %d\n", ret);
		return 0;
	}

	ret = mng_display(mngh);
	while (ret == MNG_NEEDTIMERWAIT) {
		/*
		extern char* frame_buffer;
		mng_display_next(frame_buffer, 0, 0, 1024, 768);
		*/
		usleep(mng_data.wait_msecs*1000);
		ret = mng_display_resume(mngh);
	}
	if (ret != MNG_NOERROR) {
		/* Do something about it */
		fprintf(stderr, "mng_display(_resume)?: Error %d\n", ret);
		return 0;
	}

	return 1;
}

int mng_display_next(char* dest, int x, int y, int width, int height) {
	char *src;
	int line;
	int bpp = mng_data.canvas_bytes_pp;

	dest += y * width * bpp;
	src = mng_data.canvas;

	/* FIXME: no bounds checking if the canvas goes off the edge! */
	for (line = 0; line < mng_data.canvas_h; line++) {
		memcpy(dest + (x * bpp), src, mng_data.canvas_w * bpp);
		dest += width * bpp;
		src  += mng_data.canvas_w * bpp;
	}

	return 1;
}

void mng_done(void) {
	mng_cleanup(&mngh);
}
