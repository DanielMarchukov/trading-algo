#include <cstdlib>
#include <python3.12/Python.h>
#include <ql/quantlib.hpp>
#include <zmq.hpp>

class PythonEmbedder {
  private:
    void *zmq_context_;

  public:
    PythonEmbedder(void *ctx) : zmq_context_(ctx) { Py_Initialize(); }

    ~PythonEmbedder() { Py_Finalize(); }

    int runScript() { return 0; }
};

int main(int argc, char *argv[]) {
    zmq::context_t context(1);
    PythonEmbedder embedder(&context);
    for (int i = 0; i < argc; ++i) {
        std::cout << "Argument " << i << ": " << argv[i] << std::endl;
    }
    return 0;
}
