#pragma once

#include <cctype>
#include <cstdint>
#include <string>

namespace Shard::Engine::Rendering {

    // The terms the light leaving a surface is the sum of. The real-time renderer computes each one its own way (lights and shadow
    // maps, probes and lightmaps, reflections) where the path tracer computes them all at once : drawing one term alone in both,
    // and comparing, says which approximation is wrong and by how much (scripts/compare_reference.py).
    //
    // A term is defined at the surface the camera sees :
    // - Direct : the lights (directional, point, spot), straight onto it, shadowed ;
    // - IndirectDiffuse : everything else that arrives through the diffuse lobe : the sky, the light other surfaces bounce, the
    //   emissive surfaces, the world's flat ambient floor (real time : probes, lightmaps, the sky's irradiance, SSAO) ;
    // - IndirectSpecular : everything else that arrives through the specular lobe : the reflections of the sky and of the scene
    //   (real time : the environment map, reflection probes, SSR) ;
    // - Emission : the light the surface gives off itself, and the sky where no surface is seen.
    //
    // This is the C++ twin of LIGHTING_TERM_* in shaders/common/light.glsl : the bits are the same there, and have to stay so.

    enum LightingTerm : uint32_t
    {
        LightingTermDirect           = 1u << 0,
        LightingTermIndirectDiffuse  = 1u << 1,
        LightingTermIndirectSpecular = 1u << 2,
        LightingTermEmission         = 1u << 3,

        LightingTermsAll = LightingTermDirect | LightingTermIndirectDiffuse | LightingTermIndirectSpecular | LightingTermEmission
    };

    /// Every term, in the order of their bits (the order of the path tracer's term images, see path_trace.comp)
    inline constexpr LightingTerm kLightingTerms[] = { LightingTermDirect, LightingTermIndirectDiffuse, LightingTermIndirectSpecular, LightingTermEmission };

    inline const char* LightingTermName(LightingTerm term)
    {
        switch (term)
        {
            case LightingTermDirect: return "direct";
            case LightingTermIndirectDiffuse: return "indirect_diffuse";
            case LightingTermIndirectSpecular: return "indirect_specular";
            case LightingTermEmission: return "emission";
            default: break;
        }
        return "?";
    }

    /// "all", or the names of the terms joined by '+' (in the order of their bits)
    inline std::string FormatLightingTerms(uint32_t terms)
    {
        terms &= LightingTermsAll;
        if (terms == LightingTermsAll)
            return "all";
        if (terms == 0)
            return "none";

        std::string text;
        for (LightingTerm term : kLightingTerms)
        {
            if ((terms & term) == 0)
                continue;
            if (!text.empty())
                text += "+";
            text += LightingTermName(term);
        }
        return text;
    }

    /// Reads "all", "none", or names of terms joined by '+' or ',' (case does not matter) : "direct", "direct+emission"...
    /// False, `out` untouched, when a name is not one of them.
    inline bool ParseLightingTerms(const std::string& text, uint32_t& out)
    {
        uint32_t terms = 0;
        size_t start = 0;
        bool any = false;
        while (start <= text.size())
        {
            size_t end = text.find_first_of("+,", start);
            if (end == std::string::npos)
                end = text.size();

            std::string name;
            for (size_t i = start; i < end; i++)
                if (!std::isspace(static_cast<unsigned char>(text[i])))
                    name.push_back(char(std::tolower(static_cast<unsigned char>(text[i]))));

            if (name == "all")
                terms |= LightingTermsAll;
            else if (name != "none")
            {
                bool known = false;
                for (LightingTerm term : kLightingTerms)
                {
                    if (name == LightingTermName(term))
                    {
                        terms |= term;
                        known = true;
                    }
                }
                if (!known)
                    return false;
            }

            any = true;
            start = end + 1;
        }

        if (!any)
            return false;
        out = terms;
        return true;
    }
}
