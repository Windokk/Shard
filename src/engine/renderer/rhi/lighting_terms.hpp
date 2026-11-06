#pragma once

#include <cctype>
#include <cstdint>
#include <string>

namespace Shard::Engine::Rendering {

    // Twin of LIGHTING_TERM_* in shaders/common/light.glsl, the bits must match.

    enum LightingTerm : uint32_t
    {
        LightingTermDirect           = 1u << 0,
        LightingTermIndirectDiffuse  = 1u << 1,
        LightingTermIndirectSpecular = 1u << 2,
        LightingTermEmission         = 1u << 3,

        LightingTermsAll = LightingTermDirect | LightingTermIndirectDiffuse | LightingTermIndirectSpecular | LightingTermEmission
    };

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
