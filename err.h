#ifndef ERR_H
#define ERR_H

#include <exception>
#include <string>

// Exception for fatal errors.
class FatalError : public std::exception {
private:
    std::string message;
public:
    FatalError(const std::string &msg) : message(msg) {}
    const char* what() const noexcept override {
        return message.c_str();
    }
};

// Exception for system errors.
class SystemError : public std::exception {
private:
    std::string message;
public:
    SystemError(const std::string &msg) : message(msg) {}
    const char* what() const noexcept override {
        return message.c_str();
    }
};

#endif // ERR_H