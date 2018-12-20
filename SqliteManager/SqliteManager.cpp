#include "SqliteManager.h"

EzSqlite::SqliteManager::SqliteManager()
{
    database_ = nullptr;
}

EzSqlite::SqliteManager::~SqliteManager()
{
    this->CloseDatabase();
}

EzSqlite::Errors EzSqlite::SqliteManager::CreateDatabase(
    _In_ const std::wstring& databasePath,
    _In_ DesiredAccess desiredAccess,
    _In_ CreationDisposition creationDisposition,
    _In_ const std::vector<std::string>& verifyTableStmtStringList,
    _In_opt_ const std::vector<std::string>* createTableStmtStringList /*= nullptr*/
)
{
    Errors retValue = Errors::kUnsuccess;

    int sqliteStatus = SQLITE_ERROR;
    int openFlags = 0;

    std::wstring_convert<std::codecvt_utf8<wchar_t>> convert;
    std::string databasePathUtf8;

    auto raii = RAIIRegister([&] 
    {
        if (retValue != Errors::kSuccess)
        {
            this->CloseDatabase();
        }
    });

    // SQLite Database가 열려있는 경우
    if (database_ != nullptr)
    {
        if (this->CloseDatabase() != Errors::kSuccess)
        {
            return retValue;
        }
    }

    //
    // 인자 검증 
    //
    
    // kReadOnly를 지정하려면 kOpenExisting 값이어야 됨
    if (desiredAccess == DesiredAccess::kReadOnly && creationDisposition != CreationDisposition::kOpenExisting)
    {
        return retValue;
    }

    // 테이블 생성 명령어를 지정하지 않으려면 kOpenExisting 값이어야 됨
    if (createTableStmtStringList == nullptr && creationDisposition != CreationDisposition::kOpenExisting)
    {
        return retValue;
    }

    if (creationDisposition == CreationDisposition::kCreateAlways)
    {
        if ((::DeleteFileW(databasePath.c_str()) == FALSE) && (GetLastError() != ERROR_FILE_NOT_FOUND))
        {
            return retValue;
        }
    }

    databasePathUtf8 = convert.to_bytes(databasePath);
    if (desiredAccess == DesiredAccess::kReadOnly)
    {
        openFlags = SQLITE_OPEN_READONLY;
    }
    else if (desiredAccess == DesiredAccess::kReadWrite)
    {
        openFlags = SQLITE_OPEN_READWRITE;
    }
    
    sqliteStatus = sqlite3_open_v2(databasePathUtf8.c_str(), &database_, openFlags, nullptr);
    if ((sqliteStatus != SQLITE_OK) || (VerifyTable_(verifyTableStmtStringList) != Errors::kSuccess))
    {
        // sqlite3_open_v2 함수가 실패해도 database_ 엔 값이 리턴 됨
        if (this->CloseDatabase() != Errors::kSuccess)
        {
            return retValue;
        }

        if (creationDisposition == CreationDisposition::kOpenExisting)
        {
            return retValue;
        }
        else if (creationDisposition == CreationDisposition::kOpenAlways)
        {
            if ((::DeleteFileW(databasePath.c_str()) == FALSE) && (GetLastError() != ERROR_FILE_NOT_FOUND))
            {
                return retValue;
            }
        }

        sqliteStatus = sqlite3_open_v2(databasePathUtf8.c_str(), &database_, openFlags | SQLITE_OPEN_CREATE, nullptr);
        if (sqliteStatus != SQLITE_OK)
        {
            return retValue;
        }
        
        // 테이블 생성
        if (createTableStmtStringList->size() == 0)
        {
            return retValue;
        }

        for (const auto& createTableStmtStringListEntry : *createTableStmtStringList)
        {
            if (_strnicmp(createTableStmtStringListEntry.c_str(), "CREATE ", strlen("CREATE ") != 0))
            {
                return retValue;
            }

            if (this->ExecStmt(createTableStmtStringListEntry) != Errors::kSuccess)
            {
                return retValue;
            }
        }
    }

    retValue = PrepareInternalStmt_();
    if (retValue != Errors::kSuccess)
    {
        return retValue;
    }

    databasePath_ = databasePath;

    retValue = Errors::kSuccess;
    return retValue;
}

