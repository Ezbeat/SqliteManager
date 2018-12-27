#ifndef __H_SQLITEMANAGERERRORS_H__
#define __H_SQLITEMANAGERERRORS_H__

namespace EzSqlite
{
enum class Errors
{
    kSuccess,
    kAlreadyOpen,
    kNotFound,
    kStopCallback,

    kUnsuccess,
    kFailedVerifyTable
};

enum class CallbackErrors
{
    kContinue,
    kStop
};
} // namespace EzSqlite

#endif // #ifndef __H_SQLITEMANAGERERRORS_H__