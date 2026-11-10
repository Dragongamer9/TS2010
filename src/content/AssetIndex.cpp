#include "AssetIndex.h"

#include "ContentError.h"
#include "ReadOnlyFile.h"

#include <algorithm>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>

namespace trainz::content {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kMaxWarnings = 500;
constexpr std::uintmax_t kMaxLooseConfigSize = 16u << 20;
constexpr int kMaxFolderDepth = 6;

std::string lower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return s;
}

// "a/b/config.txt" -> folder "a/b", base "config.txt"
std::pair<std::string, std::string> splitPath(const std::string& p) {
    const auto slash = p.rfind('/');
    if (slash == std::string::npos) {
        return {"", p};
    }
    return {p.substr(0, slash), p.substr(slash + 1)};
}

bool isConfigName(const std::string& base, bool& isChump) {
    const std::string b = lower(base);
    if (b == "config.txt") {
        isChump = false;
        return true;
    }
    if (b == "config.chump") {
        isChump = true;
        return true;
    }
    return false;
}

std::string lastPart(const std::string& folder) {
    const auto slash = folder.rfind('/');
    return slash == std::string::npos ? folder : folder.substr(slash + 1);
}

// Finds a direct child of dir by name, ignoring ASCII case (Windows installs copied to Linux keep any case).
std::optional<fs::path> childNoCase(const fs::path& dir, const std::string& name) {
    std::error_code ec;
    const std::string want = lower(name);
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        if (lower(it->path().filename().string()) == want) {
            return it->path();
        }
    }
    return std::nullopt;
}

}  // namespace

std::vector<std::pair<std::string, std::string>> selectAssetConfigs(const std::vector<std::string>& paths,
                                                                    std::vector<std::string>* notes) {
    struct Seen {
        std::string txtName;
        std::string chumpName;
    };
    std::map<std::string, Seen> folders;
    for (const auto& p : paths) {
        auto [folder, base] = splitPath(p);
        bool isChump = false;
        if (!isConfigName(base, isChump)) {
            continue;
        }
        auto& seen = folders[folder];
        (isChump ? seen.chumpName : seen.txtName) = base;
    }
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& [folder, seen] : folders) {
        // A config inside another config's folder belongs to that asset (a sub-part).
        bool nested = false;
        if (!folder.empty()) {
            if (folders.count("")) {
                nested = true;
            }
            for (std::size_t pos = folder.find('/'); !nested && pos != std::string::npos;
                 pos = folder.find('/', pos + 1)) {
                if (folders.count(folder.substr(0, pos))) {
                    nested = true;
                }
            }
        }
        if (nested) {
            continue;
        }
        if (!seen.chumpName.empty() && !seen.txtName.empty() && notes) {
            notes->push_back("'" + folder + "' has both config.txt and config.chump; using config.chump");
        }
        out.emplace_back(folder, seen.chumpName.empty() ? seen.txtName : seen.chumpName);
    }
    return out;
}

void AssetIndex::warn(std::string message) {
    ++warningCount_;
    if (warnings_.size() < kMaxWarnings) {
        warnings_.push_back(std::move(message));
    } else if (warnings_.size() == kMaxWarnings) {
        warnings_.push_back("(further warnings not shown)");
    }
}

void AssetIndex::addRecord(AssetRecord rec, const std::string& folderName) {
    if (auto named = parseKuid(folderName)) {
        if (named->normalized() != rec.kuid) {
            warn("'" + rec.where.source.filename().string() + ":" + rec.where.folder + "': folder says " +
                 formatKuid(*named) + " but its config says " + formatKuid(rec.kuid) + "; using the config");
        }
    }
    byUserContent_.emplace(std::make_pair(rec.kuid.user, rec.kuid.content), records_.size());
    records_.push_back(std::move(rec));
}

namespace {

AssetRecord recordFromConfig(const AssetConfig& cfg, AssetLocation where, bool& ok, std::string& why) {
    AssetRecord rec;
    rec.where = std::move(where);
    const auto k = cfg.assetKuid();
    if (!k) {
        ok = false;
        why = "no readable top-level kuid";
        return rec;
    }
    rec.kuid = k->normalized();
    if (const ConfigNode* n = cfg.root.child("kind"); n && n->type == ConfigType::String) {
        rec.kind = n->text;
    }
    if (const ConfigNode* n = cfg.root.child("username"); n && n->type == ConfigType::String) {
        rec.username = n->text;
    }
    if (const ConfigNode* t = cfg.root.child("obsolete-table"); t && t->type == ConfigType::Group) {
        for (const auto& c : t->children) {
            if (auto ok2 = c.asKuid()) {
                rec.obsoletes.push_back(ok2->normalized());
            }
        }
    }
    ok = true;
    return rec;
}

}  // namespace

