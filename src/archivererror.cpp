#include "archivererror.h"

ArchiverError::ArchiverError():
    type_{FR_PROC_ERROR_NONE},
    status_{0},
    domain_{0},
    code_{0} {
}

ArchiverError::ArchiverError(FrProcError* err): ArchiverError{} {
    if(err) {
        type_ = err->type;
        status_ = err->status;
        if(err->gerror) {
            domain_ = err->gerror->domain;
            code_ = err->gerror->code;
            message_ = QString::fromUtf8(err->gerror->message);
        }
    }
}

bool ArchiverError::hasError() const {
    return type_ != FR_PROC_ERROR_NONE;
}

FrProcErrorType ArchiverError::type() const {
    return type_;
}

int ArchiverError::status() const {
    return status_;
}

GQuark ArchiverError::domain() const {
    return domain_;
}

int ArchiverError::code() const {
    return code_;
}

QString ArchiverError::message() const {
    return message_;
}
