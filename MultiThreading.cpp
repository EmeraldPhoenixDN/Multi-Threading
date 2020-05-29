#include <vector>
#include <iostream>
#include <chrono>
#include <queue>
#include <thread> // thread support
#include <mutex>  // mutex support
#include <atomic> // atomic variables
#include <future> // later on for std::async
struct Settings
{
	enum class Priority : int {
		Low = 0,
		Medium,
		High
	};

#if _DEBUG
	static const int JobCountLow = 120;
	static const int JobCountMedium = 60;
	static const int JobCountHigh = 25;
#else
	static const int JobCountLow = 1200;
	static const int JobCountMedium = 600;
	static const int JobCountHigh = 250;
#endif

#if _DEBUG
	static const int ThreadPause = 100;
#else
	static const int ThreadPause = 0;
#endif

	static const int IterationCountLow = 5000;
	static const int IterationCountMedium = 10000;
	static const int IterationCountHigh = 20000;

	static const int PrecisionHigh = 100;
	static const int PrecisionMedium = 100;
	static const int PrecisionLow = 100;
};

double CalcPi(int n)
{
	double sum = 0.0;
	int sign = 1;
	for (int i = 0; i < n; ++i)
	{
		sum += sign / (2.0 * i + 1.0);
		sign *= -1;
	}
	return 4.0 * sum;
}

class CalcPiJob
{
public:
	CalcPiJob(int iterations)
		: m_iterations(iterations)
	{ }

	void DoWork()
	{
		float p = 0.0f;
		for (int i = 0; i < m_iterations; ++i) {
			p += CalcPi(m_iterations);
		}

		p /= m_iterations;
		std::this_thread::sleep_for(std::chrono::milliseconds(Settings::ThreadPause));
	}

private:
	int m_iterations;
};

std::queue<CalcPiJob*> GetJobsOfType(int count, int iterations)
{
	std::queue<CalcPiJob*> jobQ;
	for (int i = 0; i < count; ++i)
	{
		jobQ.emplace(new CalcPiJob(iterations));
	}
	return jobQ;
}

std::queue<CalcPiJob*> GetJobsQ()
{
	std::queue<CalcPiJob*> jobQ;
	for (int i = 0; i < Settings::JobCountHigh; ++i)
	{
		jobQ.emplace(new CalcPiJob(Settings::IterationCountHigh));
	}

	for (int i = 0; i < Settings::JobCountMedium; ++i)
	{
		jobQ.emplace(new CalcPiJob(Settings::IterationCountMedium));
	}

	for (int i = 0; i < Settings::JobCountLow; ++i)
	{
		jobQ.emplace(new CalcPiJob(Settings::IterationCountLow));
	}
	return jobQ;
}

std::vector<CalcPiJob*> GetJobVector()
{
	std::vector<CalcPiJob*> jobs;
	for (int i = 0; i < Settings::JobCountHigh; ++i)
	{
		jobs.push_back(new CalcPiJob(Settings::IterationCountHigh));
	}

	for (int i = 0; i < Settings::JobCountMedium; ++i)
	{
		jobs.push_back(new CalcPiJob(Settings::IterationCountMedium));
	}

	for (int i = 0; i < Settings::JobCountLow; ++i)
	{
		jobs.push_back(new CalcPiJob(Settings::IterationCountLow));
	}
	return jobs;
}

static std::mutex g_mutexOutput;

void RunSequential()
{
	std::queue<CalcPiJob*> jobQ = GetJobsQ();
	while (!jobQ.empty())
	{
		CalcPiJob* job = jobQ.front();
		jobQ.pop();

		job->DoWork();
		delete job;
	}
}

static std::mutex g_mutexJobQ;

CalcPiJob* GetAndPopJob(std::queue<CalcPiJob*>& jobQ, std::mutex& mutex)
{
	std::scoped_lock<std::mutex> lock(mutex);
	if (!jobQ.empty())
	{
		CalcPiJob* job = jobQ.front();
		jobQ.pop();

		return job;
	}
	return nullptr;
}

