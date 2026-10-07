#pragma once

#include "reflection_types.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <map>
#include <set>

#include "clang/Tooling/Tooling.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;

enum FieldFlags : uint32_t {
    Editable     = 1 << 0,
    ReadOnly     = 1 << 1
};

struct Container{
    // element info
    TypeID elementType;
    size_t elementSize;

    // common queries
    size_t (*size)(void* container);
    bool   (*isAssociative)();
};

struct FieldInfo {
    // Identity
    const char* name;         // Field name
    TypeID type;      // Type stored in the component

    // Memory access
    uint32_t offset;          // Offset in the struct (used if no getter/setter)

    // Editor metadata

    uint32_t flags;           // Editable / ReadOnly
    float min;                // Optional min value for editor widgets
    float max;                // Optional max value for editor widgets

    const Container* container = nullptr;
    const EnumDescriptor* enumDesc = nullptr;
};

struct ClassDescriptor{
    std::string name;
    std::vector<FieldInfo*> fields;
};

template<typename T>
Container MakeVectorContainer() {
    Container c{};

    c.elementType = GetTypeIDFromString(typeid(T).name());
    c.elementSize = sizeof(T);

    c.size = [](void* c) -> size_t {
        return static_cast<std::vector<T>*>(c)->size();
    };

    c.isAssociative = []() { return false; };

    return c;
}

template<typename K, typename V>
Container MakeMapContainer() {
    Container c{};

    c.elementType = GetTypeIDFromString(typeid(V).name());
    c.elementSize = sizeof(V);

    c.size = [](void* c) -> size_t {
        return static_cast<std::map<K, V>*>(c)->size();
    };

    c.isAssociative = []() { return true; };

    return c;
}

QualType getInnerTypeFromStdVector(QualType type, ASTContext& Context) {
    type = type.getCanonicalType().getUnqualifiedType();

    // Makin sure it's a class template specialization
    if (const auto* recordType = type->getAs<RecordType>()) {
        const auto* decl = recordType->getDecl();
        if (const auto* tmplSpec = dyn_cast<ClassTemplateSpecializationDecl>(decl)) {
            const TemplateArgument& arg = tmplSpec->getTemplateArgs()[0];
            QualType innerType = arg.getAsType();

            // Keep shared_ptr as-is
            return innerType;
        }
    }

    return QualType(); // failed to extract
}

std::pair<QualType, QualType> getKeyValueTypeFromStdMap(QualType type, ASTContext& Context) {
    type = type.getCanonicalType().getUnqualifiedType();

    // Make sure it's a class template specialization
    if (const auto* recordType = type->getAs<RecordType>()) {
        const auto* decl = recordType->getDecl();
        if (const auto* tmplSpec = dyn_cast<ClassTemplateSpecializationDecl>(decl)) {
            const TemplateArgumentList& argsList = tmplSpec->getTemplateArgs();

            if (argsList.size() < 2)
                return {QualType(), QualType()};

            QualType keyType   = argsList[0].getAsType();
            QualType valueType = argsList[1].getAsType();

            // Keep shared_ptr or unique_ptr as-is for valueType
            return {keyType, valueType};
        }
    }

    return {QualType(), QualType()}; // failed to extract
}

struct StructDescriptor{
    std::string name;
    std::vector<FieldInfo*> fields;
};

class FieldHandler : public MatchFinder::MatchCallback {
public:
    void run(const MatchFinder::MatchResult &Result) override {
        if (!Result.SourceManager) return;

        const auto* Decl = Result.Nodes.getNodeAs<CXXRecordDecl>("record");
        if (!Decl) return;

        const auto &SM = *Result.SourceManager;
        const CXXRecordDecl* Definition = Decl->getDefinition();
        if (!Definition) {
            // No definition in this TU, skip it
            return;
        }

        std::string filename = SM.getFilename(Definition->getLocation()).str();

        // Initialize storage for this file if not already
        auto& fileEntries = perFileGenerated[filename];

        // Check all annotations
        for (auto* Attr : Decl->specific_attrs<AnnotateAttr>()) {
            std::string annotation = Attr->getAnnotation().str();

            if (annotation == "class") {
                std::string code = handleClass(Decl, SM);
                if (!code.empty())
                    fileEntries.push_back(code);
            }
            else if (annotation == "struct") {
                std::string code = handleStruct(Decl, SM);
                if (!code.empty())
                    fileEntries.push_back(code);
            }
        }
    }

