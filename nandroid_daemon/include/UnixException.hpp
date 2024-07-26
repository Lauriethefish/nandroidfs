#pragma once

#include <stdexcept>
#include <errno.h>
#include <string.h>

namespace nandroidfs {
    // Exception used to wrap errors returned by unix socket APIs
    class UnixException : public std::exception {
    public:
        UnixException(int err_num);
        virtual const char* what();

        // Gets the unix error code that this exception represents
        int get_err_num();
    private:
        int err_num;
    };

    // If `return_val` is `-1`, this will throw a UnixException with the appropriate error message.
    // Otherwise, this will return `return_val`
    // Useful to wrap the result of a unix API call and turn it into an std::exception
    int throw_unless(int return_val);
}
