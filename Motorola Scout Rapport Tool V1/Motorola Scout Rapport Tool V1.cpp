// img_modulate_tool.cpp
// Build: C++17, Visual Studio (Windows). Uses Magick++ (ImageMagick C++ API).
// Functionality: Walk the EXE's folder recursively, list folders + PNG counts,
// ask whether to apply ImageMagick modulate (default 75,125,100), optionally add a hue
// indicator PNG to an edge, saving results next to sources.

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <algorithm>
#include <cctype>
#ifdef _WIN32
#include <windows.h>
#endif
#include <Magick++.h>

namespace fs = std::filesystem;

static void print_header()
{
    std::cout << "\n\n";
    std::cout << "  __  __       _                  _          _____                 _     _____                              _     _______          _  __      ____  \n";
    std::cout << " |  \\  |     | |                | |        / ____|               | |   |  __ \\                            | |   |__   __|        | | \\ \\    / /_ |\n";
    std::cout << " | \\  / | ___ | |_ ___  _ __ ___ | | __ _  | (___   ___ ___  _   _| |_  | |__) |__ _ _ __  _ __   ___  _ __| |_     | | ___   ___ | |  \\ \\  / / | |\n";
    std::cout << " | |\\/| |/ _ \\| __/ _ \\| '__/ _ \\| |/ _` |  \\___ \\ / __/ _ \\| | | | __| |  _  // _` | '_ \\| '_ \\ / _ \\\n";
    std::cout << " | |  | | (_) | || (_) | | | (_) | | (_| |  ____) | (_| (_) | |_| | |_  | | \\ \\ (_| | |_) | |_) | (_) | |  | |_     | | (_) | (_) | |    \\  /   | |\n";
    std::cout << " |_|  |_|\\___/ \\__\\___/|_|  \\___/|_|\\__,_| |_____/ \\___\\___/ \\__,_|\\__| |_|  \\_\\__,_| .__/| .__/ \\___/|_|   \\__|    |_|\\___/ \\___/|_|     \\/    |_|\n";
    std::cout << "                                                                                     | |   | |                                                      \n";
    std::cout << "                                                                                     |_|   |_|                                                      \n";
    std::cout << "\n\n";
}

#ifdef _WIN32
static fs::path exe_dir()
{
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0) {
        return fs::current_path();
    }
    fs::path p(buf);
    return p.parent_path();
}
#else
static fs::path exe_dir() { return fs::current_path(); }
#endif

static bool iequals(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
            return false;
    return true;
}

static bool has_png_ext(const fs::path& p)
{
    auto ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return (ext == ".png");
}

struct ModulateParams { double brightness = 75, saturation = 125, hue = 100; };

static bool parse_modulate_triplet(const std::string& s, ModulateParams& out)
{
    std::string t = s;
    for (char& c : t) if (c == ',') c = ' ';
    double b = 0, sat = 0, h = 0;
    if (sscanf(t.c_str(), "%lf %lf %lf", &b, &sat, &h) == 3) {
        if (b < 0 || sat < 0 || h < 0) return false;
        out.brightness = b; out.saturation = sat; out.hue = h;
        return true;
    }
    return false;
}

static std::string ask(const std::string& prompt)
{
    std::cout << prompt;
    std::cout.flush();
    std::string line;
    std::getline(std::cin, line);
    return line;
}

static bool yesno(const std::string& prompt, bool def = false)
{
    std::string line = ask(prompt + (def ? " [Y/n] " : " [y/N] "));
    if (line.empty()) return def;
    char c = (char)std::tolower((unsigned char)line[0]);
    return c == 'y' || c == '1' || iequals(line, "yes");
}

