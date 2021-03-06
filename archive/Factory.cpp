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

#include "Factory.hpp"

#include "com.hpp"

#include "Module.hpp"

#include <filesystem>
#include <limits>

namespace archive
{
    CLASS_IMPLEMENTATION(Factory,
public:
    FormatsCollection Formats;
    );

    static void LoadModule(Factory::FormatsCollection& formats, const std::filesystem::path& path)
    {
        const auto library = Module(path);
        auto formatCount = UINT32(0);
        COM_DO_OR_THROW(library.GetNumberOfFormats(formatCount));
        for (auto i = UINT32(0); i < formatCount; i++)
        {
            try
            {
                // add all formats for non-existing extensions
                const auto format = Format(library, i);
                for (const auto& ext : format.Extensions)
                {
                    if (formats.find(ext) == formats.end())
                    {
                        formats.insert_or_assign(ext, format);
                    }
                }
            }
            catch (...) {}
        }
    }

    static void LoadAllModules(Factory::FormatsCollection& formats, const std::filesystem::path& directory)
    {
        if (!std::filesystem::is_directory(directory)) { return; } // ensure the argument is a directory
        for (const auto& entry : std::filesystem::directory_iterator(directory))
        {
            // only load dlls (I can't believe I have to resort to _wcsnicmp in C++)
            const auto& path = entry.path();
            if (entry.is_directory() || _wcsnicmp(path.extension().c_str(), STR(".dll").c_str(), std::numeric_limits<size_t>::max())) { continue; }

            // ignore errors
            try { LoadModule(formats, path); }
            catch (...) {}
        }
    }

    Factory::Factory() : PIMPL_INIT()
    {
        // load 7z.dll and all other formats
        const auto filterDir = utils::get_module_file_path(utils::get_current_module().get()).parent_path();
        LoadModule(PIMPL_(Formats), filterDir / L"7z.dll");
        LoadAllModules(PIMPL_(Formats), filterDir / L"codecs"); // in case someone misplaces a DLL or a codec DLL also includes formats
        LoadAllModules(PIMPL_(Formats), filterDir / L"formats");
    }

    PIMPL_GETTER(Factory, const Factory::FormatsCollection&, Formats);

    const Factory& Factory::GetInstance()
    {
        static const auto instance = Factory();
        return instance;
    }

    sevenzip::IInArchivePtr Factory::CreateArchiveFromExtension(const std::wstring& extension)
    {
        const auto& formats = GetInstance().Formats;
        const auto formatEntry = formats.find(extension);
        if (formatEntry == formats.end()) { COM_THROW(FILTER_E_UNKNOWNFORMAT); }
        return formatEntry->second.CreateArchive();
    }
}
