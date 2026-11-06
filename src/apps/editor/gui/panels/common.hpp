#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

void TextEllipsis(const char* text);

void TextEllipsisCentered(const char* text, float cellWidth);

#define IM_MIN(A, B)            (((A) < (B)) ? (A) : (B))
#define IM_MAX(A, B)            (((A) >= (B)) ? (A) : (B))
#define IM_CLAMP(V, MN, MX)     ((V) < (MN) ? (MN) : (V) > (MX) ? (MX) : (V))