EzSqlite::Errors EzSqlite::SqliteManager::CloseDatabase(
    _In_opt_ bool deleteDatabase /*= false*/ // true여도 이미 닫힌 경우엔 제거 불가
)
{
    Errors retValue = Errors::kUnsuccess;

    int sqliteStatus = SQLITE_ERROR;

    auto raii = RAIIRegister([&] 
    {
        if (retValue != Errors::kSuccess)
        {
            PrepareInternalStmt_();
        }
    });

    if (database_ == nullptr)
    {
        retValue = Errors::kSuccess;
        return retValue;
    }

    this->ClearPreparedStmt();

    sqliteStatus = sqlite3_close(database_);
    if(sqliteStatus != SQLITE_OK)
    {
        /*sqlite3_stmt * stmt;
        while ((stmt = sqlite3_next_stmt(database_, NULL)) != NULL) {
        sqlite3_finalize(stmt);
        }*/

        return retValue;
    }

    database_ = nullptr;

    if (deleteDatabase == true)
    {
        if ((::DeleteFileW(databasePath_.c_str()) == FALSE) && (GetLastError() != ERROR_FILE_NOT_FOUND))
        {
            return retValue;
        }
    }    

    databasePath_.clear();

    retValue = Errors::kSuccess;
    return retValue;
}

EzSqlite::Errors EzSqlite::SqliteManager::PrepareStmt(
    _In_ const std::string& stmtString, 
    _Out_opt_ uint32_t* preparedStmtIndex /*= nullptr*/
)
{
    Errors retValue = Errors::kUnsuccess;

    int sqliteStatus = SQLITE_ERROR;

    StmtInfo stmtInfo;

    auto raii = RAIIRegister([&] 
    {
        if (retValue != Errors::kSuccess && stmtInfo.stmt != nullptr)
        {
            sqlite3_finalize(stmtInfo.stmt);
            stmtInfo.stmt = nullptr;
        }
    });

    if (database_ == nullptr)
    {
        return retValue;
    }

    sqliteStatus = sqlite3_prepare_v2(
        database_,
        stmtString.c_str(),
        -1,
        &stmtInfo.stmt,
        nullptr
    );
    if (sqliteStatus != SQLITE_OK)
    {
        return retValue;
    }

    stmtInfo.stmtString = stmtString;
    GetStmtInfo_(stmtInfo.stmt, stmtInfo.columnCount, stmtInfo.bindParameterCount);

    preparedStmtInfoList_.push_back(stmtInfo);

    if (preparedStmtIndex != nullptr)
    {
        *preparedStmtIndex = static_cast<uint32_t>(preparedStmtInfoList_.size()) - 1;
    }

    retValue = Errors::kSuccess;
    return retValue;
}

void EzSqlite::SqliteManager::ClearPreparedStmt()
{
    if (preparedStmtInfoList_.size() == 0)
    {
        return;
    }

    for (auto& stmtInfoListEntry : preparedStmtInfoList_)
    {
        if (stmtInfoListEntry.stmt != nullptr)
        {
            sqlite3_finalize(stmtInfoListEntry.stmt);
            stmtInfoListEntry.stmt = nullptr;
        }
    }

    preparedStmtInfoList_.clear();
}

EzSqlite::Errors EzSqlite::SqliteManager::FindPreparedStmt(
    _In_ const std::string& preparedStmtString, 
    _Out_ const StmtInfo*& preparedStmtInfo
)
{
    Errors retValue = Errors::kNotFound;
    
    if (preparedStmtInfoList_.size() == 0)
    {
        return retValue;
    }

    for (const auto& preparedStmtInfoListEntry : preparedStmtInfoList_)
    {
        if (preparedStmtInfoListEntry.stmtString == preparedStmtString)
        {
            preparedStmtInfo = &preparedStmtInfoListEntry;
            
            retValue = Errors::kSuccess;
            break;
        }
    }

    return retValue;
}

