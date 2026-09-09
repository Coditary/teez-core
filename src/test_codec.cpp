#include "teez/core/test_codec.hpp"

#include <fstream>
#include <functional>
#include <map>
#include <stdexcept>

#include "teez/core/test_report_msgpack.hpp"

namespace teez::core {

namespace {

class JsonTestReportCodec final : public TestReportCodec {
  public:
    std::string name() const override {
        return "json";
    }
    std::string default_extension() const override {
        return ".json";
    }
    std::string encode(const TestRunReport& report) const override {
        return test_report_to_json_string(report);
    }
    TestRunReport decode(const std::string& content) const override {
        return test_report_from_json_string(content);
    }
};

class JunitTestReportCodec final : public TestReportCodec {
  public:
    std::string name() const override {
        return "junit";
    }
    std::string default_extension() const override {
        return ".xml";
    }
    std::string encode(const TestRunReport& report) const override {
        return test_report_to_junit_xml(report);
    }
    TestRunReport decode(const std::string& content) const override {
        return test_report_from_junit_xml(content);
    }
};

class MsgpackTestReportCodec final : public TestReportCodec {
  public:
    std::string name() const override {
        return "msgpack";
    }
    std::string default_extension() const override {
        return ".msgpack";
    }
    bool is_binary() const override {
        return true;
    }
    std::string encode(const TestRunReport& report) const override {
        return test_report_to_msgpack_bytes(report);
    }
    TestRunReport decode(const std::string& content) const override {
        return test_report_from_msgpack_bytes(content);
    }
};

class ZstTestReportCodec final : public TestReportCodec {
  public:
    std::string name() const override {
        return "zst";
    }
    std::string default_extension() const override {
        return ".msgpack.zst";
    }
    bool is_binary() const override {
        return true;
    }
    std::string encode(const TestRunReport& report) const override {
        return test_report_to_zstd_msgpack_bytes(report);
    }
    TestRunReport decode(const std::string& content) const override {
        return test_report_from_zstd_msgpack_bytes(content);
    }
};

using CodecFactory = std::function<std::unique_ptr<TestReportCodec>()>;

const std::map<std::string, CodecFactory>& codec_factories() {
    static const std::map<std::string, CodecFactory> factories = {
        {"json", [] { return std::make_unique<JsonTestReportCodec>(); }},
        {"junit", [] { return std::make_unique<JunitTestReportCodec>(); }},
        {"msgpack", [] { return std::make_unique<MsgpackTestReportCodec>(); }},
        {"zst", [] { return std::make_unique<ZstTestReportCodec>(); }},
    };
    return factories;
}

} // namespace

void TestReportCodec::write(const TestRunReport& report, const std::filesystem::path& path) const {
    const auto content = encode(report);
    std::ofstream out(path, is_binary() ? std::ios::binary : std::ios::openmode{});
    if (!out.is_open()) {
        throw std::runtime_error("failed to write test report (" + name() + "): " + path.string());
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

TestRunReport TestReportCodec::read(const std::filesystem::path& path) const {
    std::ifstream in(path, is_binary() ? std::ios::binary : std::ios::openmode{});
    if (!in.is_open()) {
        throw std::runtime_error("failed to read test report (" + name() + "): " + path.string());
    }
    const std::string content((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    return decode(content);
}

std::vector<std::string> test_report_codec_names() {
    std::vector<std::string> names;
    for (const auto& [name, _] : codec_factories()) {
        names.push_back(name);
    }
    return names;
}

bool is_test_report_codec_name(const std::string& name) {
    return codec_factories().contains(name);
}

const TestReportCodec& test_report_codec_by_name(const std::string& name) {
    static std::map<std::string, std::unique_ptr<TestReportCodec>> codecs;
    if (!codecs.contains(name)) {
        const auto& factories = codec_factories();
        const auto it = factories.find(name);
        if (it == factories.end()) {
            throw std::runtime_error("unknown test report codec: " + name);
        }
        codecs[name] = it->second();
    }
    return *codecs.at(name);
}

std::unique_ptr<TestReportCodec> make_test_report_codec(const std::string& name) {
    const auto& factories = codec_factories();
    const auto it = factories.find(name);
    if (it == factories.end()) {
        throw std::runtime_error("unknown test report codec: " + name);
    }
    return it->second();
}

std::string encode_test_report(const TestRunReport& report, const std::string& codec_name) {
    return test_report_codec_by_name(codec_name).encode(report);
}

TestRunReport decode_test_report(const std::string& content, const std::string& codec_name) {
    return test_report_codec_by_name(codec_name).decode(content);
}

void write_test_report(const TestRunReport& report, const std::string& codec_name,
                       const std::filesystem::path& path) {
    test_report_codec_by_name(codec_name).write(report, path);
}

TestRunReport read_test_report(const std::filesystem::path& path, const std::string& codec_name) {
    return test_report_codec_by_name(codec_name).read(path);
}

} // namespace teez::core