void ExecuteJobsQ(std::atomic<bool>& hasWork, 
	std::queue<CalcPiJob*>& jobQ, 
	std::mutex& mutex)
{
	while (hasWork)
	{
		CalcPiJob* currentJob = GetAndPopJob(jobQ, mutex);
		if (currentJob)
		{
			currentJob->DoWork();
			delete currentJob;
		}
		else
		{
			hasWork = false;
		}
	}
}

void RunOneThread()
{
	std::queue<CalcPiJob*> jobQ = GetJobsQ();

	std::atomic<bool> jobsPending = true;

	// Starting new thread
	std::thread t([&]() {
		ExecuteJobsQ(jobsPending, jobQ, g_mutexJobQ);
	});

	// main thread, also does the same.
	ExecuteJobsQ(jobsPending, jobQ, g_mutexJobQ);

	t.join();
}

void RunThreaded()
{
	int nThreads = std::thread::hardware_concurrency() - 1;
	std::vector<std::thread> threads;

	std::queue<CalcPiJob*> jobQ = GetJobsQ();

	std::atomic<bool> hasJobsLeft = true;
	for (int i = 0; i < nThreads; ++i)
	{
		std::thread t([&]() {
			ExecuteJobsQ(hasJobsLeft, jobQ, g_mutexJobQ);
		});
		threads.push_back(std::move(t));
	}

	// main thread
	ExecuteJobsQ(hasJobsLeft, jobQ, g_mutexJobQ);

	for (int i = 0; i < nThreads; ++i)
	{
		threads[i].join();
	}
}

std::mutex g_syncMutex;
std::condition_variable g_conditionVariable;

void RunSynchronizedThreads()
{
	int nThreads = std::thread::hardware_concurrency() - 1;
	std::vector<std::thread> threads;

	std::queue<CalcPiJob*> jobQ = GetJobsQ();

	std::atomic<bool> signal = false;
	std::atomic<bool> threadsActive = true;
	for (int i = 0; i < nThreads; ++i)
	{
		std::thread t([&]() {
			while (threadsActive)
			{
				// Tell main thread, worker is available for work
				{
					std::unique_lock<std::mutex> lk(g_syncMutex);
					g_conditionVariable.wait(lk, [&] { return signal == true; });
				}

				CalcPiJob* currentJob = GetAndPopJob(jobQ, g_mutexJobQ);

				if (currentJob)
				{
					currentJob->DoWork();
					delete currentJob;
				}
				else
				{
					threadsActive = false;
				}
			}
		});
		threads.push_back(std::move(t));
	}

	// main thread
	std::atomic<bool> mainThreadActive = true;
	while (mainThreadActive && threadsActive)
	{
		// send signal to worker threads, they can start work.
		{
			std::lock_guard<std::mutex> lk(g_syncMutex);
			signal = true;
		}
		g_conditionVariable.notify_all();

		// send signal to worker threads, so they have to wait for their next update.
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		{
			std::lock_guard<std::mutex> lk(g_syncMutex);
			signal = false;
		}
		g_conditionVariable.notify_all();

		// main thread work.
		CalcPiJob* currentJob = GetAndPopJob(jobQ, g_mutexJobQ);

		if (currentJob)
		{
			currentJob->DoWork();
			delete currentJob;
		}
		else
		{
			mainThreadActive = false;
		}
	}

	for (int i = 0; i < nThreads; ++i)
	{
		threads[i].join();
	}
}

static std::mutex g_mutexLowJobQ;
static std::mutex g_mutexMediumJobQ;
static std::mutex g_mutexHighJobQ;