    // Call this at the end of the tool run to write the file
    void writeAllFiles() {
        for (auto& [file, codeList] : perFileGenerated) {
            std::filesystem::path p(file);
            std::string outFile = (p.parent_path() / (p.stem().string() + ".reflection.hpp")).string();
            std::ofstream out(outFile, std::ios::trunc);
            if (!out.is_open()) {
                llvm::errs() << "Failed to open file: " << outFile << "\n";
                continue;
            }

            out << "#pragma once\n\n";
            out << "#include \"" << p.filename().string() << "\"\n";
            out << "#include \"engine/core/reflection_fields.hpp\"\n\n";

            for (auto& code : codeList)
                out << code << "\n";
                
            out.close();
        }

        llvm::outs() << "Successfully wrote all reflection files" << "\n";
    }

private:
    std::map<std::string, std::vector<std::string>> perFileGenerated;
    
    std::string handleClass(const CXXRecordDecl* C,const SourceManager& SM) {
        if (!C || !C->isThisDeclarationADefinition())
            return {};

        std::ostringstream out;

        out << "// Reflection for class " << C->getNameAsString() << "\n\n";

        std::vector<std::string> fieldInfos;

        for (const FieldDecl* F : C->fields()) {
            for (auto *attr : F->attrs()) {
                if (const auto *aa = dyn_cast<AnnotateAttr>(attr)) {
                    std::string annotation = aa->getAnnotation().str();
                    if (annotation.find("field") == 0) {
                        std::string fieldCode = handleField(F, SM);
                        if (!fieldCode.empty()) {
                            fieldInfos.push_back(C->getNameAsString() + "_" + F->getNameAsString() + "_info");
                            out << fieldCode;
                        }
                    }
                }
            }
        }

        out << "inline ClassDescriptor "
            << C->getQualifiedNameAsString() << "::descriptor = {\n"
            << "    \"" << C->getNameAsString() << "\",\n    {\n";

        for (auto& f : fieldInfos)
            out << "        &" << f << ",\n";

        out << "    }\n};\n";

        return out.str();
    }

    std::string handleStruct(const CXXRecordDecl* S, const clang::SourceManager &SM){
        if (!S || !S->isThisDeclarationADefinition())
            return {};

        std::ostringstream out;

        out << "// Reflection for struct " << S->getNameAsString() << "\n\n";

        std::vector<std::string> fieldInfos;

        for (const FieldDecl* F : S->fields()) {
            for (auto *attr : F->attrs()) {
                if (const auto *aa = dyn_cast<AnnotateAttr>(attr)) {
                    std::string annotation = aa->getAnnotation().str();
                    if (annotation.find("field") == 0) {
                        std::string fieldCode = handleField(F, SM);
                        if (!fieldCode.empty()) {
                            fieldInfos.push_back(S->getNameAsString() + "_" + F->getNameAsString() + "_info");
                            out << fieldCode;
                        }
                    }
                }
            }
        }

        std::string structName = S->getNameAsString();
        std::string structQualifiedName = S->getQualifiedNameAsString();

        out << "inline StructDescriptor "
            << structName << "_descriptor = {\n"
            << "    \"" << structName << "\",\n"
            << "    {\n";

        for (auto& f : fieldInfos)
            out << "        &" << f << ",\n";

        out << "    },\n"
            << "    sizeof(" << structQualifiedName << "),\n"
            << "    [](void* p) { new (p) " << structQualifiedName << "(); },\n"
            << "    [](void* p) { static_cast<" << structQualifiedName << "*>(p)->~"
            << structName << "(); },\n"
            << "    [](void* d, const void* s) {\n"
            << "        *static_cast<" << structQualifiedName << "*>(d) =\n"
            << "        *static_cast<const " << structQualifiedName << "*>(s);\n"
            << "    },\n"
            << "    [](const void* a, const void* b) {\n"
            << "        return *static_cast<const " << structQualifiedName << "*>(a)\n"
            << "            == *static_cast<const " <<structQualifiedName<<"*>(b);\n"
            << "    }\n"
            << "};\n";

        return out.str();
    }

