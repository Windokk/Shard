#pragma once

#include "../glad/include/glad/gl.h"

#include "engine/core/diagnostics/logger.hpp"

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

    inline void CheckGLError(const char* context){
        GLenum err;
        while((err = glGetError()) != GL_NO_ERROR){
            DEBUG_ERROR("GL error after ", context, " : ", GLErrorToString(err), " (", err, ")");
        }
    }

    // Redundant-state filter. Call Reset() when GL state changes outside it (ImGui, compute), done once per pass.
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

            // Depth sweep overrides the pipeline: Prepass writes with LESS, Equal tests EQUAL and writes nothing.
            enum class DepthMode { Normal, Prepass, Equal };

            static void SetDepthMode(DepthMode mode){
                if (s_DepthMode == mode)
                    return;
                s_DepthMode = mode;
                s_DepthTestValid = false;
                s_DepthWriteValid = false;
                s_DepthFuncValid = false;
            }

            static DepthMode GetDepthMode(){ return s_DepthMode; }

            static void SetColorWrite(bool enabled){
                if (s_ColorWriteValid && s_ColorWrite == enabled)
                    return;
                glColorMask(enabled, enabled, enabled, enabled);
                s_ColorWrite = enabled;
                s_ColorWriteValid = true;
            }

            static void SetDepthTest(bool enabled){
                if (s_DepthMode != DepthMode::Normal)
                    enabled = true;
                if (s_DepthTestValid && s_DepthTest == enabled)
                    return;
                if (enabled) glEnable(GL_DEPTH_TEST);
                else glDisable(GL_DEPTH_TEST);
                s_DepthTest = enabled;
                s_DepthTestValid = true;
            }

            static void SetDepthWrite(bool enabled){
                if (s_DepthMode == DepthMode::Prepass)
                    enabled = true;
                else if (s_DepthMode == DepthMode::Equal)
                    enabled = false;
                if (s_DepthWriteValid && s_DepthWrite == enabled)
                    return;
                glDepthMask(enabled ? GL_TRUE : GL_FALSE);
                s_DepthWrite = enabled;
                s_DepthWriteValid = true;
            }

            static void SetDepthFunc(GLenum func){
                if (s_DepthMode == DepthMode::Prepass)
                    func = GL_LESS;
                else if (s_DepthMode == DepthMode::Equal)
                    func = GL_EQUAL;
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

            // Pass globals are set once per program per pass. Not valid for Material parameter maps (shadow data):
            // materials share programs, so later ones would be starved. One slot per kind.
            enum class PassGlobalsKind { World = 0, Camera = 1, DepthSweep = 2, Count = 3 };

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

            inline static DepthMode s_DepthMode = DepthMode::Normal;
            inline static bool s_ColorWrite = true;
            inline static bool s_ColorWriteValid = false;

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