void RunThreadedPriority()
{
	int nHighThreads = 3;
	int nMediumThreads = 2;
	int nLowThreads = 2;
	
	std::queue<CalcPiJob*> lowJobQ = GetJobsOfType(Settings::JobCountLow, Settings::IterationCountLow);
	std::queue<CalcPiJob*> mediumJobQ = GetJobsOfType(Settings::JobCountMedium, Settings::IterationCountMedium);
	std::queue<CalcPiJob*> highJobQ = GetJobsOfType(Settings::JobCountHigh, Settings::IterationCountHigh);

	std::vector<std::thread> threads;

	std::atomic<bool> hasHighJobsLeft = true;
	for (int i = 0; i < nHighThreads; ++i)
	{
		std::thread t([&]() {
			ExecuteJobsQ(hasHighJobsLeft, highJobQ, g_mutexHighJobQ);
		});
		threads.push_back(std::move(t));
	}

	std::atomic<bool> hasMediumJobsLeft = true;
	for (int i = 0; i < nMediumThreads; ++i)
	{
		std::thread t([&]() {
			ExecuteJobsQ(hasMediumJobsLeft, mediumJobQ, g_mutexMediumJobQ);
		});
		threads.push_back(std::move(t));
	}

	std::atomic<bool> hasLowJobsLeft = true;
	for (int i = 0; i < nLowThreads; ++i)
	{
		std::thread t([&]() {
			ExecuteJobsQ(hasLowJobsLeft, lowJobQ, g_mutexLowJobQ);
		});
		threads.push_back(std::move(t));
	}

	// main thread
	while (hasHighJobsLeft || hasMediumJobsLeft || hasLowJobsLeft)
	{
		if (hasHighJobsLeft) 
		{
			ExecuteJobsQ(hasHighJobsLeft, highJobQ, g_mutexHighJobQ);
		}
		else
		{
			// wait for other threads to complete.
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}

	const int threadCount = threads.size();
	for (int i = 0; i < threadCount; ++i)
	{
		threads[i].join();
	}
}

void RunThreadedPriorityWorkStealing()
{
	int nHighThreads = 5;
	int nMediumThreads = 1;
	int nLowThreads = 1;

	std::queue<CalcPiJob*> lowJobQ = GetJobsOfType(Settings::JobCountLow, Settings::IterationCountLow);
	std::queue<CalcPiJob*> mediumJobQ = GetJobsOfType(Settings::JobCountMedium, Settings::IterationCountMedium);
	std::queue<CalcPiJob*> highJobQ = GetJobsOfType(Settings::JobCountHigh, Settings::IterationCountHigh);

	std::vector<std::thread> threads;

	std::atomic<bool> isHighPriorityThreadsActive = true;
	for (int i = 0; i < nHighThreads; ++i)
	{
		std::thread t([&]() {
			
			while (isHighPriorityThreadsActive)
			{
				CalcPiJob* currentJob = GetAndPopJob(highJobQ, g_mutexHighJobQ);

				// if no more High Jobs, take on Medium ones.
				if (!currentJob)
				{
					currentJob = GetAndPopJob(mediumJobQ, g_mutexMediumJobQ);
				}

				// if no more Medium Jobs, take on Small ones.
				if (!currentJob)
				{
					currentJob = GetAndPopJob(lowJobQ, g_mutexLowJobQ);
				}

				if (currentJob)
				{
					currentJob->DoWork();
					delete currentJob;
				}
				else
				{
					isHighPriorityThreadsActive = false;
				}
			}
		});
		threads.push_back(std::move(t));
	}

	std::atomic<bool> isMediumThreadsActive = true;
	for (int i = 0; i < nMediumThreads; ++i)
	{
		std::thread t([&]() {
			while (isMediumThreadsActive)
			{
				CalcPiJob* currentJob = GetAndPopJob(mediumJobQ, g_mutexMediumJobQ);

				// if no more Medium Jobs, take on Small ones.
				if (!currentJob)
				{
					currentJob = GetAndPopJob(lowJobQ, g_mutexLowJobQ);
				}

				if (currentJob)
				{
					currentJob->DoWork();
					delete currentJob;
				}
				else
				{
					isMediumThreadsActive = false;
				}
			}
		});
		threads.push_back(std::move(t));
	}

	std::atomic<bool> isLowThreadsActive = true;
	for (int i = 0; i < nLowThreads; ++i)
	{
		std::thread t([&]() {
			while (isLowThreadsActive)
			{
				CalcPiJob* currentJob = GetAndPopJob(lowJobQ, g_mutexLowJobQ);

				if (currentJob)
				{
					currentJob->DoWork();
					delete currentJob;
				}
				else
				{
					isLowThreadsActive = false;
				}
			}
			});
		threads.push_back(std::move(t));
	}

	// main thread
	while (isLowThreadsActive || isMediumThreadsActive || isHighPriorityThreadsActive)
	{
		if (isHighPriorityThreadsActive)
		{
			CalcPiJob* currentJob = GetAndPopJob(highJobQ, g_mutexHighJobQ);

			// if no more High Jobs, take on Medium ones.
			if (!currentJob)
			{
				currentJob = GetAndPopJob(mediumJobQ, g_mutexMediumJobQ);
			}

			// if no more Medium Jobs, take on Small ones.
			if (!currentJob)
			{
				currentJob = GetAndPopJob(lowJobQ, g_mutexLowJobQ);
			}

			if (currentJob)
			{
				currentJob->DoWork();
				delete currentJob;
			}
			else
			{
				isHighPriorityThreadsActive = false;
			}
		}
		else
		{
			// wait for other threads to complete.
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}

	const int threadCount = threads.size();
	for (int i = 0; i < threadCount; ++i)
	{
		threads[i].join();
	}
}

void RunJobsOnAsync()
{
	std::vector<CalcPiJob*> jobs = GetJobVector();

	std::vector<std::future<void>> futures;
	for (int i = 0; i < jobs.size(); ++i)
	{
		auto j = std::async([&jobs, i]() {
			jobs[i]->DoWork();
			});
		futures.push_back(std::move(j));
	}

	// Wait for Jobs to finish, .get() is a blocking operation.
	for (int i = 0; i < futures.size(); ++i)
	{
		futures[i].get();
	}

	for (int i = 0; i < jobs.size(); ++i)
	{
		delete jobs[i];
	}
}

#define START_TIMER(methodName) {								\
	std::cout << methodName;									\
	auto fulltime = std::chrono::high_resolution_clock::now()	\

#define END_TIMER() \
	auto timeSpan = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - fulltime); \
	float frameTimeMs = static_cast<float>(timeSpan.count()); \
	std::cout << " - time " << frameTimeMs << " ms \n";  \
	} \

