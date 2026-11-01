#pragma once
#include <xamlom.h>
#include <string>
#include "Rules.h"

namespace dt {
inline std::wstring_view Text(BSTR value) { return value ? std::wstring_view(value, SysStringLen(value)) : L""; }
struct BString {
    BSTR value;
    explicit BString(const wchar_t* text) : value(SysAllocString(text)) {}
    ~BString() { SysFreeString(value); }
    BString(const BString&) = delete;
    BString& operator=(const BString&) = delete;
};
struct Properties {
    unsigned sourceCount = 0, count = 0;
    PropertyChainSource* sources = nullptr;
    PropertyChainValue* values = nullptr;
    explicit Properties(IVisualTreeService* service, InstanceHandle node) {
        result = service->GetPropertyValuesChain(node, &sourceCount, &sources, &count, &values);
    }
    HRESULT result;
    ~Properties() {
        if (sources) for (unsigned i = 0; i < sourceCount; ++i) {
            auto& s = sources[i];
            SysFreeString(s.TargetType); SysFreeString(s.Name);
            SysFreeString(s.SrcInfo.FileName); SysFreeString(s.SrcInfo.Hash);
        }
        if (values) for (unsigned i = 0; i < count; ++i) {
            auto& v = values[i];
            SysFreeString(v.Type); SysFreeString(v.DeclaringType); SysFreeString(v.ValueType);
            SysFreeString(v.ItemType); SysFreeString(v.Value); SysFreeString(v.PropertyName);
        }
        CoTaskMemFree(sources); CoTaskMemFree(values);
    }
    Properties(const Properties&) = delete;
    Properties& operator=(const Properties&) = delete;
    const PropertyChainValue* Find(std::wstring_view name) const {
        if (FAILED(result)) return nullptr;
        for (unsigned i = 0; i < count; ++i)
            if (!values[i].Overridden && IsProperty(Text(values[i].PropertyName), name)) return &values[i];
        return nullptr;
    }
    bool HasBinding(unsigned index) const {
        for (unsigned i = 0; i < count; ++i)
            if (values[i].Index == index && (values[i].MetadataBits & IsValueBindingExpression)) return true;
        return false;
    }
    BaseValueSource Source(const PropertyChainValue& value) const {
        return value.PropertyChainIndex < sourceCount ? sources[value.PropertyChainIndex].Source : BaseValueSourceUnknown;
    }
};
inline HRESULT CreateValue(IVisualTreeService* service, const std::wstring& type,
                           const std::wstring& value, InstanceHandle* handle) {
    BString t(type.c_str()), v(value.c_str());
    if (!t.value || !v.value) return E_OUTOFMEMORY;
    return service->CreateInstance(t.value, v.value, handle);
}
}
