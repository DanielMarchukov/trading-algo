#include <cstdlib>
#include <ql/quantlib.hpp>

int main(int argc, char *argv[]) {
    if (argc > 0) {
        std::cout << "Usage: " << argv[0] << std::endl;
        return 1;
    }
    return argc;
}
