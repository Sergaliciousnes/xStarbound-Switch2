#pragma once

#include "base.hpp"
#if SOUP_POSIX
#include <signal.h>

NAMESPACE_SOUP
{
	struct signal
	{
		static void handle(int signum, void(*fp)(int signum))
		{
#ifdef __SWITCH2__
            ::signal(signum, fp);
#else
			struct sigaction s;
			s.sa_handler = fp;
			sigemptyset(&s.sa_mask);
			s.sa_flags = 0;
			sigaction(signum, &s, nullptr);
#endif
		}
	};
}
#endif
