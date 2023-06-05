/*
 * Copyright (C) 2013-2019 Nikos Mavrogiannopoulos
 *
 * This file is part of ocserv.
 *
 * ocserv is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * ocserv is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <worker.h>

#ifdef HAVE_LIBSECCOMP

#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif
#include <unistd.h>
#include <sys/syscall.h>
#include <seccomp.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <errno.h>

/* libseccomp 2.4.2 broke accidentally the API. Work around it. */
#ifndef __SNR_ppoll
# ifdef __NR_ppoll
#  define __SNR_ppoll			__NR_ppoll
# else
#  define __SNR_ppoll			__PNR_ppoll
# endif
#endif

/* On certain cases gnulib defines gettimeofday as macro; avoid that */
#undef gettimeofday

#ifdef USE_SECCOMP_TRAP
# define _SECCOMP_ERR SCMP_ACT_TRAP
#include <execinfo.h>
#include <signal.h>
void sigsys_action(int sig, siginfo_t * info, void* ucontext)
{
	char * call_addr = *backtrace_symbols(&info->si_call_addr, 1);
	fprintf(stderr, "Function %s called disabled syscall %d\n", call_addr, info->si_syscall);
	exit(EXIT_FAILURE);
}

int set_sigsys_handler(struct worker_st *ws)
{
	struct sigaction sa = {};

	sa.sa_sigaction = sigsys_action;
	sa.sa_flags = SA_SIGINFO;

	return sigaction(SIGSYS, &sa, NULL);
}
#else
# define _SECCOMP_ERR SCMP_ACT_ERRNO(ENOSYS)
int set_sigsys_handler(struct worker_st *ws)
{
	return 0;
}
#endif


