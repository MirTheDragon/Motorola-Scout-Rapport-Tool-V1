#include "docx_report.h"
#include "tinyxml2.h"
#include <zipper/zipper.h>
#include <zipper/unzipper.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>

using namespace tinyxml2;
namespace fs = std::filesystem;

static bool unzip(const fs::path& in, const fs::path& out)
{
    try {
        zipper::Unzipper unzip(in.string());
        unzip.extract(out.string());
        unzip.close();
        return true;
    }
    catch (...) {
        return false;
    }
}

static bool rezip(const fs::path& in, const fs::path& out)
{
    try {
        zipper::Zipper zip(out.string());
        zip.add(in.string());
        zip.close();
        return true;
    }
    catch (...) {
        return false;
    }
}

// Replace {{HEADER}} / {{DESCRIPTION}} tokens
static void replaceTokens(XMLNode* node,
    const std::string& key,
    const std::string& val)
{
    for (auto* e = node->FirstChildElement(); e; e = e->NextSiblingElement())
    {
        if (std::string(e->Name()) == "w:t")
        {
            const char* t = e->GetText();
            if (t && key == t)
                e->SetText(val.c_str());
        }
        replaceTokens(e, key, val);
    }
}

namespace reportgen
{
    bool generateDocx(const fs::path& templateDocx,
        const fs::path& outputDocx,
        const std::vector<Entry>& entries)
    {
        fs::path tmp = fs::temp_directory_path() / "docx_tmp";
        fs::remove_all(tmp);
        if (!unzip(templateDocx, tmp))
        {
            std::cerr << "Failed to unzip template\n";
            return false;
        }

        // Parse main document
        fs::path docXml = tmp / "word/document.xml";
        XMLDocument doc;
        if (doc.LoadFile(docXml.string().c_str()) != XML_SUCCESS)
        {
            std::cerr << "Failed to open document.xml\n";
            return false;
        }

        // Grab <w:body>
        auto* body = doc.FirstChildElement("w:document")->FirstChildElement("w:body");
        if (!body) { std::cerr << "No <w:body>\n"; return false; }

        // Save first paragraph as template block (page pattern)
        XMLNode* templateBlock = body->FirstChild()->DeepClone(&doc);

        // remove everything else
        while (body->FirstChild())
            body->DeleteChild(body->FirstChild());

        int relId = 10; // start arbitrary id numbering
        for (auto& e : entries)
        {
            XMLNode* page = templateBlock->DeepClone(&doc);

            replaceTokens(page, "{{HEADER}}", e.header);
            replaceTokens(page, "{{DESCRIPTION}}", e.description);

            // Copy image into media/
            fs::path mediaDir = tmp / "word/media";
            fs::create_directories(mediaDir);
            std::string imgName = "image" + std::to_string(relId) + e.imagePath.extension().string();
            fs::copy_file(e.imagePath, mediaDir / imgName,
                fs::copy_options::overwrite_existing);

            // Update the rels
            fs::path relsXml = tmp / "word/_rels/document.xml.rels";
            XMLDocument rels;
            rels.LoadFile(relsXml.string().c_str());
            auto* root = rels.FirstChildElement("Relationships");
            auto* newRel = rels.NewElement("Relationship");
            std::string rId = "rId" + std::to_string(relId);
            newRel->SetAttribute("Id", rId.c_str());
            newRel->SetAttribute("Type",
                "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image");
            newRel->SetAttribute("Target", ("media/" + imgName).c_str());
            root->InsertEndChild(newRel);
            rels.SaveFile(relsXml.string().c_str());

            // Replace the old rId in drawing tag (assumes one image per page pattern)
            for (auto* e2 = page->FirstChildElement(); e2; e2 = e2->NextSiblingElement())
            {
                if (std::string(e2->Name()).find("a:blip") != std::string::npos)
                    e2->SetAttribute("r:embed", rId.c_str());
            }

            body->InsertEndChild(page);

            // Add a page break
            XMLElement* br = doc.NewElement("w:p");
            XMLElement* run = doc.NewElement("w:r");
            XMLElement* breakTag = doc.NewElement("w:br");
            breakTag->SetAttribute("w:type", "page");
            run->InsertEndChild(breakTag);
            br->InsertEndChild(run);
            body->InsertEndChild(br);

            ++relId;
        }

        doc.SaveFile(docXml.string().c_str());
        rezip(tmp, outputDocx);
        fs::remove_all(tmp);
        return true;
    }
}
