#if defined(__linux__) && defined(SHARD_HAS_IO_URING)

#include "engine/platform/filesystem/async_io_backend.hpp"

#include <liburing.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

namespace Shard::Engine::Core::Platform::AsyncIO {

    namespace {
        constexpr unsigned kQueueDepth = 64;
        constexpr uint64_t kChunkSize = 1024 * 1024;

        struct Request {
            int fd = -1;
            bool write = false;
            std::string path;
            std::vector<uint8_t> buffer;
            uint64_t offset = 0;
            std::promise<IOResult> promise;
        };

        class UringBackend : public Backend {
        public:
            // The ring is initialised by CreateNativeBackend (it can fail : old kernel, seccomp filter in a container...)
            explicit UringBackend(io_uring ring) : m_Ring(ring)
            {
                m_Thread = std::thread([this] { Run(); });
            }

            ~UringBackend() override
            {
                Drain();

                // A NOP without a request wakes the completion thread up and tells it to stop
                {
                    std::lock_guard<std::mutex> lock(m_RingMutex);
                    io_uring_sqe* sqe = GetSqe();
                    io_uring_prep_nop(sqe);
                    io_uring_sqe_set_data(sqe, nullptr);
                    io_uring_submit(&m_Ring);
                }
                m_Thread.join();
                io_uring_queue_exit(&m_Ring);
            }

            const char* Name() const override { return "io_uring"; }

            void Read(std::string path, std::promise<IOResult> promise) override
            {
                auto* request = new Request();
                request->path = std::move(path);
                request->promise = std::move(promise);
                BeginRequest();

                request->fd = open(request->path.c_str(), O_RDONLY | O_CLOEXEC);
                if (request->fd < 0)
                    return Fail(request, "can't open " + request->path);

                struct stat info{};
                if (fstat(request->fd, &info) != 0)
                    return Fail(request, "can't get the size of " + request->path);
                request->buffer.resize(static_cast<size_t>(info.st_size));

                Start(request);
            }

            void Write(std::string path, std::vector<uint8_t> data, std::promise<IOResult> promise) override
            {
                auto* request = new Request();
                request->write = true;
                request->path = std::move(path);
                request->buffer = std::move(data);
                request->promise = std::move(promise);
                BeginRequest();

                request->fd = open(request->path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
                if (request->fd < 0)
                    return Fail(request, "can't open " + request->path + " for writing");

                Start(request);
            }

            void Drain() override
            {
                std::unique_lock<std::mutex> lock(m_Mutex);
                m_Idle.wait(lock, [this] { return m_Pending == 0; });
            }

        private:
            void BeginRequest()
            {
                std::lock_guard<std::mutex> lock(m_Mutex);
                ++m_Pending;
            }

            void EndRequest()
            {
                {
                    std::lock_guard<std::mutex> lock(m_Mutex);
                    --m_Pending;
                }
                m_Idle.notify_all();
            }

            // Called with m_RingMutex held. A full submission queue is flushed to make room.
            io_uring_sqe* GetSqe()
            {
                io_uring_sqe* sqe = io_uring_get_sqe(&m_Ring);
                if (!sqe)
                {
                    io_uring_submit(&m_Ring);
                    sqe = io_uring_get_sqe(&m_Ring);
                }
                return sqe;
            }

            void Start(Request* request)
            {
                if (request->buffer.empty())
                    return Finish(request);
                Issue(request);
            }

            void Issue(Request* request)
            {
                const uint64_t remaining = request->buffer.size() - request->offset;
                const unsigned length = static_cast<unsigned>(std::min(remaining, kChunkSize));
                uint8_t* position = request->buffer.data() + request->offset;

                std::lock_guard<std::mutex> lock(m_RingMutex);
                io_uring_sqe* sqe = GetSqe();
                if (!sqe)
                    return Fail(request, "io_uring submission queue is full");

                if (request->write)
                    io_uring_prep_write(sqe, request->fd, position, length, request->offset);
                else
                    io_uring_prep_read(sqe, request->fd, position, length, request->offset);
                io_uring_sqe_set_data(sqe, request);

                if (io_uring_submit(&m_Ring) < 0)
                    Fail(request, "io_uring submit failed for " + request->path);
            }

            void Finish(Request* request)
            {
                IOResult result;
                result.ok = true;
                if (!request->write)
                    result.data = std::move(request->buffer);
                Complete(request, std::move(result));
            }

            void Fail(Request* request, std::string error)
            {
                IOResult result;
                result.error = std::move(error);
                Complete(request, std::move(result));
            }

            void Complete(Request* request, IOResult result)
            {
                if (request->fd >= 0)
                    close(request->fd);
                request->promise.set_value(std::move(result));
                delete request;
                EndRequest();
            }

            void Run()
            {
                for (;;)
                {
                    io_uring_cqe* cqe = nullptr;
                    const int wait = io_uring_wait_cqe(&m_Ring, &cqe);
                    if (wait == -EINTR)
                        continue;
                    if (wait < 0)
                        return;

                    auto* request = static_cast<Request*>(io_uring_cqe_get_data(cqe));
                    const int result = cqe->res;
                    io_uring_cqe_seen(&m_Ring, cqe);

                    if (!request)
                        return;     // the stop NOP

                    if (result == -EINTR || result == -EAGAIN)
                        Issue(request);     // nothing transferred : try the same chunk again
                    else if (result < 0)
                        Fail(request, std::string(request->write ? "can't write " : "can't read ") + request->path + " : " + std::strerror(-result));
                    else
                    {
                        request->offset += static_cast<uint64_t>(result);
                        if (request->offset >= request->buffer.size())
                            Finish(request);
                        else if (result == 0)
                            Fail(request, "unexpected end of " + request->path);
                        else
                            Issue(request);
                    }
                }
            }

            io_uring m_Ring;
            std::mutex m_RingMutex;     // the submission side of the ring is not thread safe
            std::thread m_Thread;
            std::mutex m_Mutex;
            std::condition_variable m_Idle;
            int m_Pending = 0;
        };
    }

    std::unique_ptr<Backend> CreateNativeBackend()
    {
        io_uring ring;
        if (io_uring_queue_init(kQueueDepth, &ring, 0) != 0)
            return nullptr;
        return std::make_unique<UringBackend>(ring);
    }
}

#endif