static void list_folders_and_pngs(const fs::path& root, std::vector<fs::path>& pngs)
{
    std::map<fs::path, int> counts;
    for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it) {
        if (!it->is_regular_file()) continue;
        const fs::path& p = it->path();
        if (has_png_ext(p)) {
            counts[p.parent_path()] += 1;
            pngs.push_back(p);
        }
    }

    std::cout << "\nFolders discovered and PNG counts:" << std::endl;
    for (const auto& kv : counts) {
        std::cout << "  - " << kv.first.string() << "  (" << kv.second << " PNG)" << std::endl;
    }
    std::cout << "\nTotal PNG files: " << pngs.size() << "\n\n";
}

static fs::path output_grey_name(const fs::path& in)
{
    fs::path out = in;
    out.replace_filename(in.stem().string() + "_GreyFilter" + in.extension().string());
    return out;
}

static fs::path output_scaled_name(const fs::path& grey)
{
    fs::path out = grey;
    out.replace_filename(grey.stem().string() + "_WithScale" + grey.extension().string());
    return out;
}

static bool apply_modulate_to_image(const fs::path& inPath, const fs::path& outPath, const ModulateParams& mp)
{
    try {
        Magick::Image img;
        img.read(inPath.string());
        img.modulate(mp.brightness, mp.saturation, mp.hue);
        img.write(outPath.string());
        std::cout << "Processed: " << outPath.string() << std::endl;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "ERROR (modulate): " << inPath.string() << " -> " << e.what() << std::endl;
        return false;
    }
}

enum class Edge { Top, Right, Bottom, Left };

static Edge parse_edge(const std::string& s)
{
    if (s.size() == 0) return Edge::Right;
    char c = (char)std::tolower((unsigned char)s[0]);
    switch (c) {
    case 't': return Edge::Top;
    case 'r': return Edge::Right;
    case 'b': return Edge::Bottom;
    case 'l': return Edge::Left;
    default:  return Edge::Right;
    }
}

static bool composite_scale_on_edge(const fs::path& baseGrey,
    const fs::path& scalePng,
    const fs::path& outPath,
    const ModulateParams& mp,
    Edge edge,
    int percent)
{
    try {
        // Read base (the canvas we preserve 1:1) and the overlay scale
        Magick::Image base; base.read(baseGrey.string());
        Magick::Image scale; scale.read(scalePng.string());


        // Apply the SAME modulate so the indicator colors match processed images
        scale.modulate(mp.brightness, mp.saturation, mp.hue);


        const size_t baseW = base.columns();
        const size_t baseH = base.rows();


        // Resize the overlay relative to the relevant base dimension, preserving AR
        if (edge == Edge::Left || edge == Edge::Right) {
            const size_t targetH = std::max<size_t>(1, static_cast<size_t>(baseH * (percent / 100.0)));
            scale.resize(Magick::Geometry(0, targetH)); // height-driven
        }
        else {
            const size_t targetW = std::max<size_t>(1, static_cast<size_t>(baseW * (percent / 100.0)));
            scale.resize(Magick::Geometry(targetW, 0)); // width-driven
        }


        // Compute explicit pixel offsets (overlay ON TOP of the base canvas)
        // Canvas size is unchanged; we only place the overlay with padding.
        const ssize_t pad = 10;
        const ssize_t sW = static_cast<ssize_t>(scale.columns());
        const ssize_t sH = static_cast<ssize_t>(scale.rows());
        const ssize_t bW = static_cast<ssize_t>(baseW);
        const ssize_t bH = static_cast<ssize_t>(baseH);


        ssize_t x = 0, y = 0;
        switch (edge) {
        case Edge::Right:
            x = std::max<ssize_t>(0, bW - sW - pad);
            y = std::max<ssize_t>(0, (bH - sH) / 2);
            break;
        case Edge::Left:
            x = pad;
            y = std::max<ssize_t>(0, (bH - sH) / 2);
            break;
        case Edge::Top:
            x = std::max<ssize_t>(0, (bW - sW) / 2);
            y = pad;
            break;
        case Edge::Bottom:
            x = std::max<ssize_t>(0, (bW - sW) / 2);
            y = std::max<ssize_t>(0, bH - sH - pad);
            break;
        }


        // Composite overlay onto the original-sized canvas without extending it
        base.composite(scale, x, y, Magick::OverCompositeOp);


        // Write out. Base canvas size/scale preserved.
        base.write(outPath.string());
        std::cout << "Scaled+Composited: " << outPath.string() << std::endl;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "ERROR (composite): " << baseGrey.string() << " -> " << e.what() << std::endl;
        return false;
    }
}

