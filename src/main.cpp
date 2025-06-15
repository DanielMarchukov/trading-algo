#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <python3.12/Python.h>
#include <python3.12/pylifecycle.h>
#include <python3.12/pystate.h>
#include <ql/quantlib.hpp>
#include <string>
#include <thread>
#include <vector>
#include <zmq.hpp>

std::atomic<bool> is_running(true);

void signal_handler(int signum) {
    std::cout << "Signal " << signum << " received." << std::endl;
    is_running.store(false);
}

void runPublisher(const std::string &address);

#pragma pack(push, 1)
struct MarketEvent {
    uint8_t eventType;
    uint64_t timestamp;
    double price;  // Bid or Trade price
    uint32_t size; // Bid or Trade size
    double askPrice;
    uint32_t askSize;
    char padding[7];
};
#pragma pack(pop)

void consumer_worker(const std::string &address, const std::string &symbol) {
    zmq::context_t context(1);
    zmq::socket_t subscriber(context, zmq::socket_type::sub);
    subscriber.set(zmq::sockopt::rcvtimeo, 500);
    subscriber.connect(address);
    subscriber.set(zmq::sockopt::subscribe, symbol);

    while (is_running.load()) {
        try {
            zmq::message_t topic;
            auto res1 = subscriber.recv(topic, zmq::recv_flags::none);
            if (!res1.has_value()) {
                continue;
            }
            zmq::message_t payload;
            auto res2 = subscriber.recv(payload, zmq::recv_flags::none);
            if (res2.has_value() && payload.size() == sizeof(MarketEvent)) {
                const MarketEvent *event = payload.data<MarketEvent>();
                std::cout << "Received event for " << symbol
                          << ": Type=" << static_cast<int>(event->eventType)
                          << ", TS=" << event->timestamp
                          << ", P1=" << event->price << ", S1=" << event->size
                          << ", P2(Ask)=" << event->askPrice
                          << ", S2(Ask)=" << event->askSize << std::endl;
            }
        } catch (const zmq::error_t &e) {
            if (e.num() == ETERM) {
                break;
            }
            std::cerr << "Error: " << e.what() << std::endl;
            break;
        }
    }
}

int main() {
    std::signal(SIGINT, signal_handler);
    static_assert(sizeof(MarketEvent) == 40, "Struct size mismatch");
    Py_Initialize();

    const std::string address = "inproc://alpaca_data";
    std::vector<std::string> symbols = {"AAPL", "GOOGL", "AMZN"};
    std::vector<std::thread> consumers;

    PyThreadState *mainThreadState = PyEval_SaveThread();
    for (const auto &symbol : symbols) {
        consumers.emplace_back(consumer_worker, address, symbol);
    }

    std::thread python(runPublisher, address);

    while (is_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (Py_IsInitialized()) {
        PyGILState_STATE gstate = PyGILState_Ensure();
        PyErr_SetInterrupt();
        PyGILState_Release(gstate);
    }

    if (python.joinable()) {
        python.join();
    }
    for (auto &t : consumers) {
        if (t.joinable()) {
            t.join();
        }
    }

    PyEval_RestoreThread(mainThreadState);
    Py_FinalizeEx();
    return 0;
}

// --- Fully Rewritten and Robust runPublisher Function ---
void runPublisher(const std::string &address) {
    PyGILState_STATE gstate = PyGILState_Ensure();

    PyObject *pName = nullptr, *pModule = nullptr, *pFunc = nullptr;
    PyObject *pArgs = nullptr, *pResult = nullptr;

    // Use a do-while(false) loop as a scope for structured error handling
    // with `break` instead of `goto`.
    do {
        PyObject *sys_path = PySys_GetObject("path");
        if (sys_path == NULL) {
            PyErr_Print();
            std::cerr << "Fatal: Could not get sys.path." << std::endl;
            break;
        }
        PyList_Append(sys_path,
                      PyUnicode_FromString("env/lib/python3.12/site-packages"));
        PyList_Append(sys_path, PyUnicode_FromString("data-ingestion/src"));

        pName = PyUnicode_DecodeFSDefault("publisher");
        if (!pName) {
            PyErr_Print();
            break;
        }

        pModule = PyImport_Import(pName);
        if (!pModule) {
            PyErr_Print();
            break;
        }

        pFunc = PyObject_GetAttrString(pModule, "start_publisher");
        if (!pFunc || !PyCallable_Check(pFunc)) {
            if (PyErr_Occurred())
                PyErr_Print();
            std::cerr << "Error: Cannot find function 'start_publishing'."
                      << std::endl;
            break;
        }

        pArgs = PyTuple_New(1);
        if (!pArgs) {
            PyErr_Print();
            break;
        }

        // Create the string argument for the tuple
        PyObject *pValue = PyUnicode_FromString(address.c_str());
        if (!pValue) {
            PyErr_Print();
            break;
        }

        // PyTuple_SetItem STEALS the reference to pValue. Do NOT DECREF pValue
        // after this. It becomes part of pArgs.
        PyTuple_SetItem(pArgs, 0, pValue);

        // Release GIL before long-running call
        PyThreadState *save_state = PyEval_SaveThread();

        pResult = PyObject_CallObject(pFunc, pArgs);

        // Re-acquire GIL to handle result
        PyEval_RestoreThread(save_state);

        if (pResult == NULL) {
            if (PyErr_ExceptionMatches(PyExc_KeyboardInterrupt)) {
                std::cout
                    << "Python KeyboardInterrupt caught and cleared in C++."
                    << std::endl;
                PyErr_Clear();
            } else {
                std::cerr << "Python function call failed:" << std::endl;
                PyErr_Print();
            }
        }
    } while (false);

    // This cleanup block is now always reachable without a goto.
    // It correctly decrements references for any objects that were created.
    // Py_XDECREF is safe to call on a NULL pointer.
    Py_XDECREF(pResult);
    Py_XDECREF(pArgs); // This will also decref pValue if SetItem succeeded
    Py_XDECREF(pFunc);
    Py_XDECREF(pModule);
    Py_XDECREF(pName);

    PyGILState_Release(gstate);
}