EzSqlite::Errors EzSqlite::SqliteManager::FindPreparedStmt(
    _In_ uint32_t preparedStmtIndex,
    _Out_ const StmtInfo*& preparedStmtInfo
)
{
    Errors retValue = Errors::kNotFound;

    if (preparedStmtIndex == static_cast<uint32_t>(StmtIndex::kNoIndex))
    {
        return retValue;
    }

    if (preparedStmtInfoList_.size() <= preparedStmtIndex)
    {
        return retValue;
    }

    preparedStmtInfo = &(preparedStmtInfoList_[preparedStmtIndex]);

    retValue = Errors::kSuccess;
    return retValue;
}

EzSqlite::Errors EzSqlite::SqliteManager::ExecStmt(
    _In_ const std::string& stmtString,
    _In_opt_ const std::vector<StmtBindParameterInfo>* stmtBindParameterInfoList /*= nullptr */,
    _In_opt_ StepCallbackFunc* stmtStepCallback /*= nullptr*/
)
{
    Errors retValue = Errors::kUnsuccess;

    int sqliteStatus = SQLITE_ERROR;

    const StmtInfo* preparedStmtInfo = nullptr;
    StmtInfo stmtInfo;

    auto raii = RAIIRegister([&] 
    {
        if (stmtInfo.stmt != nullptr)
        {
            sqlite3_finalize(stmtInfo.stmt);
            stmtInfo.stmt = nullptr;
        }
    });

    if (database_ == nullptr)
    {
        return retValue;
    }

    retValue = FindPreparedStmt(stmtString, preparedStmtInfo);
    if (retValue == Errors::kNotFound)
    {
        sqliteStatus = sqlite3_prepare_v2(
            database_,
            stmtString.c_str(),
            -1,
            &stmtInfo.stmt,
            nullptr
        );
        if (sqliteStatus != SQLITE_OK)
        {
            retValue = Errors::kUnsuccess;
            return retValue;
        }

        stmtInfo.stmtString = stmtString;
        GetStmtInfo_(stmtInfo.stmt, stmtInfo.columnCount, stmtInfo.bindParameterCount);
    }

    return ExecStmt_(&stmtInfo, stmtBindParameterInfoList, stmtStepCallback);
}

EzSqlite::Errors EzSqlite::SqliteManager::ExecStmt(
    _In_ uint32_t preparedStmtIndex, 
    _In_opt_ const std::vector<StmtBindParameterInfo>* stmtBindParameterInfoList /*= nullptr */,
    _In_opt_ StepCallbackFunc* stmtStepCallback /*= nullptr*/
)
{
    Errors retValue = Errors::kUnsuccess;

    const StmtInfo* stmtInfo = nullptr;

    if (database_ == nullptr)
    {
        return retValue;
    }

    retValue = FindPreparedStmt(preparedStmtIndex, stmtInfo);
    if (retValue != Errors::kSuccess)
    {
        return retValue;
    }

    return ExecStmt_(stmtInfo, stmtBindParameterInfoList, stmtStepCallback);
}

EzSqlite::Errors EzSqlite::SqliteManager::PrepareInternalStmt_()
{
    Errors retValue = Errors::kUnsuccess;

    auto raii = RAIIRegister([&] 
    {
        if (retValue != Errors::kSuccess)
        {
            this->ClearPreparedStmt();
        }
    });

    retValue = this->PrepareStmt("BEGIN;");
    if(retValue != Errors::kSuccess)
    {
        return retValue;
    }

    retValue = this->PrepareStmt("COMMIT;");
    if (retValue != Errors::kSuccess)
    {
        return retValue;
    }

    retValue = this->PrepareStmt("ROLLBACK;");
    if (retValue != Errors::kSuccess)
    {
        return retValue;
    }

    retValue = this->PrepareStmt("PRAGMA query_only = TRUE;");
    if (retValue != Errors::kSuccess)
    {
        return retValue;
    }

    retValue = this->PrepareStmt("PRAGMA query_only = FALSE;");
    if (retValue != Errors::kSuccess)
    {
        return retValue;
    }

    retValue = this->PrepareStmt("VACUUM;");
    if (retValue != Errors::kSuccess)
    {
        return retValue;
    }

    retValue = this->PrepareStmt("PRAGMA synchronous = ON;");
    if (retValue != Errors::kSuccess)
    {
        return retValue;
    }

    retValue = this->PrepareStmt("PRAGMA synchronous = OFF;");
    if (retValue != Errors::kSuccess)
    {
        return retValue;
    }

    retValue = Errors::kSuccess;
    return retValue;
}

