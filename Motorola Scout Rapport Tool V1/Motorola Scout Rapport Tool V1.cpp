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
#include <list>
#include <iomanip>  // for std::setprecision in the progress bar

namespace fs = std::filesystem;

#include <sstream>
#include <cstdlib>

// Run magick.exe with given arguments
static bool run_magick(const std::string& args)
{
#ifdef _WIN32
    // Build full command line (convert to wide string)
    std::wstring fullCmd = L"magick.exe " + std::wstring(args.begin(), args.end());

    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);

    if (!CreateProcessW(
        nullptr,                         // Application name
        fullCmd.data(),                  // Command line
        nullptr, nullptr, FALSE, 0,
        nullptr, nullptr, &si, &pi))
    {
        std::wcerr << L"[ERR] Failed to start: " << fullCmd << std::endl;
        return false;
    }

    // Wait for process to finish
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (exitCode == 0);
#else
    // fallback for non-Windows
    std::string cmd = "\"magick.exe\" " + args;
    int ret = std::system(cmd.c_str());
    return (ret == 0);
#endif
}




// Shorten long paths for cleaner console output (e.g., .../file.png)
static std::string short_path(const fs::path& full)
{
    std::string s = full.string();
    const size_t cutoff = 60; // max chars before shortening
    if (s.size() <= cutoff) return s;

    size_t pos = s.find_last_of("\\/");
    if (pos != std::string::npos)
        return std::string(".../") + s.substr(pos + 1);
    return std::string(".../") + s.substr(s.size() - 20);
}


// Returns the directory path of the running executable
static fs::path exe_dir() {
#ifdef _WIN32
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    fs::path exePath(buffer);
    return exePath.parent_path();
#else
    return fs::current_path();
#endif
}



// --- Progress helpers -------------------------------------------------------
static void draw_progress(const char* label, size_t done, size_t total)
{
    if (total == 0) total = 1;
    const double pct = (100.0 * done) / total;
    const int barWidth = 40; // characters
    const int filled = static_cast<int>(barWidth * pct / 100.0);

    std::cout << "\r" << label << " [";
    for (int i = 0; i < filled; ++i)  std::cout << '#';
    for (int i = filled; i < barWidth; ++i) std::cout << '.';
    std::cout << "] " << std::fixed << std::setprecision(1) << pct << "%   ";
    std::cout.flush();
}

static void end_progress_line() {
    std::cout << "\r";           // return to start
    std::cout << std::string(80, ' '); // clear line
    std::cout << "\r";           // return again
}


static void print_header()
{
    std::cout << R"raw(
      __  __       _                  _          _____                 _     _____                              _   _               _______          _  __      ____ 
     |  \/  |     | |                | |        / ____|               | |   |  __ \                            | | (_)             |__   __|        | | \ \    / /_ |
     | \  / | ___ | |_ ___  _ __ ___ | | __ _  | (___   ___ ___  _   _| |_  | |__) |__ _ _ __  _ __   ___  _ __| |_ _ _ __   __ _     | | ___   ___ | |  \ \  / / | |
     | |\/| |/ _ \| __/ _ \| '__/ _ \| |/ _` |  \___ \ / __/ _ \| | | | __| |  _  // _` | '_ \| '_ \ / _ \| '__| __| | '_ \ / _` |    | |/ _ \ / _ \| |   \ \/ /  | |
     | |  | | (_) | || (_) | | | (_) | | (_| |  ____) | (_| (_) | |_| | |_  | | \ \ (_| | |_) | |_) | (_) | |  | |_| | | | | (_| |    | | (_) | (_) | |    \  /   | |
     |_|  |_|\___/ \__\___/|_|  \___/|_|\__,_| |_____/ \___\___/ \__,_|\__| |_|  \_\__,_| .__/| .__/ \___/|_|   \__|_|_| |_|\__, |    |_|\___/ \___/|_|     \/    |_|
                                                                                        | |   | |                            __/ |                                   
                                                                                        |_|   |_|                           |___/                                    
    )raw" << "\n";
}


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

// New: accept any common raster image extension
static bool has_image_ext(const fs::path& p)
{
    auto ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return (ext == ".png" || ext == ".bmp" || ext == ".jpg" || ext == ".jpeg" || ext == ".tif" || ext == ".tiff" || ext == ".webp");
}

struct ModulateParams { double brightness = 75, saturation = 125, hue = 100; };

