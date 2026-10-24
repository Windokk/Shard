#include <algorithm>
#include <cstddef>

#include "engine/renderer/rhi/backends/opengl/renderer_api/gl_api.hpp"
#include "engine/renderer/rhi/backends/opengl/gl_utils.hpp"
#include "engine/renderer/rhi/render_snapshot.hpp"
#include "engine/renderer/rhi/render_thread.hpp"
#include "engine/renderer/rhi/resources/texture/texture.hpp"

// The 2D overlay of shard.render, in OpenGL : the geometry a plugin recorded (a UI's rectangles and glyphs) drawn over the picture with premultiplied
// alpha, in pixels from the top left of the target. One small program of its own, no pipeline or material of the renderer : it reprograms the API
// state, so the renderer invalidates its cache afterwards.

namespace Shard::Engine::Rendering {

    namespace {
        const char* kVertex = R"(#version 430 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aUV;
uniform mat4 uProjection;
uniform mat4 uTransform;
uniform vec2 uTranslation;
out vec4 vColor;
out vec2 vUV;
void main()
{
    gl_Position = uProjection * (uTransform * vec4(aPos + uTranslation, 0.0, 1.0));
    vColor = aColor;
    vUV = aUV;
}
)";

        const char* kFragment = R"(#version 430 core
in vec4 vColor;
in vec2 vUV;
uniform sampler2D uTexture;
uniform int uUseTexture;
out vec4 FragColor;
void main()
{
    vec4 color = vColor;
    if (uUseTexture != 0)
        color *= texture(uTexture, vUV);
    FragColor = color;
}
)";

        struct Program {
            GLuint id = 0;
            GLint projection = -1, transform = -1, translation = -1, texture = -1, useTexture = -1;
            GLuint vao = 0, vbo = 0, ebo = 0;       // for the triangles drawn once (Kind::Triangles)
        };

        GLuint Compile(GLenum type, const char* source)
        {
            const GLuint shader = glCreateShader(type);
            glShaderSource(shader, 1, &source, nullptr);
            glCompileShader(shader);
            GLint ok = 0;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                char log[1024] = {};
                glGetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
                DEBUG_ERROR("Overlay shader : ", log);
                glDeleteShader(shader);
                return 0;
            }
            return shader;
        }

        // Created on first use, on the graphics thread (the only one that draws overlays)
        Program& GetProgram()
        {
            static Program program;
            if (program.id)
                return program;
            const GLuint vs = Compile(GL_VERTEX_SHADER, kVertex);
            const GLuint fs = Compile(GL_FRAGMENT_SHADER, kFragment);
            if (!vs || !fs)
                return program;
            program.id = glCreateProgram();
            glAttachShader(program.id, vs);
            glAttachShader(program.id, fs);
            glLinkProgram(program.id);
            glDeleteShader(vs);
            glDeleteShader(fs);
            GLint ok = 0;
            glGetProgramiv(program.id, GL_LINK_STATUS, &ok);
            if (!ok) {
                DEBUG_ERROR("The overlay program did not link.");
                glDeleteProgram(program.id);
                program.id = 0;
                return program;
            }
            program.projection = glGetUniformLocation(program.id, "uProjection");
            program.transform = glGetUniformLocation(program.id, "uTransform");
            program.translation = glGetUniformLocation(program.id, "uTranslation");
            program.texture = glGetUniformLocation(program.id, "uTexture");
            program.useTexture = glGetUniformLocation(program.id, "uUseTexture");
            glGenVertexArrays(1, &program.vao);
            glGenBuffers(1, &program.vbo);
            glGenBuffers(1, &program.ebo);
            return program;
        }

        void SetLayout()
        {
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(OverlayVertex), reinterpret_cast<void*>(offsetof(OverlayVertex, x)));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(OverlayVertex), reinterpret_cast<void*>(offsetof(OverlayVertex, color)));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(OverlayVertex), reinterpret_cast<void*>(offsetof(OverlayVertex, u)));
        }

        // The buffers of a geometry, uploaded the first time it is drawn
        void Upload(OverlayGeometry& g)
        {
            if (g.gpu[0])
                return;
            glGenVertexArrays(1, &g.gpu[0]);
            glGenBuffers(1, &g.gpu[1]);
            glGenBuffers(1, &g.gpu[2]);
            glBindVertexArray(g.gpu[0]);
            glBindBuffer(GL_ARRAY_BUFFER, g.gpu[1]);
            glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(g.vertices.size() * sizeof(OverlayVertex)), g.vertices.data(), GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.gpu[2]);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(g.indices.size() * sizeof(uint32_t)), g.indices.data(), GL_STATIC_DRAW);
            SetLayout();
        }

        struct Rect {
            int x = 0, y = 0, w = 0, h = 0;
            Rect Intersect(const Rect& o) const
            {
                const int x0 = std::max(x, o.x), y0 = std::max(y, o.y);
                const int x1 = std::min(x + w, o.x + o.w), y1 = std::min(y + h, o.y + o.h);
                return Rect{ x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0) };
            }
        };
    }

    void GLRendererAPI::ReleaseOverlayGeometry(const uint32_t gpu[3])
    {
        if (gpu[0]) glDeleteVertexArrays(1, &gpu[0]);
        if (gpu[1]) glDeleteBuffers(1, &gpu[1]);
        if (gpu[2]) glDeleteBuffers(1, &gpu[2]);
    }

    void GLRendererAPI::DrawOverlay(const OverlayProxy& overlay, uint32_t width, uint32_t height)
    {
        SHARD_ASSERT_GRAPHICS_THREAD();
        if (overlay.commands.empty() || width == 0 || height == 0)
            return;
        Program& program = GetProgram();
        if (!program.id)
            return;

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_STENCIL_TEST);
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_FALSE);
        glEnable(GL_SCISSOR_TEST);
        glViewport(0, 0, GLsizei(width), GLsizei(height));

        glUseProgram(program.id);
        const glm::mat4 projection = glm::mat4(2.0f / float(width), 0, 0, 0,
                                               0, -2.0f / float(height), 0, 0,
                                               0, 0, 1, 0,
                                               -1, 1, 0, 1);
        glUniformMatrix4fv(program.projection, 1, GL_FALSE, &projection[0][0]);
        glUniform1i(program.texture, 0);
        glActiveTexture(GL_TEXTURE0);

        glm::mat4 transform(1.0f);
        const Rect full{ 0, 0, int(width), int(height) };
        Rect user = full;                           // set_scissor
        Rect clip = full;                           // set_clip_mask, as the bounding rectangle of the mask (no stencil on the viewport)
        auto applyScissor = [&] {
            const Rect r = user.Intersect(clip);
            glScissor(r.x, int(height) - (r.y + r.h), r.w, r.h);
        };
        applyScissor();
        glUniformMatrix4fv(program.transform, 1, GL_FALSE, &transform[0][0]);

        GLint previousVao = 0;
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);

        for (const OverlayCommand& c : overlay.commands) {
            switch (c.kind) {
                case OverlayCommand::Kind::Transform:
                    transform = c.transform;
                    glUniformMatrix4fv(program.transform, 1, GL_FALSE, &transform[0][0]);
                    break;

                case OverlayCommand::Kind::Scissor:
                    user = c.enabled ? Rect{ c.scissor[0], c.scissor[1], c.scissor[2], c.scissor[3] } : full;
                    applyScissor();
                    break;

                case OverlayCommand::Kind::ClipMask: {
                    // SHARD_CLIP_DISABLE 0, SET 1, SET_INVERSE 2, INTERSECT 3
                    if (c.operation == 0 || c.operation == 2 || !c.geometry || c.geometry->vertices.empty()) {
                        clip = full;                // no mask, or everything but a shape : not representable by a rectangle
                    } else {
                        float lo[2] = { 1e30f, 1e30f }, hi[2] = { -1e30f, -1e30f };
                        for (const OverlayVertex& v : c.geometry->vertices) {
                            const glm::vec4 p = transform * glm::vec4(v.x + c.translation.x, v.y + c.translation.y, 0.0f, 1.0f);
                            lo[0] = std::min(lo[0], p.x); lo[1] = std::min(lo[1], p.y);
                            hi[0] = std::max(hi[0], p.x); hi[1] = std::max(hi[1], p.y);
                        }
                        const Rect mask{ int(lo[0]), int(lo[1]), int(hi[0] - lo[0] + 1.0f), int(hi[1] - lo[1] + 1.0f) };
                        clip = c.operation == 3 ? clip.Intersect(mask) : mask;
                    }
                    applyScissor();
                    break;
                }

                case OverlayCommand::Kind::Clear: {
                    glDisable(GL_SCISSOR_TEST);
                    glClearColor(c.color.r, c.color.g, c.color.b, c.color.a);
                    glClear(GL_COLOR_BUFFER_BIT);
                    glEnable(GL_SCISSOR_TEST);
                    break;
                }

                case OverlayCommand::Kind::Geometry: {
                    if (!c.geometry || c.geometry->indices.empty())
                        break;
                    Upload(*c.geometry);
                    glUniform2f(program.translation, c.translation.x, c.translation.y);
                    glUniform1i(program.useTexture, c.texture ? 1 : 0);
                    if (c.texture) glBindTexture(GL_TEXTURE_2D, c.texture->GetHandle());
                    glBindVertexArray(c.geometry->gpu[0]);
                    glDrawElements(GL_TRIANGLES, GLsizei(c.geometry->indices.size()), GL_UNSIGNED_INT, nullptr);
                    break;
                }

                case OverlayCommand::Kind::Triangles: {
                    if (c.count == 0 || c.first + c.count > overlay.inlineIndices.size())
                        break;
                    glUniform2f(program.translation, 0.0f, 0.0f);
                    glUniform1i(program.useTexture, c.texture ? 1 : 0);
                    if (c.texture) glBindTexture(GL_TEXTURE_2D, c.texture->GetHandle());
                    glBindVertexArray(program.vao);
                    glBindBuffer(GL_ARRAY_BUFFER, program.vbo);
                    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(overlay.inlineVertices.size() * sizeof(OverlayVertex)), overlay.inlineVertices.data(), GL_STREAM_DRAW);
                    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, program.ebo);
                    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(c.count * sizeof(uint32_t)), overlay.inlineIndices.data() + c.first, GL_STREAM_DRAW);
                    SetLayout();
                    glDrawElements(GL_TRIANGLES, GLsizei(c.count), GL_UNSIGNED_INT, nullptr);
                    break;
                }
            }
        }

        glBindVertexArray(GLuint(previousVao));
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_SCISSOR_TEST);
        glDepthMask(GL_TRUE);
        glUseProgram(0);
        InvalidateStateCache();
    }
}
