#pragma once
#include "../vendor/json.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace pulse {
using json = nlohmann::json;
namespace fs = std::filesystem;
inline const json &field(const json &j, const char *key) {
    static const json nil;
    if (!j.is_object())
        return nil;
    auto i = j.find(key);
    return i == j.end() ? nil : *i;
}
inline std::string str(const json &j, const char *key, std::string fallback = {}) {
    const auto &v = field(j, key);
    return v.is_string() ? v.get<std::string>() : fallback;
}
inline double number(const json &v, double fallback = -1) {
    try {
        double d = fallback;
        if (v.is_number())
            d = v.get<double>();
        else if (v.is_string()) {
            size_t n;
            auto s = v.get<std::string>();
            d = std::stod(s, &n);
            if (n != s.size())
                return fallback;
        }
        return std::isfinite(d) ? d : fallback;
    } catch (...) {
        return fallback;
    }
}
inline int64_t integer(const json &j, const char *k, int64_t fallback = 0) {
    double d = number(field(j, k), static_cast<double>(fallback));
    return d >= 0 && d < 9e15 ? static_cast<int64_t>(d) : fallback;
}
inline bool boolean(const json &j, const char *k, bool fallback = false) {
    const auto &v = field(j, k);
    return v.is_boolean() ? v.get<bool>() : fallback;
}
inline json parse(const std::string &s) {
    return json::parse(s, nullptr, false);
}
inline std::string readSmall(const fs::path &p, size_t cap = 1024 * 1024) {
    std::ifstream f(p, std::ios::binary);
    if (!f)
        return {};
    std::string s(cap + 1, '\0');
    f.read(s.data(), static_cast<std::streamsize>(s.size()));
    s.resize(static_cast<size_t>(f.gcount()));
    return s.size() > cap ? std::string{} : s;
}
inline std::tm localTime(time_t t) {
    std::tm v{};
#ifdef _WIN32
    localtime_s(&v, &t);
#else
    localtime_r(&t, &v);
#endif
    return v;
}
inline std::string dateKey(time_t t) {
    auto tm = localTime(t);
    char b[16];
    std::strftime(b, sizeof b, "%Y-%m-%d", &tm);
    return b;
}
inline time_t isoTime(const std::string &s) {
    if (s.size() < 19)
        return 0;
    std::tm tm{};
    std::istringstream in(s.substr(0, 19));
    in >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (in.fail())
        return 0;
#ifdef _WIN32
    time_t t = _mkgmtime(&tm);
#else
    time_t t = timegm(&tm);
#endif
    size_t p = s.find_first_of("Z+-", 19);
    if (p == std::string::npos)
        return 0; // Do not silently assume local or UTC.
    if (s[p] != 'Z') {
        int h = 0, m = 0;
        if (p + 6 > s.size() || std::sscanf(s.c_str() + p + 1, "%2d:%2d", &h, &m) != 2 || h > 23 || m > 59)
            return 0;
        t += (s[p] == '+' ? -1 : 1) * (h * 3600 + m * 60);
    }
    return t;
}
struct Window {
    std::string name;
    double used = -1;
    int64_t seconds = 0;
    time_t reset = 0;
    bool valid() const {
        return used >= 0 && used <= 100;
    }
};
struct Quota {
    std::vector<Window> windows;
    std::string plan, creditBalance, source;
    bool hasCredits = false, unlimited = false, limitReached = false;
    time_t observed = 0;
    bool usable() const {
        return std::any_of(windows.begin(), windows.end(), [](auto &w) { return w.valid(); });
    }
};
inline Window parseWindow(const json &j, std::string name, bool local, time_t observed) {
    Window w;
    w.name = std::move(name);
    w.used = number(field(j, "used_percent"));
    if (w.used < 0 || w.used > 100)
        w.used = -1;
    w.seconds = integer(j, local ? "window_minutes" : "limit_window_seconds") * (local ? 60 : 1);
    w.reset = static_cast<time_t>(integer(j, local ? "resets_at" : "reset_at"));
    if (!w.reset && !local)
        w.reset = observed + integer(j, "reset_after_seconds", 0);
    if (!field(j, "reset_after_seconds").is_number() && !integer(j, local ? "resets_at" : "reset_at"))
        w.reset = 0;
    return w;
}
inline Quota parseQuota(const json &j, bool local = false, time_t observed = std::time(nullptr)) {
    Quota q;
    q.observed = observed;
    q.plan = str(j, "plan_type");
    q.source = local ? "local" : "online";
    const auto &r = local ? j : field(j, "rate_limit");
    auto add = [&](const json &v, std::string name) {
        if (v.is_object())
            q.windows.push_back(parseWindow(v, name, local, observed));
    };
    add(field(r, local ? "primary" : "primary_window"), "主要额度");
    add(field(r, local ? "secondary" : "secondary_window"), "周期额度");
    q.limitReached =
        boolean(r, "limit_reached") || (field(r, "allowed").is_boolean() && !boolean(r, "allowed"));
    if (!local) {
        const auto &extras = field(j, "additional_rate_limits");
        if (extras.is_array())
            for (const auto &e : extras) {
                auto name = str(e, "limit_name", str(e, "metered_feature", "附加额度"));
                const auto &er = field(e, "rate_limit");
                add(field(er, "primary_window"), name);
                add(field(er, "secondary_window"), name + " · 周期");
                if (q.windows.size() >= 16)
                    break;
            }
        const auto &cr = field(j, "code_review_rate_limit");
        add(field(cr, "primary_window"), "代码审查");
        add(field(cr, "secondary_window"), "代码审查 · 周期");
    }
    const auto &c = field(j, "credits");
    q.hasCredits = boolean(c, "has_credits");
    q.unlimited = boolean(c, "unlimited");
    double balance = number(field(c, "balance"));
    if (balance >= 0) {
        std::ostringstream s;
        s << std::fixed << std::setprecision(2) << balance;
        q.creditBalance = s.str();
    }
    return q;
}
struct Tokens {
    int64_t input = 0, cached = 0, output = 0, reasoning = 0, total = 0;
    Tokens &operator+=(const Tokens &b) {
        input += b.input;
        cached += b.cached;
        output += b.output;
        reasoning += b.reasoning;
        total += b.total;
        return *this;
    }
};
inline Tokens parseTokens(const json &j) {
    Tokens t;
    t.input = integer(j, "input_tokens");
    t.cached = std::min(t.input, integer(j, "cached_input_tokens"));
    t.output = integer(j, "output_tokens");
    t.reasoning = std::min(t.output, integer(j, "reasoning_output_tokens"));
    t.total = integer(j, "total_tokens", t.input + t.output);
    return t;
}
inline Tokens delta(const Tokens &a, const Tokens &b) {
    Tokens d;
    d.input = std::max<int64_t>(0, a.input - b.input);
    d.cached = std::min(d.input, std::max<int64_t>(0, a.cached - b.cached));
    d.output = std::max<int64_t>(0, a.output - b.output);
    d.reasoning = std::min(d.output, std::max<int64_t>(0, a.reasoning - b.reasoning));
    d.total = std::max<int64_t>(0, a.total - b.total);
    return d;
}
struct Session {
    std::string id, model = "未标注模型";
    bool forked = false, haveTotal = false;
    Tokens previous, total;
    std::map<std::string, Tokens> daily, models;
    std::map<std::string, std::map<std::string, Tokens>> dailyModels;
    time_t last = 0;
    int events = 0, malformed = 0;
    Quota quota;
    void consume(const std::string &line) {
        // Ignore conversation bodies before JSON decoding; never retain prompt/completion text.
        if (line.find("token_count") == std::string::npos && line.find("session_meta") == std::string::npos &&
            line.find("turn_context") == std::string::npos)
            return;
        auto j = parse(line);
        if (!j.is_object()) {
            ++malformed;
            return;
        }
        auto type = str(j, "type");
        const auto &p = field(j, "payload");
        if (type == "session_meta") {
            id = str(p, "id", id);
            forked = !str(p, "forked_from_id").empty();
            return;
        }
        if (type == "turn_context") {
            model = str(p, "model", model);
            return;
        }
        if (type != "event_msg" || str(p, "type") != "token_count")
            return;
        time_t when = isoTime(str(j, "timestamp"));
        if (!when) {
            ++malformed;
            return;
        }
        const auto &r = field(p, "rate_limits");
        if (r.is_object() && when >= quota.observed) {
            auto q = parseQuota(r, true, when);
            if (q.usable())
                quota = std::move(q);
        }
        const auto &info = field(p, "info");
        const auto &totalJson = field(info, "total_token_usage");
        if (!totalJson.is_object())
            return; // last_token_usage alone cannot be safely deduplicated.
        Tokens current = parseTokens(totalJson);
        if (haveTotal && current.total <= previous.total)
            return; // Repeated and out-of-order snapshots.
        Tokens d;
        if (!haveTotal && forked)
            d = parseTokens(field(info, "last_token_usage"));
        else
            d = delta(current, previous);
        previous = current;
        haveTotal = true;
        if (d.total <= 0)
            return;
        ++events;
        total += d;
        daily[dateKey(when)] += d;
        models[model] += d;
        dailyModels[dateKey(when)][model] += d;
        last = std::max(last, when);
    }
};
struct Activity {
    Tokens today, week, month;
    std::map<std::string, Tokens> daily, models;
    std::vector<Session> recent;
    Quota localQuota;
    int sessions = 0, files = 0, malformed = 0, inaccessible = 0, skipped = 0;
    bool partial = false;
    uint64_t bytesRead = 0;
};
class Scanner {
    struct File {
        uint64_t offset = 0;
        fs::file_time_type modified{};
        Session session;
        std::string pending;
        bool oversized = false;
    };
    std::map<fs::path, File> cache;

