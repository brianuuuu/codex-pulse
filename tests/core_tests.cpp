#include "../src/core.hpp"
#include <iostream>
#include <stdexcept>
using namespace pulse;
int checks = 0;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        ++checks;                                                                                            \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " #x);             \
    } while (0)
std::string event(int64_t total, std::string ts = "2026-11-13T02:00:00Z", int64_t last = 0) {
    json t = {{"input_tokens", total * 8 / 10},
              {"cached_input_tokens", total * 4 / 10},
              {"output_tokens", total * 2 / 10},
              {"reasoning_output_tokens", total / 10},
              {"total_tokens", total}};
    json j;
    j["timestamp"] = ts;
    j["type"] = "event_msg";
    j["payload"]["type"] = "token_count";
    j["payload"]["info"]["total_token_usage"] = t;
    j["payload"]["info"]["last_token_usage"]["total_tokens"] = last;
    return j.dump();
}
void write(const fs::path &p, const std::string &s, bool append = false) {
    std::ofstream f(p, append ? std::ios::app : std::ios::trunc);
    f << s;
}
int main() {
    try {
        CHECK(number(json("NaN")) == -1);
        CHECK(number(json("12junk")) == -1);
        CHECK(number(json("12.5")) == 12.5);
        CHECK(str(parse("{\"x\":null}"), "x", "fallback") == "fallback");
        CHECK(integer(parse("{\"x\":-4}"), "x", 12) == 12);
        CHECK(isoTime("2026-11-13T10:00:00+08:00") == isoTime("2026-11-13T02:00:00Z"));
        CHECK(isoTime("2026-11-13T02:00:00.123Z") == isoTime("2026-11-13T02:00:00Z"));
        CHECK(isoTime("invalid") == 0);
        CHECK(isoTime("2026-11-13T02:00:00") == 0);
        auto q = parseQuota(
            parse(
                R"({"plan_type":"plus","rate_limit":{"primary_window":{"used_percent":34,"limit_window_seconds":18000,"reset_at":1800000000},"secondary_window":{"used_percent":0,"limit_window_seconds":604800}},"additional_rate_limits":[null,{}, {"limit_name":"Spark","rate_limit":{"primary_window":{"used_percent":99}}}],"credits":{"balance":"12.50","has_credits":true}})"),
            false, 1000);
        CHECK(q.usable());
        CHECK(q.windows.size() == 3);
        CHECK(q.windows[0].seconds == 18000);
        CHECK(q.windows[1].used == 0);
        CHECK(q.windows[0].reset == 1800000000);
        CHECK(q.windows[1].reset == 0);
        CHECK(q.creditBalance == "12.50");
        CHECK(q.hasCredits);
        auto bad = parseQuota(parse(
            R"({"rate_limit":{"primary_window":{"used_percent":-1},"secondary_window":{"used_percent":140}},"credits":{"balance":null}})"));
        CHECK(!bad.usable());
        CHECK(bad.creditBalance.empty());
        CHECK(!parseQuota(parse("{\"unrelated\":12}")).usable());
        CHECK(!parseQuota(json(nullptr)).usable());
        auto local = parseQuota(
            parse(
                R"({"primary":{"used_percent":22,"window_minutes":300,"resets_at":1800000000},"secondary":null})"),
            true, 123);
        CHECK(local.source == "local");
        CHECK(local.windows[0].seconds == 18000);
        CHECK(local.observed == 123);
        auto after = parseQuota(
            parse(R"({"rate_limit":{"primary_window":{"used_percent":10,"reset_after_seconds":3600}}})"),
            false, 1000);
        CHECK(after.windows[0].reset == 4600);
        Session s;
        s.consume(R"({"type":"session_meta","payload":{"id":"abc"}})");
        s.consume(R"({"type":"turn_context","payload":{"model":"gpt-test"}})");
        s.consume(event(100));
        s.consume(event(100));
        s.consume(event(80));
        s.consume(event(170));
        CHECK(s.total.total == 170);
        CHECK(s.events == 2);
        CHECK(s.total.input == 136);
        CHECK(s.total.cached == 68);
        CHECK(s.total.output == 34);
        CHECK(s.total.reasoning == 17);
        CHECK(s.models["gpt-test"].total == 170);
        CHECK(s.id == "abc");
        s.consume("{token_count broken");
        CHECK(s.malformed == 1);
        s.consume(
            R"({"type":"event_msg","timestamp":"2026-11-13T02:00:00Z","payload":{"type":"token_count","info":null,"rate_limits":{"primary":{"used_percent":50,"window_minutes":300}}}})");
        CHECK(s.quota.usable());
        CHECK(s.total.total == 170);
        s.consume(
            R"({"type":"event_msg","payload":{"type":"token_count","info":{"total_token_usage":{"total_tokens":300}}}})");
        CHECK(s.total.total == 170);
        CHECK(s.malformed == 2);
        Session fork;
        fork.consume(R"({"type":"session_meta","payload":{"id":"fork","forked_from_id":"abc"}})");
        fork.consume(event(1000, "2026-11-13T03:00:00Z", 50));
        fork.consume(event(1100, "2026-11-13T03:01:00Z", 100));
        CHECK(fork.total.total == 150);
        auto root = fs::temp_directory_path() /
                    ("codex-pulse-tests-" +
                     std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
        fs::create_directories(root / "sessions");
        fs::create_directories(root / "archived_sessions");
        auto file = root / "sessions" / "a.jsonl";
        write(file, R"({"type":"session_meta","payload":{"id":"id1"}})"
                    "\n" +
                        event(100) + "\n" + event(100) + "\n");
        Scanner scan;
        auto now = isoTime("2026-11-13T02:01:00Z");
        auto a = scan.scan(root, now);
        CHECK(a.month.total == 100);
        CHECK(a.sessions == 1);
        CHECK(a.today.total == 100);
        CHECK(a.bytesRead > 0);
        a = scan.scan(root, now);
        CHECK(a.bytesRead == 0);
        CHECK(a.month.total == 100);
        auto next = event(250);
        write(file, next.substr(0, next.size() / 2), true);
        a = scan.scan(root, now);
        CHECK(a.month.total == 100);
        write(file, next.substr(next.size() / 2) + "\n", true);
        a = scan.scan(root, now);
        CHECK(a.month.total == 250);
        fs::copy_file(file, root / "archived_sessions" / "copy.jsonl");
        a = scan.scan(root, now);
        CHECK(a.month.total == 250);
        CHECK(a.sessions == 1);
        CHECK(a.files == 2);
        fs::remove(root / "archived_sessions" / "copy.jsonl");
        write(file, event(30) + "\n");
        a = scan.scan(root, now);
        CHECK(a.month.total == 30);
        CHECK(a.files == 1);
        write(file, event(150) + "\n", true);
        Scanner bounded;
        auto tiny = bounded.scan(root, now, 20);
        CHECK(tiny.partial);
        CHECK(tiny.bytesRead == 20);
        for (int i = 0; i < 100 && tiny.partial; i++)
            tiny = bounded.scan(root, now, 20);
        CHECK(!tiny.partial);
        CHECK(tiny.month.total == 150);
        write(file, event(100, "2026-09-01T02:00:00Z") + "\n" + event(300) + "\n");
        Scanner dated;
        a = dated.scan(root, now);
        CHECK(a.month.total == 200);
        CHECK(a.models["未标注模型"].total == 200);
        write(file, std::string(2 * 1024 * 1024 + 10, 'x') + "\n" + event(400) + "\n");
        Scanner oversized;
        a = oversized.scan(root, now);
        CHECK(a.malformed == 1);
        CHECK(a.month.total == 400);
        fs::remove(file);
        a = oversized.scan(root, now);
        CHECK(a.files == 0);
        CHECK(a.month.total == 0);
        fs::remove_all(root);
        std::cout << "PASS: " << checks << " checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
