/* Minimal main program -- everything is loaded from the library */

#include "Python.h"

#ifdef MS_WINDOWS
#ifdef __MINGW32__
int
main(int argc, char **argv)
{
    return _Py_UnixMain(argc, argv);
}
#else
int
wmain(int argc, wchar_t **argv)
{
    return Py_Main(argc, argv);
}
#endif
#else
int
main(int argc, char **argv)
{
    return _Py_UnixMain(argc, argv);
}
#endif