    std::string handleField(const FieldDecl* F, const clang::SourceManager &SM){
        
        if (!F || !SM.isWrittenInMainFile(F->getLocation()))
            return {};

        std::ostringstream out;

        const auto* Parent = dyn_cast<CXXRecordDecl>(F->getParent());
        if (!Parent)
            return {};

        // ---------- parse annotations ----------
        bool isEditable = false;
        bool isReadOnly = false;

        std::string readFunc, writeFunc, copyFunc, equalsFunc;
        std::string range;

        for (const Attr* attr : F->attrs()) {
            if (const auto* aa = dyn_cast<AnnotateAttr>(attr)) {
                if (aa->getAnnotation() == "field")
                    continue; // skip kind tag

                std::istringstream ss(aa->getAnnotation().str());
                std::string token;
                while (std::getline(ss, token, ',')) {
                    token.erase(0, token.find_first_not_of(" \t"));
                    token.erase(token.find_last_not_of(" \t") + 1);

                    if (token == "Editable") isEditable = true;
                    else if (token == "ReadOnly") isReadOnly = true;
                    else if (token.find("read=") == 0)   readFunc   = token.substr(5);
                    else if (token.find("write=") == 0)  writeFunc  = token.substr(6);
                    else if (token.find("copy=") == 0)   copyFunc   = token.substr(5);
                    else if (token.find("equals=") == 0) equalsFunc = token.substr(7);
                    else if (token.find("range=") == 0) {
                        std::string r = token.substr(6);
                        auto pos = r.find('|');
                        if (pos != std::string::npos) {
                            std::string min = r.substr(0, pos);
                            std::string max = r.substr(pos + 1);
                            range = min + ", " + max;
                        }
                    }
                }
            }
        }

        if (isEditable == isReadOnly)
            return {}; // invalid or irrelevant

        // ---------- type inspection ----------
        ASTContext& Ctx = F->getASTContext();
        PrintingPolicy policy(Ctx.getLangOpts());
        policy.SuppressTagKeyword = true;

        QualType fieldType = F->getType();
        std::string typeName = fieldType.getAsString(policy);

        bool isVector = typeName.find("std::vector") != std::string::npos;
        bool isMap    = typeName.find("std::map")    != std::string::npos;
        bool isEnum   = fieldType->isEnumeralType();
        bool isStruct = false;
        if (const RecordType* RT = fieldType->getAs<RecordType>()) {
            const CXXRecordDecl* RD = dyn_cast<CXXRecordDecl>(RT->getDecl());
            if (RD && RD->getNameAsString() == "InstancedStruct") {
                isStruct = true;
            }
        }
        const std::string structDescVar = fieldType.getAsString() + "_descriptor";

        std::string fieldName = F->getNameAsString();
        std::string containerVar = Parent->getNameAsString() + "_" + fieldName + "_container";
        std::string enumDescVar;

        // ---------- container / enum generation ----------
        if (isVector) {
            std::string inner =
                getInnerTypeFromStdVector(fieldType, Ctx).getAsString(policy);

            out << "static Container " << containerVar
                << " = MakeVectorContainer<"
                << inner << ">();\n\n";
            
            typeName = "std::vector<";
        }
        else if (isMap) {
            auto [K, V] = getKeyValueTypeFromStdMap(fieldType, Ctx);

            out << "static Container " << containerVar
                << " = MakeMapContainer<"
                << K.getAsString(policy) << "," << V.getAsString(policy)<< ");\n\n";
            
            typeName = "std::map<";
        }
        else if (isEnum) {

            const EnumDecl* E = fieldType->getAs<EnumType>()->getDecl();
            enumDescVar = E->getNameAsString() + "_descriptor";

            out << "static EnumDescriptor " << enumDescVar << " = {\n";
            out << "    \"" << E->getNameAsString() << "\",\n    {\n";
            for (const auto* it : E->enumerators()) {
                out << "        { " << it->getInitVal().getSExtValue()
                    << ", ""\"" << it->getNameAsString() << "\" },\n";
            }
            out << "    },\n";
            out << "    sizeof(" << fieldType.getAsString(policy) << ")\n";
            out << "};\n\n";

            typeName = "enum";
        }
        else if(isStruct){
            
            typeName = "struct";
        }

        // ---------- FieldInfo ----------

        out << "inline FieldInfo "
            << Parent->getNameAsString() << "_" << fieldName << "_info = {\n"
            << "    \"" << fieldName << "\",\n"
            << "    TypeID::"
            << GetStringFromTypeID(GetTypeIDFromString(typeName))
            << ",\n"
            << "    offsetof(" 
            << Parent->getQualifiedNameAsString() 
            << ", " 
            << fieldName 
            << "),\n"
            << "    " << (isEditable ? "Editable" : "ReadOnly") << ",\n"
            << "    " << (range.empty() ? "0, 0" : range) << ",\n"
            << "    " << ((isVector || isMap) ? "&" + containerVar : "nullptr") << ",\n"
            << "    " << (isEnum ? "&" + enumDescVar : "nullptr") << ",\n"
            << "    " << "&CopyConstruct<" << fieldType.getAsString(policy) << ">,\n"
            << "    " << "&Assign<" << fieldType.getAsString(policy) << ">,\n"
            << "    " << "&Destroy<" << fieldType.getAsString(policy) << ">,\n"
            << "    " << "&Equals<" << fieldType.getAsString(policy) << ">\n"
            << "};\n\n";

        return out.str();
    }
};