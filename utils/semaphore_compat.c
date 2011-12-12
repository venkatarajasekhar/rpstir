#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <stdbool.h>

#include "semaphore_compat.h"

#ifndef HAVE_SEM_TIMEDWAIT
int sem_timedwait(sem_t *sem, const struct timespec *abs_timeout)
{
	int retval;
	int sem_errno;
	struct timespec now;
	static const struct timespec sleep_interval = {0, 100000000L};

	while (true)
	{
		retval = sem_trywait(sem);
		if (retval == 0)
		{
			return 0;
		}

		sem_errno = errno;

		if (sem_errno != EAGAIN)
			break;

		if (abs_timeout->tv_nsec < 0 || abs_timeout->tv_nsec >= 1000000000L)
		{
			sem_errno = EINVAL;
			break;
		}

		if (clock_gettime(CLOCK_REALTIME, &now) != 0)
		{
			// unfortunately none of the valid errors for sem_timedwait actually match
			// this error, but EINVAL seems the closest
			sem_errno = EINVAL;
			break;
		}

		if (now.tv_sec > abs_timeout->tv_sec || (
			now.tv_sec == abs_timeout->tv_sec &&
			now.tv_nsec >= abs_timeout->tv_nsec))
		{
			sem_errno = ETIMEDOUT;
			break;
		}

		// Ignore the return value because if nanosleep() doesn't work
		// it's best to just spin.
		nanosleep(&sleep_interval, NULL);
	}

	errno = sem_errno;
	return retval;
}
#endif
