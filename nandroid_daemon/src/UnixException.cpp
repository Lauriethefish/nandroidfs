#include "UnixException.hpp"

namespace nandroidfs {
    UnixException::UnixException(int err_num) : std::exception() {
        this->err_num = err_num;
    }

    const char* UnixException::what() {
        return strerror(err_num);
    }

    int UnixException::get_err_num() {
        return err_num;
    }

    int throw_unless(int return_val) {
        if(return_val == -1) {
            throw UnixException(errno);
        }   else    {
            return return_val;
        }
    }
}