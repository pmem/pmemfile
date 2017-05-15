/*
 * Copyright 2014-2017, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of the copyright holder nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * out.c -- support for logging, tracing, and assertion output
 *
 * Macros like LOG(), OUT, ASSERT(), etc. end up here.
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "os_thread.h"
#include "os_util.h"
#include "out.h"
#include "valgrind_internal.h"
#include "util.h"

#define UTIL_MAX_ERR_MSG 128

static char nvml_src_version[] = "SRCVERSION:" SRCVERSION;

static const char *Log_prefix;
static int Log_level;
static FILE *Out_fp;
static unsigned Log_alignment;

#define MAXPRINT 8192	/* maximum expected log line */

static os_once_t Last_errormsg_key_once;
static os_tls_key_t Last_errormsg_key;

static void
_Last_errormsg_key_alloc(void)
{
	int pth_ret = os_tls_key_create(&Last_errormsg_key, free);
	if (pth_ret)
		FATAL("!os_tls_key_create");

	VALGRIND_ANNOTATE_HAPPENS_BEFORE(&Last_errormsg_key_once);
}

static void
Last_errormsg_key_alloc(void)
{
	os_once(&Last_errormsg_key_once, _Last_errormsg_key_alloc);
	/*
	 * Workaround Helgrind's bug:
	 * https://bugs.kde.org/show_bug.cgi?id=337735
	 */
	VALGRIND_ANNOTATE_HAPPENS_AFTER(&Last_errormsg_key_once);
}

static inline void
Last_errormsg_fini(void)
{
	void *p = os_tls_get(Last_errormsg_key);
	if (p) {
		free(p);
		(void) os_tls_set(Last_errormsg_key, NULL);
	}
}

static inline const char *
Last_errormsg_get(void)
{
	Last_errormsg_key_alloc();

	char *errormsg = os_tls_get(Last_errormsg_key);
	if (errormsg == NULL) {
		errormsg = malloc(MAXPRINT);
		int ret = os_tls_set(Last_errormsg_key, errormsg);
		if (ret)
			FATAL("!os_tls_set");
	}
	return errormsg;
}

/*
 * out_init -- initialize the log
 *
 * This is called from the library initialization code.
 */
void
out_init(const char *log_prefix, const char *log_level_var,
		const char *log_file_var, int major_version,
		int minor_version)
{
	static int once;

	/* only need to initialize the out module once */
	if (once)
		return;
	once++;

	Log_prefix = log_prefix;

#ifdef DEBUG
	char *log_level;
	char *log_file;

	if ((log_level = getenv(log_level_var)) != NULL) {
		Log_level = atoi(log_level);
		if (Log_level < 0) {
			Log_level = 0;
		}
	}

	if ((log_file = getenv(log_file_var)) != NULL && log_file[0] != '\0') {
		size_t cc = strlen(log_file);

		/* reserve more than enough space for a PID + '\0' */
		char *log_file_pid = alloca(cc + 30);

		if (cc > 0 && log_file[cc - 1] == '-') {
			snprintf(log_file_pid, cc + 30, "%s%d",
				log_file, os_getpid());
			log_file = log_file_pid;
		}
		if ((Out_fp = fopen(log_file, "w")) == NULL) {
			char buff[UTIL_MAX_ERR_MSG];
			os_describe_errno(errno, buff, UTIL_MAX_ERR_MSG);
			fprintf(stderr, "Error (%s): %s=%s: %s\n",
					log_prefix, log_file_var,
					log_file, buff);
			abort();
		}
	}
#endif	/* DEBUG */

	char *log_alignment = getenv("NVML_LOG_ALIGN");
	if (log_alignment) {
		int align = atoi(log_alignment);
		if (align > 0)
			Log_alignment = (unsigned)align;
	}

	if (Out_fp == NULL)
		Out_fp = stderr;
	else
		setvbuf(Out_fp, NULL, _IOLBF, 0);

#ifdef DEBUG
	LOG(1, "pid %d: program: %s", os_getpid(), os_getexecname());
#endif
	LOG(1, "%s version %d.%d", log_prefix, major_version, minor_version);
	LOG(1, "src version %s", nvml_src_version);
#ifdef USE_VG_PMEMCHECK
	/*
	 * Attribute "used" to prevent compiler from optimizing out the variable
	 * when LOG expands to no code (!DEBUG)
	 */
	static pf_used_var const char *pmemcheck_msg =
			"compiled with support for Valgrind pmemcheck";
	LOG(1, "%s", pmemcheck_msg);
#endif /* USE_VG_PMEMCHECK */
#ifdef USE_VG_HELGRIND
	static pf_used_var const char *helgrind_msg =
			"compiled with support for Valgrind helgrind";
	LOG(1, "%s", helgrind_msg);
#endif /* USE_VG_HELGRIND */
#ifdef USE_VG_MEMCHECK
	static pf_used_var const char *memcheck_msg =
			"compiled with support for Valgrind memcheck";
	LOG(1, "%s", memcheck_msg);
#endif /* USE_VG_MEMCHECK */
#ifdef USE_VG_DRD
	static pf_used_var const char *drd_msg =
			"compiled with support for Valgrind drd";
	LOG(1, "%s", drd_msg);
#endif /* USE_VG_DRD */

	Last_errormsg_key_alloc();
}

