#include "gl_storage_buffer.hpp"

#include "engine/renderer/rhi/backends/opengl/gl_utils.hpp"

namespace Shard::Engine::Rendering{

    GLStorageBuffer::GLStorageBuffer(uint32_t size) : m_Size(size)
    {
        glGenBuffers(1, &m_Buffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_Buffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, size, nullptr, GL_DYNAMIC_DRAW);
    }

    GLStorageBuffer::~GLStorageBuffer()
    {
        glDeleteBuffers(1, &m_Buffer);
    }

    void GLStorageBuffer::SetData(const void *data, uint32_t size)
    {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_Buffer);

        if (size == m_Size)
        {
            // Same size as the buffer's current GPU allocation - the common case, since every caller
            // either just Create()'d this buffer at exactly `size` (e.g. ProbeManager::UploadScene()
            // uploading the BVH/triangle/material buffers right after a scene rebuild) or re-uploads the
            // same fixed layout every frame (the per-volume light/probe-state buffers in
            // ProbeManager::Update()). A plain sub-range upload into the existing storage is all that's
            // needed here - no reallocation.
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, size, data);
        }
        else
        {
            // Size actually changed : one call both (re)allocates the new storage and uploads `data`
            // into it. This used to be glBufferData(size, nullptr, ...) followed by a full
            // glBufferSubData(data) right below it - two full-buffer allocations (this one, and another
            // one back in the constructor's Create() call that this call's caller almost always follows
            // immediately) for a single upload, for no benefit : glBufferData already accepts the data
            // pointer directly.
            glBufferData(GL_SHADER_STORAGE_BUFFER, size, data, GL_DYNAMIC_DRAW);
            m_Size = size;
        }
    }

    void GLStorageBuffer::GetData(void* outData, uint32_t size, uint32_t offset) const
    {
        if ((uint64_t)offset + (uint64_t)size > (uint64_t)m_Size)
        {
            DEBUG_ERROR("GetData range [" + std::to_string(offset) + ", " + std::to_string(offset + size) + ") exceeds storage buffer size (" + std::to_string(m_Size) + ") - buffer was not read back.");
            return;
        }

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_Buffer);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, offset, size, outData);
    }

    void GLStorageBuffer::Bind(uint32_t binding)
    {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, m_Buffer);
    }

}