void EzSqlite::SqliteManager::GetStmtInfo_(
    _In_ sqlite3_stmt* stmt, 
    _Out_ uint32_t& columnCount, 
    _Out_ uint32_t& bindParameterCount
)
{
    columnCount = 0;
    bindParameterCount = 0;

    if (stmt == nullptr)
    {
        return;
    }

    columnCount = sqlite3_column_count(stmt);
    bindParameterCount = sqlite3_bind_parameter_count(stmt);

    return;
}

EzSqlite::Errors EzSqlite::SqliteManager::ExecStmt_(
    _In_ const StmtInfo* stmtInfo,
    _In_opt_ const std::vector<StmtBindParameterInfo>* stmtBindParameterInfoList /*= nullptr */,
    _In_opt_ StepCallbackFunc* stmtStepCallback /*= nullptr*/
)
{
    Errors retValue = Errors::kUnsuccess;

    int sqliteStatus = SQLITE_ERROR;
    CallbackErrors callbackStatus;

    auto raii = RAIIRegister([&]
    {
        sqlite3_clear_bindings(stmtInfo->stmt);
        sqlite3_reset(stmtInfo->stmt);
    });

    // Bind Parameter
    if (stmtInfo->bindParameterCount != 0)
    {
        if (stmtBindParameterInfoList == nullptr)
        {
            return retValue;
        }
        else if (stmtInfo->bindParameterCount != stmtBindParameterInfoList->size())
        {
            return retValue;
        }

        retValue = StmtBindParameter_(stmtInfo, *stmtBindParameterInfoList);
        if (retValue != Errors::kSuccess)
        {
            return retValue;
        }
    }

    sqliteStatus = sqlite3_step(stmtInfo->stmt);

    do
    {
        if (sqliteStatus == SQLITE_ROW && stmtStepCallback != nullptr)
        {
            callbackStatus = (*stmtStepCallback)(*stmtInfo);
            if (callbackStatus == CallbackErrors::kStop)
            {
                retValue = Errors::kStopCallback;
                break;
            }
        }
        else if (sqliteStatus == SQLITE_DONE)
        {
            retValue = Errors::kSuccess;
            break;
        }
        else
        {
            retValue = Errors::kUnsuccess;
            break;
        }

        sqliteStatus = sqlite3_step(stmtInfo->stmt);

    } while (true);

    return retValue;
}