static bool parse_modulate_triplet(const std::string& s, ModulateParams& out)
{
    std::string t = s;
    for (char& c : t) if (c == ',') c = ' ';
    double b = 0, sat = 0, h = 0;
    if (sscanf_s(t.c_str(), "%lf %lf %lf", &b, &sat, &h) == 3) {
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

        // only .png files
        if (!has_png_ext(p)) continue;

        // skip previously processed images (_GreyFilter or _WithScale)
        std::string stem = p.stem().string();
        if (stem.find("_GreyFilter") != std::string::npos ||
            stem.find("_WithScale") != std::string::npos)
            continue;

        counts[p.parent_path()] += 1;
        pngs.push_back(p);
    }

    std::cout << "\nFolders discovered and PNG counts:" << std::endl;
    for (const auto& kv : counts) {
        std::string shortPath = kv.first.filename().string().empty() ? "..." : (".../" + kv.first.filename().string());
        std::cout << "  - " << shortPath << "  (" << kv.second << " PNG)" << std::endl;
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


// Apply the modulate filter to a single image and print clean progress
static bool apply_modulate_to_image(const fs::path& inPath, const fs::path& outPath,
    const ModulateParams& mp, size_t index, size_t total)
{
    // Build command for ImageMagick
    std::ostringstream ss;
    ss << "\"" << inPath.string() << "\" -modulate "
        << mp.brightness << "," << mp.saturation << "," << mp.hue
        << " \"" << outPath.string() << "\"";

    bool ok = run_magick(ss.str());

    double pct = ((index + 1) * 100.0) / total;
    std::ostringstream line;
    line << "[" << (ok ? "OK " : "ERR") << "] ("
        << std::setw(3) << std::fixed << std::setprecision(0) << pct
        << "%) " << short_path(inPath)
        << " → " << outPath.filename().string();
    std::cout << line.str() << std::endl;

    return ok;

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

static bool composite_scale_on_edge(
    const fs::path& baseGrey,
    const fs::path& legendPath,
    const fs::path& outPath,
    const ModulateParams& mp,
    Edge edge,
    int scalePercent,
    bool cropLegendFirst)
{
    // Temporary resized legend path (stored beside output)
    fs::path tmpLegend = outPath.parent_path() / "_tmp_legend_overlay.png";

    // Step 1: Prepare (trim + modulate + resize) legend into temporary file
    {
        std::ostringstream prep;
        prep << "\"" << legendPath.string() << "\"";
        if (cropLegendFirst)
            prep << " -trim";
        prep << " -modulate " << mp.brightness << "," << mp.saturation << "," << mp.hue
            << " -resize " << scalePercent << "% \"" << tmpLegend.string() << "\"";

        if (!run_magick(prep.str())) {
            std::cerr << "[ERR] Legend prep failed: " << short_path(legendPath) << std::endl;
            return false;
        }
    }

    // Step 2: Composite overlay onto base image
    {
        std::ostringstream comp;
        comp << "\"" << baseGrey.string() << "\" \"" << tmpLegend.string()
            << "\" -gravity ";

        switch (edge) {
        case Edge::Top:    comp << "north"; break;
        case Edge::Bottom: comp << "south"; break;
        case Edge::Left:   comp << "west"; break;
        case Edge::Right:  comp << "east"; break;
        }

        comp << " -composite \"" << outPath.string() << "\"";

        bool ok = run_magick(comp.str());

        // Clean up temp file
        std::error_code ec;
        fs::remove(tmpLegend, ec);

        return ok;
    }
}







static fs::path legend_for_base(const fs::path& basePng)
{
    const auto dir = basePng.parent_path();
    const auto stem = basePng.stem().string();
    fs::path bmp = dir / (stem + "_Legend.bmp");
    fs::path png = dir / (stem + "_Legend.png");
    if (fs::exists(bmp)) return bmp;
    if (fs::exists(png)) return png;
    return {};
}


int main(int argc, char** argv)
{

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

    // Confirm before doing any writes
    if (!yesno("Proceed with processing these files?", true)) {
        std::cout << "Cancelled by user. Press Enter to exit..." << std::endl;
        std::string dummy; std::getline(std::cin, dummy);
        return 0;
    }

    // ------------------ Gather ALL options up front ------------------
    std::cout << "The ImageMagick modulate filter darkens the background and "
        << "makes thin, vibrant lines more defined.\n";

    bool doMod = yesno("Apply the modulate filter to all PNGs?", true);
    ModulateParams mp; // defaults 75,125,100
    if (doMod) {
        bool useDefault = yesno("Use standard values 75,125,100?", true);
        if (!useDefault) {
            while (true) {
                std::string s = ask("Enter brightness,saturation,hue (e.g. 110,95,100): ");
                if (parse_modulate_triplet(s, mp)) break;
                std::cout << "Invalid entry. Please try again.\n";
            }
        }
        std::cout << "Using modulate: " << mp.brightness << "," << mp.saturation << "," << mp.hue << "\n";
    }

    bool doLegend = yesno("Overlay matching *_Legend.(bmp|png) onto images?", true);
    bool cropFirst = false;
    int  legendPct = 500;
    Edge edge = Edge::Right;

    if (doLegend) {
        cropFirst = yesno("Crop legend (trim) before applying modulate?", true);

        bool custom = yesno("Use custom legend scale percentage? (default 500%)", false);
        if (custom) {
            while (true) {
                std::string s = ask("Enter legend scale percentage (1..2000, e.g., 500 for 5x): ");
                try { int v = std::stoi(s); if (v > 0 && v <= 2000) { legendPct = v; break; } }
                catch (...) {}
                std::cout << "Please enter a number between 1 and 2000.\n";
            }
        }
        std::string sideStr = ask("Which side to place the legend? (top/right/left/bottom) [right]: ");
        edge = parse_edge(sideStr);
    }
    // ----------------------------------------------------------------


    // Check if any output files already exist
    bool anyExist = false;
    for (const auto& p : pngs) {
        fs::path out = output_grey_name(p);
        if (fs::exists(out)) {
            anyExist = true;
            break;
        }
    }

    bool allowOverwrite = true;
    if (anyExist) {
        allowOverwrite = yesno("Some output files (e.g. *_GreyFilter.png) already exist. Overwrite them?", false);
        if (!allowOverwrite) {
            std::cout << "Aborted to avoid overwriting existing results. Press Enter to exit..." << std::endl;
            std::string dummy; std::getline(std::cin, dummy);
            return 0;
        }
    }

    // -------------------------- Modulate pass ------------------------
    for (size_t i = 0; i < pngs.size(); ++i) {
        const auto& p = pngs[i];
        fs::path out = output_grey_name(p);

        if (fs::exists(out) && !allowOverwrite) {
            std::cout << "[SKP] .../" << p.filename().string() << " (already processed)" << std::endl;
            continue;
        }

        bool ok = apply_modulate_to_image(p, out, mp, i, pngs.size());

    }


    // ----------------------------------------------------------------

   // -------------------------- Legend pass --------------------------
   // -------------------------- Legend pass --------------------------
    if (doLegend) {
        std::cout << "Applying legends to processed images...\n";

        // Work on all *_GreyFilter.png we produced (or that already exist)
        std::vector<fs::path> greys;
        for (const auto& p : pngs) {
            fs::path g = output_grey_name(p);
            if (fs::exists(g)) greys.push_back(g);
        }

        if (greys.empty()) {
            std::cout << "No _GreyFilter.png outputs found to annotate.\n";
        }
        else {
            const size_t total = greys.size();
            for (size_t i = 0; i < total; ++i) {
                const auto& g = greys[i];

                // Recover base name (strip _GreyFilter)
                const std::string greyStem = g.stem().string();
                const std::string suffix = "_GreyFilter";
                std::string baseStem = greyStem;
                if (greyStem.size() > suffix.size() &&
                    greyStem.rfind(suffix) == greyStem.size() - suffix.size())
                    baseStem = greyStem.substr(0, greyStem.size() - suffix.size());

                fs::path legend = g.parent_path() / (baseStem + "_Legend.bmp");
                if (!fs::exists(legend)) {
                    fs::path alt = g.parent_path() / (baseStem + "_Legend.png");
                    if (fs::exists(alt)) legend = alt; else legend.clear();
                }

                if (legend.empty()) {
                    std::cout << "[MISS] " << short_path(g) << " (no legend found)\n";
                    continue;
                }

                fs::path out = output_scaled_name(g);
                bool ok = composite_scale_on_edge(g, legend, out, mp, edge, legendPct, cropFirst);

                double pct = ((i + 1) * 100.0) / total;
                std::cout << "[" << (ok ? "OK " : "ERR") << "] ("
                    << std::setw(3) << std::fixed << std::setprecision(0) << pct
                    << "%) " << short_path(out) << std::endl;
            }
            std::cout << "Legend overlay complete (" << total << " files).\n";
        }
    }


    // ----------------------------------------------------------------

    std::cout << "\nDone. Press Enter to exit..." << std::endl;
    std::string dummy; std::getline(std::cin, dummy);
    return 0;
}
