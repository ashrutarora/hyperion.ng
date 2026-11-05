// STL includes
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include <QCoreApplication>

#include <utils/DefaultSignalHandler.h>

extern char** environ;

// A crashing process must still write its stack trace when stdout is a file or a pipe,
// as it is when Hyperion runs as a service. stdout is then fully buffered, so the trace
// is lost unless it is flushed before the signal is re-raised.
//
// The test starts itself again with --crash and stdout redirected to a file.
// (A plain fork() is not enough: on macOS, Qt must not be used in a forked child without exec.)
int main(int argc, char* argv[])
{
	if (argc > 1 && strcmp(argv[1], "--crash") == 0)
	{
		QCoreApplication app(argc, argv);
		DefaultSignalHandler::install();
		abort();
	}

	char path[] = "/tmp/hyperion-test-signal-trace-XXXXXX";
	const int fd = mkstemp(path);
	if (fd < 0)
	{
		std::cout << "NOK could not create a temporary file" << std::endl;
		return EXIT_FAILURE;
	}

	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_init(&actions);
	posix_spawn_file_actions_adddup2(&actions, fd, STDOUT_FILENO);

	char crashArg[] = "--crash";
	char* childArgv[] = { argv[0], crashArg, nullptr };
	pid_t pid = 0;
	const int spawnResult = posix_spawn(&pid, argv[0], &actions, nullptr, childArgv, environ);
	posix_spawn_file_actions_destroy(&actions);
	close(fd);

	if (spawnResult != 0)
	{
		std::cout << "NOK could not start the crashing process: " << strerror(spawnResult) << std::endl;
		unlink(path);
		return EXIT_FAILURE;
	}

	int status = 0;
	waitpid(pid, &status, 0);

	std::ifstream file(path);
	std::stringstream content;
	content << file.rdbuf();
	unlink(path);

	int failures = 0;

	const bool killedByAbort = WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
	std::cout << (killedByAbort ? "OK  " : "NOK ") << "process is terminated by SIGABRT" << std::endl;
	if (!killedByAbort)
	{
		++failures;
		if (WIFSIGNALED(status))
		{
			std::cout << "    terminated by signal " << WTERMSIG(status) << std::endl;
		}
		else
		{
			std::cout << "    exited with status " << WEXITSTATUS(status) << std::endl;
		}
	}

	const bool hasTrace = content.str().find("SIGNAL-HANDLER") != std::string::npos;
	std::cout << (hasTrace ? "OK  " : "NOK ") << "stack trace is written to a redirected stdout" << std::endl;
	if (!hasTrace)
	{
		++failures;
		std::cout << "    stdout of the crashed process:" << std::endl << "[" << content.str() << "]" << std::endl;
	}

	std::cout << (failures == 0 ? "All tests passed" : "Some tests failed") << std::endl;
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
