/***********************************************************
Copyright 1991, 1992, 1993, 1994 by Stichting Mathematisch Centrum,
Amsterdam, The Netherlands.

                        All Rights Reserved

Permission to use, copy, modify, and distribute this software and its 
documentation for any purpose and without fee is hereby granted, 
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in 
supporting documentation, and that the names of Stichting Mathematisch
Centrum or CWI not be used in advertising or publicity pertaining to
distribution of the software without specific, written prior permission.

STICHTING MATHEMATISCH CENTRUM DISCLAIMS ALL WARRANTIES WITH REGARD TO
THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS, IN NO EVENT SHALL STICHTING MATHEMATISCH CENTRUM BE LIABLE
FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

******************************************************************/

/* Signal module -- many thanks to Lance Ellinghaus */

#include "Python.h"
#include "intrcheck.h"

#include <signal.h>
#include <errno.h>

#ifndef SIG_ERR
#define SIG_ERR ((RETSIGTYPE (*)())-1)
#endif

#ifndef NSIG
#define NSIG (_SIGMAX + 1)	/* For QNX */
#endif


/*
   NOTES ON THE INTERACTION BETWEEN SIGNALS AND THREADS

   When threads are supported, we want the following semantics:

   - only the main thread can set a signal handler
   - any thread can get a signal handler
   - signals are only delivered to the main thread

   I.e. we don't support "synchronous signals" like SIGFPE (catching
   this doesn't make much sense in Python anyway) nor do we support
   signals as a means of inter-thread communication, since not all
   thread implementations support that (at least our thread library
   doesn't).

   We still have the problem that in some implementations signals
   generated by the keyboard (e.g. SIGINT) are delivered to all
   threads (e.g. SGI), while in others (e.g. Solaris) such signals are
   delivered to one random thread (an intermediate possibility would
   be to deliver it to the main thread -- POSIX???).  For now, we have
   a working implementation that works in all three cases -- the
   handler ignores signals if getpid() isn't the same as in the main
   thread.  XXX This is a hack.

*/

#ifdef WITH_THREAD
#include "thread.h"
static long main_thread;
static pid_t main_pid;
#endif

struct PySignal_SignalArrayStruct {
	int	tripped;
	PyObject *func;
};

static struct PySignal_SignalArrayStruct PySignal_SignalHandlerArray[NSIG];
static int PySignal_IsTripped = 0; /* Speed up sigcheck() when none tripped */

static PyObject *PySignal_SignalDefaultHandler;
static PyObject *PySignal_SignalIgnoreHandler;
static PyObject *PySignal_DefaultIntHandler;

static PyObject *
PySignal_CDefaultIntHandler(self, arg)
	PyObject *self;
	PyObject *arg;
{
	PyErr_SetNone(PyExc_KeyboardInterrupt);
	return (PyObject *)NULL;
}

static RETSIGTYPE
PySignal_Handler(sig_num)
	int sig_num;
{
#ifdef WITH_THREAD
	/* See NOTES section above */
	if (getpid() == main_pid) {
#endif
		PySignal_IsTripped++;
		PySignal_SignalHandlerArray[sig_num].tripped = 1;
#ifdef WITH_THREAD
	}
#endif
#ifdef SIGCHLD
	if (sig_num == SIGCHLD) {
		/* To avoid infinite recursion, this signal remains
		   reset until explicit re-instated.
		   Don't clear the 'func' field as it is our pointer
		   to the Python handler... */
		return;
	}
#endif
	(void *)signal(sig_num, &PySignal_Handler);
}
 

static PyObject *
PySignal_Alarm(self, args)
	PyObject *self; /* Not used */
	PyObject *args;
{
	int t;
	if (!PyArg_Parse(args, "i", &t))
		return (PyObject *)NULL;
	/* alarm() returns the number of seconds remaining */
	return PyInt_FromLong(alarm(t));
}