int main(int argc, char** argv)
{
    Magick::InitializeMagick(*argv);
    print_header();

    fs::path root = exe_dir();
    std::cout << "Working root: " << root.string() << "\n";

    std::vector<fs::path> pngs;
    list_folders_and_pngs(root, pngs);
    if (pngs.empty()) {
        std::cout << "No PNG files were found. Press Enter to exit..." << std::endl;
        std::string dummy; std::getline(std::cin, dummy);
        return 0;
    }

    // Step 1: Ask whether to apply modulate filter
    std::cout << "The ImageMagick modulate filter darkens the background and makes thin, vibrant lines more defined." << std::endl;
    bool doMod = yesno("Do you want to apply the modulate filter to all PNGs?", true);
    ModulateParams mp; // defaults 75,125,100
    if (doMod) {
        bool useDefault = yesno("Use standard values 75,125,100?", true);
        if (!useDefault) {
            while (true) {
                std::string s = ask("Enter modulate triplet as brightness,saturation,hue (e.g. 110,95,100): ");
                if (parse_modulate_triplet(s, mp)) break;
                std::cout << "Invalid entry. Please try again.\n";
            }
        }
        std::cout << "Using modulate: " << mp.brightness << "," << mp.saturation << "," << mp.hue << "\n";

        for (const auto& p : pngs) {
            fs::path out = output_grey_name(p);
            apply_modulate_to_image(p, out, mp);
        }
    }

    // Step 2: Optional hue indicator overlay on modulated images
    bool doScale = yesno("Add hue value indicator (scale PNG) onto images?", false);
    if (doScale) {
        std::cout << "Note: Place the scale PNG in the TOP folder (same directory as this EXE).\n";
        fs::path scalePath;
        while (true) {
            std::string s = ask("Enter scale PNG filename (in top folder): ");
            if (s.empty()) { std::cout << "Please provide a filename.\n"; continue; }
            scalePath = root / s;
            if (!fs::exists(scalePath)) { std::cout << "Not found: " << scalePath.string() << "\n"; continue; }
            if (!has_png_ext(scalePath)) { std::cout << "File is not a .png. Try again.\n"; continue; }
            break;
        }

        std::string sideStr = ask("Which side to place it? (top/right/left/bottom) [right]: ");
        Edge edge = parse_edge(sideStr);

        bool custom = yesno("Custom scaling percentage? (otherwise use standard)", false);
        int pct = 0;
        if (custom) {
            while (true) {
                std::string s = ask("Enter percentage (e.g. 40 for 40% of edge dimension): ");
                try {
                    int v = std::stoi(s);
                    if (v <= 0 || v > 1000) { std::cout << "Out of range. Try 1-1000.\n"; continue; }
                    pct = v; break;
                }
                catch (...) { std::cout << "Invalid number.\n"; }
            }
        }
        else {
            pct = (edge == Edge::Left || edge == Edge::Right) ? 40 : 15;
            std::cout << "Using standard scale: " << pct << "%\n";
        }

        std::vector<fs::path> greys;
        for (const auto& p : pngs) {
            fs::path g = output_grey_name(p);
            if (fs::exists(g)) greys.push_back(g);
        }

        if (greys.empty()) {
            std::cout << "No _GreyFilter.png outputs found to annotate.\n";
        }
        else {
            for (const auto& g : greys) {
                fs::path out = output_scaled_name(g);
                composite_scale_on_edge(g, scalePath, out, mp, edge, pct);
            }
        }
    }

    std::cout << "\nDone. Press Enter to exit..." << std::endl;
    std::string dummy; std::getline(std::cin, dummy);
    return 0;
}