void AssetIndex::addArchive(const JaArchive& archive) {
    std::vector<std::string> names;
    std::unordered_map<std::string, const JaEntry*> byName;
    for (const auto& e : archive.entries()) {
        if (archive.isLookupWinner(e)) {
            names.push_back(e.name);
            byName.emplace(e.name, &e);
        }
    }
    std::vector<std::string> notes;
    auto chosen = selectAssetConfigs(names, &notes);
    for (auto& n : notes) {
        warn(archive.path().filename().string() + ": " + n);
    }

    struct Job {
        const JaEntry* entry;
        std::string folder;
        std::string name;
    };
    std::vector<Job> jobs;
    for (auto& [folder, name] : chosen) {
        const std::string full = folder.empty() ? name : folder + "/" + name;
        if (auto it = byName.find(full); it != byName.end()) {
            jobs.push_back({it->second, folder, name});
        }
    }
    // Read in file order so the hard drive moves forward only.
    std::sort(jobs.begin(), jobs.end(),
              [](const Job& a, const Job& b) { return a.entry->dataOffset < b.entry->dataOffset; });

    for (const auto& job : jobs) {
        AssetLocation where{AssetLocation::Container::Archive, archive.path(), job.folder, job.name};
        try {
            const AssetConfig cfg = parseAssetConfig(archive.read(*job.entry));
            bool ok = false;
            std::string why;
            AssetRecord rec = recordFromConfig(cfg, std::move(where), ok, why);
            if (!ok) {
                warn(archive.path().filename().string() + ":" + job.entry->name + ": " + why);
                continue;
            }
            addRecord(std::move(rec), lastPart(job.folder));
        } catch (const ContentError& err) {
            warn(archive.path().filename().string() + ":" + job.entry->name + ": " + err.what());
        }
    }
}

void AssetIndex::addFolder(const fs::path& root) {
    std::vector<std::string> rels;
    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        warn("cannot read folder '" + root.string() + "': " + ec.message());
        return;
    }
    for (const fs::recursive_directory_iterator end; it != end; it.increment(ec)) {
        if (ec) {
            warn("while reading '" + root.string() + "': " + ec.message());
            ec.clear();
            continue;
        }
        if (it.depth() >= kMaxFolderDepth) {
            it.disable_recursion_pending();
        }
        std::error_code fec;
        if (!it->is_regular_file(fec)) {
            continue;
        }
        bool isChump = false;
        if (isConfigName(it->path().filename().string(), isChump)) {
            rels.push_back(it->path().lexically_relative(root).generic_string());
        }
    }
    std::sort(rels.begin(), rels.end());
    std::vector<std::string> notes;
    const auto chosen = selectAssetConfigs(rels, &notes);
    for (auto& n : notes) {
        warn(root.filename().string() + ": " + n);
    }
    for (const auto& [folder, name] : chosen) {
        const fs::path file = folder.empty() ? root / name : root / fs::path(folder) / name;
        AssetLocation where{AssetLocation::Container::Folder, root, folder, name};
        try {
            const auto f = ReadOnlyFile::open(file);
            if (f.size() > kMaxLooseConfigSize) {
                throw ContentError("config is larger than 16 MiB");
            }
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(f.size()));
            f.readExact(0, bytes);
            const AssetConfig cfg = parseAssetConfig(bytes);
            bool ok = false;
            std::string why;
            AssetRecord rec = recordFromConfig(cfg, std::move(where), ok, why);
            if (!ok) {
                warn(file.string() + ": " + why);
                continue;
            }
            addRecord(std::move(rec), lastPart(folder));
        } catch (const ContentError& err) {
            warn(file.string() + ": " + err.what());
        }
    }
}

AssetIndex AssetIndex::scanInstall(const fs::path& installRoot) {
    AssetIndex index;
    auto addArchiveAt = [&](const fs::path& p) {
        try {
            index.addArchive(JaArchive::open(p));
        } catch (const ContentError& err) {
            index.warn(err.what());
        }
    };
    for (const char* name : {"Base.ja", "Data.ja"}) {
        if (auto p = childNoCase(installRoot, name)) {
            addArchiveAt(*p);
        } else {
            index.warn(std::string(name) + " not found in '" + installRoot.string() + "'");
        }
    }
    if (auto builtin = childNoCase(installRoot, "Builtin")) {
        std::vector<fs::path> jas;
        std::error_code ec;
        for (fs::directory_iterator it(*builtin, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (lower(it->path().extension().string()) == ".ja") {
                jas.push_back(it->path());
            }
        }
        std::sort(jas.begin(), jas.end());
        for (const auto& p : jas) {
            addArchiveAt(p);
        }
    } else {
        index.warn("no Builtin folder in '" + installRoot.string() + "'");
    }
    if (auto userData = childNoCase(installRoot, "UserData")) {
        if (auto local = childNoCase(*userData, "local")) {
            index.addFolder(*local);
        }
    }
    return index;
}

std::vector<const AssetRecord*> AssetIndex::find(const Kuid& kuid) const {
    const Kuid k = kuid.normalized();
    std::vector<const AssetRecord*> out;
    auto [first, last] = byUserContent_.equal_range({k.user, k.content});
    for (auto it = first; it != last; ++it) {
        if (records_[it->second].kuid == k) {
            out.push_back(&records_[it->second]);
        }
    }
    return out;
}

std::vector<const AssetRecord*> AssetIndex::allVersions(std::int32_t user, std::int32_t content) const {
    std::vector<const AssetRecord*> out;
    auto [first, last] = byUserContent_.equal_range({user, content});
    for (auto it = first; it != last; ++it) {
        out.push_back(&records_[it->second]);
    }
    std::stable_sort(out.begin(), out.end(), [](const AssetRecord* a, const AssetRecord* b) {
        return a->kuid.version.value_or(0) < b->kuid.version.value_or(0);
    });
    return out;
}

std::vector<const AssetRecord*> AssetIndex::obsoletedBy(const Kuid& kuid) const {
    const Kuid k = kuid.normalized();
    std::vector<const AssetRecord*> out;
    for (const auto& r : records_) {
        if (std::find(r.obsoletes.begin(), r.obsoletes.end(), k) != r.obsoletes.end()) {
            out.push_back(&r);
        }
    }
    return out;
}

}  // namespace trainz::content
