#include <libmng.h>
#include <sys/time.h>
#include <time.h>
#include "fbanim_mng.h"

#if 0
#define func_enter() fprintf(stderr, "Enter %s()\n", __FUNCTION__)
#else
#define func_enter() do { } while (0)
#endif

mng_ptr fbanim_mng_memalloc(mng_size_t len) {
	func_enter();
	return calloc(1, len);
}

void fbanim_mng_memfree(mng_ptr p, mng_size_t len) {
	func_enter();
	free(p);
}

static mng_bool fbanim_mng_openstream(mng_handle handle) {
	struct fbanim_mng_data *data = mng_get_userdata(handle);

	func_enter();

	if (data->data == NULL || data->len == 0)
		return MNG_FALSE;

	data->ptr = 0;
	data->open = 1;

	return MNG_TRUE;
}

static mng_bool fbanim_mng_closestream(mng_handle handle) {
	struct fbanim_mng_data *data = mng_get_userdata(handle);

	func_enter();

	data->open = 0;
	return MNG_TRUE;
}

static mng_bool fbanim_mng_readdata(mng_handle handle, mng_ptr buf, 
		mng_uint32 len, mng_uint32p pread) {
	struct fbanim_mng_data *data = mng_get_userdata(handle);
	char *src_buf;
	
	func_enter();

	if (data->data == NULL || !data->open)
		return MNG_FALSE;

	src_buf = ((char*)data->data) + data->ptr;

	if (data->ptr + len > data->len)
		len = data->len - data->ptr;

	memcpy(buf, src_buf, len);
	if (pread)
		*pread = len;

	data->ptr += len;
	
	return MNG_TRUE;
}

static mng_ptr fbanim_mng_getcanvasline(mng_handle handle, mng_uint32 line_num) {
	struct fbanim_mng_data *data = mng_get_userdata(handle);

	func_enter();

	if (data->canvas == NULL || line_num >= data->canvas_h) {
		fprintf(stderr, "%s(mngh, %d): Requested invalid line or canvas was NULL.\n",
				__FUNCTION__, line_num);
		return MNG_NULL;
	}

	return data->canvas + (line_num * data->canvas_w * data->canvas_bytes_pp);
}

static mng_bool fbanim_mng_refresh(mng_handle handle, mng_uint32 x, mng_uint32 y,
		mng_uint32 width, mng_uint32 height) {

	func_enter();

	/*fprintf(stderr, "mng_refresh(handle, %d, %d, %d, %d) called\n",
			x, y, width, height);*/
	return MNG_TRUE;
}

static mng_uint32 fbanim_mng_gettickcount(mng_handle handle) {
	struct timeval tv;
	struct fbanim_mng_data *data = mng_get_userdata(handle);

	func_enter();

	if (gettimeofday(&tv, NULL) < 0) {
		perror("fbanim_mng_gettickcount: gettimeofday");
		abort();
	}

	if (data->start_time.tv_sec == 0) {
		data->start_time.tv_sec = tv.tv_sec;
		data->start_time.tv_usec = tv.tv_usec;
	}

	return ((tv.tv_sec - data->start_time.tv_sec)*1000) +
		((tv.tv_usec - data->start_time.tv_usec)/1000);
}

static mng_bool fbanim_mng_settimer(mng_handle handle, mng_uint32 msecs) {
	struct fbanim_mng_data *data = mng_get_userdata(handle);

	func_enter();

	data->wait_msecs = msecs;
	return MNG_TRUE;
}

static mng_bool fbanim_mng_processheader(mng_handle handle, mng_uint32 width,
		mng_uint32 height) {
	struct fbanim_mng_data *data = mng_get_userdata(handle);

	func_enter();

	free(data->canvas);

	if ((data->canvas = malloc(width*height*data->canvas_bytes_pp)) == NULL) {
		fprintf(stderr, "%s: Unable to allocate memory for MNG canvas\n",
				__FUNCTION__);
		return MNG_FALSE;
	}
	data->canvas_w = width;
	data->canvas_h = height;

	mng_set_canvasstyle(handle,
			(data->canvas_bytes_pp == 3)?MNG_CANVAS_BGR8:MNG_CANVAS_BGRA8);

	mng_set_bgcolor(handle, 0, 0, 0); /* FIXME - make configurable */

	return MNG_TRUE;
}

static mng_bool fbanim_mng_errorproc(mng_handle handler, mng_int32 code,
		mng_int8 severity, mng_chunkid chunkname, mng_uint32 chunkseq,
		mng_int32 extra1, mng_int32 extra2, mng_pchar errtext) {
	fprintf(stderr, "libmng error: Code: %d, Severity: %d - %s\n",
			code, severity, errtext);
	abort();
	return MNG_TRUE;
}

#ifdef MNG_SUPPORT_TRACE
static mng_bool fbanim_mng_traceproc(mng_handle handle, mng_int32 funcnr,
		mng_int32 seq, mng_pchar funcname) {
	fprintf(stderr, "libmng trace: %s (seq %d\n)", funcname, seq);
	return MNG_TRUE;

}
#endif

mng_retcode mng_init_callbacks(mng_handle handle) {
	mng_retcode ret;

#define set_cb(x) \
		if ((ret = mng_setcb_##x(handle, fbanim_mng_##x)) != MNG_NOERROR) \
			return ret;

	set_cb(errorproc);
	set_cb(openstream);
	set_cb(closestream);
	set_cb(readdata);
	set_cb(getcanvasline);
	set_cb(refresh);
	set_cb(gettickcount);
	set_cb(settimer);
	set_cb(processheader);
#ifdef MNG_SUPPORT_TRACE
	set_cb(traceproc);
#endif

	return MNG_NOERROR;
#undef set_cb
}