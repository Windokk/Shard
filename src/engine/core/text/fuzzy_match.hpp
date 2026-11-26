#pragma once

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

// Ranked fuzzy matching for the editor's search boxes (content browser, asset pickers, outliner, console, ...).
//
// A query is split on whitespace into tokens. Every token has to match the candidate, so "wall brick" finds
// "BrickWall_02" and "wb" finds "WallBrick". A token scores best as an exact match, then a prefix, then a prefix
// of a word ("brick" in "Wall_Brick"), then a plain substring, and finally as a subsequence (letters in order,
// with bonuses for consecutive letters and word starts, and a small penalty for gaps).
//
// Header only and free of any UI dependency, so it is unit tested with the engine.
namespace Shard::Engine::Core::Text{

    struct FuzzyResult{
        bool matched = false;
        int score = 0;
    };

    namespace FuzzyDetail{

        inline char Lower(char c){
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }

        inline bool IsSeparator(char c){
            return c == ' ' || c == '_' || c == '-' || c == '.' || c == '/' || c == '\\' || c == ':' || c == '|' || c == '(' || c == ')';
        }

        inline bool IsUpper(char c){ return c >= 'A' && c <= 'Z'; }
        inline bool IsLowerCase(char c){ return c >= 'a' && c <= 'z'; }
        inline bool IsDigit(char c){ return c >= '0' && c <= '9'; }

        // Start of a word : first character, after a separator, a lower to upper step (camelCase) or a letter to digit step.
        inline bool IsWordStart(std::string_view text, size_t i){
            if(i == 0)
                return true;
            char prev = text[i - 1];
            char cur = text[i];
            if(IsSeparator(prev))
                return true;
            if(IsUpper(cur) && IsLowerCase(prev))
                return true;
            if(IsDigit(cur) != IsDigit(prev) && !IsSeparator(cur))
                return true;
            return false;
        }

        inline int ScoreToken(std::string_view token, std::string_view text){
            const size_t n = token.size();
            const size_t m = text.size();
            if(n == 0)
                return 0;
            if(n > m)
                return -1;

            // Scratch buffers reused between calls : the editor scores every candidate of a list each time the query changes,
            // allocating for each one is what made it slow (a Debug build most of all).
            thread_local std::string t;
            thread_local std::string x;
            t.resize(n);
            for(size_t i = 0; i < n; i++)
                t[i] = Lower(token[i]);
            x.resize(m);
            for(size_t i = 0; i < m; i++)
                x[i] = Lower(text[i]);

            // Contiguous : best occurrence of the token as a substring.
            int best = -1;
            for(size_t pos = x.find(t); pos != std::string::npos; pos = x.find(t, pos + 1)){
                int s = 1000;
                if(pos == 0)
                    s += 400;
                else if(IsWordStart(text, pos))
                    s += 250;
                if(n == m)
                    s += 600;
                // Shorter candidates first : the query covers more of them.
                s += static_cast<int>(100 * n / m);
                // Earlier is better.
                s -= static_cast<int>(std::min<size_t>(pos, 50));
                best = std::max(best, s);
            }
            if(best >= 0)
                return best;

            // Cheap rejection before the alignment search : the letters have to be there in order at all.
            {
                size_t from = 0;
                for(size_t i = 0; i < n; i++){
                    size_t p = x.find(t[i], from);
                    if(p == std::string::npos)
                        return -1;
                    from = p + 1;
                }
            }

            // Subsequence : best alignment of the token's letters in order, via dynamic programming.
            // prev[j] / cur[j] = best score with the token's letters up to i placed, the last at text position j.
            constexpr int kNeg = -1000000;
            constexpr int kBase = 16;
            constexpr int kConsecutive = 18;
            constexpr int kWordStart = 14;
            constexpr int kGap = 3;

            thread_local std::vector<int> prev;
            thread_local std::vector<int> cur;
            prev.assign(m, kNeg);
            cur.resize(m);
            for(size_t i = 0; i < n; i++){
                std::fill(cur.begin(), cur.end(), kNeg);
                // gapBest = best prev[k] - kGap * (j - k - 1) over k <= j - 2 : a match that skipped characters.
                int gapBest = kNeg;
                for(size_t j = 0; j < m; j++){
                    if(gapBest > kNeg)
                        gapBest -= kGap;
                    if(j >= 2 && prev[j - 2] > kNeg)
                        gapBest = std::max(gapBest, prev[j - 2] - kGap);

                    if(x[j] != t[i])
                        continue;

                    int bonus = kBase;
                    if(IsWordStart(text, j))
                        bonus += kWordStart;

                    if(i == 0){
                        // Leading letters skipped cost a little, so earlier matches rank higher.
                        cur[j] = bonus - static_cast<int>(std::min<size_t>(j, 12));
                    }
                    else{
                        int v = kNeg;
                        if(j >= 1 && prev[j - 1] > kNeg)
                            v = prev[j - 1] + bonus + kConsecutive;
                        if(gapBest > kNeg)
                            v = std::max(v, gapBest + bonus);
                        cur[j] = v;
                    }
                }
                std::swap(prev, cur);
            }

            int sub = kNeg;
            for(size_t j = 0; j < m; j++)
                sub = std::max(sub, prev[j]);
            if(sub <= kNeg)
                return -1;
            // Stays below every contiguous match.
            return std::max(1, std::min(sub, 900));
        }
    }

