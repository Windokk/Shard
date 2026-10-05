#pragma once

#include "../glad/include/glad/gl.h"

#include "engine/debugging/logger.hpp"

namespace Shard::Engine::Rendering{

    inline const char* GLErrorToString(GLenum err){
        switch(err){
            case GL_INVALID_ENUM:                  return "GL_INVALID_ENUM";
            case GL_INVALID_VALUE:                 return "GL_INVALID_VALUE";
            case GL_INVALID_OPERATION:             return "GL_INVALID_OPERATION";
            case GL_INVALID_FRAMEBUFFER_OPERATION:  return "GL_INVALID_FRAMEBUFFER_OPERATION";
            case GL_OUT_OF_MEMORY:                 return "GL_OUT_OF_MEMORY";
            case GL_STACK_UNDERFLOW:               return "GL_STACK_UNDERFLOW";
            case GL_STACK_OVERFLOW:                return "GL_STACK_OVERFLOW";
            default:                                return "Unknown GL error";
        }
    }

    // Drains and logs every pending GL error, tagged with a caller-provided context string.
    inline void CheckGLError(const char* context){
        GLenum err;
        while((err = glGetError()) != GL_NO_ERROR){
            DEBUG_ERROR("GL error after ", context, " : ", GLErrorToString(err), " (", err, ")");
        }
    }

    // Redundant-state filter for the OpenGL backend. Every field here mirrors a single, real piece
    // of GL context state (the currently bound program/VAO, whether depth test is enabled, ...), so
    // skipping the call when the desired value already matches what's cached is always correct : it
    // just avoids a driver round-trip that would have been a no-op. GLPipeline::Bind() used to issue
    // ~9 such calls unconditionally on every single draw command, even though consecutive draws
    // usually share the same pipeline/material.
    //
    // Call Reset() whenever GL state may have changed through code that doesn't go through this
    // cache (ImGui, compute dispatches, ...) so the next bind reflects reality instead of a stale
    // assumption. This is currently done once per render pass (see GLRendererAPI::Clear()).
    class GLStateCache {
        public:
            static void Reset(){
                s_ProgramValid = false;
                s_VAOValid = false;
                s_DepthTestValid = false;
                s_DepthWriteValid = false;
                s_DepthFuncValid = false;
                s_CullValid = false;
                s_BlendValid = false;
                s_BlendFuncValid = false;
                s_BlendEqValid = false;
                s_PolygonModeValid = false;

                for (bool& valid : s_PassGlobalsValid)
                    valid = false;

                for (bool& valid : s_TextureUnitValid)
                    valid = false;
            }

            static void BindProgram(GLuint program){
                if (s_ProgramValid && s_Program == program)
                    return;
                glUseProgram(program);
                s_Program = program;
                s_ProgramValid = true;
            }

            static void BindVertexArray(GLuint vao){
                if (s_VAOValid && s_VAO == vao)
                    return;
                glBindVertexArray(vao);
                s_VAO = vao;
                s_VAOValid = true;
            }

            static void SetDepthTest(bool enabled){
                if (s_DepthTestValid && s_DepthTest == enabled)
                    return;
                if (enabled) glEnable(GL_DEPTH_TEST);
                else glDisable(GL_DEPTH_TEST);
                s_DepthTest = enabled;
                s_DepthTestValid = true;
            }

            static void SetDepthWrite(bool enabled){
                if (s_DepthWriteValid && s_DepthWrite == enabled)
                    return;
                glDepthMask(enabled ? GL_TRUE : GL_FALSE);
                s_DepthWrite = enabled;
                s_DepthWriteValid = true;
            }

            static void SetDepthFunc(GLenum func){
                if (s_DepthFuncValid && s_DepthFunc == func)
                    return;
                glDepthFunc(func);
                s_DepthFunc = func;
                s_DepthFuncValid = true;
            }

            static void SetCullMode(bool enabled, GLenum face){
                if (s_CullValid && s_CullEnabled == enabled && (!enabled || s_CullFace == face))
                    return;
                if (enabled){
                    glEnable(GL_CULL_FACE);
                    glCullFace(face);
                }
                else{
                    glDisable(GL_CULL_FACE);
                }
                s_CullEnabled = enabled;
                s_CullFace = face;
                s_CullValid = true;
            }

            static void SetBlendEnabled(bool enabled){
                if (s_BlendValid && s_BlendEnabled == enabled)
                    return;
                if (enabled) glEnable(GL_BLEND);
                else glDisable(GL_BLEND);
                s_BlendEnabled = enabled;
                s_BlendValid = true;
            }

            static void SetBlendFunc(GLenum src, GLenum dst){
                if (s_BlendFuncValid && s_BlendSrc == src && s_BlendDst == dst)
                    return;
                glBlendFunc(src, dst);
                s_BlendSrc = src;
                s_BlendDst = dst;
                s_BlendFuncValid = true;
            }

