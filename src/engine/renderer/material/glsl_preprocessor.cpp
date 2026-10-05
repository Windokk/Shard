#include "glsl_preprocessor.hpp"

#include "engine/debugging/logger.hpp"

#include <sstream>
#include <unordered_set>
#include <vector>

namespace Shard::Engine::Rendering {

    namespace {

        // Threaded through one whole top-level ResolveGLSLIncludes() call.
        struct IncludeContext
        {
            std::unordered_set<std::string> included; // normalized absolute paths already spliced in
            std::vector<std::string> stack;            // active include chain, for cycle detection
        };

        std::string StripCarriageReturn(std::string line)
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            return line;
        }

        // Recognizes `#include "relative/path"` - only the quoted form, since every include here is
        // project-relative, never a system/angle-bracket header. Leading whitespace before the `#` is
        // allowed, matching how GLSL/C preprocessor directives are normally written.
        bool ParseIncludeDirective(const std::string& line, std::string& outPath)
        {
            size_t i = 0;
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
                i++;

            static const std::string kDirective = "#include";
            if (line.compare(i, kDirective.size(), kDirective) != 0)
                return false;

            i += kDirective.size();
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
                i++;

            if (i >= line.size() || line[i] != '"')
                return false;
            i++;

            size_t start = i;
            while (i < line.size() && line[i] != '"')
                i++;

            if (i >= line.size())
                return false;

            outPath = line.substr(start, i - start);
            return true;
        }

        std::string ResolveRecursive(const Filesystem::Path& path, IncludeContext& ctx, bool emitMarkers)
        {
            std::string normalized = path.GetAbsolutePath();

            for (const std::string& active : ctx.stack)
            {
                if (active == normalized)
                {
                    DEBUG_ERROR("GLSL include cycle detected at : " + path.full);
                    return "";
                }
            }

            // Already spliced in earlier in this compile (via a different include chain) - skip
            // entirely rather than emitting an empty marker, so shared headers behave like they have a
            // standard header guard without needing one written by hand.
            if (ctx.included.count(normalized) != 0)
                return "";

            ctx.included.insert(normalized);
            ctx.stack.push_back(normalized);

            std::string source = path.ReadFile();
            Filesystem::Path dir = path.GetParentPath();

            std::ostringstream out;
            if (emitMarkers)
                out << "// ---- begin include: " << path.full << " ----\n";

            std::istringstream in(source);
            std::string line;
            int lineNumber = 0;

            while (std::getline(in, line))
            {
                lineNumber++;
                line = StripCarriageReturn(line);

                std::string includePath;
                if (ParseIncludeDirective(line, includePath))
                {
                    Filesystem::Path resolved = dir / includePath;
                    out << ResolveRecursive(resolved, ctx, true);
                    // Core GLSL #line only takes a numeric line (no filename operand) - this at least
                    // keeps line numbers in driver compile-error output close to correct for whatever
                    // file happens to be active, even though it can't name that file itself (the
                    // "begin/end include" comment markers above are what a human traces by instead).
                    out << "#line " << (lineNumber + 1) << "\n";
                }
                else
                {
                    out << line << "\n";
                }
            }

            if (emitMarkers)
                out << "// ---- end include: " << path.full << " ----\n";

            ctx.stack.pop_back();
            return out.str();
        }

    }

    std::string ResolveGLSLIncludes(const Filesystem::Path& path)
    {
        IncludeContext ctx;
        return ResolveRecursive(path, ctx, false);
    }

}