void DisplayHelp()
{
	std::cout << "USAGE: threads.exe [command] [method] \n";
	std::cout << "OPTIONS: \n";
	std::cout << "-c specify [command]\n";
	std::cout << "   all\n";
	std::cout << "   seq\n";
	std::cout << "   onethread\n";
	std::cout << "   threaded\n";
	std::cout << "   threaded_p\n";
	std::cout << "   threaded_ws\n";
	std::cout << "   threaded_sync\n";
	std::cout << "   async\n";
	std::cout << "-h show help\n";
}

int main(const int argc, const char* argv[])
{
	std::string method = "all";
	std::string command = "";
	bool runAll = false;
	bool error = false;
	if (argc == 1)
	{
		runAll = true;
	}
	else if (argc == 3)
	{
		command = argv[1];
		error = command != "-c";

		method = argv[2];
	}
	else
	{
		error = true;
	}

	if (error || command == "-h")
	{
		DisplayHelp();
		return 0;
	}

	runAll = method == "all";

	std::cout << "Available concurrent threads: " << std::thread::hardware_concurrency() << "\n";
	std::cout << "Big Jobs: \t" << Settings::JobCountHigh << "\n";
	std::cout << "Medium Jobs: \t" << Settings::JobCountMedium << "\n";
	std::cout << "Small Jobs: \t" << Settings::JobCountLow << "\n";

	if (runAll || method == "seq")
	{
		START_TIMER("Sequential \t\t");
		RunSequential();
		END_TIMER();
	}

	if (runAll || method == "onethread")
	{
		START_TIMER("OneThread \t\t\t");
		RunOneThread();
		END_TIMER();
	}

	if (runAll || method == "threaded")
	{
		START_TIMER("Threaded \t\t\t");
		RunThreaded();
		END_TIMER();
	}

	if (runAll || method == "threaded_p")
	{
		START_TIMER("Priority Threaded \t\t");
		RunThreadedPriority();
		END_TIMER();
	}

	if (runAll || method == "threaded_ws")
	{
		START_TIMER("Priority Threaded Work Stealing ");
		RunThreadedPriorityWorkStealing();
		END_TIMER();
	}

	if (runAll || method == "threaded_sync")
	{
		START_TIMER("Synchronize Thread \t\t");
		RunSynchronizedThreads();
		END_TIMER();
	}

	if (runAll || method == "async")
	{
		START_TIMER("Async Jobs \t\t\t");
		RunJobsOnAsync();
		END_TIMER();
	}

	getchar();
	return 0;
}