static object *
PySignal_Pause(self, args)
	PyObject *self; /* Not used */
	PyObject *args;
{
	if (!PyArg_NoArgs(args))
		return NULL;
	BGN_SAVE
	pause();
	END_SAVE
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject *
PySignal_Signal(self, args)
	PyObject *self; /* Not used */
	PyObject *args;
{
	PyObject *obj;
	int sig_num;
	PyObject *old_handler;
	RETSIGTYPE (*func)();
	if (!PyArg_Parse(args, "(iO)", &sig_num, &obj))
		return (PyObject *)NULL;
#ifdef WITH_THREAD
	if (get_thread_ident() != main_thread) {
		PyErr_SetString(PyExc_ValueError, "signal only works in main thread");
		return (PyObject *)NULL;
	}
#endif
	if (sig_num < 1 || sig_num >= NSIG) {
		PyErr_SetString(PyExc_ValueError, "signal number out of range");
		return (PyObject *)NULL;
	}
	if (obj == PySignal_SignalIgnoreHandler)
		func = SIG_IGN;
	else if (obj == PySignal_SignalDefaultHandler)
		func = SIG_DFL;
	else if (!PyCFunction_Check(obj) &&
		 !PyFunction_Check(obj) &&
		 !PyMethod_Check(obj)) {
		PyErr_SetString(PyExc_TypeError,
"signal handler must be signal.SIG_IGN, signal.SIG_DFL, or a callable object");
		return (PyObject *)NULL;
	}
	else
		func = PySignal_Handler;
	if (signal(sig_num, func) == SIG_ERR) {
		PyErr_SetFromErrno(PyExc_RuntimeError);
		return (PyObject *)NULL;
	}
	old_handler = PySignal_SignalHandlerArray[sig_num].func;
	PySignal_SignalHandlerArray[sig_num].tripped = 0;
	Py_INCREF(obj);
	PySignal_SignalHandlerArray[sig_num].func = obj;
	return old_handler;
}

static PyObject *
PySignal_GetSignal(self, args)
	PyObject *self; /* Not used */
	PyObject *args;
{
	int sig_num;
	PyObject *old_handler;
	if (!PyArg_Parse(args, "i", &sig_num))
		return (PyObject *)NULL;
	if (sig_num < 1 || sig_num >= NSIG) {
		PyErr_SetString(PyExc_ValueError, "signal number out of range");
		return (PyObject *)NULL;
	}
	old_handler = PySignal_SignalHandlerArray[sig_num].func;
	Py_INCREF(old_handler);
	return old_handler;
}


/* List of functions defined in the module */

static PyMethodDef PySignal_methods[] = {
	{"alarm",	PySignal_Alarm},
	{"signal",	PySignal_Signal},
	{"getsignal",	PySignal_GetSignal},
	{"pause",	PySignal_Pause},
	{NULL,		NULL}		/* sentinel */
};

void
initsignal()
{
	PyObject *m, *d, *x;
	int i;

#ifdef WITH_THREAD
	main_thread = get_thread_ident();
	main_pid = getpid();
#endif

	/* Create the module and add the functions */
	m = Py_InitModule("signal", PySignal_methods);

	/* Add some symbolic constants to the module */
	d = PyModule_GetDict(m);

	PySignal_SignalDefaultHandler = PyInt_FromLong((long)SIG_DFL);
	PyDict_SetItemString(d, "SIG_DFL", PySignal_SignalDefaultHandler);
	PySignal_SignalIgnoreHandler = PyInt_FromLong((long)SIG_IGN);
	PyDict_SetItemString(d, "SIG_IGN", PySignal_SignalIgnoreHandler);
	PyDict_SetItemString(d, "NSIG", PyInt_FromLong((long)NSIG));
	PySignal_DefaultIntHandler = PyCFunction_New("default_int_handler",
						     PySignal_CDefaultIntHandler,
						     (PyObject *)NULL,
						     0);
	PyDict_SetItemString(d, "default_int_handler", PySignal_DefaultIntHandler);

	PySignal_SignalHandlerArray[0].tripped = 0;
	for (i = 1; i < NSIG; i++) {
		RETSIGTYPE (*t)();
		t = signal(i, SIG_IGN);
		signal(i, t);
		PySignal_SignalHandlerArray[i].tripped = 0;
		if (t == SIG_DFL)
			PySignal_SignalHandlerArray[i].func = PySignal_SignalDefaultHandler;
		else if (t == SIG_IGN)
			PySignal_SignalHandlerArray[i].func = PySignal_SignalIgnoreHandler;
		else
			PySignal_SignalHandlerArray[i].func = Py_None; /* None of our business */
		Py_INCREF(PySignal_SignalHandlerArray[i].func);
	}
	if (PySignal_SignalHandlerArray[SIGINT].func == PySignal_SignalDefaultHandler) {
		/* Install default int handler */
		Py_DECREF(PySignal_SignalHandlerArray[SIGINT].func);
		PySignal_SignalHandlerArray[SIGINT].func = PySignal_DefaultIntHandler;
		Py_INCREF(PySignal_DefaultIntHandler);
		signal(SIGINT, &PySignal_Handler);
	}

#ifdef SIGHUP
	x = PyInt_FromLong(SIGHUP);
	PyDict_SetItemString(d, "SIGHUP", x);
#endif
#ifdef SIGINT
	x = PyInt_FromLong(SIGINT);
	PyDict_SetItemString(d, "SIGINT", x);
#endif
#ifdef SIGQUIT
	x = PyInt_FromLong(SIGQUIT);
	PyDict_SetItemString(d, "SIGQUIT", x);
#endif
#ifdef SIGILL
	x = PyInt_FromLong(SIGILL);
	PyDict_SetItemString(d, "SIGILL", x);
#endif
#ifdef SIGTRAP
	x = PyInt_FromLong(SIGTRAP);
	PyDict_SetItemString(d, "SIGTRAP", x);
#endif
#ifdef SIGIOT
	x = PyInt_FromLong(SIGIOT);
	PyDict_SetItemString(d, "SIGIOT", x);
#endif
#ifdef SIGABRT
	x = PyInt_FromLong(SIGABRT);
	PyDict_SetItemString(d, "SIGABRT", x);
#endif
#ifdef SIGEMT
	x = PyInt_FromLong(SIGEMT);
	PyDict_SetItemString(d, "SIGEMT", x);
#endif
#ifdef SIGFPE
	x = PyInt_FromLong(SIGFPE);
	PyDict_SetItemString(d, "SIGFPE", x);
#endif
#ifdef SIGKILL
	x = PyInt_FromLong(SIGKILL);
	PyDict_SetItemString(d, "SIGKILL", x);
#endif
#ifdef SIGBUS
	x = PyInt_FromLong(SIGBUS);
	PyDict_SetItemString(d, "SIGBUS", x);
#endif
#ifdef SIGSEGV
	x = PyInt_FromLong(SIGSEGV);
	PyDict_SetItemString(d, "SIGSEGV", x);
#endif
#ifdef SIGSYS
	x = PyInt_FromLong(SIGSYS);
	PyDict_SetItemString(d, "SIGSYS", x);
#endif
#ifdef SIGPIPE
	x = PyInt_FromLong(SIGPIPE);
	PyDict_SetItemString(d, "SIGPIPE", x);
#endif
#ifdef SIGALRM
	x = PyInt_FromLong(SIGALRM);
	PyDict_SetItemString(d, "SIGALRM", x);
#endif
#ifdef SIGTERM
	x = PyInt_FromLong(SIGTERM);
	PyDict_SetItemString(d, "SIGTERM", x);
#endif
#ifdef SIGUSR1
	x = PyInt_FromLong(SIGUSR1);
	PyDict_SetItemString(d, "SIGUSR1", x);
#endif
#ifdef SIGUSR2
	x = PyInt_FromLong(SIGUSR2);
	PyDict_SetItemString(d, "SIGUSR2", x);
#endif
#ifdef SIGCLD
	x = PyInt_FromLong(SIGCLD);
	PyDict_SetItemString(d, "SIGCLD", x);
#endif
#ifdef SIGCHLD
	x = PyInt_FromLong(SIGCHLD);
	PyDict_SetItemString(d, "SIGCHLD", x);
#endif
#ifdef SIGPWR
	x = PyInt_FromLong(SIGPWR);
	PyDict_SetItemString(d, "SIGPWR", x);
#endif
#ifdef SIGIO
	x = PyInt_FromLong(SIGIO);
	PyDict_SetItemString(d, "SIGIO", x);
#endif
#ifdef SIGURG
	x = PyInt_FromLong(SIGURG);
	PyDict_SetItemString(d, "SIGURG", x);
#endif
#ifdef SIGWINCH
	x = PyInt_FromLong(SIGWINCH);
	PyDict_SetItemString(d, "SIGWINCH", x);
#endif
#ifdef SIGPOLL
	x = PyInt_FromLong(SIGPOLL);
	PyDict_SetItemString(d, "SIGPOLL", x);
#endif
#ifdef SIGSTOP
	x = PyInt_FromLong(SIGSTOP);
	PyDict_SetItemString(d, "SIGSTOP", x);
#endif
#ifdef SIGTSTP
	x = PyInt_FromLong(SIGTSTP);
	PyDict_SetItemString(d, "SIGTSTP", x);
#endif
#ifdef SIGCONT
	x = PyInt_FromLong(SIGCONT);
	PyDict_SetItemString(d, "SIGCONT", x);
#endif
#ifdef SIGTTIN
	x = PyInt_FromLong(SIGTTIN);
	PyDict_SetItemString(d, "SIGTTIN", x);
#endif
#ifdef SIGTTOU
	x = PyInt_FromLong(SIGTTOU);
	PyDict_SetItemString(d, "SIGTTOU", x);
#endif
#ifdef SIGVTALRM
	x = PyInt_FromLong(SIGVTALRM);
	PyDict_SetItemString(d, "SIGVTALRM", x);
#endif
#ifdef SIGPROF
	x = PyInt_FromLong(SIGPROF);
	PyDict_SetItemString(d, "SIGPROF", x);
#endif
#ifdef SIGCPU
	x = PyInt_FromLong(SIGCPU);
	PyDict_SetItemString(d, "SIGCPU", x);
#endif
#ifdef SIGFSZ
	x = PyInt_FromLong(SIGFSZ);
	PyDict_SetItemString(d, "SIGFSZ", x);
#endif
	/* Check for errors */
	if (PyErr_Occurred())
		Py_FatalError("can't initialize module signal");
}

int
sigcheck()
{
	int i;
	PyObject *f;
	if (!PySignal_IsTripped)
		return 0;
#ifdef WITH_THREAD
	if (get_thread_ident() != main_thread)
		return 0;
#endif
	f = getframe();
	if (f == (PyObject *)NULL)
		f = Py_None;
	for (i = 1; i < NSIG; i++) {
		if (PySignal_SignalHandlerArray[i].tripped) {
			PyObject *arglist, *result;
			PySignal_SignalHandlerArray[i].tripped = 0;
			arglist = Py_BuildValue("(iO)", i, f);
			if (arglist == (PyObject *)NULL)
				result = (PyObject *)NULL;
			else {
				result = PyEval_CallObject(PySignal_SignalHandlerArray[i].func,arglist);
				Py_DECREF(arglist);
			}
			if (result == (PyObject *)NULL) {
				return 1;
			} else {
				Py_DECREF(result);
			}
		}
	}
	PySignal_IsTripped = 0;
	return 0;
}

/* Replacement for intrcheck.c functionality */

void
initintr()
{
	initsignal();
}

int
intrcheck()
{
	if (PySignal_SignalHandlerArray[SIGINT].tripped) {
#ifdef WITH_THREAD
		if (get_thread_ident() != main_thread)
			return 0;
#endif
		PySignal_SignalHandlerArray[SIGINT].tripped = 0;
		return 1;
	}
	return 0;
}
