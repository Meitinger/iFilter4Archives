/*
 * iFilter4Archives
 * Copyright (C) 2019  Manuel Meitinger
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "win32.hpp"

#include "registry.hpp"

#include <stdexcept>
#include <vector>

#pragma comment(lib, "Rpcrt4")
#include <Rpc.h>
#define RPC_DO_OR_THROW(op) \
    do { \
        const auto rpc = (op); \
        if (rpc != RPC_S_OK) { throw std::system_error(rpc, std::system_category()); } \
    } \
    while (0)

namespace win32
{
    void handle_deleter::operator()(HANDLE hObject) noexcept
    {
        ::CloseHandle(hObject); // might be called with INVALID_HANDLE_VALUE
    }

    void localmem_deleter::operator()(void* hMem) noexcept
    {
        ::LocalFree(reinterpret_cast<HLOCAL>(hMem));
    }

    void registry_deleter::operator()(HKEY hKey) noexcept
    {
        ::RegCloseKey(hKey); // might be called with HKEY_*
    }

    void library_deleter::operator()(HMODULE hLibModule) noexcept
    {
        ::FreeLibrary(hLibModule);
    }

    void fileview_delete::operator()(LPVOID lpBaseAddress) noexcept
    {
        ::UnmapViewOfFile(lpBaseAddress);
    }

    /******************************************************************************/

    constexpr static const auto string_length = int(38);
    constexpr static const auto string_format = STR("{%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX}");

    guid::guid() noexcept : GUID() {}

    guid::guid(const GUID& guid) noexcept : GUID(guid) {}

    std::wstring guid::to_wstring() const
    {
        auto result = std::wstring(string_length + 1, CHR('\0'));
        if (_snwprintf_s(result.data(), result.length(), string_length, string_format.c_str(),
                         Data1, Data2, Data3,
                         Data4[0], Data4[1], Data4[2], Data4[3],
                         Data4[4], Data4[5], Data4[6], Data4[7]) != string_length)
        {
            throw std::system_error(errno, std::generic_category());
        }
        result.resize(string_length);
        return result;
    }

    guid guid::create()
    {
        auto result = guid();
        RPC_DO_OR_THROW(::UuidCreate(&result));
        return result;
    }

    guid guid::create_sequential()
    {
        auto result = guid();
        RPC_DO_OR_THROW(::UuidCreateSequential(&result));
        return result;
    }

    guid guid::parse(std::wstring_view s)
    {
        auto result = guid();
        if (!try_parse(s, result)) { throw std::invalid_argument("s"); }
        return result;
    }

    bool guid::try_parse(std::wstring_view s, guid& guid) noexcept
    {
        if (s.empty() || s.length() != string_length) { return false; }
        return _snwscanf_s(
            s.data(), s.length(), string_format.c_str(),
            &guid.Data1, &guid.Data2, &guid.Data3,
            &guid.Data4[0], &guid.Data4[1], &guid.Data4[2], &guid.Data4[3],
            &guid.Data4[4], &guid.Data4[5], &guid.Data4[6], &guid.Data4[7]
        ) == 11;
    }

    /******************************************************************************/

    transaction::transaction(czwstring description)
    {
        reset(::CreateTransaction(nullptr, nullptr, TRANSACTION_DO_NOT_PROMOTE, 0, 0, 0, const_cast<LPWSTR>(description.c_str())));
        if (get() == INVALID_HANDLE_VALUE) { release(); }
        WIN32_DO_OR_THROW(get());
    }

    void transaction::commit()
    {
        WIN32_DO_OR_THROW(::CommitTransaction(get()));
    }

    HANDLE transaction::handle() const noexcept
    {
        return get();
    }

    void transaction::rollback()
    {
        WIN32_DO_OR_THROW(::RollbackTransaction(get()));
    }
}

/******************************************************************************/

namespace utils
{
    win32::unique_library_ptr get_current_module()
    {
        auto safe_ptr = win32::unique_library_ptr();
        auto native_ptr = HMODULE();
        const auto result = ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&get_current_module), &native_ptr);
        safe_ptr.reset(native_ptr);
        WIN32_DO_OR_THROW(result);
        return safe_ptr;
    }

    std::filesystem::path get_module_file_path(HMODULE module_handle)
    {
        auto buffer = std::vector<wchar_t>(MAX_PATH + 1);
        while (true)
        {
            const auto length_excluding_null_terminator = ::GetModuleFileNameW(module_handle, buffer.data(), static_cast<DWORD>(buffer.size()));
            const auto last_error = ::GetLastError();
            if (length_excluding_null_terminator == 0) { WIN32_THROW(last_error); }
            if (length_excluding_null_terminator < buffer.size()) { return std::filesystem::path(std::wstring(buffer.data(), length_excluding_null_terminator)); }
            if (last_error != ERROR_INSUFFICIENT_BUFFER) { WIN32_THROW(last_error); }
            buffer.resize(buffer.size() * 2);
        }
    }

    std::filesystem::path get_system_temp_path()
    {
        const auto key = win32::registry_key::local_machine().open_sub_key_readonly(STR("SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment"));
        if (key)
        {
            const auto temp = key->get_string_value(STR("TEMP"));
            if (temp) { return *temp; }
            const auto tmp = key->get_string_value(STR("TMP"));
            if (tmp) { return *tmp; }
        }
        WIN32_THROW(ERROR_ENVVAR_NOT_FOUND);
    }

    std::wstring get_temp_file_name()
    {
        return win32::guid::create_sequential().to_wstring().append(STR(".tmp"));
    }

    std::filesystem::path get_temp_path()
    {
        auto buffer = std::vector<wchar_t>(MAX_PATH + 1); // "maximum possible return value is MAX_PATH + 1"
        const auto length_excluding_null_terminator = ::GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
        WIN32_DO_OR_THROW(length_excluding_null_terminator);
        return std::wstring(buffer.data(), length_excluding_null_terminator);
    }

    win32::unique_library_ptr load_module(win32::czwstring path)
    {
        auto safe_ptr = win32::unique_library_ptr();
        safe_ptr.reset(::LoadLibraryW(path.c_str()));
        WIN32_DO_OR_THROW(safe_ptr);
        return safe_ptr;
    }
}