/*
 * out_fini -- close the log file
 *
 * This is called to close log file before process stop.
 */
void
out_fini(void)
{
	if (Out_fp != NULL && Out_fp != stderr) {
		fclose(Out_fp);
		Out_fp = stderr;
	}

	Last_errormsg_fini();
}

/*
 * out_print_func -- default print_func, goes to stderr or Out_fp
 */
static void
out_print_func(const char *s)
{
	/* to suppress drd false-positive */
#ifdef SUPPRESS_FPUTS_DRD_ERROR
	VALGRIND_ANNOTATE_IGNORE_READS_BEGIN();
	VALGRIND_ANNOTATE_IGNORE_WRITES_BEGIN();
#endif
	fputs(s, Out_fp);
#ifdef SUPPRESS_FPUTS_DRD_ERROR
	VALGRIND_ANNOTATE_IGNORE_READS_END();
	VALGRIND_ANNOTATE_IGNORE_WRITES_END();
#endif
}

/*
 * calling Print(s) calls the current print_func...
 */
typedef void (*Print_func)(const char *s);
typedef int (*Vsnprintf_func)(char *str, size_t size, const char *format,
		va_list ap);
static Print_func Print = out_print_func;
static Vsnprintf_func Vsnprintf = vsnprintf;

/*
 * out_set_print_func -- allow override of print_func used by out module
 */
void
out_set_print_func(void (*print_func)(const char *s))
{
	LOG(3, "print %p", print_func);

	Print = (print_func == NULL) ? out_print_func : print_func;
}

/*
 * out_set_vsnprintf_func -- allow override of vsnprintf_func used by out module
 */
void
out_set_vsnprintf_func(int (*vsnprintf_func)(char *str, size_t size,
				const char *format, va_list ap))
{
	LOG(3, "vsnprintf %p", vsnprintf_func);

	Vsnprintf = (vsnprintf_func == NULL) ? vsnprintf : vsnprintf_func;
}

/*
 * out_snprintf -- (internal) custom snprintf implementation
 */
static pf_printf_like(3, 4) int
out_snprintf(char *str, size_t size, const char *format, ...)
{
	int ret;
	va_list ap;

	va_start(ap, format);
	ret = Vsnprintf(str, size, format, ap);
	va_end(ap);

	return (ret);
}

/*
 * out_common -- common output code, all output goes through here
 */
