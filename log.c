#include <stdio.h>
#include <stdarg.h>
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static int NVDBG = 0;

void enable_debug(void)
{
	NVDBG = 1;
}

void print_debug(const char* fmt, ...)
{
	if (NVDBG) {
		va_list args;
		va_start(args, fmt);
		vfprintf(stdout, fmt, args);
		va_end(args);
	}

}
