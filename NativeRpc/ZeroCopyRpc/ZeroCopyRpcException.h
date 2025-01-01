#pragma once
#include <exception>
#include <string>
#include "Export.h"

class EXPORT ZeroCopyRpcException : public std::exception {
public:
    ZeroCopyRpcException(const char* message);

    virtual const char* what() const noexcept override;

private:
    std::string msg_;
};
