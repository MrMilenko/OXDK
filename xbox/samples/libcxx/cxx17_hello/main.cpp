// Proof that modern C++17 (libc++) compiles and links to an XBE on OXDK.
// Exercises the standard containers/RNG that the XDK's own STL can't provide.
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

int main(int argc, char* argv[]) {
    std::unordered_map<int, std::string> names;
    names[1] = "vault";
    names[13] = "dweller";

    std::vector<int> xs{3, 1, 4, 1, 5, 9};
    std::mt19937 rng(0xF2CE);

    int acc = static_cast<int>(names.size());
    for (int x : xs) {
        acc += x;
    }
    acc += static_cast<int>(rng() & 0x7);
    return acc;
}
