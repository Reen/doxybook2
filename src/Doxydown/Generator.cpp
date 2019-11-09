#include <fstream>
#include <Doxydown/Generator.hpp>
#include <Doxydown/Doxygen.hpp>
#include <Doxydown/Renderer.hpp>
#include <Doxydown/Path.hpp>
#include <Doxydown/TemplateLoader.hpp>
#include <Doxydown/Exception.hpp>
#include <Doxydown/Utils.hpp>
#include "ExceptionUtils.hpp"


std::string Doxydown::Generator::kindToTemplateName(const Kind kind) {
    using namespace Doxydown;
    switch (kind) {
        case Kind::STRUCT:
            return config.templateKindStruct;
        case Kind::INTERFACE:
            return config.templateKindInterface;
        case Kind::UNION:
            return config.templateKindUnion;
        case Kind::CLASS:
            return config.templateKindClass;
        case Kind::NAMESPACE:
            return config.templateKindNamespace;
        case Kind::MODULE:
            return config.templateKindGroup;
        case Kind::DIR:
            return config.templateKindDir;
        case Kind::FILE:
            return config.templateKindFile;
        case Kind::PAGE:
            return config.templateKindPage;
        default: {
            throw EXCEPTION("Unrecognised kind {} please contant the author!", int(kind));
        }
    }
}

Doxydown::Generator::Generator(const Config& config, const JsonConverter& jsonConverter, const TemplateLoader& templateLoader)
    : config(config),
      jsonConverter(jsonConverter),
      renderer(config) {

    for (const auto& pair : templateLoader.getTemplates()) {
        renderer.addTemplate(pair.name, pair.contents);
    }
}

void Doxydown::Generator::summary(const Doxygen& doxygen,
                                  const std::string& inputFile,
                                  const std::string& outputFile,
                                  const std::vector<SummarySection>& sections) {

    std::ifstream input(inputFile);
    if (!input) {
        throw EXCEPTION("File {} failed to open for reading", inputFile);
    }

    std::ofstream output(outputFile);
    if (!output) {
        throw EXCEPTION("File {} failed to open for writing", outputFile);
    }

    static const auto compare = [](const char* a, const char* b) {
        while (*a && *b) {
            if (*a++ != *b++) return false;
        }
        return true;
    };

    std::string tmpl((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    auto offset = tmpl.size();
    size_t indent = 0;
    for (size_t i = 0; i < tmpl.size(); i++) {
        const auto& c = tmpl[i];
        if (compare(&tmpl[i], "{{doxygen}}")) {
            offset = i;
            break;
        }
        if (c == ' ') indent++;
        else indent = 0;
    }

    std::stringstream ss;

    for (const auto& section : sections) {
        const auto name = typeToIndexTitle(config, section.type);
        const auto path = typeToIndexName(config, section.type) + "." + config.fileExt;
        ss << std::string(indent, ' ') << "* [" << name << "](" << path << ")\n";
        summaryRecursive(ss, indent + 2, name, doxygen.getIndex(), section.filter);
    }

    output << tmpl.substr(0, offset);

    output << ss.str().substr(indent);

    if (offset + ::strlen("{{doxygen}}") < tmpl.size()) {
        output << tmpl.substr(offset + ::strlen("{{doxygen}}"));
    }
}

void Doxydown::Generator::summaryRecursive(std::stringstream& ss,
                                           const int indent,
                                           const std::string& folderName,
                                           const Node& node,
                                           const Filter& filter) {

    for (const auto& child : node.getChildren()) {
        if (filter.find(child->getKind()) != filter.end()) {
            ss << std::string(indent, ' ') << "* [" << child->getName() << "](" << folderName << "/" << child->getRefid() << ".md)\n";
            summaryRecursive(ss, indent, folderName, *child, filter);
        }
    }
}

void Doxydown::Generator::printRecursively(const Node& parent, const Filter& filter) {
    for (const auto& child : parent.getChildren()) {
        if (filter.find(child->getKind()) != filter.end()) {
            nlohmann::json data = jsonConverter.getAsJson(*child);

            std::string path;
            if (child->getKind() == Kind::PAGE && child->getRefid() == config.mainPageName) {
                path = child->getRefid() + "." + config.fileExt;
            } else {
                path = Path::join(
                    typeToFolderName(config, child->getType()),
                    child->getRefid() + "." + config.fileExt
                );
            }

            renderer.render(
                kindToTemplateName(child->getKind()),
                path,
                data
            );
            printRecursively(*child, filter);
        }
    }
}

void Doxydown::Generator::print(const Doxygen& doxygen, const Filter& filter) {
    printRecursively(doxygen.getIndex(), filter);
}

void Doxydown::Generator::printIndex(const Doxygen& doxygen, const FolderCategory type, const Filter& filter) {
    const auto path = typeToIndexName(config, type) + "." + config.fileExt;

    nlohmann::json data;
    data["children"] = buildIndexRecursively(doxygen.getIndex(), filter);
    data["title"] = typeToIndexTitle(config, type);
    data["name"] = typeToIndexTitle(config, type);
    renderer.render(typeToIndexTemplate(config, type), path, data);
}

nlohmann::json Doxydown::Generator::buildIndexRecursively(const Node& node, const Filter& filter) {
    auto json = nlohmann::json::array();
    std::vector<const Node*> sorted;
    sorted.reserve(node.getChildren().size());

    for (const auto& child : node.getChildren()) {
        if (filter.find(child->getKind()) != filter.end()) {
            sorted.push_back(child.get());
        }
    }

    std::sort(sorted.begin(), sorted.end(), [](const Node* a, const Node* b) -> bool {
        return a->getName() < b->getName();
    });

    for (const auto& child : sorted) {
        auto data = jsonConverter.convert(*child);

        auto test = buildIndexRecursively(*child, filter);
        if (!test.empty()) {
            data["children"] = std::move(test);
        }

        json.push_back(std::move(data));
    }

    return json;
}