    // Scores `text` against `query`. An empty query matches everything with score 0.
    inline FuzzyResult FuzzyMatch(std::string_view query, std::string_view text){
        FuzzyResult result;

        int total = 0;
        bool any = false;
        size_t i = 0;
        while(i < query.size()){
            while(i < query.size() && (query[i] == ' ' || query[i] == '\t'))
                i++;
            size_t start = i;
            while(i < query.size() && query[i] != ' ' && query[i] != '\t')
                i++;
            if(i == start)
                break;

            any = true;
            int s = FuzzyDetail::ScoreToken(query.substr(start, i - start), text);
            if(s < 0)
                return result;
            total += s;
        }

        result.matched = true;
        result.score = any ? total : 0;
        return result;
    }

    inline bool FuzzyContains(std::string_view query, std::string_view text){
        return FuzzyMatch(query, text).matched;
    }

    // Best score over several fields of one candidate (name, type, path...). `weights` biases a field, 100 = neutral.
    inline FuzzyResult FuzzyMatchAny(std::string_view query, const std::string_view* fields, size_t fieldCount, const int* weights, size_t weightCount){
        FuzzyResult best;
        for(size_t f = 0; f < fieldCount; f++){
            FuzzyResult r = FuzzyMatch(query, fields[f]);
            if(!r.matched)
                continue;
            int w = f < weightCount ? weights[f] : 100;
            r.score = r.score * w / 100;
            if(!best.matched || r.score > best.score)
                best = r;
        }
        return best;
    }

    inline FuzzyResult FuzzyMatchAny(std::string_view query, const std::vector<std::string_view>& fields, const std::vector<int>& weights = {}){
        return FuzzyMatchAny(query, fields.data(), fields.size(), weights.data(), weights.size());
    }

    // Braced lists at the call site (no heap allocation for them).
    inline FuzzyResult FuzzyMatchAny(std::string_view query, std::initializer_list<std::string_view> fields, std::initializer_list<int> weights = {}){
        return FuzzyMatchAny(query, fields.begin(), fields.size(), weights.begin(), weights.size());
    }

    // Byte positions of `text` the query's letters landed on, to highlight them. Greedy and independent of the scoring
    // alignment, which only matters visually for the subsequence case. Empty when nothing matches.
    inline std::vector<uint32_t> FuzzyMatchPositions(std::string_view query, std::string_view text){
        std::vector<uint32_t> out;
        if(!FuzzyMatch(query, text).matched)
            return out;

        std::string lowered(text.size(), ' ');
        for(size_t k = 0; k < text.size(); k++)
            lowered[k] = FuzzyDetail::Lower(text[k]);

        size_t i = 0;
        while(i < query.size()){
            while(i < query.size() && (query[i] == ' ' || query[i] == '\t'))
                i++;
            size_t start = i;
            while(i < query.size() && query[i] != ' ' && query[i] != '\t')
                i++;
            if(i == start)
                break;

            std::string token(i - start, ' ');
            for(size_t k = 0; k < token.size(); k++)
                token[k] = FuzzyDetail::Lower(query[start + k]);

            size_t pos = std::string::npos;
            // Prefer a word start occurrence, otherwise the first one.
            for(size_t p = lowered.find(token); p != std::string::npos; p = lowered.find(token, p + 1)){
                if(pos == std::string::npos)
                    pos = p;
                if(FuzzyDetail::IsWordStart(text, p)){
                    pos = p;
                    break;
                }
            }
            if(pos != std::string::npos){
                for(size_t k = 0; k < token.size(); k++)
                    out.push_back(static_cast<uint32_t>(pos + k));
            }
            else{
                size_t from = 0;
                for(char c : token){
                    size_t p = lowered.find(c, from);
                    if(p == std::string::npos)
                        break;
                    out.push_back(static_cast<uint32_t>(p));
                    from = p + 1;
                }
            }
        }

        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }
}