            static void SetBlendEquation(GLenum eq){
                if (s_BlendEqValid && s_BlendEq == eq)
                    return;
                glBlendEquation(eq);
                s_BlendEq = eq;
                s_BlendEqValid = true;
            }

            // Forces every subsequent draw to rasterize as lines regardless of the pipeline's own
            // polygon mode (editor wireframe views), with a small bias so lines win the depth test
            // against a filled sweep of the same geometry.
            static void SetForceLine(bool force){
                if (s_ForceLine == force)
                    return;
                s_ForceLine = force;
                s_PolygonModeValid = false;
                if (force){
                    glEnable(GL_POLYGON_OFFSET_LINE);
                    glPolygonOffset(-1.0f, -1.0f);
                }
                else{
                    glDisable(GL_POLYGON_OFFSET_LINE);
                    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                }
            }

            static void SetPolygonMode(GLenum mode){
                if (s_ForceLine)
                    mode = GL_LINE;
                if (s_PolygonModeValid && s_PolygonMode == mode)
                    return;
                glPolygonMode(GL_FRONT_AND_BACK, mode);
                s_PolygonMode = mode;
                s_PolygonModeValid = true;
            }

            // Camera/light uniforms (uProjection, uView, camPos, lightNB, ambientIntensity, skybox
            // IBL textures) are constant for every draw call in a pass and are set directly on the
            // currently bound GL program (glUniform*/texture-unit binds), so they only need
            // reapplying the first time a given program is used since the last Reset() (once per
            // pass). Returns true the first time `program` is seen for `kind`, false on every
            // subsequent call with the same program.
            //
            // NOT valid for anything that instead writes into a Material instance's own parameter
            // maps (e.g. shadow map data via ShadowManager::BindShadowMaps) : those are re-uploaded
            // per-Material by GLMaterial::Bind(), and different Material instances can share the same
            // program, so gating them per-program would starve every material after the first one
            // using that program of the data (this caused a real shadow-flicker regression once).
            //
            // Each kind gets its own independent slot rather than sharing one flag : whether a draw
            // command needs camera state is decided per-command (not per-program), so a program's
            // first use might skip one kind while still needing another later - sharing a single flag
            // could wrongly suppress that later update.
            enum class PassGlobalsKind { Level = 0, Camera = 1, Count = 2 };

            static bool NeedsPassGlobalsUpdate(GLuint program, PassGlobalsKind kind){
                size_t i = static_cast<size_t>(kind);
                if (s_PassGlobalsValid[i] && s_PassGlobalsProgram[i] == program)
                    return false;
                s_PassGlobalsProgram[i] = program;
                s_PassGlobalsValid[i] = true;
                return true;
            }

            static constexpr size_t kMaxTrackedTextureUnits = 32;

            static void BindTextureUnit(GLuint unit, GLuint texture){
                if (unit < kMaxTrackedTextureUnits && s_TextureUnitValid[unit] && s_TextureUnit[unit] == texture)
                    return;
                glBindTextureUnit(unit, texture);
                if (unit < kMaxTrackedTextureUnits){
                    s_TextureUnit[unit] = texture;
                    s_TextureUnitValid[unit] = true;
                }
            }

        private:
            inline static GLuint s_Program = 0;
            inline static bool s_ProgramValid = false;

            inline static GLuint s_VAO = 0;
            inline static bool s_VAOValid = false;

            inline static bool s_DepthTest = false;
            inline static bool s_DepthTestValid = false;

            inline static bool s_DepthWrite = false;
            inline static bool s_DepthWriteValid = false;

            inline static GLenum s_DepthFunc = 0;
            inline static bool s_DepthFuncValid = false;

            inline static bool s_CullEnabled = false;
            inline static GLenum s_CullFace = 0;
            inline static bool s_CullValid = false;

            inline static bool s_BlendEnabled = false;
            inline static bool s_BlendValid = false;

            inline static GLenum s_BlendSrc = 0;
            inline static GLenum s_BlendDst = 0;
            inline static bool s_BlendFuncValid = false;

            inline static GLenum s_BlendEq = 0;
            inline static bool s_BlendEqValid = false;

            inline static GLenum s_PolygonMode = 0;
            inline static bool s_PolygonModeValid = false;
            inline static bool s_ForceLine = false;

            inline static GLuint s_PassGlobalsProgram[static_cast<size_t>(PassGlobalsKind::Count)] = {};
            inline static bool s_PassGlobalsValid[static_cast<size_t>(PassGlobalsKind::Count)] = {};

            inline static GLuint s_TextureUnit[kMaxTrackedTextureUnits] = {};
            inline static bool s_TextureUnitValid[kMaxTrackedTextureUnits] = {};
    };

}