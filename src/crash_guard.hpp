#pragma once
#include <string>
#include <filesystem>
#include <cstdio>
#include <exception>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
namespace lua2omp_crash {
inline wchar_t activeDirectory[MAX_PATH]{};
inline LPTOP_LEVEL_EXCEPTION_FILTER previousFilter=nullptr;
inline void setActiveDirectory(const std::filesystem::path& path) noexcept {
    if (path.empty()) return;
    GetFullPathNameW(path.c_str(), MAX_PATH, activeDirectory, nullptr);
}
inline void appendLog(const wchar_t* directory, const char* message, DWORD length) noexcept {
    wchar_t path[MAX_PATH]{};
    if (directory && directory[0]) {
        wcsncpy_s(path, directory, _TRUNCATE);
        size_t used=wcslen(path);
        if (used && path[used-1]!=L'\\' && path[used-1]!=L'/') wcscat_s(path,L"\\");
    } else {
        GetCurrentDirectoryW(MAX_PATH,path);
        size_t used=wcslen(path);
        if (used && path[used-1]!=L'\\' && path[used-1]!=L'/') wcscat_s(path,L"\\");
    }
    wcscat_s(path,L"crash.log");
    HANDLE file=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return;
    DWORD written=0;
    WriteFile(file,message,length,&written,nullptr);
    WriteFile(file,"\r\n",2,&written,nullptr);
    FlushFileBuffers(file);
    CloseHandle(file);
}
inline LONG WINAPI exceptionFilter(EXCEPTION_POINTERS* info) noexcept {
    char message[1024]{};
    const DWORD code=info&&info->ExceptionRecord?info->ExceptionRecord->ExceptionCode:0;
    const void* address=info&&info->ExceptionRecord?info->ExceptionRecord->ExceptionAddress:nullptr;
    const int count=sprintf_s(message,sizeof(message),
        "[Lua2OMP CRASH] Exception=0x%08lX Address=%p Process=%lu Thread=%lu",
        static_cast<unsigned long>(code),address,GetCurrentProcessId(),GetCurrentThreadId());
    appendLog(activeDirectory,message,count>0?static_cast<DWORD>(count):0);
    wchar_t dumpPath[MAX_PATH]{};
    if(activeDirectory[0]) {
        wcsncpy_s(dumpPath,activeDirectory,_TRUNCATE);
        size_t used=wcslen(dumpPath);
        if(used&&dumpPath[used-1]!=L'\\'&&dumpPath[used-1]!=L'/')wcscat_s(dumpPath,L"\\");
    } else {
        GetCurrentDirectoryW(MAX_PATH,dumpPath);
        size_t used=wcslen(dumpPath);
        if(used&&dumpPath[used-1]!=L'\\'&&dumpPath[used-1]!=L'/')wcscat_s(dumpPath,L"\\");
    }
    wcscat_s(dumpPath,L"crash.dmp");
    HANDLE dump=CreateFileW(dumpPath,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(dump!=INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION exceptionInfo{};
        exceptionInfo.ThreadId=GetCurrentThreadId();
        exceptionInfo.ExceptionPointers=info;
        exceptionInfo.ClientPointers=FALSE;
        MiniDumpWriteDump(GetCurrentProcess(),GetCurrentProcessId(),dump,MiniDumpWithThreadInfo,info?&exceptionInfo:nullptr,nullptr,nullptr);
        CloseHandle(dump);
    }
    if(previousFilter)return previousFilter(info);
    return EXCEPTION_EXECUTE_HANDLER;
}
inline void terminateHandler() noexcept {
    char message[1024] = "[Lua2OMP CRASH] Unhandled C++ exception or std::terminate";
    try {
        const auto error = std::current_exception();
        if (error) std::rethrow_exception(error);
    } catch (const std::exception& exception) {
        sprintf_s(message, sizeof(message), "[Lua2OMP CRASH] Unhandled C++ exception: %s", exception.what());
    } catch (...) {}
    appendLog(activeDirectory, message, static_cast<DWORD>(strlen(message)));
    std::abort();
}
inline void install(const std::filesystem::path& fallbackDirectory) noexcept {
    setActiveDirectory(fallbackDirectory);
    previousFilter=SetUnhandledExceptionFilter(exceptionFilter);
    std::set_terminate(terminateHandler);
}
}
#else
namespace lua2omp_crash {
inline void setActiveDirectory(const std::filesystem::path&) noexcept {}
inline void install(const std::filesystem::path&) noexcept {}
}
#endif
