#ifndef ICRYPTOSTRING_H
#define ICRYPTOSTRING_H
#include <string>
#include <string.h>
#include <stdexcept>

namespace rtcModule
{
class IDestroy
{
protected:
    virtual ~IDestroy() {}
public:
    virtual void destroy() { delete this; }
};

class IString: public IDestroy
{
public:
    virtual const char* c_str() const = 0;
    virtual bool empty() const = 0;
    virtual size_t size() const = 0;
};


class CString
{
protected:
    const char* mStr;
    mutable size_t mSize;
public:
    CString(const std::string& str)
        :mStr(str.c_str()), mSize(str.size())
    {}
    CString(const char* str, size_t aSize=((size_t)-1))
        :mStr(str), mSize(aSize)
    {}
    const char* c_str() const {return mStr;}
    operator const char*() const {return mStr;}
    operator bool() const {return mStr!=nullptr;}
    size_t size() const
    {
        if (mSize != (size_t)-1)
            return mSize;
        return (mSize = strlen(mStr));
    }
};

class IDeviceList: public IDestroy
{
public:
    virtual size_t size() const = 0;
    virtual bool empty() const = 0;
    virtual CString name(size_t idx) const = 0;
    virtual CString id(size_t idx) const = 0;
};
template <class T>
class IPtrNoNull
{
protected:
    T* mPtr;
public:
    explicit IPtrNoNull(T* ptr):mPtr(ptr)
    {
        if (!mPtr)
            throw std::runtime_error("NULL pointer given AutoIPtrNoNull ctor");
    }
    ~IPtrNoNull() { mPtr->destroy(); }
    T* get() const {return mPtr;}
    void reset(T* ptr)
    {
        mPtr->destroy();
        if (!ptr)
            throw std::runtime_error("NULL pointer in AutoIPtrNoNull::reset()");
        mPtr = ptr;
    }
    T* operator->() const {return mPtr;}
    T& operator*() const {return mPtr;}
};
template <class T>
class IPtr
{
    T* mPtr;
public:
    explicit IPtr(T* ptr): mPtr(ptr){}
    IPtr(IPtr<T>& other)
    {
        reset(other.release());
    }
    ~IPtr()
    {
        if (mPtr)
            mPtr->destroy();
    }
    T* get() const {return mPtr;}
    void reset(T* ptr)
    {
        if (mPtr)
            mPtr->destroy();
        mPtr = ptr;
    }
    T* release()
    {
        T* ret = mPtr;
        mPtr = nullptr;
        return ret;
    }
    operator bool() const {return (mPtr != nullptr);}
    T* operator->() const {return mPtr;}
    T& operator*() const {return mPtr;}
};
//Convenience type to accomodate returned IString objects from ICryptoFunctions api
struct VString: public IPtrNoNull<IString>
{
    VString(IString* obj): IPtrNoNull(obj) {}
    bool empty() {return get()->empty();}
    const char* c_str() const {return get()->c_str();}
    operator const char*() const {return get()->c_str();}
    size_t size() const {return get()->size();}
};

}
#endif // ICRYPTOSTRING_H
