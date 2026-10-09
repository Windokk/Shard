#include "engine/platform/filesystem/async_io.hpp"
#include "engine/platform/filesystem/async_io_backend.hpp"

#include "engine/platform/thread/thread.hpp"

#include <condition_variable>
#include <deque>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>

namespace Shard::Engine::Core::Platform::AsyncIO {

    namespace {
        // Generic backend : two threads doing blocking reads and writes
        class ThreadPoolBackend : public Backend {
        public:
            ThreadPoolBackend()
            {
                // IO threads mostly wait on the disk : two keep it busy without competing with the workers for the cores
                for (int i = 0; i < 2; ++i)
                {
                    ThreadDesc desc;
                    desc.name = "Shard IO " + std::to_string(i);
                    m_Threads.emplace_back(desc, [this] { Run(); });
                }
            }

            ~ThreadPoolBackend() override
            {
                {
                    std::lock_guard<std::mutex> lock(m_Mutex);
                    m_Stopping = true;
                }
                m_Wake.notify_all();
                m_Threads.clear();  // joins : the requests already queued are completed first
            }

            const char* Name() const override { return "thread pool"; }

            void Read(std::string path, std::promise<IOResult> promise) override
            {
                Submit([path = std::move(path), promise = std::make_shared<std::promise<IOResult>>(std::move(promise))]
                {
                    IOResult result;
                    std::ifstream file(path, std::ios::binary | std::ios::ate);
                    if (!file)
                    {
                        result.error = "can't open " + path;
                        promise->set_value(std::move(result));
                        return;
                    }
                    const std::streamsize size = file.tellg();
                    file.seekg(0);
                    result.data.resize(static_cast<size_t>(size));
                    if (size > 0 && !file.read(reinterpret_cast<char*>(result.data.data()), size))
                    {
                        result.data.clear();
                        result.error = "can't read " + path;
                    }
                    else
                        result.ok = true;
                    promise->set_value(std::move(result));
                });
            }

            void Write(std::string path, std::vector<uint8_t> data, std::promise<IOResult> promise) override
            {
                Submit([path = std::move(path), data = std::move(data), promise = std::make_shared<std::promise<IOResult>>(std::move(promise))]
                {
                    IOResult result;
                    std::ofstream file(path, std::ios::binary | std::ios::trunc);
                    if (!file)
                        result.error = "can't open " + path + " for writing";
                    else
                    {
                        file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
                        // Flush and close BEFORE the promise is set : the caller may read the file back right away
                        file.close();
                        result.ok = static_cast<bool>(file);
                        if (!result.ok)
                            result.error = "can't write " + path;
                    }
                    promise->set_value(std::move(result));
                });
            }

            void Drain() override
            {
                std::unique_lock<std::mutex> lock(m_Mutex);
                m_Idle.wait(lock, [this] { return m_Jobs.empty() && m_Running == 0; });
            }

        private:
            void Submit(std::function<void()> job)
            {
                {
                    std::lock_guard<std::mutex> lock(m_Mutex);
                    m_Jobs.push_back(std::move(job));
                }
                m_Wake.notify_one();
            }

            void Run()
            {
                for (;;)
                {
                    std::function<void()> job;
                    {
                        std::unique_lock<std::mutex> lock(m_Mutex);
                        m_Wake.wait(lock, [this] { return m_Stopping || !m_Jobs.empty(); });
                        if (m_Jobs.empty())
                            return;
                        job = std::move(m_Jobs.front());
                        m_Jobs.pop_front();
                        ++m_Running;
                    }
                    job();
                    {
                        std::lock_guard<std::mutex> lock(m_Mutex);
                        --m_Running;
                    }
                    m_Idle.notify_all();
                }
            }

            std::mutex m_Mutex;
            std::condition_variable m_Wake;
            std::condition_variable m_Idle;
            std::deque<std::function<void()>> m_Jobs;
            int m_Running = 0;
            bool m_Stopping = false;
            std::vector<Thread> m_Threads;
        };

        std::mutex g_BackendMutex;
        std::unique_ptr<Backend> g_Backend;
        bool g_ForceThreadPool = false;

        Backend& Get()
        {
            std::lock_guard<std::mutex> lock(g_BackendMutex);
            if (!g_Backend)
            {
                if (!g_ForceThreadPool)
                    g_Backend = CreateNativeBackend();
                if (!g_Backend)
                    g_Backend = std::make_unique<ThreadPoolBackend>();
            }
            return *g_Backend;
        }
    }

#if !defined(_WIN32) && !defined(SHARD_HAS_IO_URING)
    std::unique_ptr<Backend> CreateNativeBackend()
    {
        return nullptr;
    }
#endif

    std::future<IOResult> ReadFile(const std::string& path)
    {
        std::promise<IOResult> promise;
        std::future<IOResult> future = promise.get_future();
        Get().Read(path, std::move(promise));
        return future;
    }

    std::future<IOResult> WriteFile(const std::string& path, std::vector<uint8_t> data)
    {
        std::promise<IOResult> promise;
        std::future<IOResult> future = promise.get_future();
        Get().Write(path, std::move(data), std::move(promise));
        return future;
    }

    const char* BackendName()
    {
        return Get().Name();
    }

    void UseThreadPoolBackend()
    {
        Shutdown();
        std::lock_guard<std::mutex> lock(g_BackendMutex);
        g_ForceThreadPool = true;
    }

    void UseNativeBackend()
    {
        Shutdown();
        std::lock_guard<std::mutex> lock(g_BackendMutex);
        g_ForceThreadPool = false;
    }

    void Shutdown()
    {
        std::unique_ptr<Backend> backend;
        {
            std::lock_guard<std::mutex> lock(g_BackendMutex);
            backend = std::move(g_Backend);
        }
        if (backend)
            backend->Drain();
        backend.reset();
    }
}
