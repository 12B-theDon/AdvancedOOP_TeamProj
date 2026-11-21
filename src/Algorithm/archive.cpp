#include <torch/script.h>
#include <torch/csrc/jit/serialization/export.h>
#include <caffe2/serialize/read_adapter_interface.h>
#include "archive.h"
#include <QFile>
#include <QSaveFile>
#include <QString>
#include <algorithm>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace algorithm {
namespace {
std::runtime_error FileError(const char* operation, const QFileDevice& file) {
    return std::runtime_error((QString::fromLatin1(operation) + ": " + file.fileName()
        + " (" + file.errorString() + ")").toStdString());
}
class ArchiveReader final : public caffe2::serialize::ReadAdapterInterface {
public:
    explicit ArchiveReader(const QString& path) : file_(path) {
        if (!file_.open(QIODevice::ReadOnly)) throw FileError("Cannot open model archive", file_);
        const qint64 length = file_.size();
        if (length < 0 || static_cast<quint64>(length) > std::numeric_limits<size_t>::max())
            throw FileError("Cannot determine model archive size", file_);
        size_ = static_cast<size_t>(length);
    }
    size_t size() const override { return size_; }
    size_t read(uint64_t pos, void* data, size_t length, const char*) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        pos = std::min<uint64_t>(pos, size_);
        length = std::min(length, size_ - static_cast<size_t>(pos));
        if (!file_.seek(static_cast<qint64>(pos))) throw FileError("Cannot seek model archive", file_);
        size_t done = 0;
        while (done < length) {
            const qint64 count = file_.read(static_cast<char*>(data) + done, static_cast<qint64>(length - done));
            if (count <= 0) throw FileError("Cannot read model archive", file_);
            done += static_cast<size_t>(count);
        }
        return done;
    }
private:
    mutable QFile file_;
    mutable std::mutex mutex_;
    size_t size_ = 0;
};
}

torch::jit::Module LoadArchive(const QString& path, ArchiveExtras& extras) {
    return torch::jit::load(std::make_shared<ArchiveReader>(path), torch::kCPU, extras);
}
torch::jit::Module LoadArchive(const QString& path) {
    ArchiveExtras extras;
    return LoadArchive(path, extras);
}
void SaveArchive(const torch::jit::Module& module, const QString& path, const ArchiveExtras& extras) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) throw FileError("Cannot create model archive", file);
    torch::jit::ExportModule(module, [&file](const void* data, size_t length) -> size_t {
        if (length > static_cast<quint64>(std::numeric_limits<qint64>::max() - file.pos()))
            throw std::runtime_error("Model archive is too large.");
        if (!data && length != 0) {
            if (!file.seek(file.pos() + static_cast<qint64>(length))) throw FileError("Cannot seek model archive", file);
            return length;
        }
        size_t done = 0;
        while (done < length) {
            const qint64 count = file.write(static_cast<const char*>(data) + done, static_cast<qint64>(length - done));
            if (count <= 0) throw FileError("Cannot write model archive", file);
            done += static_cast<size_t>(count);
        }
        return done;
    }, extras);
    if (!file.commit()) throw FileError("Cannot commit model archive", file);
}
}