static void
out_common(const char *file, int line, const char *func, int level,
		const char *suffix, const char *fmt, va_list ap)
{
	int oerrno = errno;
	char buf[MAXPRINT];
	unsigned cc = 0;
	int ret;
	const char *sep = "";
	char errstr[UTIL_MAX_ERR_MSG] = "";

	if (file) {
		char *f = strrchr(file, DIR_SEPARATOR);
		if (f)
			file = f + 1;
		ret = out_snprintf(&buf[cc], MAXPRINT - cc,
				"<%s>: <%d> [%s:%d %s] ",
				Log_prefix, level, file, line, func);
		if (ret < 0) {
			Print("out_snprintf failed");
			goto end;
		}
		cc += (unsigned)ret;
		if (cc < Log_alignment) {
			memset(buf + cc, ' ', Log_alignment - cc);
			cc = Log_alignment;
		}
	}

	if (fmt) {
		if (*fmt == '!') {
			fmt++;
			sep = ": ";
			os_describe_errno(errno, errstr, UTIL_MAX_ERR_MSG);
		}
		ret = Vsnprintf(&buf[cc], MAXPRINT - cc, fmt, ap);
		if (ret < 0) {
			Print("Vsnprintf failed");
			goto end;
		}
		cc += (unsigned)ret;
	}

	out_snprintf(&buf[cc], MAXPRINT - cc, "%s%s%s", sep, errstr, suffix);

	Print(buf);

end:
	errno = oerrno;
}

/*
 * out_error -- common error output code, all error messages go through here
 */
static void
out_error(const char *file, int line, const char *func,
		const char *suffix, const char *fmt, va_list ap)
{
	int oerrno = errno;
	unsigned cc = 0;
	int ret;
	const char *sep = "";
	char errstr[UTIL_MAX_ERR_MSG] = "";

	char *errormsg = (char *)out_get_errormsg();

	if (fmt) {
		if (*fmt == '!') {
			fmt++;
			sep = ": ";
			os_describe_errno(errno, errstr, UTIL_MAX_ERR_MSG);
		}
		ret = Vsnprintf(&errormsg[cc], MAXPRINT, fmt, ap);
		if (ret < 0) {
			strcpy(errormsg, "Vsnprintf failed");
			goto end;
		}
		cc += (unsigned)ret;
		out_snprintf(&errormsg[cc], MAXPRINT - cc, "%s%s",
				sep, errstr);
	}

#ifdef DEBUG
	if (Log_level >= 1) {
		char buf[MAXPRINT];
		cc = 0;

		if (file) {
			char *f = strrchr(file, DIR_SEPARATOR);
			if (f)
				file = f + 1;
			ret = out_snprintf(&buf[cc], MAXPRINT,
					"<%s>: <1> [%s:%d %s] ",
					Log_prefix, file, line, func);
			if (ret < 0) {
				Print("out_snprintf failed");
				goto end;
			}
			cc += (unsigned)ret;
			if (cc < Log_alignment) {
				memset(buf + cc, ' ', Log_alignment - cc);
				cc = Log_alignment;
			}
		}

		out_snprintf(&buf[cc], MAXPRINT - cc, "%s%s", errormsg,
				suffix);

		Print(buf);
	}
#endif

end:
	errno = oerrno;
}

/*
 * out -- output a line, newline added automatically
 */
void
out(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);

	out_common(NULL, 0, NULL, 0, "\n", fmt, ap);

	va_end(ap);
}

/*
 * out_nonl -- output a line, no newline added automatically
 */
void
out_nonl(int level, const char *fmt, ...)
{
	va_list ap;

	if (Log_level < level)
			return;

	va_start(ap, fmt);
	out_common(NULL, 0, NULL, level, "", fmt, ap);

	va_end(ap);
}

/*
 * out_log -- output a log line if Log_level >= level
 */
void
out_log(const char *file, int line, const char *func, int level,
		const char *fmt, ...)
{
	va_list ap;

	if (Log_level < level)
		return;

	va_start(ap, fmt);
	out_common(file, line, func, level, "\n", fmt, ap);

	va_end(ap);
}

/*
 * out_fatal -- output a fatal error & die (i.e. assertion failure)
 */
void
out_fatal(const char *file, int line, const char *func,
		const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);

	out_common(file, line, func, 1, "\n", fmt, ap);

	va_end(ap);

	abort();
}

/*
 * out_err -- output an error message
 */
void
out_err(const char *file, int line, const char *func,
		const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);

	out_error(file, line, func, "\n", fmt, ap);

	va_end(ap);
}

/*
 * out_get_errormsg -- get the last error message
 */
const char *
out_get_errormsg(void)
{
	return Last_errormsg_get();
}
