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

#include "ClassFactory.hpp"

#include "Filter.hpp"

#include <mutex>

namespace com
{
    static auto _lockCount = ULONG(0);
    static auto _lockedFactory = IClassFactoryPtr();
    static auto _mutex = std::mutex();

    CLASS_IMPLEMENTATION(ClassFactory, );

    ClassFactory::ClassFactory() : PIMPL_INIT() {}

    STDMETHODIMP ClassFactory::CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObject) noexcept
    {
        return utils::make_com<Filter>(pUnkOuter, riid, ppvObject);
    }

    STDMETHODIMP ClassFactory::LockServer(BOOL fLock) noexcept
    {
        COM_NOTHROW_BEGIN;

        auto lock = std::unique_lock(_mutex);
        if (fLock)
        {
            if (!_lockedFactory)
            {
                _lockedFactory = static_cast<IClassFactory*>(this);
            }
            ++_lockCount;
        }
        else
        {
            if (!_lockedFactory) { return E_FAIL; } // more unlocks than locks?

            if (--_lockCount == 0)
            {
                _lockedFactory = nullptr;
            }
        }
        return S_OK;

        COM_NOTHROW_END;
    }

    HRESULT ClassFactory::GetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) noexcept
    {
        COM_CHECK_POINTER_AND_SET(ppv, nullptr);
        if (rclsid != __uuidof(Filter)) { return CLASS_E_CLASSNOTAVAILABLE; } // only handle com::Filter
        auto factory = IClassFactoryPtr();
        COM_NOTHROW_BEGIN;

        // get a AddRef'd copy of a possibly locked factory
        auto lock = std::unique_lock(_mutex);
        factory = _lockedFactory;

        // query the existing factory or create a new one
        COM_NOTHROW_END;
        return factory ? factory->QueryInterface(riid, ppv) : utils::make_com<ClassFactory>(nullptr, riid, ppv);
    }
}
