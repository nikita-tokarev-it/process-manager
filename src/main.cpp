#include <iostream>
#include <fstream>
#include <thread>
#include <atomic>
#include <chrono>
#include <ctime>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/file.h>
#endif

// ===== Shared counter =====

struct SharedData {
    int counter;
};

SharedData* shared = nullptr;

#ifdef _WIN32
HANDLE hMap;
HANDLE hMutex;
#else
int shm_fd;
#endif

// ===== Time string =====
std::string now()
{
    auto tp = std::chrono::system_clock::now();
    auto s = std::chrono::system_clock::to_time_t(tp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        tp.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << std::ctime(&s);
    ss.seekp(-1, std::ios_base::end);
    ss << "." << ms.count();
    return ss.str();
}

// ===== PID =====
int pid()
{
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return getpid();
#endif
}

// ===== Logging =====
std::ofstream logFile;

void log(const std::string& msg)
{
    logFile << msg << std::endl;
    logFile.flush();
}

// ===== Shared memory init =====
void init_shared()
{
#ifdef _WIN32
    hMap = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL,
        PAGE_READWRITE, 0, sizeof(SharedData), "GlobalCounter");

    shared = (SharedData*)MapViewOfFile(hMap,
        FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedData));

    hMutex = CreateMutexA(NULL, FALSE, "GlobalCounterMutex");

#else
    shm_fd = shm_open("/counter_shm", O_CREAT | O_RDWR, 0666);
    ftruncate(shm_fd, sizeof(SharedData));

    shared = (SharedData*)mmap(0, sizeof(SharedData),
        PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
#endif
}

// ===== Mutex lock =====
void lock()
{
#ifdef _WIN32
    WaitForSingleObject(hMutex, INFINITE);
#else
    flock(shm_fd, LOCK_EX);
#endif
}

void unlock()
{
#ifdef _WIN32
    ReleaseMutex(hMutex);
#else
    flock(shm_fd, LOCK_UN);
#endif
}

// ===== Counter thread =====
void counter_thread()
{
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        lock();
        shared->counter++;
        unlock();
    }
}

// ===== CLI thread =====
void cli_thread()
{
    while (true)
    {
        int val;
        std::cin >> val;

        lock();
        shared->counter = val;
        unlock();
    }
}

// ===== Child copies =====
void run_copy1()
{
    log("COPY1 start pid=" + std::to_string(pid()));

    lock();
    shared->counter += 10;
    unlock();

    log("COPY1 exit pid=" + std::to_string(pid()));
}

void run_copy2()
{
    log("COPY2 start pid=" + std::to_string(pid()));

    lock();
    shared->counter *= 2;
    unlock();

    std::this_thread::sleep_for(std::chrono::seconds(2));

    lock();
    shared->counter /= 2;
    unlock();

    log("COPY2 exit pid=" + std::to_string(pid()));
}

// ===== Spawn process =====
void spawn_copy(const std::string& arg)
{
#ifdef _WIN32
    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);

    std::string cmd = "program.exe " + arg;

    CreateProcessA(NULL, cmd.data(), NULL, NULL, FALSE,
        0, NULL, NULL, &si, &pi);

#else
    pid_t p = fork();
    if (p == 0)
    {
        execl("./program", "./program", arg.c_str(), NULL);
        exit(0);
    }
#endif
}

// ===== Leader election =====
bool is_leader()
{
#ifdef _WIN32
    HANDLE m = CreateMutexA(NULL, TRUE, "LeaderMutex");
    return GetLastError() != ERROR_ALREADY_EXISTS;
#else
    int fd = open("/tmp/leader.lock", O_CREAT, 0666);
    return flock(fd, LOCK_EX | LOCK_NB) == 0;
#endif
}

// ===== Main =====
int main(int argc, char* argv[])
{
    logFile.open("log.txt", std::ios::app);

    init_shared();

    if (argc > 1)
    {
        std::string arg = argv[1];
        if (arg == "copy1") run_copy1();
        if (arg == "copy2") run_copy2();
        return 0;
    }

    log("START pid=" + std::to_string(pid()) + " time=" + now());

    std::thread t1(counter_thread);
    std::thread t2(cli_thread);

    bool leader = is_leader();

    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        lock();
        int val = shared->counter;
        unlock();

        if (leader)
        {
            log("LOG pid=" + std::to_string(pid()) +
                " time=" + now() +
                " counter=" + std::to_string(val));
        }

        static int sec = 0;
        sec++;

        if (leader && sec % 3 == 0)
        {
            spawn_copy("copy1");
            spawn_copy("copy2");
        }
    }

    t1.join();
    t2.join();
}
