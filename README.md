# Multi-Threading
Modified version of Multi-Threading program
This already brings a few new concepts to be aware of such as sharing data across multiple threads. 
We protect our data access with a std::mutex, and lock it with std::scoped_lock.
GetAndPopJob does exactly what it says, it will get a job if one exists and pop it from the queue. empty(), front() and pop() are protected inside this method with the use of the std::scoped_lock.
ExecuteJobsQ will run in the main thread and the worker thread. It gets a job, executes it, and continues until there is no more work to do.
C++ provides us a way of determining how many concurrent threads our system supports, so lets just use that: std::thread::hardware_concurrency().