  public:
    void clear() {
        cache.clear();
    }
    Activity scan(const fs::path &home, time_t now = std::time(nullptr), uint64_t budget = 64 * 1024 * 1024) {
        Activity a;
        std::vector<fs::path> files;
        std::error_code ec;
        auto cutoff = fs::file_time_type::clock::now() - std::chrono::hours(24 * 31);
        for (const auto &root : {home / "sessions", home / "archived_sessions"}) {
            if (!fs::exists(root, ec)) {
                ec.clear();
                continue;
            }
            fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
            if (ec) {
                ++a.inaccessible;
                ec.clear();
            }
            while (it != end) {
                const auto entry = *it;
                if (entry.is_regular_file(ec) && entry.path().extension() == ".jsonl") {
                    auto mod = entry.last_write_time(ec);
                    if (!ec && mod >= cutoff)
                        files.push_back(entry.path());
                }
                ec.clear();
                it.increment(ec);
                if (ec) {
                    ++a.inaccessible;
                    ec.clear();
                }
                if (files.size() >= 25000) {
                    a.partial = true;
                    break;
                }
            }
        }
        // Newer files first makes live quota available before a large backfill finishes.
        std::sort(files.begin(), files.end(), std::greater<fs::path>());
        std::set<fs::path> live(files.begin(), files.end());
        for (auto i = cache.begin(); i != cache.end();) {
            if (!live.count(i->first))
                i = cache.erase(i);
            else
                ++i;
        }
        for (const auto &p : files) {
            auto size = fs::file_size(p, ec);
            if (ec) {
                ec.clear();
                ++a.inaccessible;
                continue;
            }
            auto mod = fs::last_write_time(p, ec);
            if (ec) {
                ec.clear();
                ++a.inaccessible;
                continue;
            }
            auto &f = cache[p];
            if (size < f.offset || (size == f.offset && mod != f.modified))
                f = File{};
            if (size > f.offset) {
                if (a.bytesRead >= budget) {
                    a.partial = true;
                    continue;
                }
                std::ifstream in(p, std::ios::binary);
                if (!in) {
                    ++a.inaccessible;
                    continue;
                }
                in.seekg(static_cast<std::streamoff>(f.offset));
                std::array<char, 65536> buf;
                while (in && f.offset < size && a.bytesRead < budget) {
                    auto count = std::min<uint64_t>({buf.size(), size - f.offset, budget - a.bytesRead});
                    in.read(buf.data(), static_cast<std::streamsize>(count));
                    auto n = in.gcount();
                    if (n <= 0)
                        break;
                    f.offset += static_cast<uint64_t>(n);
                    a.bytesRead += static_cast<uint64_t>(n);
                    for (std::streamsize k = 0; k < n; ++k) {
                        char c = buf[static_cast<size_t>(k)];
                        if (c == '\n') {
                            if (!f.oversized)
                                f.session.consume(f.pending);
                            f.pending.clear();
                            f.oversized = false;
                        } else if (!f.oversized) {
                            f.pending += c;
                            if (f.pending.size() > 2 * 1024 * 1024) {
                                f.pending.clear();
                                f.oversized = true;
                                ++f.session.malformed;
                            }
                        }
                    }
                }
                if (f.offset < size)
                    a.partial = true;
            }
            f.modified = mod;
        }
        std::map<std::string, const Session *> unique;
        for (const auto &[p, f] : cache) {
            ++a.files;
            a.malformed += f.session.malformed;
            auto key = f.session.id.empty() ? p.u8string() : f.session.id;
            auto i = unique.find(key);
            if (i == unique.end() || f.session.total.total > i->second->total.total)
                unique[key] = &f.session;
        }
        std::string today = dateKey(now);
        // Calendar dates, not rolling 24h periods; DST-aware mktime normalizes subtraction.
        auto ago = [&](int d) {
            auto tm = localTime(now);
            tm.tm_mday -= d;
            tm.tm_isdst = -1;
            return dateKey(std::mktime(&tm));
        };
        auto week = ago(6), month = ago(29);
        for (const auto &[id, s] : unique) {
            if (s->quota.usable() && s->quota.observed > a.localQuota.observed)
                a.localQuota = s->quota;
            bool active = false;
            for (const auto &[d, t] : s->daily)
                if (d >= month && d <= today) {
                    active = true;
                    a.month += t;
                    a.daily[d] += t;
                    if (d >= week)
                        a.week += t;
                    if (d == today)
                        a.today += t;
                }
            if (active) {
                ++a.sessions;
                a.recent.push_back(*s);
                for (const auto &[d, byModel] : s->dailyModels)
                    if (d >= month && d <= today)
                        for (const auto &[m, t] : byModel)
                            a.models[m] += t;
            }
        }
        std::sort(a.recent.begin(), a.recent.end(), [](auto &l, auto &r) { return l.last > r.last; });
        if (a.recent.size() > 40)
            a.recent.resize(40);
        return a;
    }
};
} // namespace pulse