int disable_system_calls(struct worker_st *ws)
{
	int ret;
	scmp_filter_ctx ctx;
	vhost_cfg_st *vhost = NULL;

	if (set_sigsys_handler(ws))
	{
		oclog(ws, LOG_ERR, "set_sigsys_handler");
		return -1;
	}

	ctx = seccomp_init(_SECCOMP_ERR);
	if (ctx == NULL) {
		oclog(ws, LOG_DEBUG, "could not initialize seccomp");
		return -1;
	}

#define ADD_SYSCALL(name, ...) \
	ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(name), __VA_ARGS__); \
	/* libseccomp returns EDOM for pseudo-syscalls due to a bug */ \
	if (ret < 0 && ret != -EDOM) { \
		oclog(ws, LOG_DEBUG, "could not add " #name " to seccomp filter: %s", strerror(-ret)); \
		ret = -1; \
		goto fail; \
	}

	/* These seem to be called by libc or some other dependent library;
	 * they are not necessary for functioning, but we must allow them in order
	 * to run under trap mode. */
	ADD_SYSCALL(getcwd, 0);
	ADD_SYSCALL(lstat, 0);

	/* Socket wrapper tests use additional syscalls; only enable
	 * them when socket wrapper is active */
	if (getenv("SOCKET_WRAPPER_DIR") != NULL) {
		ADD_SYSCALL(readlink, 0);
	}

	/* we use quite some system calls here, and in the end
	 * we don't even know whether a newer libc will change the
	 * underlying calls to something else. seccomp seems to be useful
	 * in very restricted designs.
	 */
	ADD_SYSCALL(time, 0);
	ADD_SYSCALL(gettimeofday, 0);
#if defined(HAVE_CLOCK_GETTIME)
	ADD_SYSCALL(clock_gettime, 0);
#if defined(SYS_clock_gettime64) || defined(__NR_clock_gettime64)
	ADD_SYSCALL(clock_gettime64, 0);
#endif
#endif
	ADD_SYSCALL(clock_nanosleep, 0);
#if defined(SYS_clock_nanosleep64) || defined(__NR_clock_nanosleep64)
	ADD_SYSCALL(clock_nanosleep64, 0);
#endif
	ADD_SYSCALL(nanosleep, 0);
	ADD_SYSCALL(getrusage, 0);
	ADD_SYSCALL(alarm, 0);
	/* musl libc doesn't call alarm but setitimer */
	ADD_SYSCALL(setitimer, 0);
	ADD_SYSCALL(getpid, 0);

	/* memory allocation - both are used by different platforms */
	ADD_SYSCALL(brk, 0);
	ADD_SYSCALL(mmap, 0);

#if defined(SYS_getrandom) || defined(__NR_getrandom)
	ADD_SYSCALL(getrandom, 0); /* used by gnutls 3.5.x */
#endif
	ADD_SYSCALL(recvmsg, 0);
	ADD_SYSCALL(sendmsg, 0);

	ADD_SYSCALL(read, 0);

	ADD_SYSCALL(write, 0);
	ADD_SYSCALL(writev, 0);

	ADD_SYSCALL(send, 0);
	ADD_SYSCALL(recv, 0);

	/* Required by new versions of glibc */
	ADD_SYSCALL(futex, 0);

	/* it seems we need to add sendto and recvfrom
	 * since send() and recv() aren't called by libc.
	 */
	ADD_SYSCALL(sendto, 0);
	ADD_SYSCALL(recvfrom, 0);

	/* allow returning from the signal handler */
	ADD_SYSCALL(sigreturn, 0);
	ADD_SYSCALL(rt_sigreturn, 0);

	/* we use it in select */
	ADD_SYSCALL(sigprocmask, 0);
	ADD_SYSCALL(rt_sigprocmask, 0);

	ADD_SYSCALL(poll, 0);
	ADD_SYSCALL(ppoll, 0);

	/* allow setting non-blocking sockets */
	ADD_SYSCALL(fcntl, 0);
#if defined(SYS_fcntl64) || defined(__NR_fcntl64)
	ADD_SYSCALL(fcntl64, 0);
#endif
	ADD_SYSCALL(close, 0);
	ADD_SYSCALL(exit, 0);
	ADD_SYSCALL(exit_group, 0);
	ADD_SYSCALL(socket, 0);
	ADD_SYSCALL(connect, 0);

	ADD_SYSCALL(openat, 0);
	ADD_SYSCALL(fstat, 0);
	ADD_SYSCALL(stat, 0);
#if defined(SYS_fstat64) || defined(__NR_fstat64)
	ADD_SYSCALL(fstat64, 0);
#endif
	ADD_SYSCALL(stat64, 0);
	ADD_SYSCALL(newfstatat, 0);
	ADD_SYSCALL(lseek, 0);

	ADD_SYSCALL(getsockopt, 0);
	ADD_SYSCALL(setsockopt, 0);


#ifdef ANYCONNECT_CLIENT_COMPAT
	/* we need to open files when we have an xml_config_file setup on any vhost */
	list_for_each(ws->vconfig, vhost, list) {
		if (vhost->perm_config.config->xml_config_file) {
			ADD_SYSCALL(open, 0);
			ADD_SYSCALL(openat, 0);
			break;
		}
	}
#endif

	/* this we need to get the MTU from
	 * the TUN device */
	ADD_SYSCALL(ioctl, 1, SCMP_A1(SCMP_CMP_EQ, (int)SIOCGIFMTU));

	// Add calls to support libev
	ADD_SYSCALL(epoll_wait, 0);
	ADD_SYSCALL(epoll_pwait, 0);
	ADD_SYSCALL(epoll_create1, 0);
	ADD_SYSCALL(epoll_ctl, 0);
	ADD_SYSCALL(rt_sigaction, 0);
	ADD_SYSCALL(eventfd2, 0);

	ret = seccomp_load(ctx);
	if (ret < 0) {
		oclog(ws, LOG_DEBUG, "could not load seccomp filter");
		ret = -1;
		goto fail;
	}

	ret = 0;

fail:
	seccomp_release(ctx);
	return ret;
}
#else
int disable_system_calls(struct worker_st *ws)
{
	return 0;
}
#endif
