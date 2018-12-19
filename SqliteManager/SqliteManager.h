#ifndef __H_SQLITEMANAGER_H__
#define __H_SQLITEMANAGER_H__

#include "SqliteManagerErrors.h"

#include "SQLite/sqlite3.h"

#include <windows.h>
#include <functional>
#include <codecvt>
#include <vector>

#include <iostream>

namespace EzSqlite
{

/* std::string으로 된 값은 전부 utf8 */

enum class DesiredAccess
{
    kReadOnly,
    kReadWrite
};

enum class CreationDisposition
{
    kCreateAlways,  // DB 파일 무조건 새로 생성
    kOpenAlways,    // DB 파일 있는 경우 열고, 실패할 경우 새로 생성 후 열기
    kOpenExisting   // DB 파일 있는 경우 열고, 실패할 경우 실패
};

enum class BasicStmtIndex
{
    kBegin,     // BEGIN;
    kCommit,    // COMMIT;
    kRollback,  // ROLLBACK;
    kPragmaQueryOnlyTrue,   // PRAGMA query_only = TRUE;
    kPragmaQueryOnlyFalse,  // PRAGMA query_only = FALSE;
    kVacuum,    // VACUUM;

    kBasicStmtNumber
};

enum class StmtDataType
{
    kInteger = SQLITE_INTEGER,
    kFloat = SQLITE_FLOAT,
    kText = SQLITE_TEXT,
    kBlob = SQLITE_BLOB,
    kNull = SQLITE_NULL
};

enum class StmtBindParameterOptions
{
    kDestructorStatic,    // SQLITE_STATIC
    kDestructorTransient, // SQLITE_TRANSIENT

    kNone
};

struct StmtBindParameterInfo
{
    StmtBindParameterInfo()
    {
        data = nullptr;
        dataByteSize = 0;
        options = StmtBindParameterOptions::kNone;
    };

    void* data;
    StmtDataType dataType;
    uint32_t dataByteSize;
    StmtBindParameterOptions options;   // blob과 string interface인 경우 Default는 kDestructorTransient
};

struct StmtInfo
{
    StmtInfo()
    {
        stmt = nullptr;
        columnCount = 0;
        bindParameterCount = 0;
    };

    mutable sqlite3_stmt* stmt; // 외부 라이브러리 변수라서 mutable 선언
    std::string stmtString;
    uint32_t columnCount;
    uint32_t bindParameterCount;
};

typedef std::function<CallbackErrors(const StmtInfo&)> StepCallbackFunc;

class SqliteManager
{
public:
    SqliteManager();
    ~SqliteManager();

    Errors CreateDatabase(
        _In_ const std::wstring& databasePath,
        _In_ DesiredAccess desiredAccess,
        _In_ CreationDisposition creationDisposition,
        _In_ const std::vector<std::string>& verifyTableStmtStringList,
        _In_opt_ const std::vector<std::string>* createTableStmtStringList = nullptr
    );

    Errors CloseDatabase(_In_opt_ bool deleteDatabase = false);

    Errors PrepareStmt(_In_ const std::string& stmtString, _Out_opt_ uint32_t* preparedStmtIndex = nullptr);
    void ClearPreparedStmt();

    Errors FindPreparedStmt(_In_ const std::string& preparedStmtString, _Out_ const StmtInfo*& preparedStmtInfo);
    Errors FindPreparedStmt(_In_ uint32_t preparedStmtIndex, _Out_ const StmtInfo*& preparedStmtInfo);

    Errors ExecStmt(
        _In_ const std::string& stmtString, 
        _In_opt_ const std::vector<StmtBindParameterInfo>* stmtBindParameterInfoList = nullptr,
        _In_opt_ StepCallbackFunc* stmtStepCallback = nullptr
    );
    Errors ExecStmt(
        _In_ uint32_t preparedStmtIndex, 
        _In_opt_ const std::vector<StmtBindParameterInfo>* stmtBindParameterInfoList = nullptr,
        _In_opt_ StepCallbackFunc* stmtStepCallback = nullptr
    );

private:
    class RAIIRegister
    {
    private:
        std::function<void()> raiiFunction_;

    public:
        RAIIRegister(std::function<void()> raiiFunc) : raiiFunction_(raiiFunc)
        {

        }
        ~RAIIRegister()
        {
            raiiFunction_();
        }
    };

private:
    Errors PrepareBasicStmt_();
    void GetStmtInfo_(_In_ sqlite3_stmt* stmt, _Out_ uint32_t& columnCount, _Out_ uint32_t& bindParameterCount);

    Errors ExecStmt_(
        _In_ const StmtInfo* stmtInfo,
        _In_opt_ const std::vector<StmtBindParameterInfo>* stmtBindParameterInfoList = nullptr,
        _In_opt_ StepCallbackFunc* stmtStepCallback = nullptr
    );

    Errors StmtBindParameter_(_In_ const StmtInfo*& stmtInfo, _In_ const std::vector<StmtBindParameterInfo>& stmtBindParameterInfoList);
    Errors VerifyTable_(_In_ const std::vector<std::string>& verifyTableStmtStringList);

private:
    std::wstring databasePath_;
    sqlite3* database_;

    std::vector<StmtInfo> preparedStmtInfoList_;
};

} // namespace EzSqlite

#endif // #ifndef __H_SQLITEMANAGER_H__