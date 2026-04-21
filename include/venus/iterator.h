#pragma once
#include <string>

#include "venus/slice.h"
#include "venus/status.h"

namespace venus {

class Iterator {
public:
    virtual ~Iterator() = default;

    virtual bool Valid() const = 0;
    virtual void SeekToFirst() = 0;
    virtual void Seek(const Slice& target) = 0;
    virtual void Next() = 0;

    virtual Slice key() const = 0;
    virtual Slice value() const = 0;

    virtual Status status() const = 0;

    Iterator(const Iterator&) = delete;
    Iterator& operator=(const Iterator&) = delete;

protected:
    Iterator() = default;
};

}  // namespace venus
