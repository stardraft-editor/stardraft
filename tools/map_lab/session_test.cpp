// SPDX-License-Identifier: MIT
#include "session.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
namespace {
void check(bool value) { if (!value) throw std::runtime_error("Map Lab session check failed"); }
maplab::Snapshot finish(maplab::Session& session) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        auto value = session.snapshot();
        if (value.done) return value;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("Map Lab error was not returned to the UI");
}
}
int main() {
    try {
        maplab::Request request;
        request.race = -1;
        request.read = [](const std::string&) -> maplab::Bytes {
            throw std::runtime_error("The invalid request must fail before it reads game data");
        };
        maplab::Session invalid(request);
        auto result = finish(invalid);
        check(!result.ready && result.error == "Invalid mining configuration");
        for (int technique : {-1, 3}) {
            bool refused = false;
            try { invalid.search(technique, 1); } catch (const std::invalid_argument&) { refused = true; }
            check(refused);
        }
        invalid.search(0,600); // Valid allowance, ignored on a finished session.
        bool refused=false;
        try { invalid.search(0,601); } catch(const std::invalid_argument&) { refused=true; }
        check(refused);
        invalid.configureRush(.5,50);
        refused=false;
        try { invalid.configureRush(.4,10); } catch(const std::invalid_argument&) { refused=true; }
        check(refused);
        request.race = 1;
        request.scenario = {1, 2, 3};
        const auto original = request.scenario;
        maplab::Session truncated(request);
        result = finish(truncated);
        check(result.done && !result.ready && !result.error.empty());
        check(original == request.scenario);
        truncated.stop();
        truncated.stop();
        maplab::Session canceled(request);
        canceled.stop();
        std::cout << "PASS: asynchronous errors, invalid searches, input ownership and repeated cancellation\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
