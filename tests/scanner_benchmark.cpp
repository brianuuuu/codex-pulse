#include "../src/core.hpp"
#include <iostream>
using namespace pulse;
int main() {
    auto root = fs::temp_directory_path() /
                ("pulse-benchmark-" +
                 std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "sessions");
    auto now = std::time(nullptr);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char ts[40];
    std::strftime(ts, sizeof ts, "%Y-%m-%dT%H:%M:%SZ", &utc);
    for (int i = 0; i < 500; i++) {
        std::ofstream f(root / "sessions" / (std::to_string(i) + ".jsonl"));
        f << json{{"type", "session_meta"}, {"payload", {{"id", std::to_string(i)}}}}.dump() << '\n';
        for (int j = 1; j <= 40; j++) {
            f << "{\"type\":\"response_item\",\"payload\":\"" << std::string(1500, 'a') << "\"}\n";
            json e;
            e["timestamp"] = ts;
            e["type"] = "event_msg";
            e["payload"]["type"] = "token_count";
            e["payload"]["info"]["total_token_usage"] = {
                {"input_tokens", j * 80}, {"output_tokens", j * 20}, {"total_tokens", j * 100}};
            f << e.dump() << '\n';
        }
    }
    Scanner s;
    auto start = std::chrono::steady_clock::now();
    auto a = s.scan(root, now);
    auto middle = std::chrono::steady_clock::now();
    auto b = s.scan(root, now);
    auto end = std::chrono::steady_clock::now();
    fs::remove_all(root);
    std::cout << "files=" << a.files << " first_bytes=" << a.bytesRead
              << " first_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(middle - start).count()
              << " unchanged_bytes=" << b.bytesRead << " unchanged_ms="
              << std::chrono::duration_cast<std::chrono::milliseconds>(end - middle).count()
              << " tokens=" << a.month.total << "\n";
    return (a.month.total == 2000000 && a.files == 500 && b.bytesRead == 0 && !a.partial) ? 0 : 1;
}