EzSqlite::Errors EzSqlite::SqliteManager::StmtBindParameter_(
    _In_ const StmtInfo* stmtInfo, 
    _In_ const std::vector<StmtBindParameterInfo>& stmtBindParameterInfoList
)
{
    Errors retValue = Errors::kUnsuccess;

    int sqliteStatus = SQLITE_ERROR;

    /*
        가장 왼쪽의 SQL Parameter Index는 1
    */
    uint32_t parameterIndex = 1;

    for (const auto& stmtBindParameterInfoListEntry : stmtBindParameterInfoList)
    {
        switch (stmtBindParameterInfoListEntry.dataType)
        {
        case StmtDataType::kInteger:
            if (stmtBindParameterInfoListEntry.dataByteSize <= sizeof(int32_t))
            {
                sqliteStatus = sqlite3_bind_int(
                    stmtInfo->stmt, 
                    parameterIndex++, 
                    *reinterpret_cast<int32_t*>(stmtBindParameterInfoListEntry.data)
                );
            }
            else if (stmtBindParameterInfoListEntry.dataByteSize <= sizeof(int64_t))
            {
                sqliteStatus = sqlite3_bind_int64(
                    stmtInfo->stmt,
                    parameterIndex++,
                    *(reinterpret_cast<int64_t*>(stmtBindParameterInfoListEntry.data))
                );
            }

            break;

        case StmtDataType::kFloat:
            if (stmtBindParameterInfoListEntry.dataByteSize <= sizeof(double_t))
            {
                sqliteStatus = sqlite3_bind_double(
                    stmtInfo->stmt,
                    parameterIndex++,
                    *reinterpret_cast<double_t*>(stmtBindParameterInfoListEntry.data)
                );
            }

            break;

        case StmtDataType::kText:
            if (stmtBindParameterInfoListEntry.options == StmtBindParameterOptions::kDestructorStatic)
            {
                sqliteStatus = sqlite3_bind_text(
                    stmtInfo->stmt,
                    parameterIndex++,
                    reinterpret_cast<std::string::traits_type::char_type*>(stmtBindParameterInfoListEntry.data),
                    stmtBindParameterInfoListEntry.dataByteSize,
                    SQLITE_STATIC
                );
            }
            else if (stmtBindParameterInfoListEntry.options == StmtBindParameterOptions::kNone ||
                stmtBindParameterInfoListEntry.options == StmtBindParameterOptions::kDestructorTransient)
            {
                sqliteStatus = sqlite3_bind_text(
                    stmtInfo->stmt,
                    parameterIndex++,
                    reinterpret_cast<std::string::traits_type::char_type*>(stmtBindParameterInfoListEntry.data),
                    stmtBindParameterInfoListEntry.dataByteSize,
                    SQLITE_TRANSIENT
                );
            }

            break;

        case StmtDataType::kBlob:
            if (stmtBindParameterInfoListEntry.options == StmtBindParameterOptions::kDestructorStatic)
            {
                sqliteStatus = sqlite3_bind_blob(
                    stmtInfo->stmt,
                    parameterIndex++,
                    stmtBindParameterInfoListEntry.data,
                    stmtBindParameterInfoListEntry.dataByteSize,
                    SQLITE_STATIC
                );
            }
            else if (stmtBindParameterInfoListEntry.options == StmtBindParameterOptions::kNone ||
                stmtBindParameterInfoListEntry.options == StmtBindParameterOptions::kDestructorTransient)
            {
                sqliteStatus = sqlite3_bind_blob(
                    stmtInfo->stmt,
                    parameterIndex++,
                    stmtBindParameterInfoListEntry.data,
                    stmtBindParameterInfoListEntry.dataByteSize,
                    SQLITE_TRANSIENT
                );
            }

            break;

        case StmtDataType::kNull:
            sqliteStatus = sqlite3_bind_null(
                stmtInfo->stmt,
                parameterIndex++
            );

            break;
        }

        if (sqliteStatus != SQLITE_OK)
        {
            return retValue;
        }
    }

    retValue = Errors::kSuccess;
    return retValue;
}

EzSqlite::Errors EzSqlite::SqliteManager::VerifyTable_(
    _In_ const std::vector<std::string>& verifyTableStmtStringList
)
{
    Errors retValue = Errors::kFailedVerifyTable;

    int sqliteStatus = SQLITE_ERROR;

    sqlite3_stmt* stmt = nullptr;

    auto raii = RAIIRegister([&] 
    {
        if (stmt != nullptr)
        {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
    });

    if (verifyTableStmtStringList.size() == 0)
    {
        retValue = Errors::kSuccess;
        return retValue;
    }

    retValue = Errors::kSuccess;
    for (const auto& verifyTableStmtStringListEntry : verifyTableStmtStringList)
    {
        if (_strnicmp(verifyTableStmtStringListEntry.c_str(), "SELECT ", strlen("SELECT ") != 0))
        {
            retValue = Errors::kFailedVerifyTable;
            break;
        }

        sqliteStatus = sqlite3_prepare_v2(
            database_,
            verifyTableStmtStringListEntry.c_str(),
            -1,
            &stmt,
            nullptr
        );
        if (sqliteStatus != SQLITE_OK)
        {
            retValue = Errors::kFailedVerifyTable;
            break;
        }

        sqlite3_finalize(stmt);
        stmt = nullptr;
    }

    return retValue;
}
